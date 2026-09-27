/***********************************************************************************************************************
 * File Name    : road_navigation.h
 * Description  : Pseudo-segmentation, road-centre extraction, and fail-safe drive command generation.
 **********************************************************************************************************************/

#ifndef ROAD_NAVIGATION_H_
#define ROAD_NAVIGATION_H_

#include <stdint.h>
#include "bsp_api.h"
#include "ai/obstacle_detector.h"

#define ROAD_NAVIGATION_PATH_POINTS    (12U)

typedef enum e_navigation_stop_reason
{
    NAVIGATION_STOP_NONE = 0,
    NAVIGATION_STOP_ROAD_NOT_FOUND = 1,
    NAVIGATION_STOP_ROAD_TOO_NARROW = 2,
    NAVIGATION_STOP_AI_OBJECT_AHEAD = 3,
    NAVIGATION_STOP_PATH_BLOCKED = 4,
    NAVIGATION_STOP_INVALID_INPUT = 5,
} navigation_stop_reason_t;

typedef struct st_road_path_sample
{
    uint16_t y;
    uint16_t left;
    uint16_t center;
    uint16_t right;
} road_path_sample_t;

typedef struct st_road_navigation_result
{
    int16_t steering_angle_cdeg;
    uint16_t throttle_per_mille;
    uint16_t road_confidence_per_mille;
    uint16_t obstacle_risk_per_mille;
    uint16_t white_ratio_per_mille;
    uint16_t brown_ratio_per_mille;
    uint16_t target_x;
    uint16_t target_y;
    uint16_t horizon_y;
    uint8_t drive_allowed;
    uint8_t stop_reason;
    uint8_t path_count;
    uint8_t reserved;
    road_path_sample_t path[ROAD_NAVIGATION_PATH_POINTS];
} road_navigation_result_t;

fsp_err_t road_navigation_analyze_rgb565(uint8_t const * p_frame,
                                         uint16_t width,
                                         uint16_t height,
                                         uint16_t stride_pixels,
                                         obstacle_detector_result_t const * p_detections,
                                         road_navigation_result_t * p_result);

fsp_err_t road_navigation_render_rgb565(uint8_t const * p_frame,
                                        uint16_t width,
                                        uint16_t height,
                                        uint16_t stride_pixels,
                                        obstacle_detector_result_t const * p_detections,
                                        road_navigation_result_t const * p_navigation,
                                        uint16_t * p_output);


#endif /* ROAD_NAVIGATION_H_ */
