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
                                                  uint16_t height)
{
    obstacle_detector_result_t result;
    memset(&result, 0, sizeof(result));
    result.detection_count = 1U;
    result.detections[0].x = x;
    result.detections[0].y = y;
    result.detections[0].width = width;
    result.detections[0].height = height;
    result.detections[0].score_per_mille = 900U;
    result.detections[0].class_id = OBSTACLE_CLASS_PERSON;
    return result;
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

    obstacle_detector_result_t avoidable = make_detection(130U, 100U, 60U, 90U);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &avoidable,
                                                          &navigation));
    assert(1U == navigation.drive_allowed);
    assert((navigation.target_x < 115U) || (navigation.target_x >= 205U));

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

    obstacle_detector_result_t blocked = make_detection(60U, 80U, 200U, 130U);
    assert(FSP_SUCCESS == road_navigation_analyze_rgb565((uint8_t const *) g_frame,
                                                          TEST_WIDTH,
                                                          TEST_HEIGHT,
                                                          TEST_WIDTH,
                                                          &blocked,
                                                          &navigation));
    assert(0U == navigation.drive_allowed);
    assert(NAVIGATION_STOP_NONE != navigation.stop_reason);
    assert(NAVIGATION_STOP_INVALID_INPUT != navigation.stop_reason);

    puts("road_navigation contract tests: PASS");
    return 0;
}
