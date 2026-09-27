#include <stddef.h>
#include <string.h>
#include "hal_data.h"
#include "autonomy_controller.h"
#include "dualcore_board.h"
#include "user_config.h"
#include "m85_gateway_runtime.h"
static uint32_t g_perception_seq;
static unsigned g_initialized;
/* Debugger-visible snapshots.  Updated by the camera task without I/O. */
volatile uint32_t g_ai_person_count;
volatile uint32_t g_ai_car_count;
volatile uint32_t g_ai_navigation_stop_reason;
volatile uint32_t g_ai_processing_time_ms;
volatile uint32_t g_ai_submit_failures;
uint32_t autonomy_controller_now_ms(void) {
 uint32_t now=0xffffffffU;
 /* Capture ISR reads the published clock without taking HSEM or using producer. */
 if(DC_SHARED->ready==DC_READY&&DC_SHARED->control.magic==IPC_MAGIC) {
  __DMB();now=DC_SHARED->control.control_ms;
 }
 return now;
}
fsp_err_t autonomy_controller_init(void) {
 if(g_initialized)return FSP_SUCCESS;
 if(!m85_gateway_runtime_init())return FSP_ERR_TIMEOUT;
 g_initialized=1;
 return FSP_SUCCESS;
}
static float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

static float detection_corridor_overlap(obstacle_detection_t const * detection,
                                        uint16_t width,
                                        uint16_t corridor_center)
{
    uint32_t half = ((uint32_t) width * NAVIGATION_SAFETY_CORRIDOR_PERCENT) / 200U;
    uint32_t corridor_left = (corridor_center > half) ? corridor_center - half : 0U;
    uint32_t corridor_right = ((uint32_t) corridor_center + half < width) ?
                              (uint32_t) corridor_center + half : width;
    uint32_t detection_left = detection->x;
    uint32_t detection_right = (uint32_t) detection->x + detection->width;
    uint32_t overlap_left = (detection_left > corridor_left) ? detection_left : corridor_left;
    uint32_t overlap_right = (detection_right < corridor_right) ? detection_right : corridor_right;
    if (overlap_right <= overlap_left)
    {
        return 0.0F;
    }
    uint32_t detection_width = (detection->width > 0U) ? detection->width : 1U;
    return clamp_float((float) (overlap_right - overlap_left) / (float) detection_width,
                       0.0F,
                       1.0F);
}

static void add_synthetic_obstacle(ai_perception_result_t * perception, float confidence)
{
    if (perception->obstacle_count >= AI_CONTROL_MAX_OBSTACLES)
    {
        perception->obstacle_count = AI_CONTROL_MAX_OBSTACLES - 1U;
    }

    ai_obstacle_result_t * obstacle =
        &perception->obstacles[perception->obstacle_count++];
    obstacle->confidence = confidence;
    obstacle->corridor_overlap = 1.0F;
    obstacle->center_x = 0.0F;
    obstacle->bbox_bottom = 1.0F;
}

static void make_perception(ai_perception_result_t * perception,
                            road_navigation_result_t const * navigation,
                            obstacle_detector_result_t const * detections,
                            uint16_t width,
                            uint16_t height,
                            uint32_t now_ms)
{
    memset(perception, 0, sizeof(*perception));
    perception->interface_version = AI_CONTROL_INTERFACE_VERSION;
    perception->seq = ++g_perception_seq;
    perception->capture_timestamp_ms = now_ms;

    if ((NULL == navigation) || (NULL == detections) ||
        (0U == width) || (0U == height))
    {
        return;
    }

    uint32_t age = autonomy_controller_now_ms() - now_ms;
    perception->processing_time_ms = (uint16_t)(age > 65535U ? 65535U : age);
    perception->obstacle_valid = 1U;

    uint8_t path_geometry_valid =
        (uint8_t) ((navigation->path_count > 0U) &&
                   ((uint32_t) width * height <= MOTOR_CONTROL_MAX_FRAME_PIXELS) &&
                   (NAVIGATION_STOP_ROAD_NOT_FOUND != navigation->stop_reason) &&
                   (NAVIGATION_STOP_ROAD_TOO_NARROW != navigation->stop_reason) &&
                   (NAVIGATION_STOP_INVALID_INPUT != navigation->stop_reason));
    perception->path_valid = path_geometry_valid;
    perception->path_confidence =
        clamp_float((float) navigation->road_confidence_per_mille / 1000.0F, 0.0F, 1.0F);
    perception->lateral_error = clamp_float(
        ((float) navigation->target_x - ((float) width * 0.5F)) / ((float) width * 0.5F),
        -1.0F,
        1.0F);
    perception->heading_error = clamp_float(
        (float) navigation->steering_angle_cdeg / (float) NAVIGATION_MAX_STEERING_CDEG,
        -1.0F,
        1.0F);

    if (navigation->path_count > 0U)
    {
        road_path_sample_t const * near_path = &navigation->path[0];
        perception->path_width =
            clamp_float((float) (near_path->right - near_path->left) / (float) width,
                        0.0F,
                        1.0F);
    }

    uint32_t count = detections->detection_count;
    if (count > OBSTACLE_DETECTOR_MAX_DETECTIONS)
    {
        count = OBSTACLE_DETECTOR_MAX_DETECTIONS;
    }
    for (uint32_t index = 0U;
         (index < count) && (perception->obstacle_count < AI_CONTROL_MAX_OBSTACLES);
         index++)
    {
        obstacle_detection_t const * detection = &detections->detections[index];
        ai_obstacle_result_t * obstacle =
            &perception->obstacles[perception->obstacle_count++];
        obstacle->confidence =
            clamp_float((float) detection->score_per_mille / 1000.0F, 0.0F, 1.0F);
        obstacle->corridor_overlap =
            detection_corridor_overlap(detection, width, navigation->target_x);
        obstacle->center_x = clamp_float(
            (((float) detection->x + (float) detection->width * 0.5F) -
             (float) width * 0.5F) / ((float) width * 0.5F),
            -1.0F,
            1.0F);
        obstacle->bbox_bottom = clamp_float(
            (float) ((uint32_t) detection->y + detection->height) / (float) height,
            0.0F,
            1.0F);
    }

    if ((NAVIGATION_STOP_AI_OBJECT_AHEAD == navigation->stop_reason) ||
        (NAVIGATION_STOP_PATH_BLOCKED == navigation->stop_reason))
    {
        add_synthetic_obstacle(perception, 1.0F);
    }
}

void autonomy_controller_update(road_navigation_result_t const * nav,
                                obstacle_detector_result_t const * det,
                                uint16_t w,
                                uint16_t h,
                                uint32_t capture)
{
    if (!g_initialized)
    {
        return;
    }
    if (UINT32_MAX == capture)
    {
        /* No M33 clock at capture time: the frame cannot be time-stamped. */
        autonomy_controller_report_ai_unavailable();
        return;
    }
    ai_perception_result_t result;
    make_perception(&result, nav, det, w, h, capture);
    uint32_t person_count = 0U;
    uint32_t car_count = 0U;
    if (det)
    {
        uint32_t count = det->detection_count;
        if (count > OBSTACLE_DETECTOR_MAX_DETECTIONS)
        {
            count = OBSTACLE_DETECTOR_MAX_DETECTIONS;
        }
        for (uint32_t i = 0U; i < count; ++i)
        {
            person_count += det->detections[i].class_id == OBSTACLE_CLASS_PERSON;
            car_count += det->detections[i].class_id == OBSTACLE_CLASS_CAR;
        }
    }
    g_ai_person_count = person_count;
    g_ai_car_count = car_count;
    g_ai_navigation_stop_reason = nav ? nav->stop_reason : NAVIGATION_STOP_INVALID_INPUT;
    g_ai_processing_time_ms = result.processing_time_ms;
    /* A rejected submit needs no extra action: the M33 treats missing AI
     * frames as stale (AUTO -> TOR) on its own. */
    if (!m85_gateway_runtime_submit_ai(&result))
    {
        ++g_ai_submit_failures;
    }
}

void autonomy_controller_report_ai_unavailable(void)
{
    uint32_t now = autonomy_controller_now_ms();
    if (!g_initialized || (UINT32_MAX == now))
    {
        return; /* nothing can be stamped; M33 staleness handling applies */
    }
    ai_perception_result_t result;
    /* NULL inputs produce path_valid=0 / obstacle_valid=0. */
    make_perception(&result, NULL, NULL, 0U, 0U, now);
    (void) m85_gateway_runtime_submit_ai(&result);
}

void autonomy_controller_service(void)
{
    /* Intentionally empty: see autonomy_controller.h. */
}
