/***********************************************************************************************************************
 * File Name    : road_navigation.c
 * Description  : Lightweight colour/geometry road analysis for the periodic USB image stream.
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "road_navigation.h"
#include "user_config.h"

#define SEGMENT_COLOR_ROAD        (NAVIGATION_SEGMENT_COLOR_ROAD_RGB565)
#define SEGMENT_COLOR_WALL        (NAVIGATION_SEGMENT_COLOR_WALL_RGB565)
#define SEGMENT_COLOR_PATH        (0xFFE0U)
#define SEGMENT_COLOR_TARGET      (0xF800U)

typedef struct st_navigation_rgb
{
    uint16_t red;
    uint16_t green;
    uint16_t blue;
} navigation_rgb_t;

static int16_t g_smoothed_steering_cdeg = 0;

static int32_t navigation_abs_i32(int32_t value);
static int32_t navigation_clamp_i32(int32_t value, int32_t minimum, int32_t maximum);
static navigation_rgb_t navigation_pixel_to_rgb(uint16_t pixel);
static bool navigation_is_road_color(uint16_t pixel);
static bool navigation_is_wall_color(uint16_t pixel);
static void navigation_detection_avoid_bounds(obstacle_detection_t const * p_detection,
                                               uint32_t * p_left,
                                               uint32_t * p_top,
                                               uint32_t * p_right,
                                               uint32_t * p_bottom);
static bool navigation_point_in_detection(uint32_t x,
                                          uint32_t y,
                                          obstacle_detector_result_t const * p_detections);
static bool navigation_sample_is_road(uint16_t const * p_pixels,
                                      uint16_t stride_pixels,
                                      uint32_t x,
                                      uint32_t y,
                                      road_navigation_result_t const * p_result,
                                      obstacle_detector_result_t const * p_detections);
static void navigation_measure_color_coverage(uint16_t const * p_pixels,
                                              uint16_t width,
                                              uint16_t height,
                                              uint16_t stride_pixels,
                                              road_navigation_result_t * p_result);
static bool navigation_find_run(uint16_t const * p_pixels,
                                uint16_t width,
                                uint16_t stride_pixels,
                                uint32_t y,
                                uint32_t expected_center,
                                road_navigation_result_t const * p_result,
                                obstacle_detector_result_t const * p_detections,
                                uint16_t * p_left,
                                uint16_t * p_center,
                                uint16_t * p_right);
static void navigation_bounds_at_y(road_navigation_result_t const * p_result,
                                   uint16_t width,
                                   uint16_t y,
                                   uint16_t * p_left,
                                   uint16_t * p_center,
                                   uint16_t * p_right);
static uint16_t navigation_measure_path_occupancy(uint16_t const * p_pixels,
                                                  uint16_t width,
                                                  uint16_t height,
                                                  uint16_t stride_pixels,
                                                  road_navigation_result_t const * p_result);
static uint16_t navigation_measure_ai_risk(uint16_t width,
                                           uint16_t height,
                                           obstacle_detector_result_t const * p_detections,
                                           road_navigation_result_t const * p_result,
                                           bool * p_ai_object_ahead);
static void navigation_draw_line(uint16_t * p_output,
                                 uint16_t width,
                                 uint16_t height,
                                 int32_t x0,
                                 int32_t y0,
                                 int32_t x1,
                                 int32_t y1,
                                 uint16_t color);
static void navigation_draw_target(uint16_t * p_output,
                                   uint16_t width,
                                   uint16_t height,
                                   uint16_t x,
                                   uint16_t y);

fsp_err_t road_navigation_analyze_rgb565(uint8_t const * p_frame,
                                         uint16_t width,
                                         uint16_t height,
                                         uint16_t stride_pixels,
                                         obstacle_detector_result_t const * p_detections,
                                         road_navigation_result_t * p_result)
{
    if ((NULL == p_frame) || (NULL == p_result) || (0U == width) || (0U == height) ||
        (stride_pixels < width))
    {
        if (NULL != p_result)
        {
            memset(p_result, 0, sizeof(*p_result));
            p_result->stop_reason = (uint8_t) NAVIGATION_STOP_INVALID_INPUT;
        }
        return FSP_ERR_INVALID_ARGUMENT;
    }

    memset(p_result, 0, sizeof(*p_result));
    p_result->stop_reason = (uint8_t) NAVIGATION_STOP_ROAD_NOT_FOUND;
    p_result->horizon_y = (uint16_t) (((uint32_t) height * NAVIGATION_HORIZON_PERCENT) / 100U);

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    {
        uint32_t source_length = (((uint32_t) height - 1U) * stride_pixels + width) * 2U;
        SCB_InvalidateDCache_by_Addr((void *) p_frame, (int32_t) source_length);
    }
#endif

    uint16_t const * p_pixels = (uint16_t const *) p_frame;
    navigation_measure_color_coverage(p_pixels, width, height, stride_pixels, p_result);

    uint32_t bottom_y = (uint32_t) height - 1U;
    uint32_t scan_height = bottom_y - p_result->horizon_y;
    uint32_t expected_center = (uint32_t) width / 2U;
    uint32_t valid_count = 0U;
    uint32_t width_sum = 0U;

    for (uint32_t i = 0U; i < ROAD_NAVIGATION_PATH_POINTS; i++)
    {
        uint32_t y = bottom_y -
                     (scan_height * i) / (ROAD_NAVIGATION_PATH_POINTS - 1U);
        uint16_t left = 0U;
        uint16_t center = (uint16_t) expected_center;
        uint16_t right = (uint16_t) ((uint32_t) width - 1U);
        bool valid = navigation_find_run(p_pixels,
                                         width,
                                         stride_pixels,
                                         y,
                                         expected_center,
                                         p_result,
                                         p_detections,
                                         &left,
                                         &center,
                                         &right);

        if (valid)
        {
            valid_count++;
            p_result->path_valid_mask |= (uint16_t) (1U << i);
            width_sum += (uint32_t) right - left + 1U;
            expected_center = ((expected_center * 2U) + center) / 3U;
        }
        else if (i > 0U)
        {
            road_path_sample_t const * p_previous = &p_result->path[i - 1U];
            uint32_t previous_half_width = ((uint32_t) p_previous->right - p_previous->left) / 2U;
            uint32_t fallback_half_width = (previous_half_width * 9U) / 10U;
            center = (uint16_t) expected_center;
            left = (uint16_t) ((expected_center > fallback_half_width) ?
                               (expected_center - fallback_half_width) : 0U);
            right = (uint16_t) (((expected_center + fallback_half_width) < width) ?
                                (expected_center + fallback_half_width) : ((uint32_t) width - 1U));
        }

        p_result->path[i].y = (uint16_t) y;
        p_result->path[i].left = left;
        p_result->path[i].center = center;
        p_result->path[i].right = right;
    }
    p_result->path_count = ROAD_NAVIGATION_PATH_POINTS;

    /* 2026-09-27: confidence counts only the near half of the scan rows, so the
     * paper ending or bending out of view far ahead no longer ends AUTO early.
     * The far rows are still used for the target/heading when they are seen. */
    uint32_t near_valid = 0U;
    for (uint32_t i = 0U; i < NAVIGATION_CONFIDENCE_ROWS; i++)
    {
        if (0U != (p_result->path_valid_mask & (uint16_t) (1U << i)))
        {
            near_valid++;
        }
    }
    uint32_t valid_ratio = (near_valid * 1000U) / NAVIGATION_CONFIDENCE_ROWS;
    uint32_t average_width = (valid_count > 0U) ? (width_sum / valid_count) : 0U;
    uint32_t width_ratio = (average_width * 1000U) / width;
    if (width_ratio > 1000U)
    {
        width_ratio = 1000U;
    }
    p_result->road_confidence_per_mille =
        (uint16_t) ((valid_ratio * 4U + width_ratio) / 5U);

    uint32_t target_index =
        ((ROAD_NAVIGATION_PATH_POINTS - 1U) * NAVIGATION_LOOKAHEAD_PERCENT) / 100U;
    bool target_is_measured = false;
    if (target_index >= ROAD_NAVIGATION_PATH_POINTS)
    {
        target_index = ROAD_NAVIGATION_PATH_POINTS - 1U;
    }

    /* Prefer the configured lookahead when measured. If it is missing, use
     * the nearest measured point toward the camera before considering farther
     * points. Extrapolated rows remain useful for display/safety geometry only. */
    if (0U == (p_result->path_valid_mask & (uint16_t) (1U << target_index)))
    {
        uint32_t selected_index = ROAD_NAVIGATION_PATH_POINTS;
        for (uint32_t i = target_index + 1U; i > 0U; i--)
        {
            uint32_t candidate = i - 1U;
            if (0U != (p_result->path_valid_mask & (uint16_t) (1U << candidate)))
            {
                selected_index = candidate;
                break;
            }
        }
        /* If no nearer/mid sample exists, use the first recognized farther row. */
        for (uint32_t i = target_index + 1U;
             (ROAD_NAVIGATION_PATH_POINTS == selected_index) &&
             (i < ROAD_NAVIGATION_PATH_POINTS);
             i++)
        {
            if (0U != (p_result->path_valid_mask & (uint16_t) (1U << i)))
            {
                selected_index = i;
            }
        }
        if (selected_index < ROAD_NAVIGATION_PATH_POINTS)
        {
            target_index = selected_index;
        }
    }
    target_is_measured =
        (0U != (p_result->path_valid_mask & (uint16_t) (1U << target_index)));
    p_result->target_x = p_result->path[target_index].center;
    p_result->target_y = p_result->path[target_index].y;
    if (!target_is_measured)
    {
        /* No recognized row exists: do not expose display fill as a steering command. */
        p_result->target_x = (uint16_t) (width / 2U);
    }

    int32_t delta_x = (int32_t) p_result->target_x - ((int32_t) width / 2);
    int32_t delta_y = (int32_t) height - (int32_t) p_result->target_y;
    if (delta_y < 1)
    {
        delta_y = 1;
    }
    int32_t raw_angle = target_is_measured ?
                        ((delta_x * 5730 * NAVIGATION_STEERING_GAIN_PERCENT) /
                         (delta_y * 100)) : 0;
    raw_angle = navigation_clamp_i32(raw_angle,
                                     -NAVIGATION_MAX_STEERING_CDEG,
                                     NAVIGATION_MAX_STEERING_CDEG);
    g_smoothed_steering_cdeg = target_is_measured ?
        (int16_t) (((int32_t) g_smoothed_steering_cdeg * 3 + raw_angle) / 4) : 0;
    p_result->steering_angle_cdeg = g_smoothed_steering_cdeg;

    uint16_t path_occupancy = navigation_measure_path_occupancy(p_pixels,
                                                                width,
                                                                height,
                                                                stride_pixels,
                                                                p_result);
    bool ai_object_ahead = false;
    uint16_t ai_risk = navigation_measure_ai_risk(width,
                                                  height,
                                                  p_detections,
                                                  p_result,
                                                  &ai_object_ahead);
    p_result->obstacle_risk_per_mille = (path_occupancy > ai_risk) ?
                                        path_occupancy : ai_risk;

    uint32_t bottom_width = (uint32_t) p_result->path[0].right -
                            p_result->path[0].left + 1U;
    uint32_t target_width = (uint32_t) p_result->path[target_index].right -
                            p_result->path[target_index].left + 1U;
    /* Lenient (2026-09-27 field feedback): one of the three nearest rows must be
     * measured (not only the very bottom row), and the steering target may be
     * the nearest measured row chosen above instead of the exact lookahead. */
    bool near_measured = (0U != (p_result->path_valid_mask & 0x7U));
    bool road_too_narrow =
        (!near_measured) || (!target_is_measured) ||
        (bottom_width * 100U < (uint32_t) width * NAVIGATION_MIN_BOTTOM_ROAD_WIDTH_PERCENT) ||
        (target_width * 100U < (uint32_t) width * NAVIGATION_MIN_LOOKAHEAD_ROAD_WIDTH_PERCENT);

    /* A recognized forward person/car takes precedence over apparent road
     * loss: a large object can hide the white strip in the same frame. */
    if (ai_object_ahead)
    {
        p_result->stop_reason = (uint8_t) NAVIGATION_STOP_AI_OBJECT_AHEAD;
    }
    else if (p_result->road_confidence_per_mille < NAVIGATION_MIN_ROAD_CONFIDENCE_PER_MILLE)
    {
        p_result->stop_reason = (uint8_t) NAVIGATION_STOP_ROAD_NOT_FOUND;
    }
    else if (road_too_narrow)
    {
        p_result->stop_reason = (uint8_t) NAVIGATION_STOP_ROAD_TOO_NARROW;
    }
    else if (path_occupancy >= NAVIGATION_BLOCKED_THRESHOLD_PER_MILLE)
    {
        p_result->stop_reason = (uint8_t) NAVIGATION_STOP_PATH_BLOCKED;
    }
    else
    {
        uint32_t turn = (uint32_t) navigation_abs_i32(p_result->steering_angle_cdeg);
        uint32_t reduction = (turn * NAVIGATION_CRUISE_THROTTLE_PER_MILLE) /
                             (2U * NAVIGATION_MAX_STEERING_CDEG);
        uint32_t throttle = NAVIGATION_CRUISE_THROTTLE_PER_MILLE - reduction;
        if (throttle < NAVIGATION_MIN_THROTTLE_PER_MILLE)
        {
            throttle = NAVIGATION_MIN_THROTTLE_PER_MILLE;
        }

        p_result->drive_allowed = 1U;
        p_result->stop_reason = (uint8_t) NAVIGATION_STOP_NONE;
        p_result->throttle_per_mille = (uint16_t) throttle;
    }

    if (0U == p_result->drive_allowed)
    {
        p_result->throttle_per_mille = 0U;
    }

    return FSP_SUCCESS;
}

int16_t road_navigation_heading_cdeg(road_navigation_result_t const * p_result)
{
    if ((NULL == p_result) || (p_result->path_count < 2U))
    {
        return 0;
    }

    uint32_t target_index = ROAD_NAVIGATION_PATH_POINTS;
    uint32_t count = p_result->path_count;
    if (count > ROAD_NAVIGATION_PATH_POINTS)
    {
        count = ROAD_NAVIGATION_PATH_POINTS;
    }
    for (uint32_t i = 0U; i < count; i++)
    {
        if ((p_result->path[i].y == p_result->target_y) &&
            (0U != (p_result->path_valid_mask & (uint16_t) (1U << i))))
        {
            target_index = i;
            break;
        }
    }
    if (ROAD_NAVIGATION_PATH_POINTS == target_index)
    {
        return 0;
    }

    uint32_t near_index = ROAD_NAVIGATION_PATH_POINTS;
    uint32_t far_index = ROAD_NAVIGATION_PATH_POINTS;
    for (uint32_t i = 0U; i <= target_index; i++)
    {
        if (0U != (p_result->path_valid_mask & (uint16_t) (1U << i)))
        {
            if (ROAD_NAVIGATION_PATH_POINTS == near_index)
            {
                near_index = i;
            }
            far_index = i;
        }
    }
    if ((ROAD_NAVIGATION_PATH_POINTS == near_index) || (far_index == near_index))
    {
        return 0;
    }

    int32_t dx = (int32_t) p_result->path[far_index].center -
                 (int32_t) p_result->path[near_index].center;
    int32_t dy = (int32_t) p_result->path[near_index].y -
                 (int32_t) p_result->path[far_index].y;
    /* Ignore endpoint movement below one scan interval; it is within the
     * resolution of the sampled edge detector rather than a reliable slope. */
    if ((dy <= 0) ||
        (navigation_abs_i32(dx) <= (int32_t) NAVIGATION_SCAN_STEP_PIXELS))
    {
        return 0;
    }
    return (int16_t) navigation_clamp_i32((dx * 5730) / dy,
                                          -NAVIGATION_MAX_STEERING_CDEG,
                                          NAVIGATION_MAX_STEERING_CDEG);
}

fsp_err_t road_navigation_render_rgb565(uint8_t const * p_frame,
                                        uint16_t width,
                                        uint16_t height,
                                        uint16_t stride_pixels,
                                        obstacle_detector_result_t const * p_detections,
                                        road_navigation_result_t const * p_navigation,
                                        uint16_t * p_output)
{
    if ((NULL == p_frame) || (NULL == p_navigation) || (NULL == p_output) ||
        (0U == width) || (0U == height) || (stride_pixels < width))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    uint16_t const * p_pixels = (uint16_t const *) p_frame;
    for (uint32_t y = 0U; y < height; y++)
    {
        for (uint32_t x = 0U; x < width; x++)
        {
            uint16_t pixel = p_pixels[y * stride_pixels + x];
            uint16_t output_color = pixel;
            if (navigation_is_road_color(pixel))
            {
                output_color = SEGMENT_COLOR_ROAD;
            }
            else if (navigation_is_wall_color(pixel))
            {
                output_color = SEGMENT_COLOR_WALL;
            }

            p_output[y * width + x] = output_color;
        }
    }

    /* The PC viewer draws boxes from RAAI metadata. Do not hide detected
     * objects by replacing their pixels with a solid colour. */
    (void) p_detections;

    if (p_navigation->path_count > 1U)
    {
        uint32_t count = p_navigation->path_count;
        if (count > ROAD_NAVIGATION_PATH_POINTS)
        {
            count = ROAD_NAVIGATION_PATH_POINTS;
        }
        for (uint32_t i = 1U; i < count; i++)
        {
            navigation_draw_line(p_output,
                                 width,
                                 height,
                                 p_navigation->path[i - 1U].center,
                                 p_navigation->path[i - 1U].y,
                                 p_navigation->path[i].center,
                                 p_navigation->path[i].y,
                                 SEGMENT_COLOR_PATH);
        }
    }

    navigation_draw_target(p_output,
                           width,
                           height,
                           p_navigation->target_x,
                           p_navigation->target_y);
    return FSP_SUCCESS;
}

static int32_t navigation_abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static int32_t navigation_clamp_i32(int32_t value, int32_t minimum, int32_t maximum)
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

static navigation_rgb_t navigation_pixel_to_rgb(uint16_t pixel)
{
    navigation_rgb_t color = {0};
    color.red = (uint16_t) (((pixel >> 11) & 0x1FU) * 255U / 31U);
    color.green = (uint16_t) (((pixel >> 5) & 0x3FU) * 255U / 63U);
    color.blue = (uint16_t) ((pixel & 0x1FU) * 255U / 31U);
    return color;
}

static bool navigation_is_road_color(uint16_t pixel)
{
    navigation_rgb_t color = navigation_pixel_to_rgb(pixel);
    uint16_t maximum = color.red;
    uint16_t minimum = color.red;
    if (color.green > maximum)
    {
        maximum = color.green;
    }
    if (color.blue > maximum)
    {
        maximum = color.blue;
    }
    if (color.green < minimum)
    {
        minimum = color.green;
    }
    if (color.blue < minimum)
    {
        minimum = color.blue;
    }

    return (color.red >= NAVIGATION_WHITE_R_MIN) && (color.red <= NAVIGATION_WHITE_R_MAX) &&
           (color.green >= NAVIGATION_WHITE_G_MIN) && (color.green <= NAVIGATION_WHITE_G_MAX) &&
           (color.blue >= NAVIGATION_WHITE_B_MIN) && (color.blue <= NAVIGATION_WHITE_B_MAX) &&
           (((uint32_t) maximum - minimum) <= NAVIGATION_WHITE_MAX_CHANNEL_SPREAD);
}

static bool navigation_is_wall_color(uint16_t pixel)
{
    navigation_rgb_t color = navigation_pixel_to_rgb(pixel);
    return (color.red >= NAVIGATION_BROWN_R_MIN) && (color.red <= NAVIGATION_BROWN_R_MAX) &&
           (color.green >= NAVIGATION_BROWN_G_MIN) && (color.green <= NAVIGATION_BROWN_G_MAX) &&
           (color.blue >= NAVIGATION_BROWN_B_MIN) && (color.blue <= NAVIGATION_BROWN_B_MAX) &&
           (color.red >= (color.green + NAVIGATION_BROWN_MIN_RED_OVER_GREEN)) &&
           (color.green >= (color.blue + NAVIGATION_BROWN_MIN_GREEN_OVER_BLUE)) &&
           (color.red <= (color.green + NAVIGATION_BROWN_MAX_RED_OVER_GREEN));
}

static void navigation_detection_avoid_bounds(obstacle_detection_t const * p_detection,
                                               uint32_t * p_left,
                                               uint32_t * p_top,
                                               uint32_t * p_right,
                                               uint32_t * p_bottom)
{
    uint32_t horizontal_margin =
        ((uint32_t) p_detection->width * NAVIGATION_AI_AVOID_HORIZONTAL_MARGIN_PERCENT) / 100U;
    uint32_t vertical_margin =
        ((uint32_t) p_detection->height * NAVIGATION_AI_AVOID_VERTICAL_MARGIN_PERCENT) / 100U;
    if (horizontal_margin < NAVIGATION_AI_AVOID_MIN_MARGIN_PIXELS)
    {
        horizontal_margin = NAVIGATION_AI_AVOID_MIN_MARGIN_PIXELS;
    }
    if (vertical_margin < NAVIGATION_AI_AVOID_MIN_MARGIN_PIXELS)
    {
        vertical_margin = NAVIGATION_AI_AVOID_MIN_MARGIN_PIXELS;
    }

    *p_left = ((uint32_t) p_detection->x > horizontal_margin) ?
              ((uint32_t) p_detection->x - horizontal_margin) : 0U;
    *p_top = ((uint32_t) p_detection->y > vertical_margin) ?
             ((uint32_t) p_detection->y - vertical_margin) : 0U;
    *p_right = (uint32_t) p_detection->x + p_detection->width + horizontal_margin;
    *p_bottom = (uint32_t) p_detection->y + p_detection->height + vertical_margin;
}

static bool navigation_point_in_detection(uint32_t x,
                                          uint32_t y,
                                          obstacle_detector_result_t const * p_detections)
{
    if (NULL == p_detections)
    {
        return false;
    }

    uint32_t count = p_detections->detection_count;
    if (count > OBSTACLE_DETECTOR_MAX_DETECTIONS)
    {
        count = OBSTACLE_DETECTOR_MAX_DETECTIONS;
    }

    for (uint32_t i = 0U; i < count; i++)
    {
        obstacle_detection_t const * p_detection = &p_detections->detections[i];
        uint32_t left = 0U;
        uint32_t top = 0U;
        uint32_t right = 0U;
        uint32_t bottom = 0U;
        navigation_detection_avoid_bounds(p_detection, &left, &top, &right, &bottom);
        if ((x >= left) && (x < right) && (y >= top) && (y < bottom))
        {
            return true;
        }
    }
    return false;
}

static bool navigation_sample_is_road(uint16_t const * p_pixels,
                                      uint16_t stride_pixels,
                                      uint32_t x,
                                      uint32_t y,
                                      road_navigation_result_t const * p_result,
                                      obstacle_detector_result_t const * p_detections)
{
    if (navigation_point_in_detection(x, y, p_detections))
    {
        return false;
    }
    (void) p_result;
    return navigation_is_road_color(p_pixels[y * stride_pixels + x]);
}

static void navigation_measure_color_coverage(uint16_t const * p_pixels,
                                              uint16_t width,
                                              uint16_t height,
                                              uint16_t stride_pixels,
                                              road_navigation_result_t * p_result)
{
    uint32_t white_count = 0U;
    uint32_t brown_count = 0U;
    uint32_t total_count = 0U;
    for (uint32_t y = p_result->horizon_y; y < height; y += NAVIGATION_COLOR_SAMPLE_STEP)
    {
        for (uint32_t x = 0U; x < width; x += NAVIGATION_COLOR_SAMPLE_STEP)
        {
            uint16_t pixel = p_pixels[y * stride_pixels + x];
            total_count++;
            if (navigation_is_road_color(pixel))
            {
                white_count++;
            }
            else if (navigation_is_wall_color(pixel))
            {
                brown_count++;
            }
        }
    }

    if (total_count > 0U)
    {
        p_result->white_ratio_per_mille = (uint16_t) ((white_count * 1000U) / total_count);
        p_result->brown_ratio_per_mille = (uint16_t) ((brown_count * 1000U) / total_count);
    }
}

static bool navigation_find_run(uint16_t const * p_pixels,
                                uint16_t width,
                                uint16_t stride_pixels,
                                uint32_t y,
                                uint32_t expected_center,
                                road_navigation_result_t const * p_result,
                                obstacle_detector_result_t const * p_detections,
                                uint16_t * p_left,
                                uint16_t * p_center,
                                uint16_t * p_right)
{
    uint32_t step = NAVIGATION_SCAN_STEP_PIXELS;
    uint32_t search_radius = (uint32_t) width / 4U;
    uint32_t seed_x = expected_center;
    bool found = navigation_sample_is_road(p_pixels,
                                           stride_pixels,
                                           seed_x,
                                           y,
                                           p_result,
                                           p_detections);

    for (uint32_t distance = step; (!found) && (distance <= search_radius); distance += step)
    {
        if ((expected_center >= distance) &&
            navigation_sample_is_road(p_pixels,
                                      stride_pixels,
                                      expected_center - distance,
                                      y,
                                      p_result,
                                      p_detections))
        {
            seed_x = expected_center - distance;
            found = true;
        }
        else if ((expected_center + distance < width) &&
                 navigation_sample_is_road(p_pixels,
                                           stride_pixels,
                                           expected_center + distance,
                                           y,
                                           p_result,
                                           p_detections))
        {
            seed_x = expected_center + distance;
            found = true;
        }
    }

    if (!found)
    {
        return false;
    }

    uint32_t left = seed_x;
    uint32_t right = seed_x;
    uint32_t misses = 0U;
    while (left >= step)
    {
        uint32_t candidate = left - step;
        if (navigation_sample_is_road(p_pixels,
                                      stride_pixels,
                                      candidate,
                                      y,
                                      p_result,
                                      p_detections))
        {
            left = candidate;
            misses = 0U;
        }
        else
        {
            misses++;
            left = candidate;
            if (misses >= NAVIGATION_MAX_EDGE_MISSES)
            {
                left += misses * step;
                break;
            }
        }
    }

    misses = 0U;
    while (right + step < width)
    {
        uint32_t candidate = right + step;
        if (navigation_sample_is_road(p_pixels,
                                      stride_pixels,
                                      candidate,
                                      y,
                                      p_result,
                                      p_detections))
        {
            right = candidate;
            misses = 0U;
        }
        else
        {
            misses++;
            right = candidate;
            if (misses >= NAVIGATION_MAX_EDGE_MISSES)
            {
                right -= misses * step;
                break;
            }
        }
    }

    /* 2026-09-27: 6 -> 4 % of the width so a narrow or distant strip still counts. */
    if ((right <= left) || ((right - left) * 100U < (uint32_t) width * 4U))
    {
        return false;
    }

    *p_left = (uint16_t) left;
    *p_right = (uint16_t) right;
    *p_center = (uint16_t) ((left + right) / 2U);
    return true;
}

static void navigation_bounds_at_y(road_navigation_result_t const * p_result,
                                   uint16_t width,
                                   uint16_t y,
                                   uint16_t * p_left,
                                   uint16_t * p_center,
                                   uint16_t * p_right)
{
    uint32_t count = p_result->path_count;
    if (count > ROAD_NAVIGATION_PATH_POINTS)
    {
        count = ROAD_NAVIGATION_PATH_POINTS;
    }
    if (0U == count)
    {
        *p_left = 0U;
        *p_center = (uint16_t) ((uint32_t) width / 2U);
        *p_right = (uint16_t) ((uint32_t) width - 1U);
        return;
    }

    if (y >= p_result->path[0].y)
    {
        *p_left = p_result->path[0].left;
        *p_center = p_result->path[0].center;
        *p_right = p_result->path[0].right;
        return;
    }
    if (y <= p_result->path[count - 1U].y)
    {
        *p_left = p_result->path[count - 1U].left;
        *p_center = p_result->path[count - 1U].center;
        *p_right = p_result->path[count - 1U].right;
        return;
    }

    for (uint32_t i = 1U; i < count; i++)
    {
        road_path_sample_t const * p_near = &p_result->path[i - 1U];
        road_path_sample_t const * p_far = &p_result->path[i];
        if ((y <= p_near->y) && (y >= p_far->y))
        {
            uint32_t span = (uint32_t) p_near->y - p_far->y;
            uint32_t position = (uint32_t) p_near->y - y;
            if (0U == span)
            {
                span = 1U;
            }
            *p_left = (uint16_t) (((uint32_t) p_near->left * (span - position) +
                                   (uint32_t) p_far->left * position) / span);
            *p_center = (uint16_t) (((uint32_t) p_near->center * (span - position) +
                                     (uint32_t) p_far->center * position) / span);
            *p_right = (uint16_t) (((uint32_t) p_near->right * (span - position) +
                                    (uint32_t) p_far->right * position) / span);
            return;
        }
    }

    *p_left = p_result->path[count - 1U].left;
    *p_center = p_result->path[count - 1U].center;
    *p_right = p_result->path[count - 1U].right;
}

static uint16_t navigation_measure_path_occupancy(uint16_t const * p_pixels,
                                                  uint16_t width,
                                                  uint16_t height,
                                                  uint16_t stride_pixels,
                                                  road_navigation_result_t const * p_result)
{
    uint32_t start_y = ((uint32_t) height * NAVIGATION_SAFETY_ROI_TOP_PERCENT) / 100U;
    uint32_t end_y = ((uint32_t) height * 92U) / 100U;
    /* 2026-09-27: only look for an obstruction where the strip was actually
     * seen (up to the farthest measured row).  Beyond the end of the paper
     * there is nothing to block, so a short visible strip is still drivable. */
    for (uint32_t i = ROAD_NAVIGATION_PATH_POINTS; i > 0U; i--)
    {
        if (0U != (p_result->path_valid_mask & (uint16_t) (1U << (i - 1U))))
        {
            if (p_result->path[i - 1U].y > start_y)
            {
                start_y = p_result->path[i - 1U].y;
            }
            break;
        }
    }
    uint32_t blocked = 0U;
    uint32_t total = 0U;
    uint32_t half_corridor = ((uint32_t) width * NAVIGATION_SAFETY_CORRIDOR_PERCENT) / 200U;
    if (half_corridor < 4U)
    {
        half_corridor = 4U;
    }

    for (uint32_t y = start_y; y < end_y; y += NAVIGATION_SCAN_STEP_PIXELS)
    {
        uint16_t left = 0U;
        uint16_t center = 0U;
        uint16_t right = 0U;
        navigation_bounds_at_y(p_result, width, (uint16_t) y, &left, &center, &right);
        uint32_t start_x = ((uint32_t) center > half_corridor) ?
                           ((uint32_t) center - half_corridor) : 0U;
        uint32_t end_x = ((uint32_t) center + half_corridor < width) ?
                         ((uint32_t) center + half_corridor) : ((uint32_t) width - 1U);
        if (start_x < left)
        {
            start_x = left;
        }
        if (end_x > right)
        {
            end_x = right;
        }

        for (uint32_t x = start_x; x <= end_x; x += NAVIGATION_SCAN_STEP_PIXELS)
        {
            total++;
            if (!navigation_is_road_color(p_pixels[y * stride_pixels + x]))
            {
                blocked++;
            }
        }
    }

    return (uint16_t) ((total > 0U) ? ((blocked * 1000U) / total) : 1000U);
}

static uint16_t navigation_measure_ai_risk(uint16_t width,
                                           uint16_t height,
                                           obstacle_detector_result_t const * p_detections,
                                           road_navigation_result_t const * p_result,
                                           bool * p_ai_object_ahead)
{
    *p_ai_object_ahead = false;
    if (NULL == p_detections)
    {
        return 0U;
    }

    uint32_t count = p_detections->detection_count;
    if (count > OBSTACLE_DETECTOR_MAX_DETECTIONS)
    {
        count = OBSTACLE_DETECTOR_MAX_DETECTIONS;
    }
    uint32_t maximum_risk = 0U;

    for (uint32_t i = 0U; i < count; i++)
    {
        obstacle_detection_t const * p_detection = &p_detections->detections[i];
        if ((OBSTACLE_CLASS_PERSON != p_detection->class_id) &&
            (OBSTACLE_CLASS_CAR != p_detection->class_id))
        {
            continue;
        }
        uint32_t detection_left = 0U;
        uint32_t detection_top = 0U;
        uint32_t detection_right = 0U;
        uint32_t detection_bottom = 0U;
        navigation_detection_avoid_bounds(p_detection,
                                           &detection_left,
                                           &detection_top,
                                           &detection_right,
                                           &detection_bottom);
        (void) detection_top;
        if (detection_right > 0U)
        {
            detection_right--;
        }
        detection_bottom = (uint32_t) p_detection->y + p_detection->height;
        if (detection_bottom > 0U)
        {
            detection_bottom--;
        }
        if (detection_bottom >= height)
        {
            detection_bottom = (uint32_t) height - 1U;
        }
        if (detection_right >= width)
        {
            detection_right = (uint32_t) width - 1U;
        }

        (void) p_result;
        uint32_t safety_half_width = ((uint32_t) width * NAVIGATION_SAFETY_CORRIDOR_PERCENT) / 200U;
        uint32_t camera_center = (uint32_t) width / 2U;
        uint32_t safety_left = (camera_center > safety_half_width) ?
                               (camera_center - safety_half_width) : 0U;
        uint32_t safety_right = ((camera_center + safety_half_width) < width) ?
                                (camera_center + safety_half_width) : ((uint32_t) width - 1U);
        /* For the straight white-strip demo, a detected person/car in front
         * must stop even if path extraction finds white space around it. */
        bool overlaps_path = (detection_right >= safety_left) && (detection_left <= safety_right);
        if (overlaps_path && (detection_bottom > p_result->horizon_y))
        {
            uint32_t proximity = (detection_bottom * 1000U) / height;
            uint32_t risk = ((uint32_t) p_detection->score_per_mille + proximity) / 2U;
            if (risk > maximum_risk)
            {
                maximum_risk = risk;
            }
            if (detection_bottom * 100U >=
                (uint32_t) height * NAVIGATION_AI_STOP_BOTTOM_PERCENT)
            {
                *p_ai_object_ahead = true;
            }
        }
    }

    return (uint16_t) maximum_risk;
}

static void navigation_draw_line(uint16_t * p_output,
                                 uint16_t width,
                                 uint16_t height,
                                 int32_t x0,
                                 int32_t y0,
                                 int32_t x1,
                                 int32_t y1,
                                 uint16_t color)
{
    int32_t dx = navigation_abs_i32(x1 - x0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t dy = -navigation_abs_i32(y1 - y0);
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t error = dx + dy;

    for (;;)
    {
        if ((x0 >= 0) && (x0 < (int32_t) width) &&
            (y0 >= 0) && (y0 < (int32_t) height))
        {
            p_output[(uint32_t) y0 * width + (uint32_t) x0] = color;
            if (x0 + 1 < (int32_t) width)
            {
                p_output[(uint32_t) y0 * width + (uint32_t) (x0 + 1)] = color;
            }
        }
        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }
        int32_t twice_error = error * 2;
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

static void navigation_draw_target(uint16_t * p_output,
                                   uint16_t width,
                                   uint16_t height,
                                   uint16_t x,
                                   uint16_t y)
{
    for (int32_t offset = -5; offset <= 5; offset++)
    {
        int32_t horizontal_x = (int32_t) x + offset;
        int32_t vertical_y = (int32_t) y + offset;
        if ((horizontal_x >= 0) && (horizontal_x < (int32_t) width) && (y < height))
        {
            p_output[(uint32_t) y * width + (uint32_t) horizontal_x] = SEGMENT_COLOR_TARGET;
        }
        if ((x < width) && (vertical_y >= 0) && (vertical_y < (int32_t) height))
        {
            p_output[(uint32_t) vertical_y * width + x] = SEGMENT_COLOR_TARGET;
        }
    }
}
