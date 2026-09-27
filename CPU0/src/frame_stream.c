/***********************************************************************************************************************
 * File Name    : frame_stream.c
 * Description  : Sends pseudo-segmentation frames, detections, and drive guidance over USB PCDC.
 **********************************************************************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <tk/tkernel.h>

#include "frame_stream.h"
#include "frame_stream_bmp.h"
#include "frame_stream_overlay.h"
#include "m85_gateway_runtime.h"
#include "usb_pcdc_console.h"

/* Match the PCDC short-packet-safe transfer size. */
#define FRAME_STREAM_CHUNK_SIZE         (62U)
#define FRAME_STREAM_BYTES_PER_PIXEL    (2U)
#define FRAME_STREAM_AI_HEADER_SIZE     (16U)
#define FRAME_STREAM_AI_RECORD_SIZE     (12U)
#define FRAME_STREAM_NAV_HEADER_SIZE    (28U)
#define FRAME_STREAM_NAV_RECORD_SIZE    (8U)
#define HTTP_VIDEO_HEADER_CAPACITY      (192U)
#define HTTP_VIDEO_SLOT_COUNT           (2U)
#define HTTP_VIDEO_STALE_MS             (2500U)
#define HTTP_VIDEO_WIRE_CAPACITY        (HTTP_VIDEO_HEADER_CAPACITY + FRAME_STREAM_BMP_SIZE)
#define HTTP_AI_BOX_COLOR               (0xF81FU) /* magenta, visible over white tape */
#define HTTP_AI_COURSE_COLOR            (0x07FFU) /* cyan course boundaries */
#define HTTP_AI_BLOCKED_COLOR           (0xF800U) /* red path-blocked marker */

static uint32_t g_frame_stream_frame_id = 0U;
static uint8_t g_frame_stream_snapshot[VIN_BYTES_PER_FRAME] BSP_ALIGN_VARIABLE(128)
        BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
/* Keep the slow USB diagnostic reader off the HTTP/video producer.  A single
 * staged frame is enough: while USB owns it, newer USB diagnostics are dropped
 * while the HTTP snapshot path continues publishing the newest camera frame. */
static uint8_t g_usb_tx_snapshot[VIN_BYTES_PER_FRAME] BSP_ALIGN_VARIABLE(128)
        BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
static obstacle_detector_result_t g_usb_tx_detections;
static road_navigation_result_t g_usb_tx_navigation;
static volatile uint8_t g_usb_tx_state;
static uint16_t g_usb_tx_width;
static uint16_t g_usb_tx_height;
static uint32_t g_usb_tx_frame_id;
#define USB_TX_IDLE                     (0U)
#define USB_TX_PREPARING                (1U)
#define USB_TX_READY                    (2U)
#define USB_TX_SENDING                  (3U)
typedef struct
{
    uint8_t wire[HTTP_VIDEO_WIRE_CAPACITY];
} http_video_slot_t;
static http_video_slot_t g_http_video_slots[HTTP_VIDEO_SLOT_COUNT] BSP_ALIGN_VARIABLE(128)
        BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
static uint16_t g_http_video_references[HTTP_VIDEO_SLOT_COUNT];
static uint8_t g_http_video_writing[HTTP_VIDEO_SLOT_COUNT];
static volatile uint8_t g_http_video_active_slot = UINT8_MAX;
static volatile uint8_t g_http_video_valid;
static volatile uint32_t g_http_video_updated_ms;
static uint16_t g_http_video_wire_length[HTTP_VIDEO_SLOT_COUNT];
static uint32_t g_http_video_frame_sequence;
/* Descriptor-only publish. The camera buffer is leased until the low-priority
 * video task snapshots it, so producer copy cost is O(1). */
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

static uint32_t http_video_lock(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void http_video_unlock(uint32_t primask)
{
    if ((primask & 1U) == 0U)
    {
        __enable_irq();
    }
}

static void frame_stream_http_draw_line(uint16_t * p_pixels,
                                        uint16_t width,
                                        uint16_t height,
                                        int32_t x0,
                                        int32_t y0,
                                        int32_t x1,
                                        int32_t y1,
                                        uint16_t color)
{
    int32_t dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t dy = -((y0 < y1) ? (y1 - y0) : (y0 - y1));
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t error = dx + dy;
    for (;;)
    {
        if ((x0 >= 0) && (x0 < (int32_t) width) &&
            (y0 >= 0) && (y0 < (int32_t) height))
        {
            uint32_t index = (uint32_t) y0 * width + (uint32_t) x0;
            p_pixels[index] = color;
            if ((uint32_t) (x0 + 1) < width)
            {
                p_pixels[index + 1U] = color;
            }
        }
        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }
        int32_t twice_error = 2 * error;
        if (twice_error >= dy)
        {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}

static void frame_stream_http_draw_course(uint16_t * p_pixels,
                                          uint16_t width,
                                          uint16_t height,
                                          road_navigation_result_t const * p_navigation)
{
    if ((p_pixels == NULL) || (p_navigation == NULL))
    {
        return;
    }
    uint32_t count = p_navigation->path_count;
    if (count > ROAD_NAVIGATION_PATH_POINTS)
    {
        count = ROAD_NAVIGATION_PATH_POINTS;
    }
    for (uint32_t i = 1U; i < count; ++i)
    {
        road_path_sample_t const * previous = &p_navigation->path[i - 1U];
        road_path_sample_t const * current = &p_navigation->path[i];
        frame_stream_http_draw_line(p_pixels, width, height,
                                    previous->left, previous->y,
                                    current->left, current->y,
                                    HTTP_AI_COURSE_COLOR);
        frame_stream_http_draw_line(p_pixels, width, height,
                                    previous->right, previous->y,
                                    current->right, current->y,
                                    HTTP_AI_COURSE_COLOR);
    }
}

static void frame_stream_http_draw_path_blocked(uint16_t * p_pixels,
                                               uint16_t width,
                                               uint16_t height,
                                               road_navigation_result_t const * p_navigation)
{
    if ((p_pixels == NULL) || (p_navigation == NULL) ||
        (p_navigation->stop_reason != NAVIGATION_STOP_PATH_BLOCKED))
    {
        return;
    }
    int32_t center_x = p_navigation->target_x;
    int32_t center_y = p_navigation->target_y;
    frame_stream_http_draw_line(p_pixels, width, height,
                                center_x - 7, center_y - 7,
                                center_x + 7, center_y + 7,
                                HTTP_AI_BLOCKED_COLOR);
    frame_stream_http_draw_line(p_pixels, width, height,
                                center_x - 7, center_y + 7,
                                center_x + 7, center_y - 7,
                                HTTP_AI_BLOCKED_COLOR);
}

static void frame_stream_http_draw_obstacles(uint16_t * p_pixels,
                                             uint16_t width,
                                             uint16_t height,
                                             obstacle_detector_result_t const * p_detections)
{
    if ((p_pixels == NULL) || (p_detections == NULL) || (width == 0U) || (height == 0U))
    {
        return;
    }
    uint32_t count = p_detections->detection_count;
    if (count > OBSTACLE_DETECTOR_MAX_DETECTIONS)
    {
        count = OBSTACLE_DETECTOR_MAX_DETECTIONS;
    }
    /* A minimum two-pixel box survives the 240x180 HTTP downscale. */
    uint32_t thickness = (width + FRAME_STREAM_BMP_WIDTH - 1U) / FRAME_STREAM_BMP_WIDTH;
    if (thickness < 2U)
    {
        thickness = 2U;
    }
    for (uint32_t i = 0U; i < count; ++i)
    {
        obstacle_detection_t const * detection = &p_detections->detections[i];
        if (!frame_stream_http_class_is_visible(detection->class_id))
        {
            continue;
        }
        uint32_t left = detection->x;
        uint32_t top = detection->y;
        uint32_t right = left + detection->width;
        uint32_t bottom = top + detection->height;
        if (left >= width || top >= height)
        {
            continue;
        }
        if (right >= width)
        {
            right = width - 1U;
        }
        if (bottom >= height)
        {
            bottom = height - 1U;
        }
        for (uint32_t edge = 0U; edge < thickness; ++edge)
        {
            uint32_t y_top = top + edge;
            uint32_t y_bottom = (bottom >= edge) ? bottom - edge : bottom;
            uint32_t x_left = left + edge;
            uint32_t x_right = (right >= edge) ? right - edge : right;
            if (y_top <= bottom)
            {
                for (uint32_t x = left; x <= right; ++x)
                {
                    p_pixels[y_top * width + x] = HTTP_AI_BOX_COLOR;
                }
            }
            if ((y_bottom >= top) && (y_bottom != y_top))
            {
                for (uint32_t x = left; x <= right; ++x)
                {
                    p_pixels[y_bottom * width + x] = HTTP_AI_BOX_COLOR;
                }
            }
            if (x_left <= right)
            {
                for (uint32_t y = top; y <= bottom; ++y)
                {
                    p_pixels[y * width + x_left] = HTTP_AI_BOX_COLOR;
                }
            }
            if ((x_right >= left) && (x_right != x_left))
            {
                for (uint32_t y = top; y <= bottom; ++y)
                {
                    p_pixels[y * width + x_right] = HTTP_AI_BOX_COLOR;
                }
            }
        }
    }
}

/* Convert RGB565 camera pixels into a 240x180 indexed RGB332 BMP. The palette
 * preserves primary cyan/magenta/red overlays exactly; it approximates yellow
 * and quantizes camera colors. This runs only in the low-priority video task. */
static void frame_stream_http_publish(uint8_t const * p_frame, uint16_t width, uint16_t height)
{
    if ((p_frame == NULL) || (width == 0U) || (height == 0U))
    {
        return;
    }
    uint32_t primask = http_video_lock();
    uint8_t active = g_http_video_active_slot;
    uint8_t slot = UINT8_MAX;
    for (uint8_t i = 0U; i < HTTP_VIDEO_SLOT_COUNT; ++i)
    {
        if ((i != active) && (g_http_video_references[i] == 0U) &&
            (g_http_video_writing[i] == 0U))
        {
            slot = i;
            break;
        }
    }
    if ((slot == UINT8_MAX) && (active < HTTP_VIDEO_SLOT_COUNT) &&
        (g_http_video_references[active] == 0U) &&
        (g_http_video_writing[active] == 0U))
    {
        slot = active;
        g_http_video_valid = 0U;
    }
    if (slot == UINT8_MAX)
    {
        http_video_unlock(primask);
        return; /* HTTP readers own both buffers; drop this diagnostic frame. */
    }
    g_http_video_writing[slot] = 1U;
    http_video_unlock(primask);

    uint8_t * wire = g_http_video_slots[slot].wire;
    uint32_t sequence = g_http_video_frame_sequence + 1U;
    int header_length = snprintf((char *) wire, HTTP_VIDEO_HEADER_CAPACITY,
        "HTTP/1.0 200 OK\r\nContent-Type: image/bmp\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store\r\nX-Frame-Seq: %lu\r\nX-Frame-Created-Ms: %010u\r\n"
        "Connection: close\r\n\r\n",
        (unsigned) FRAME_STREAM_BMP_SIZE, (unsigned long) sequence, 0U);
    if ((header_length <= 0) || (header_length >= (int) HTTP_VIDEO_HEADER_CAPACITY) ||
        (width > VIN_CFG_IMAGE_STRIDE) ||
        ((uint32_t) width * height * FRAME_STREAM_BYTES_PER_PIXEL > VIN_BYTES_PER_FRAME))
    {
        primask = http_video_lock();
        g_http_video_writing[slot] = 0U;
        http_video_unlock(primask);
        return;
    }

    uint8_t * bmp = wire + header_length;
    if (!frame_stream_bmp_encode(bmp, FRAME_STREAM_BMP_SIZE, p_frame, width, height))
    {
        primask = http_video_lock();
        g_http_video_writing[slot] = 0U;
        http_video_unlock(primask);
        return;
    }

    SYSTIM now;
    uint32_t updated = (tk_get_otm(&now) == E_OK) ? now.lo : 0U;
    char created_ms[11];
    (void) snprintf(created_ms, sizeof(created_ms), "%010lu", (unsigned long) updated);
    char * created_ms_header = strstr((char *) wire, "X-Frame-Created-Ms: ");
    if (created_ms_header != NULL)
    {
        memcpy(created_ms_header + (sizeof("X-Frame-Created-Ms: ") - 1U), created_ms,
               sizeof(created_ms) - 1U);
    }
    __DMB();
    primask = http_video_lock();
    g_http_video_wire_length[slot] = (uint16_t) (header_length + FRAME_STREAM_BMP_SIZE);
    g_http_video_updated_ms = updated;
    g_http_video_frame_sequence = sequence;
    g_http_video_active_slot = slot;
    g_http_video_valid = 1U;
    g_http_video_writing[slot] = 0U;
    http_video_unlock(primask);
}

bool frame_stream_http_snapshot_acquire(frame_stream_http_snapshot_t * p_snapshot)
{
    if (p_snapshot == NULL)
    {
        return false;
    }
    SYSTIM now;
    bool have_time = (tk_get_otm(&now) == E_OK);
    uint32_t primask = http_video_lock();
    uint8_t slot = g_http_video_active_slot;
    bool fresh = have_time &&
                 ((uint32_t) (now.lo - g_http_video_updated_ms) <= HTTP_VIDEO_STALE_MS);
    if (!g_http_video_valid || !fresh || (slot >= HTTP_VIDEO_SLOT_COUNT) ||
        (g_http_video_writing[slot] != 0U))
    {
        http_video_unlock(primask);
        return false;
    }
    ++g_http_video_references[slot];
    p_snapshot->data = g_http_video_slots[slot].wire;
    p_snapshot->length = g_http_video_wire_length[slot];
    p_snapshot->slot = slot;
    http_video_unlock(primask);
    return true;
}

void frame_stream_http_snapshot_release(uint8_t slot)
{
    if (slot >= HTTP_VIDEO_SLOT_COUNT)
    {
        return;
    }
    uint32_t primask = http_video_lock();
    if (g_http_video_references[slot] > 0U)
    {
        --g_http_video_references[slot];
    }
    http_video_unlock(primask);
}

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
static fsp_err_t frame_stream_send_frame(uint8_t const * p_frame,
                                         uint16_t width,
                                         uint16_t height,
                                         uint32_t frame_id);
static void frame_stream_queue_usb_frame(uint8_t const * p_frame,
                                         uint16_t width,
                                         uint16_t height,
                                         obstacle_detector_result_t const * p_detections,
                                         road_navigation_result_t const * p_navigation);
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
         * previous camera buffer is owned by the video task. */
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

fsp_err_t frame_stream_video_task_poll(void)
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
    /* The HTTP producer never waits for USB. USB diagnostics are staged into
     * a separate buffer and drained by a lower-priority task. */
    (void) frame_stream_send_navigation_now(g_video_usb_snapshot, width, height,
                                            &detections, &navigation);
    return FSP_SUCCESS;
}

fsp_err_t frame_stream_usb_task_poll(void)
{
    uint32_t primask = http_video_lock();
    if (g_usb_tx_state != USB_TX_READY)
    {
        http_video_unlock(primask);
        return FSP_SUCCESS;
    }
    g_usb_tx_state = USB_TX_SENDING;
    http_video_unlock(primask);

    obstacle_detector_result_t detections = g_usb_tx_detections;
    road_navigation_result_t navigation = g_usb_tx_navigation;
    uint16_t width = g_usb_tx_width;
    uint16_t height = g_usb_tx_height;
    uint32_t frame_id = g_usb_tx_frame_id;

    /* Logging and bulk image writes may wait on the PC host. They are isolated
     * here, below the HTTP publisher, and never run in the camera/AI task. */
    frame_stream_log_detection(&detections, &navigation, height);
    fsp_err_t err = frame_stream_send_ai_metadata(&detections, frame_id);
    if (FSP_SUCCESS == err)
    {
        err = frame_stream_send_navigation_metadata(&navigation, frame_id);
    }
    if (FSP_SUCCESS == err)
    {
        err = frame_stream_send_frame(g_usb_tx_snapshot, width, height, frame_id);
    }

    primask = http_video_lock();
    g_usb_tx_state = USB_TX_IDLE;
    http_video_unlock(primask);
    return err;
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

    /* p_frame is the packed, CPU-written g_video_usb_snapshot. Invalidating
     * it here can discard dirty copied pixels (and a padded-stride length
     * would run beyond the packed frame). DMA-source invalidation belongs at
     * the camera capture boundary, before the CPU makes this snapshot. */

    fsp_err_t err = road_navigation_render_rgb565(p_frame,
                                                   width,
                                                   height,
                                                   width,
                                                   p_detections,
                                                   p_navigation,
                                                   (uint16_t *) g_frame_stream_snapshot);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* The onboard HTTP view has no metadata channel, so bake the same road
     * segmentation/path overlay into its BMP and draw AI boxes here. */
    frame_stream_http_draw_course((uint16_t *) g_frame_stream_snapshot,
                                  width,
                                  height,
                                  p_navigation);
    frame_stream_http_draw_path_blocked((uint16_t *) g_frame_stream_snapshot,
                                         width,
                                         height,
                                         p_navigation);
    frame_stream_http_draw_obstacles((uint16_t *) g_frame_stream_snapshot,
                                     width,
                                     height,
                                     p_detections);
    frame_stream_http_publish(g_frame_stream_snapshot, width, height);

    /* USB is best effort. If its one-frame staging slot is still occupied,
     * keep the web image fresh and discard only this USB diagnostic frame. */
    frame_stream_queue_usb_frame(g_frame_stream_snapshot,
                                 width,
                                 height,
                                 p_detections,
                                 p_navigation);
    return FSP_SUCCESS;
}

static void frame_stream_queue_usb_frame(uint8_t const * p_frame,
                                         uint16_t width,
                                         uint16_t height,
                                         obstacle_detector_result_t const * p_detections,
                                         road_navigation_result_t const * p_navigation)
{
    if ((p_frame == NULL) || (p_detections == NULL) || (p_navigation == NULL))
    {
        return;
    }
    uint32_t payload_length = (uint32_t) width * height * FRAME_STREAM_BYTES_PER_PIXEL;
    if ((payload_length > VIN_BYTES_PER_FRAME) || (width == 0U) || (height == 0U))
    {
        return;
    }

    uint32_t primask = http_video_lock();
    if (g_usb_tx_state != USB_TX_IDLE)
    {
        http_video_unlock(primask);
        return;
    }
    g_usb_tx_state = USB_TX_PREPARING;
    http_video_unlock(primask);

    memcpy(g_usb_tx_snapshot, p_frame, payload_length);
    g_usb_tx_detections = *p_detections;
    g_usb_tx_navigation = *p_navigation;
    g_usb_tx_width = width;
    g_usb_tx_height = height;
    g_usb_tx_frame_id = g_frame_stream_frame_id++;
    __DMB();

    primask = http_video_lock();
    g_usb_tx_state = USB_TX_READY;
    http_video_unlock(primask);
}

static fsp_err_t frame_stream_send_frame(uint8_t const * p_frame,
                                         uint16_t width,
                                         uint16_t height,
                                         uint32_t frame_id)
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
    SCB_CleanDCache_by_Addr((void *) p_frame, (int32_t) payload_length);
#endif

    fsp_err_t err = usb_pcdc_console_write_if_ready(header, FRAME_STREAM_HEADER_SIZE);
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

        err = usb_pcdc_console_write_if_ready(&p_frame[offset], chunk);
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

    return usb_pcdc_console_write_if_ready(metadata,
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

    return usb_pcdc_console_write_if_ready(metadata,
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
