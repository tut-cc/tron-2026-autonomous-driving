#ifndef FRAME_STREAM_OVERLAY_H_
#define FRAME_STREAM_OVERLAY_H_

#include <stdbool.h>
#include <stdint.h>

#include "ai/obstacle_detector.h"

/* The Web view labels its magenta boxes as the person/car safety detections. */
static inline bool frame_stream_http_class_is_visible(uint16_t class_id)
{
    return (OBSTACLE_CLASS_PERSON == class_id) || (OBSTACLE_CLASS_CAR == class_id);
}

#endif /* FRAME_STREAM_OVERLAY_H_ */
