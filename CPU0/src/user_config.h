/***********************************************************************************************************************
 * File Name    : user_config.h
 * Description  : User configuration for obstacle detection and road navigation.
 **********************************************************************************************************************/
/***********************************************************************************************************************
* Copyright (c) 2026
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************************************************************/

#ifndef USER_CONFIG_H_
#define USER_CONFIG_H_

#include "ai_control_signals.h" /* cross-core confidence band and AI frame age */

/* Use RA8P1 USB peripheral CDC (PCDC) for terminal commands and frame streaming. */
#ifndef USE_USB_PCDC_STREAM
#define USE_USB_PCDC_STREAM             (1U)
#endif

/* Keep the EK-RA8P1 J-Link OB VCOM available for imported helper objects that call serial_printf. */
#ifndef USE_VIRTUAL_COM
#define USE_VIRTUAL_COM                 (1U)
#endif

/* This project targets a PC monitor, so the optional parallel LCD path is disabled by default. */
#ifndef DISPLAY_OUTPUT
#define DISPLAY_OUTPUT                  (0U)
#endif

/* Yield between processed frames.  The wait lets the lower-priority USB/video
 * consumer run; it is not needed for the camera, which keeps capturing.
 * 2026-09-27: 100 -> 10 -> 5 ms, it was idle time inside the AI frame period. */
#ifndef PERIODIC_IMAGE_INTERVAL_MS
#define PERIODIC_IMAGE_INTERVAL_MS      (5U)
#endif
/* This wait is part of the AI frame period seen by the M33.  With the measured
 * per-frame work (copy, rotate, inference, road analysis: up to ~400 ms) the
 * period must stay below AI_FRAME_MAX_AGE_MS or AUTO can never start. */
#define AI_M85_MEASURED_FRAME_WORK_MS   (400U)
#if (PERIODIC_IMAGE_INTERVAL_MS + AI_M85_MEASURED_FRAME_WORK_MS) > AI_FRAME_MAX_AGE_MS
#error "PERIODIC_IMAGE_INTERVAL_MS too long for AI_FRAME_MAX_AGE_MS (see ai_control_signals.h)"
#endif

/* Run the NPU person/car detector on every Nth camera frame (1 = every frame).
 * Frames in between reuse the previous boxes and only redo the cheap road
 * analysis, so path updates reach the M33 about twice as often with N=2.
 * Cost: a newly appearing person/car is seen up to one extra frame later
 * (ToF stop and STOP/ESTOP are unaffected). */
#ifndef AI_DETECT_EVERY_N_FRAMES
#define AI_DETECT_EVERY_N_FRAMES        (2U)
#endif
#if (AI_DETECT_EVERY_N_FRAMES < 1U) || (AI_DETECT_EVERY_N_FRAMES > 3U)
#error "AI_DETECT_EVERY_N_FRAMES must be 1..3"
#endif

/* One Full INT8 YOLO-Fastest COCO model; firmware retains four road-user classes. */
#ifndef AI_OBSTACLE_SCORE_THRESHOLD
#define AI_OBSTACLE_SCORE_THRESHOLD     (0.45F)
#endif

#ifndef AI_OBSTACLE_NMS_THRESHOLD
#define AI_OBSTACLE_NMS_THRESHOLD       (0.45F)
#endif

/*
 * RGB565 recognition display colours: white road, brown wall, original image
 * for unclassified pixels, yellow path, and red target.
 */
#ifndef NAVIGATION_SEGMENT_COLOR_ROAD_RGB565
#define NAVIGATION_SEGMENT_COLOR_ROAD_RGB565              (0xFFFFU) /* white */
#endif

#ifndef NAVIGATION_SEGMENT_COLOR_WALL_RGB565
#define NAVIGATION_SEGMENT_COLOR_WALL_RGB565              (0x6183U) /* dark brown */
#endif

/*
 * RGB inRange-style thresholds, expressed as 8-bit values (0..255).
 * Defaults accept moderate indoor shadows. The channel relationships below
 * keep bright cardboard out of the white mask and dark neutral objects out
 * of the brown mask. Tune these values under the actual lighting.
 */
#ifndef NAVIGATION_WHITE_R_MIN
#define NAVIGATION_WHITE_R_MIN                            (90U)
#endif
#ifndef NAVIGATION_WHITE_R_MAX
#define NAVIGATION_WHITE_R_MAX                            (255U)
#endif
#ifndef NAVIGATION_WHITE_G_MIN
#define NAVIGATION_WHITE_G_MIN                            (90U)
#endif
#ifndef NAVIGATION_WHITE_G_MAX
#define NAVIGATION_WHITE_G_MAX                            (255U)
#endif
#ifndef NAVIGATION_WHITE_B_MIN
#define NAVIGATION_WHITE_B_MIN                            (80U)
#endif
#ifndef NAVIGATION_WHITE_B_MAX
#define NAVIGATION_WHITE_B_MAX                            (255U)
#endif

#ifndef NAVIGATION_WHITE_MAX_CHANNEL_SPREAD
#define NAVIGATION_WHITE_MAX_CHANNEL_SPREAD               (85U)
#endif

#ifndef NAVIGATION_BROWN_R_MIN
#define NAVIGATION_BROWN_R_MIN                            (35U)
#endif
#ifndef NAVIGATION_BROWN_R_MAX
#define NAVIGATION_BROWN_R_MAX                            (190U)
#endif
#ifndef NAVIGATION_BROWN_G_MIN
#define NAVIGATION_BROWN_G_MIN                            (15U)
#endif
#ifndef NAVIGATION_BROWN_G_MAX
#define NAVIGATION_BROWN_G_MAX                            (140U)
#endif
#ifndef NAVIGATION_BROWN_B_MIN
#define NAVIGATION_BROWN_B_MIN                            (5U)
#endif
#ifndef NAVIGATION_BROWN_B_MAX
#define NAVIGATION_BROWN_B_MAX                            (100U)
#endif

#ifndef NAVIGATION_BROWN_MIN_RED_OVER_GREEN
#define NAVIGATION_BROWN_MIN_RED_OVER_GREEN               (12U)
#endif

#ifndef NAVIGATION_BROWN_MIN_GREEN_OVER_BLUE
#define NAVIGATION_BROWN_MIN_GREEN_OVER_BLUE              (5U)
#endif

#ifndef NAVIGATION_BROWN_MAX_RED_OVER_GREEN
#define NAVIGATION_BROWN_MAX_RED_OVER_GREEN               (100U)
#endif

/* Binary-mask geometry and road-centre extraction. */
#ifndef NAVIGATION_HORIZON_PERCENT
#define NAVIGATION_HORIZON_PERCENT                       (38U)
#endif

#ifndef NAVIGATION_COLOR_SAMPLE_STEP
#define NAVIGATION_COLOR_SAMPLE_STEP                     (4U)
#endif

#ifndef NAVIGATION_SCAN_STEP_PIXELS
#define NAVIGATION_SCAN_STEP_PIXELS                      (4U)
#endif

#ifndef NAVIGATION_MAX_EDGE_MISSES
#define NAVIGATION_MAX_EDGE_MISSES                       (3U)
#endif

/* Below this the road is not found (path_valid=0).  Tied to the M33 hold
 * threshold so the M85 never reports a road the M33 would reject while
 * driving; see AI_PATH_CONFIDENCE_* in ai_control_signals.h. */
#ifndef NAVIGATION_MIN_ROAD_CONFIDENCE_PER_MILLE
#define NAVIGATION_MIN_ROAD_CONFIDENCE_PER_MILLE         AI_PATH_CONFIDENCE_HOLD_PER_MILLE
#endif
#if NAVIGATION_MIN_ROAD_CONFIDENCE_PER_MILLE != AI_PATH_CONFIDENCE_HOLD_PER_MILLE
#error "NAVIGATION_MIN_ROAD_CONFIDENCE_PER_MILLE must equal the M33 hold threshold"
#endif

#ifndef NAVIGATION_MIN_BOTTOM_ROAD_WIDTH_PERCENT
#define NAVIGATION_MIN_BOTTOM_ROAD_WIDTH_PERCENT         (12U)
#endif

#ifndef NAVIGATION_MIN_LOOKAHEAD_ROAD_WIDTH_PERCENT
#define NAVIGATION_MIN_LOOKAHEAD_ROAD_WIDTH_PERCENT      (5U)
#endif

/* Steering target row (percent of the scan height from the bottom).
 * 2026-09-27: 65 -> 50 so a short visible strip still gives a target. */
#ifndef NAVIGATION_LOOKAHEAD_PERCENT
#define NAVIGATION_LOOKAHEAD_PERCENT                     (50U)
#endif

/* Road confidence uses only this many nearest scan rows (of 12). */
#ifndef NAVIGATION_CONFIDENCE_ROWS
#define NAVIGATION_CONFIDENCE_ROWS                       (6U)
#endif

#ifndef NAVIGATION_SAFETY_ROI_TOP_PERCENT
#define NAVIGATION_SAFETY_ROI_TOP_PERCENT                (58U)
#endif

#ifndef NAVIGATION_SAFETY_CORRIDOR_PERCENT
#define NAVIGATION_SAFETY_CORRIDOR_PERCENT               (16U)
#endif

#ifndef NAVIGATION_BLOCKED_THRESHOLD_PER_MILLE
#define NAVIGATION_BLOCKED_THRESHOLD_PER_MILLE           (600U)
#endif

#ifndef NAVIGATION_AI_STOP_BOTTOM_PERCENT
#define NAVIGATION_AI_STOP_BOTTOM_PERCENT                (55U)
#endif

/* Expand every AI box before path extraction. A remaining white lane is used
 * for avoidance; otherwise the existing fail-safe stop remains active. */
#ifndef NAVIGATION_AI_AVOID_HORIZONTAL_MARGIN_PERCENT
#define NAVIGATION_AI_AVOID_HORIZONTAL_MARGIN_PERCENT    (25U)
#endif

#ifndef NAVIGATION_AI_AVOID_VERTICAL_MARGIN_PERCENT
#define NAVIGATION_AI_AVOID_VERTICAL_MARGIN_PERCENT      (10U)
#endif

#ifndef NAVIGATION_AI_AVOID_MIN_MARGIN_PIXELS
#define NAVIGATION_AI_AVOID_MIN_MARGIN_PIXELS             (6U)
#endif

/* Steering is reported in centidegrees and clamped to +/-30 degrees. */
#ifndef NAVIGATION_STEERING_GAIN_PERCENT
#define NAVIGATION_STEERING_GAIN_PERCENT                 (100)
#endif

#ifndef NAVIGATION_MAX_STEERING_CDEG
#define NAVIGATION_MAX_STEERING_CDEG                     (3000)
#endif

#ifndef NAVIGATION_CRUISE_THROTTLE_PER_MILLE
#define NAVIGATION_CRUISE_THROTTLE_PER_MILLE             (250U)
#endif

#ifndef NAVIGATION_MIN_THROTTLE_PER_MILLE
#define NAVIGATION_MIN_THROTTLE_PER_MILLE                (100U)
#endif

/*
 * Keep physical actuator output disabled until a board-specific motor driver,
 * emergency stop, steering calibration, and watchdog have been verified.
 */
#ifndef NAVIGATION_ENABLE_ACTUATOR_OUTPUT
#define NAVIGATION_ENABLE_ACTUATOR_OUTPUT                (0U)
#endif

/*
 * DRV8833 signal output. Keep this at 0 while checking SIGNAL lines over
 * USB.  Set to 1 only after the vehicle is raised off the floor and an
 * independent power cut-off has been tested.
 */
#ifndef MOTOR_PHYSICAL_OUTPUT_ENABLE
#define MOTOR_PHYSICAL_OUTPUT_ENABLE                      (0U)
#endif

#ifndef MOTOR_LEFT_DIRECTION_INVERTED
#define MOTOR_LEFT_DIRECTION_INVERTED                     (0U)
#endif

#ifndef MOTOR_RIGHT_DIRECTION_INVERTED
#define MOTOR_RIGHT_DIRECTION_INVERTED                    (0U)
#endif

#ifndef MOTOR_START_BOOST_MS
#define MOTOR_START_BOOST_MS (0U)
#endif

#ifndef MOTOR_BASE_COMMAND
#define MOTOR_BASE_COMMAND                                (0.25F)
#endif

#ifndef MOTOR_MAX_STEERING_COMMAND
#define MOTOR_MAX_STEERING_COMMAND                        (0.30F)
#endif

#ifndef MOTOR_GOOD_FRAMES_TO_AUTO
#define MOTOR_GOOD_FRAMES_TO_AUTO                         (3U)
#endif

/* Physical driving is restricted to QVGA processing latency. */
#ifndef MOTOR_CONTROL_MAX_FRAME_PIXELS
#define MOTOR_CONTROL_MAX_FRAME_PIXELS                    (320U * 240U)
#endif

/* Unit ToF4M independent emergency-stop settings. */
#ifndef TOF4M_TIMING_BUDGET_MS
#define TOF4M_TIMING_BUDGET_MS                            (50U)
#endif

#ifndef TOF4M_INTER_MEASUREMENT_MS
#define TOF4M_INTER_MEASUREMENT_MS                        (60U)
#endif

#ifndef TOF4M_EMERGENCY_STOP_MM
#define TOF4M_EMERGENCY_STOP_MM                           (200U)
#endif

#ifndef TOF4M_EMERGENCY_RELEASE_MM
#define TOF4M_EMERGENCY_RELEASE_MM                        (300U)
#endif

#endif /* USER_CONFIG_H_ */
