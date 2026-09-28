#ifndef CONTROL_MOTOR_H
#define CONTROL_MOTOR_H

#include <stdint.h>

#include "ai_control_signals.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CONTROL_STATE_INIT = 0,
    CONTROL_STATE_AUTO,
    CONTROL_STATE_TOR,
    CONTROL_STATE_SAFE_STOP
} control_state_t;

typedef enum
{
    CONTROL_REASON_NONE = 0,
    CONTROL_REASON_WAITING_FOR_VALID_DATA,
    CONTROL_REASON_INTERFACE_VERSION,
    CONTROL_REASON_AI_STALE,
    CONTROL_REASON_AI_DATA_INVALID,
    CONTROL_REASON_PATH_INVALID,
    CONTROL_REASON_PATH_LOW_CONFIDENCE,
    CONTROL_REASON_OBSTACLE_PROCESSING_INVALID,
    CONTROL_REASON_OBSTACLE_IN_CORRIDOR,
    CONTROL_REASON_TOF_INVALID,
    CONTROL_REASON_TOF_STALE,
    CONTROL_REASON_TOF_TOO_CLOSE
} control_reason_t;

typedef enum
{
    MOTOR_STOP_COAST = 0,
    MOTOR_STOP_BRAKE
} motor_stop_action_t;

typedef struct
{
    uint32_t perception_timeout_ms;
    uint32_t tof_timeout_ms;

    uint16_t tof_stop_mm;
    uint16_t tof_release_mm;

    float path_confidence_enter;
    float path_confidence_hold;

    float base_command;
    float lateral_gain;
    float heading_gain;
    float max_steering;
    /* Anti-weaving (2026-09-27): errors inside the deadband count as zero, and
     * each new camera frame moves the steering only this fraction toward the
     * new value (1.0 = no smoothing). */
    float lateral_deadband;
    float heading_deadband;
    float steering_filter_weight;
    /* Curve slowdown: speed x (1 - curve_slowdown x |steering| / max_steering). */
    float curve_slowdown;

    /* Automatic wheel-balance trim (2026-09-28): a slow integral of the raw
     * steering error learns the constant correction that unequal motors need,
     * so a straight strip is driven straight without a per-car constant.
     * Updated once per camera frame while the road is nearly straight; kept
     * across stops (reset only by control_motor_init()). */
    float steering_trim_initial;  /* start value (e.g. a value read earlier) */
    float steering_trim_rate;     /* fraction of the raw error added per frame */
    float steering_trim_limit;    /* |trim| cap                               */
    float steering_trim_heading_gate; /* learn only while |heading| <= this    */

    uint8_t good_frames_to_auto;
    uint8_t allow_reverse;
} control_motor_config_t;

typedef struct
{
    uint32_t command_seq;
    uint32_t source_ai_seq;
    uint32_t source_tof_seq;

    control_state_t state;
    control_reason_t reason;
    motor_stop_action_t stop_action;

    uint8_t motor_enable;
    uint8_t reserved[3];

    float left_command;      /* -1.0 .. +1.0 */
    float right_command;     /* -1.0 .. +1.0 */
    float steering_command;  /* -1.0 .. +1.0, right positive */
    float speed_scale;       /* 0.0 .. 1.0 */
    float obstacle_risk;     /* diagnostic only; detections don't alter output */
} control_motor_output_t;

typedef struct
{
    control_motor_config_t config;

    control_state_t state;
    control_reason_t reason;

    uint32_t command_seq;
    uint32_t last_ai_seq;
    uint32_t last_ai_seq_change_ms;
    uint32_t last_tof_seq;
    uint32_t last_tof_seq_change_ms;

    uint8_t ai_seen;
    uint8_t tof_seen;
    uint8_t good_frame_count;
    uint8_t tof_stop_latched;

    /* Steering low-pass state: updated once per new AI frame. */
    uint8_t steering_valid;
    uint32_t steering_ai_seq;
    float steering_filtered;

    /* Learned wheel-balance trim (see steering_trim_*).  Positive = more
     * left-wheel command, i.e. corrects a car that drifts left. */
    float steering_trim;
    /* Set by the owner before each update: learn the trim only while the
     * vehicle is really driving in AUTO (not in MANUAL, not stopped). */
    uint8_t trim_learning;
} control_motor_t;

void control_motor_default_config(control_motor_config_t * config);

void control_motor_init(control_motor_t * controller,
                        const control_motor_config_t * config,
                        uint32_t now_ms);

void control_motor_update(control_motor_t * controller,
                          const ai_perception_result_t * perception,
                          const tof_safety_result_t * tof,
                          uint32_t now_ms,
                          control_motor_output_t * output);


#ifdef __cplusplus
}
#endif

#endif /* CONTROL_MOTOR_H */
