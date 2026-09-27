#ifndef AI_CONTROL_SIGNALS_H
#define AI_CONTROL_SIGNALS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AI_CONTROL_INTERFACE_VERSION    (1U)
#define AI_CONTROL_MAX_OBSTACLES        (3U)

/*
 * Cross-core tuning contract: the M85 producer and the M33 consumer both read
 * these, so the two sides cannot drift apart.  Values are unchanged from the
 * 2026-09-26 bench build.
 *
 * Road/path confidence (per mille, = ai_perception_result_t.path_confidence x 1000)
 * is a hysteresis band:
 *   - M33 lets AUTO start only at >= ENTER (after good_frames_to_auto frames);
 *   - M33 keeps AUTO running while >= HOLD;
 *   - M85 road navigation reports ROAD_NOT_FOUND (path_valid=0) below HOLD.
 * A value in [HOLD, ENTER) is therefore "road seen, but not good enough to
 * start": the Web UI shows PATH_NOT_READY, while M85 stop_reason stays NONE.
 */
#define AI_PATH_CONFIDENCE_ENTER_PER_MILLE (650U)
#define AI_PATH_CONFIDENCE_HOLD_PER_MILLE  (500U)
#if AI_PATH_CONFIDENCE_ENTER_PER_MILLE < AI_PATH_CONFIDENCE_HOLD_PER_MILLE || \
    AI_PATH_CONFIDENCE_ENTER_PER_MILLE > 1000U
#error "path confidence band must satisfy HOLD <= ENTER <= 1000"
#endif

/*
 * Maximum age of an AI frame at the M33, measured from capture_timestamp_ms
 * (M33 clock), and the longest the M33 path follower waits for the next frame.
 * Budget measured on the 2026-09-26 bench (RA8P1, QVGA, YOLO on NPU):
 *   capture -> M33 arrival  ~160 ms  (copy, rotate, inference 120-140 ms, road analysis)
 *   frame period            300-500 ms (includes PERIODIC_IMAGE_INTERVAL_MS)
 * The previous 250 ms was below the frame period, so the path follower went
 * stale between every frame and AUTO could never start.  Raising this value
 * lengthens how long AUTO may keep driving on the last good frame; ToF (100 ms)
 * and STOP/ESTOP are independent of it.
 */
#define AI_FRAME_MAX_AGE_MS                (600U)

/*
 * AI/image-processing output contract.
 *
 * All normalized float values must be finite.  The producer must clamp them
 * to the documented range before publishing the result.
 *
 * These C structures are intended for task-to-task transfer on one MCU.  Do
 * not send their raw memory image between different processors.  For UART,
 * SPI, or a network link, serialize each field explicitly and add a version,
 * packet length, and CRC.
 */
typedef struct
{
    float confidence;        /* 0.0 .. 1.0 */
    float corridor_overlap;  /* 0.0 .. 1.0 */
    float center_x;          /* -1.0 (left) .. +1.0 (right) */
    float bbox_bottom;       /* 0.0 (image top) .. 1.0 (image bottom) */
} ai_obstacle_result_t;

typedef struct
{
    uint16_t interface_version;
    uint16_t processing_time_ms;
    uint32_t seq;
    uint32_t capture_timestamp_ms;

    uint8_t path_valid;
    uint8_t obstacle_valid;
    uint8_t obstacle_count;
    uint8_t reserved;

    float lateral_error;     /* -1.0 (left) .. +1.0 (right) */
    float heading_error;     /* -1.0 (left turn) .. +1.0 (right turn) */
    float path_width;        /* 0.0 .. 1.0 of image width */
    float path_confidence;   /* 0.0 .. 1.0 */

    ai_obstacle_result_t obstacles[AI_CONTROL_MAX_OBSTACLES];
} ai_perception_result_t;

/* Independent safety input.  This structure is not produced by the AI. */
typedef struct
{
    uint32_t seq;
    uint32_t sample_timestamp_ms;
    uint16_t distance_mm;
    uint8_t valid;
    uint8_t reserved;
} tof_safety_result_t;

#ifdef __cplusplus
}
#endif

#endif /* AI_CONTROL_SIGNALS_H */
