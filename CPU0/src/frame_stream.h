/***********************************************************************************************************************
 * File Name    : frame_stream.h
 * Description  : RGB565 segmentation frame and navigation metadata protocol.
 **********************************************************************************************************************/

#ifndef FRAME_STREAM_H_
#define FRAME_STREAM_H_

#include <stdbool.h>
#include "common_utils.h"
#include "ai/obstacle_detector.h"
#include "road_navigation.h"

#define FRAME_STREAM_MAGIC_SIZE         (4U)
#define FRAME_STREAM_HEADER_SIZE        (18U)
#define FRAME_STREAM_FORMAT_RGB565_LE   (1U)


/* Camera/AI publishes into a newest-frame mailbox.  The low-priority USB
 * task drains it independently; a USB error therefore cannot stop AI or the
 * M85->M33 control heartbeat. */
fsp_err_t frame_stream_publish_navigation(uint8_t const * p_frame,
                                          uint16_t width,
                                          uint16_t height,
                                          obstacle_detector_result_t const * p_detections,
                                          road_navigation_result_t const * p_navigation);
fsp_err_t frame_stream_usb_task_poll(void);

#endif /* FRAME_STREAM_H_ */
