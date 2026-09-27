/***********************************************************************************************************************
 * File Name    : frame_stream.c
 * Description  : Sends pseudo-segmentation frames, detections, and drive guidance over USB PCDC.
 **********************************************************************************************************************/

#include <stddef.h>
#include <string.h>

#include "frame_stream.h"
#include "m85_gateway_runtime.h"
#include "usb_pcdc_console.h"

/* Match the PCDC short-packet-safe transfer size. */
#define FRAME_STREAM_CHUNK_SIZE         (62U)
#define FRAME_STREAM_BYTES_PER_PIXEL    (2U)
#define FRAME_STREAM_AI_HEADER_SIZE     (16U)
#define FRAME_STREAM_AI_RECORD_SIZE     (12U)
#define FRAME_STREAM_NAV_HEADER_SIZE    (28U)
#define FRAME_STREAM_NAV_RECORD_SIZE    (8U)

static uint32_t g_frame_stream_frame_id = 0U;
static uint8_t g_frame_stream_snapshot[VIN_BYTES_PER_FRAME] BSP_ALIGN_VARIABLE(128)
        BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
/* Descriptor-only publish. The camera buffer is leased until the low-priority
 * USB task snapshots it, so producer copy cost is O(1). */
static uint8_t g_video_usb_snapshot[VIN_BYTES_PER_FRAME] BSP_ALIGN_VARIABLE(128)
        BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
static volatile uint8_t const * g_video_source;
static volatile uint16_t g_video_width;
static volatile uint16_t g_video_height;
static volatile uint32_t g_video_generation;
static volatile uint8_t g_video_lease_busy;
static volatile uint8_t g_video_pending_valid;
static obstacle_detector_result_t g_video_pending_detections;
static road_navigation_result_t g_video_pending_navigation;
static uint32_t g_diag_last_class_mask;
static uint32_t g_diag_last_state;
static uint32_t g_diag_last_reason;
static uint32_t g_diag_frames_since_log;
static uint8_t g_diag_have_log;

/* Diagnostic text belongs to the low-priority USB consumer.  Never print
 * from the camera/AI producer: a connected but stalled host can block writes. */
static void frame_stream_log_detection(obstacle_detector_result_t const * detections,
                                       road_navigation_result_t const * navigation,
                                       uint16_t height)
{
    uint32_t counts[OBSTACLE_DETECTOR_CLASS_COUNT] = {0U};
    uint32_t max_scores[OBSTACLE_DETECTOR_CLASS_COUNT] = {0U};
    uint32_t max_bottoms[OBSTACLE_DETECTOR_CLASS_COUNT] = {0U};
    uint32_t class_mask = 0U;
    uint32_t count = detections->detection_count;
    vc_status_t status = {0};
    if (count > OBSTACLE_DETECTOR_MAX_DETECTIONS)
    {
        count = OBSTACLE_DETECTOR_MAX_DETECTIONS;
    }
    for (uint32_t i = 0U; i < count; ++i)
    {
        uint16_t class_id = detections->detections[i].class_id;
        if (class_id < OBSTACLE_DETECTOR_CLASS_COUNT)
        {
            ++counts[class_id];
            class_mask |= 1UL << class_id;
            if (detections->detections[i].score_per_mille > max_scores[class_id])
            {
                max_scores[class_id] = detections->detections[i].score_per_mille;
            }
            uint32_t bottom = (uint32_t) detections->detections[i].y +
                              detections->detections[i].height;
            uint32_t bottom_per_mille = height ? (bottom * 1000U) / height : 0U;
            if (bottom_per_mille > max_bottoms[class_id])
            {
                max_bottoms[class_id] = bottom_per_mille;
            }
        }
    }
    (void) m85_gateway_runtime_get_status(&status);
    ++g_diag_frames_since_log;
    if (g_diag_have_log && class_mask == g_diag_last_class_mask &&
        status.state == g_diag_last_state && status.reason == g_diag_last_reason &&
        g_diag_frames_since_log < 20U)
    {
        return;
    }
    g_diag_have_log = 1U;
    g_diag_frames_since_log = 0U;
    g_diag_last_class_mask = class_mask;
    g_diag_last_state = status.state;
    g_diag_last_reason = status.reason;
    (void) usb_pcdc_console_printf(
        "AI_DETECTION,person=%lu,bicycle=%lu,car=%lu,motorcycle=%lu,"
        "person_score=%lu,person_bottom=%lu,car_score=%lu,car_bottom=%lu,"
        "nav_stop=%u,m33_state=%lu,m33_reason=%lu,ai_seq=%lu,tof_mm=%lu\r\n",
        (unsigned long) counts[OBSTACLE_CLASS_PERSON],
        (unsigned long) counts[OBSTACLE_CLASS_BICYCLE],
        (unsigned long) counts[OBSTACLE_CLASS_CAR],
        (unsigned long) counts[OBSTACLE_CLASS_MOTORCYCLE],
        (unsigned long) max_scores[OBSTACLE_CLASS_PERSON],
        (unsigned long) max_bottoms[OBSTACLE_CLASS_PERSON],
        (unsigned long) max_scores[OBSTACLE_CLASS_CAR],
        (unsigned long) max_bottoms[OBSTACLE_CLASS_CAR],
        (unsigned) navigation->stop_reason,
        (unsigned long) status.state,
        (unsigned long) status.reason,
        (unsigned long) status.ai_seq,
        (unsigned long) status.tof_mm);
}

static void put_u16_le(uint8_t * p_dst, uint16_t value);
static void put_i16_le(uint8_t * p_dst, int16_t value);
static void put_u32_le(uint8_t * p_dst, uint32_t value);
static fsp_err_t frame_stream_send_ai_metadata(obstacle_detector_result_t const * p_result,
                                               uint32_t frame_id);
static fsp_err_t frame_stream_send_navigation_metadata(road_navigation_result_t const * p_result,
                                                       uint32_t frame_id);
static fsp_err_t frame_stream_send_navigation_now(uint8_t const * p_frame,
                                                  uint16_t width,
                                                  uint16_t height,
                                                  obstacle_detector_result_t const * p_detections,
                                                  road_navigation_result_t const * p_navigation);
static fsp_err_t frame_stream_send_frame(uint16_t width, uint16_t height, uint32_t frame_id);

fsp_err_t frame_stream_publish_navigation(uint8_t const * p_frame,
                                          uint16_t width,
                                          uint16_t height,
                                          obstacle_detector_result_t const * p_detections,
                                          road_navigation_result_t const * p_navigation)
{
    if ((NULL == p_detections) || (NULL == p_navigation))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    uint32_t payload_length = (uint32_t) width * height * FRAME_STREAM_BYTES_PER_PIXEL;
    if ((NULL == p_frame) || (0U == width) || (0U == height) ||
        (payload_length > VIN_BYTES_PER_FRAME) || (width > VIN_CFG_IMAGE_STRIDE))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (g_video_lease_busy)
    {
        /* Keep AI/control real-time: drop a diagnostic frame while the
         * previous camera buffer is owned by USB. */
        return FSP_ERR_IN_USE;
    }
    g_video_source = p_frame;
    g_video_width = width;
    g_video_height = height;
    g_video_pending_detections = *p_detections;
    g_video_pending_navigation = *p_navigation;
    g_video_lease_busy = 1U;
    __DMB();
    ++g_video_generation;
    g_video_pending_valid = 1U;
    return FSP_SUCCESS;
}

fsp_err_t frame_stream_usb_task_poll(void)
{
    if (!g_video_pending_valid)
    {
        return FSP_SUCCESS;
    }
    uint8_t const * source = (uint8_t const *) g_video_source;
    uint16_t width = g_video_width;
    uint16_t height = g_video_height;
    uint32_t generation = g_video_generation;
    uint32_t row_bytes = (uint32_t) width * FRAME_STREAM_BYTES_PER_PIXEL;
    uint32_t source_stride_bytes = (uint32_t) VIN_CFG_IMAGE_STRIDE * FRAME_STREAM_BYTES_PER_PIXEL;
    if ((uint32_t) width * height * FRAME_STREAM_BYTES_PER_PIXEL > VIN_BYTES_PER_FRAME)
    {
        g_video_pending_valid = 0U;
        g_video_lease_busy = 0U;
        return FSP_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t y = 0U; y < height; y++)
    {
        memcpy(&g_video_usb_snapshot[y * row_bytes],
               &source[y * source_stride_bytes], row_bytes);
    }
    __DMB();
    if (generation != g_video_generation || source != (uint8_t const *)g_video_source)
    {
        g_video_pending_valid = 0U;
        g_video_lease_busy = 0U;
        return FSP_SUCCESS;
    }
    obstacle_detector_result_t detections = g_video_pending_detections;
    road_navigation_result_t navigation = g_video_pending_navigation;
    g_video_pending_valid = 0U;
    g_video_lease_busy = 0U;
    /* USB failure is diagnostic-only and never propagates to AI/heartbeat. */
    frame_stream_log_detection(&detections, &navigation, height);
    (void) frame_stream_send_navigation_now(g_video_usb_snapshot, width, height,
                                            &detections, &navigation);
    return FSP_SUCCESS;
}

static fsp_err_t frame_stream_send_navigation_now(uint8_t const * p_frame,
                                                   uint16_t width,
                                                   uint16_t height,
                                                   obstacle_detector_result_t const * p_detections,
                                                   road_navigation_result_t const * p_navigation)
{
    if ((NULL == p_detections) || (NULL == p_navigation))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    uint32_t payload_length = (uint32_t) width * height * FRAME_STREAM_BYTES_PER_PIXEL;
    if ((NULL == p_frame) || (0U == width) || (0U == height) ||
        (payload_length > VIN_BYTES_PER_FRAME) || (width > VIN_CFG_IMAGE_STRIDE))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    uint32_t source_stride_bytes = (uint32_t) VIN_CFG_IMAGE_STRIDE * FRAME_STREAM_BYTES_PER_PIXEL;
    uint32_t row_bytes = (uint32_t) width * FRAME_STREAM_BYTES_PER_PIXEL;
    uint32_t source_length = ((uint32_t) height - 1U) * source_stride_bytes + row_bytes;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr((void *) p_frame, (int32_t) source_length);
#endif

    fsp_err_t err = road_navigation_render_rgb565(p_frame,
                                                   width,
                                                   height,
                                                   VIN_CFG_IMAGE_STRIDE,
                                                   p_detections,
                                                   p_navigation,
                                                   (uint16_t *) g_frame_stream_snapshot);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    uint32_t frame_id = g_frame_stream_frame_id++;
    err = frame_stream_send_ai_metadata(p_detections, frame_id);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    err = frame_stream_send_navigation_metadata(p_navigation, frame_id);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    return frame_stream_send_frame(width, height, frame_id);
}

static fsp_err_t frame_stream_send_frame(uint16_t width, uint16_t height, uint32_t frame_id)
{
    uint32_t payload_length = (uint32_t) width * height * FRAME_STREAM_BYTES_PER_PIXEL;
    uint32_t offset = 0U;
    uint8_t header[FRAME_STREAM_HEADER_SIZE] =
    {
        'R', 'A', '8', 'P',
        1U,
        FRAME_STREAM_FORMAT_RGB565_LE,
    };
    put_u16_le(&header[6], width);
    put_u16_le(&header[8], height);
    put_u32_le(&header[10], payload_length);
    put_u32_le(&header[14], frame_id);

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr(g_frame_stream_snapshot, (int32_t) payload_length);
#endif

    fsp_err_t err = (fsp_err_t) APP_WRITE(header, FRAME_STREAM_HEADER_SIZE);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    while (offset < payload_length)
    {
        uint32_t chunk = payload_length - offset;
        if (chunk > FRAME_STREAM_CHUNK_SIZE)
        {
            chunk = FRAME_STREAM_CHUNK_SIZE;
        }

        err = (fsp_err_t) APP_WRITE(&g_frame_stream_snapshot[offset], chunk);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
        offset += chunk;
    }
    return FSP_SUCCESS;
}

static fsp_err_t frame_stream_send_ai_metadata(obstacle_detector_result_t const * p_result,
                                               uint32_t frame_id)
{
    uint8_t metadata[FRAME_STREAM_AI_HEADER_SIZE +
                     OBSTACLE_DETECTOR_MAX_DETECTIONS * FRAME_STREAM_AI_RECORD_SIZE] =
    {
        'R', 'A', 'A', 'I',
        1U,
    };
    uint32_t detection_count = p_result->detection_count;
    if (detection_count > OBSTACLE_DETECTOR_MAX_DETECTIONS)
    {
        detection_count = OBSTACLE_DETECTOR_MAX_DETECTIONS;
    }

    metadata[5] = (uint8_t) detection_count;
    put_u16_le(&metadata[6], (uint16_t) (detection_count * FRAME_STREAM_AI_RECORD_SIZE));
    put_u32_le(&metadata[8], p_result->inference_time_us);
    put_u32_le(&metadata[12], frame_id);

    for (uint32_t i = 0U; i < detection_count; i++)
    {
        uint32_t record_offset = FRAME_STREAM_AI_HEADER_SIZE + i * FRAME_STREAM_AI_RECORD_SIZE;
        obstacle_detection_t const * p_detection = &p_result->detections[i];
        put_u16_le(&metadata[record_offset], p_detection->x);
        put_u16_le(&metadata[record_offset + 2U], p_detection->y);
        put_u16_le(&metadata[record_offset + 4U], p_detection->width);
        put_u16_le(&metadata[record_offset + 6U], p_detection->height);
        put_u16_le(&metadata[record_offset + 8U], p_detection->score_per_mille);
        put_u16_le(&metadata[record_offset + 10U], p_detection->class_id);
    }

    return (fsp_err_t) APP_WRITE(metadata,
                                 FRAME_STREAM_AI_HEADER_SIZE +
                                 detection_count * FRAME_STREAM_AI_RECORD_SIZE);
}

static fsp_err_t frame_stream_send_navigation_metadata(road_navigation_result_t const * p_result,
                                                       uint32_t frame_id)
{
    uint8_t metadata[FRAME_STREAM_NAV_HEADER_SIZE +
                     ROAD_NAVIGATION_PATH_POINTS * FRAME_STREAM_NAV_RECORD_SIZE] =
    {
        'R', 'A', 'N', 'V',
        1U,
    };
    uint32_t path_count = p_result->path_count;
    if (path_count > ROAD_NAVIGATION_PATH_POINTS)
    {
        path_count = ROAD_NAVIGATION_PATH_POINTS;
    }

    metadata[5] = p_result->drive_allowed;
    metadata[6] = p_result->stop_reason;
    metadata[7] = (uint8_t) path_count;
    put_i16_le(&metadata[8], p_result->steering_angle_cdeg);
    put_u16_le(&metadata[10], p_result->throttle_per_mille);
    put_u16_le(&metadata[12], p_result->road_confidence_per_mille);
    put_u16_le(&metadata[14], p_result->obstacle_risk_per_mille);
    put_u16_le(&metadata[16], p_result->target_x);
    put_u16_le(&metadata[18], p_result->target_y);
    put_u32_le(&metadata[20], frame_id);
    put_u16_le(&metadata[24], (uint16_t) (path_count * FRAME_STREAM_NAV_RECORD_SIZE));
    put_u16_le(&metadata[26], 0U);

    for (uint32_t i = 0U; i < path_count; i++)
    {
        uint32_t offset = FRAME_STREAM_NAV_HEADER_SIZE + i * FRAME_STREAM_NAV_RECORD_SIZE;
        put_u16_le(&metadata[offset], p_result->path[i].y);
        put_u16_le(&metadata[offset + 2U], p_result->path[i].left);
        put_u16_le(&metadata[offset + 4U], p_result->path[i].center);
        put_u16_le(&metadata[offset + 6U], p_result->path[i].right);
    }

    return (fsp_err_t) APP_WRITE(metadata,
                                 FRAME_STREAM_NAV_HEADER_SIZE +
                                 path_count * FRAME_STREAM_NAV_RECORD_SIZE);
}

static void put_u16_le(uint8_t * p_dst, uint16_t value)
{
    p_dst[0] = (uint8_t) (value & 0xFFU);
    p_dst[1] = (uint8_t) ((value >> 8) & 0xFFU);
}

static void put_i16_le(uint8_t * p_dst, int16_t value)
{
    put_u16_le(p_dst, (uint16_t) value);
}

static void put_u32_le(uint8_t * p_dst, uint32_t value)
{
    p_dst[0] = (uint8_t) (value & 0xFFU);
    p_dst[1] = (uint8_t) ((value >> 8) & 0xFFU);
    p_dst[2] = (uint8_t) ((value >> 16) & 0xFFU);
    p_dst[3] = (uint8_t) ((value >> 24) & 0xFFU);
}
