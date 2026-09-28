/***********************************************************************************************************************
 * File Name    : autonomy_controller.h
 * Description  : Converts camera/AI results into the AI perception message sent to the M33 controller.
 *                This module never decides motor output or stops the vehicle; the M33 does that.
 **********************************************************************************************************************/

#ifndef AUTONOMY_CONTROLLER_H_
#define AUTONOMY_CONTROLLER_H_

#include "ai/obstacle_detector.h"
#include "road_navigation.h"

fsp_err_t autonomy_controller_init(void);
/* M33 control clock published in shared RAM, or UINT32_MAX while unavailable.
 * Safe to call from the capture ISR (no HSEM, no producer access). */
uint32_t autonomy_controller_now_ms(void);
/* Publish one analysed frame to the IPC gateway. */
void autonomy_controller_update(road_navigation_result_t const * navigation,
                                obstacle_detector_result_t const * detections,
                                uint16_t frame_width,
                                uint16_t frame_height,
                                uint32_t capture_timestamp_ms);
/* Camera/AI could not produce a frame.  Publishes a perception message with
 * path_valid=0 and obstacle_valid=0: the M33 turns an AUTO run into TOR, and
 * MANUAL driving (protected by ToF and deadman) is not affected. */
void autonomy_controller_report_ai_unavailable(void);
/* Person/car alarm for the Web UI (not a stop: the M33 decides TOR with the
 * ToF).  Packed word (AUTONOMY_ALARM_* in web_control_adapter.h), read
 * atomically by the gateway task. */
uint32_t autonomy_controller_obstacle_alarm(void);
/* Hook called by ethosu_semaphore_take() (ra/npu/.../ethosu_driver.c) while
 * the camera task spins on the NPU.  IPC and heartbeat are now serviced by the
 * uT-Kernel gateway task (m85_gateway_task.c), which preempts this wait, so
 * the hook has nothing left to do.  Kept so the vendor driver still links. */
void autonomy_controller_service(void);

#endif /* AUTONOMY_CONTROLLER_H_ */
