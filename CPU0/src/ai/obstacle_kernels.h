/***********************************************************************************************************************
 * File Name    : obstacle_kernels.h
 * Description  : Hot per-pixel / per-anchor loops of the obstacle detector, free of TFLM/FSP dependencies so the
 *                host tests can compare them bit-for-bit against the previous implementation.
 **********************************************************************************************************************/
#ifndef OBSTACLE_KERNELS_H_
#define OBSTACLE_KERNELS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors of the model contract.  obstacle_detector.cc static_asserts that
 * these equal OBSTACLE_DETECTOR_* in ai/obstacle_detector.h. */
#define OD_INPUT_WIDTH          (320U)
#define OD_INPUT_HEIGHT         (320U)
#define OD_INPUT_CHANNELS       (3U)
#define OD_RETAINED_CLASS_COUNT (4U)   /* person, bicycle, car, motorcycle */
#define OD_MODEL_CLASS_COUNT    (80U)  /* COCO */
#define OD_MODEL_BOX_VALUES     (5U)   /* tx, ty, tw, th, objectness */
#define OD_MODEL_ANCHOR_COUNT   (3U)
#define OD_MODEL_VALUES_PER_ANCHOR (OD_MODEL_BOX_VALUES + OD_MODEL_CLASS_COUNT)
#define OD_MODEL_OUTPUT_CHANNELS   (OD_MODEL_ANCHOR_COUNT * OD_MODEL_VALUES_PER_ANCHOR)
#define OD_MAX_CANDIDATES       (64U)

typedef struct
{
    float x;
    float y;
    float width;
    float height;
    float score;
    uint16_t class_id;
    bool suppressed;
} od_candidate_t;

typedef struct
{
    uint32_t scaled_width;
    uint32_t scaled_height;
    uint32_t pad_x;
    uint32_t pad_y;
} od_letterbox_t;

od_letterbox_t od_letterbox(uint16_t width, uint16_t height);

/* RGB565 frame -> letterboxed int8 RGB input (value - 128), padding 0x80.
 * Writes all out_bytes bytes; out_bytes must be >= 320*320*3. */
void od_prepare_input(uint16_t const * p_pixels,
                      uint16_t width,
                      uint16_t height,
                      uint16_t stride_pixels,
                      int8_t * p_out,
                      uint32_t out_bytes);

/* Decode one YOLO output grid (rows x columns x 255 int8) into candidates.
 * Same arithmetic and selection rules as the previous in-line decoder. */
void od_decode_output(int8_t const * p_data,
                      int32_t rows,
                      int32_t columns,
                      float scale,
                      int32_t zero_point,
                      od_candidate_t * p_candidates,
                      uint32_t * p_candidate_count);

void od_add_candidate(od_candidate_t const * p_candidate,
                      od_candidate_t * p_candidates,
                      uint32_t * p_candidate_count);

#ifdef __cplusplus
}
#endif

#endif /* OBSTACLE_KERNELS_H_ */
