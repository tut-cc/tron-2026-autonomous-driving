#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "road_navigation.h"
#include "user_config.h"

#define TEST_WIDTH   (320U)
#define TEST_HEIGHT  (240U)

static uint16_t g_frame[TEST_WIDTH * TEST_HEIGHT];
static uint16_t g_output[TEST_WIDTH * TEST_HEIGHT];

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t) ((((uint16_t) red * 31U / 255U) << 11U) |
                       (((uint16_t) green * 63U / 255U) << 5U) |
                       ((uint16_t) blue * 31U / 255U));
}

static obstacle_detector_result_t make_detection(uint16_t x,
                                                  uint16_t y,
                                                  uint16_t width,
                                                  uint16_t height,
                                                  uint16_t class_id)
{
    obstacle_detector_result_t result;
    memset(&result, 0, sizeof(result));
    result.detection_count = 1U;
    result.detections[0].x = x;
    result.detections[0].y = y;
    result.detections[0].width = width;
    result.detections[0].height = height;
    result.detections[0].score_per_mille = 900U;
    result.detections[0].class_id = class_id;
    return result;
}

static void draw_white_strip(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom)
{
    for (uint32_t y = top; y <= bottom; y++)
    {
        for (uint32_t x = left; x <= right; x++)
        {
            g_frame[y * TEST_WIDTH + x] = 0xFFFFU;
        }
    }
}

int main(void)
{
    road_navigation_result_t navigation;
    obstacle_detector_result_t no_detections;
    memset(&no_detections, 0, sizeof(no_detections));
    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0xFFFFU;
    }

    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &no_detections,
                                                          &navigation));
    assert(1U == navigation.drive_allowed);
    assert(navigation.target_x > 145U && navigation.target_x < 175U);
    assert(0x0FFFU == navigation.path_valid_mask);

    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0U;
    }
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &no_detections,
                                                          &navigation));
    assert(0U == navigation.drive_allowed);
    assert(0U == navigation.path_valid_mask);
    assert((TEST_WIDTH / 2U) == navigation.target_x);
    assert(0 == navigation.steering_angle_cdeg);

    /* Wide straight paper: lateral target remains exactly on image center. */
    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0U;
    }
    draw_white_strip(42U, 278U, 91U, 239U);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &no_detections,
                                                          &navigation));
    assert(1U == navigation.drive_allowed);
    assert((TEST_WIDTH / 2U) == navigation.target_x);
    assert(0 == road_navigation_heading_cdeg(&navigation));

    /* A narrow, but continuously visible strip must use real row samples. */
    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0U;
    }
    draw_white_strip(115U, 204U, 91U, 239U);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &no_detections,
                                                          &navigation));
    assert(1U == navigation.drive_allowed);
    assert(0x0FFFU == navigation.path_valid_mask);
    assert(navigation.target_x > 150U && navigation.target_x < 170U);
    assert(160U == navigation.target_x);
    assert(0 == road_navigation_heading_cdeg(&navigation));

    /* An offset but parallel strip has lateral error without heading error. */
    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0U;
    }
    draw_white_strip(135U, 224U, 91U, 239U);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &no_detections,
                                                          &navigation));
    assert(1U == navigation.drive_allowed);
    assert(navigation.target_x > (TEST_WIDTH / 2U));
    assert(0 == road_navigation_heading_cdeg(&navigation));

    /* A lane that tilts toward the right in the distance has an independent
     * nonzero heading signal. */
    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0U;
    }
    for (uint32_t y = 91U; y < TEST_HEIGHT; y++)
    {
        uint16_t center = (uint16_t) (160U + ((239U - y) / 5U));
        draw_white_strip((uint16_t) (center - 45U),
                         (uint16_t) (center + 44U),
                         (uint16_t) y,
                         (uint16_t) y);
    }
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &no_detections,
                                                          &navigation));
    assert(1U == navigation.drive_allowed);
    assert((int16_t) 0 != road_navigation_heading_cdeg(&navigation));

    /* A missed lookahead row and a shifted farther strip must not steer from
     * the synthetic fill value; recovery rows remain independently measured. */
    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0U;
    }
    draw_white_strip(115U, 204U, 91U, 239U);
    for (uint32_t x = 0U; x < TEST_WIDTH; x++)
    {
        g_frame[145U * TEST_WIDTH + x] = 0U;
    }
    for (uint32_t y = 91U; y < 140U; y++)
    {
        for (uint32_t x = 0U; x < TEST_WIDTH; x++)
        {
            g_frame[y * TEST_WIDTH + x] = 0U;
        }
        for (uint32_t x = 185U; x <= 274U; x++)
        {
            g_frame[y * TEST_WIDTH + x] = 0xFFFFU;
        }
    }
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &no_detections,
                                                          &navigation));
    assert(1U == navigation.drive_allowed);
    assert(0U == (navigation.path_valid_mask & (1U << 7U)));
    assert(0U != (navigation.path_valid_mask & (1U << 8U)));
    assert(navigation.target_y != navigation.path[7].y);
    assert(navigation.target_y > navigation.path[7].y);
    assert(navigation.target_x < 180U);
    assert(navigation.path[8].center > 210U);

    /* If only the bottom scan row remains, low measured coverage still stops. */
    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0U;
    }
    draw_white_strip(115U, 204U, 239U, 239U);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &no_detections,
                                                          &navigation));
    assert(0U == navigation.drive_allowed);
    assert(NAVIGATION_STOP_ROAD_NOT_FOUND == navigation.stop_reason);
    assert(0U == navigation.throttle_per_mille);

    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0xFFFFU;
    }

    obstacle_detector_result_t avoidable = make_detection(10U, 100U, 30U, 90U,
                                                           OBSTACLE_CLASS_PERSON);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &avoidable,
                                                          &navigation));
    assert(1U == navigation.drive_allowed);

    uint16_t object_pixel = rgb565(255U, 0U, 0U);
    g_frame[145U * TEST_WIDTH + 160U] = object_pixel;
    assert(FSP_SUCCESS == road_navigation_render_rgb565((uint8_t const *) g_frame,
                                                         TEST_WIDTH,
                                                         TEST_HEIGHT,
                                                         TEST_WIDTH,
                                                         &avoidable,
                                                         &navigation,
                                                         g_output));
    assert(object_pixel == g_output[145U * TEST_WIDTH + 160U]);

    uint16_t dark_cardboard = rgb565(120U, 80U, 50U);
    g_frame[20U * TEST_WIDTH + 20U] = dark_cardboard;
    assert(FSP_SUCCESS == road_navigation_render_rgb565((uint8_t const *) g_frame,
                                                         TEST_WIDTH,
                                                         TEST_HEIGHT,
                                                         TEST_WIDTH,
                                                         &no_detections,
                                                         &navigation,
                                                         g_output));
    assert(NAVIGATION_SEGMENT_COLOR_WALL_RGB565 == g_output[20U * TEST_WIDTH + 20U]);

    obstacle_detector_result_t blocked = make_detection(60U, 80U, 200U, 130U,
                                                          OBSTACLE_CLASS_PERSON);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &blocked,
                                                          &navigation));
    assert(0U == navigation.drive_allowed);
    assert(NAVIGATION_STOP_NONE != navigation.stop_reason);
    assert(NAVIGATION_STOP_INVALID_INPUT != navigation.stop_reason);

    /* Contest demo: the AI-detected person/car ahead on a white strip must
     * stop even if the white area around its box could be used to detour. */
    for (uint32_t i = 0U; i < TEST_WIDTH * TEST_HEIGHT; i++)
    {
        g_frame[i] = 0xFFFFU;
    }
    obstacle_detector_result_t person_ahead = make_detection(120U, 140U, 80U, 80U,
                                                              OBSTACLE_CLASS_PERSON);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &person_ahead,
                                                          &navigation));
    assert(0U == navigation.drive_allowed);
    assert(NAVIGATION_STOP_AI_OBJECT_AHEAD == navigation.stop_reason);

    obstacle_detector_result_t bicycle_ahead = make_detection(120U, 140U, 80U, 80U,
                                                               OBSTACLE_CLASS_BICYCLE);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &bicycle_ahead,
                                                          &navigation));
    assert(NAVIGATION_STOP_AI_OBJECT_AHEAD != navigation.stop_reason);

    obstacle_detector_result_t car_ahead = make_detection(120U, 140U, 80U, 80U,
                                                           OBSTACLE_CLASS_CAR);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &car_ahead,
                                                          &navigation));
    assert(NAVIGATION_STOP_AI_OBJECT_AHEAD == navigation.stop_reason);

    /* A nearer person/car obscures the white road; preserve the AI stop
     * reason instead of reporting only generic road loss. */
    obstacle_detector_result_t close_person = make_detection(0U, 80U, 320U, 140U,
                                                              OBSTACLE_CLASS_PERSON);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &close_person,
                                                          &navigation));
    assert(0U == navigation.drive_allowed);
    assert(NAVIGATION_STOP_AI_OBJECT_AHEAD == navigation.stop_reason);

    puts("road_navigation contract tests: PASS");
    return 0;
}
