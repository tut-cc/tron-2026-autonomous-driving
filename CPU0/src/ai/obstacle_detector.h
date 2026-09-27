/***********************************************************************************************************************
 * File Name    : obstacle_detector.h
 * Description  : C interface for the Ethos-U55 YOLO-Fastest COCO detector.
 **********************************************************************************************************************/

#ifndef OBSTACLE_DETECTOR_H_
#define OBSTACLE_DETECTOR_H_

#include <stdint.h>
#include "bsp_api.h"

#define OBSTACLE_DETECTOR_INPUT_WIDTH       (320U)
#define OBSTACLE_DETECTOR_INPUT_HEIGHT      (320U)
#define OBSTACLE_DETECTOR_INPUT_CHANNELS    (3U)
#define OBSTACLE_DETECTOR_MAX_DETECTIONS    (12U)

/* COCO class IDs emitted by the model. Only these four classes are retained. */
#define OBSTACLE_CLASS_PERSON               (0U)
#define OBSTACLE_CLASS_BICYCLE              (1U)
#define OBSTACLE_CLASS_CAR                  (2U)
#define OBSTACLE_CLASS_MOTORCYCLE           (3U)
#define OBSTACLE_DETECTOR_CLASS_COUNT       (4U)

typedef enum e_obstacle_detector_status
{
    OBSTACLE_DETECTOR_STATUS_NOT_STARTED = 0U,
    OBSTACLE_DETECTOR_STATUS_ETHOS_OPEN_FAILED = 1U,
    OBSTACLE_DETECTOR_STATUS_MODEL_INIT_FAILED = 2U,
    OBSTACLE_DETECTOR_STATUS_INPUT_TENSOR_INVALID = 3U,
    OBSTACLE_DETECTOR_STATUS_OUTPUT_COUNT_INVALID = 4U,
    OBSTACLE_DETECTOR_STATUS_OUTPUT_TENSOR_INVALID = 5U,
    OBSTACLE_DETECTOR_STATUS_READY = 100U,
    OBSTACLE_DETECTOR_STATUS_PREPROCESS_FAILED = 101U,
    OBSTACLE_DETECTOR_STATUS_INFERENCE_FAILED = 102U,
} obstacle_detector_status_t;

typedef struct st_obstacle_detection
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t score_per_mille;
    uint16_t class_id;
} obstacle_detection_t;

typedef struct st_obstacle_detector_result
{
    uint32_t inference_time_us;
    uint8_t detection_count;
    obstacle_detection_t detections[OBSTACLE_DETECTOR_MAX_DETECTIONS];
} obstacle_detector_result_t;

#ifdef __cplusplus
extern "C" {
#endif

fsp_err_t obstacle_detector_init(void);
fsp_err_t obstacle_detector_run_rgb565(uint8_t const * p_frame,
                                       uint16_t width,
                                       uint16_t height,
                                       uint16_t stride_pixels,
                                       obstacle_detector_result_t * p_result);
obstacle_detector_status_t obstacle_detector_get_status(void);
char const * obstacle_detector_class_name(uint16_t class_id);

#ifdef __cplusplus
}
#endif

#endif /* OBSTACLE_DETECTOR_H_ */
