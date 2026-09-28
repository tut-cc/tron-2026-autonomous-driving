#include "control_motor.h"

#include <stddef.h>

static float clampf_local(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

/* Values within +/-band count as zero; outside it the response starts from 0. */
static float deadband_local(float value, float band)
{
    if (value > band) return value - band;
    if (value < -band) return value + band;
    return 0.0F;
}

/* NaN fails both comparisons, so this also rejects non-finite NaN inputs. */
static uint8_t value_in_range(float value, float minimum, float maximum)
{
    return (uint8_t) ((value >= minimum) && (value <= maximum));
}

static uint8_t time_is_stale(uint32_t now_ms, uint32_t timestamp_ms, uint32_t timeout_ms)
{
    return (uint8_t) ((uint32_t) (now_ms - timestamp_ms) > timeout_ms);
}

static void set_stopped_output(control_motor_t * controller,
                               const ai_perception_result_t * perception,
                               const tof_safety_result_t * tof,
                               control_state_t state,
                               control_reason_t reason,
                               motor_stop_action_t stop_action,
                               control_motor_output_t * output)
{
    controller->state = state;
    controller->reason = reason;
    controller->good_frame_count = 0U;
    controller->steering_valid = 0U;   /* a restart begins from straight */

    output->command_seq = ++controller->command_seq;
    output->source_ai_seq = (NULL != perception) ? perception->seq : 0U;
    output->source_tof_seq = (NULL != tof) ? tof->seq : 0U;
    output->state = state;
    output->reason = reason;
    output->stop_action = stop_action;
    output->motor_enable = 0U;
    output->reserved[0] = 0U;
    output->reserved[1] = 0U;
    output->reserved[2] = 0U;
    output->left_command = 0.0F;
    output->right_command = 0.0F;
    output->steering_command = 0.0F;
    output->speed_scale = 0.0F;
    output->obstacle_risk = 0.0F;
}

void control_motor_default_config(control_motor_config_t * config)
{
    if (NULL == config)
    {
        return;
    }

    config->perception_timeout_ms = 500U;
    config->tof_timeout_ms = 300U;

    config->tof_stop_mm = 150U;
    config->tof_release_mm = 220U;

    config->path_confidence_enter = (float) AI_PATH_CONFIDENCE_ENTER_PER_MILLE / 1000.0F;
    config->path_confidence_hold = (float) AI_PATH_CONFIDENCE_HOLD_PER_MILLE / 1000.0F;

    config->base_command = 0.35F;
    /* 2026-09-27 field fixes (AUTO weaving): lateral error comes from the
     * nearest measured road centre and heading from the measured slope (no
     * double counting); gains 0.45/0.35 -> 0.30/0.25 -> 0.22/0.18, plus a
     * deadband and per-frame smoothing so a straight strip is driven straight
     * despite the 0.3-0.5 s camera latency. */
    config->lateral_gain = 0.22F;
    config->heading_gain = 0.18F;
    config->lateral_deadband = 0.06F;
    config->heading_deadband = 0.08F;
    config->steering_filter_weight = 0.50F;
    /* Slow down in curves so the camera latency costs less path error:
     * at full steering the forward command is 60 % of the straight value. */
    config->curve_slowdown = 0.40F;
    config->max_steering = 0.35F;

    /* ~7 camera frames (2-3 s) to learn a constant imbalance; at most
     * 0.10 of wheel command, well inside max_steering. */
    config->steering_trim_initial = 0.0F;
    config->steering_trim_rate = 0.15F;
    config->steering_trim_limit = 0.10F;
    config->steering_trim_heading_gate = 0.30F;

    config->good_frames_to_auto = 2U;
    config->allow_reverse = 0U;
}

void control_motor_init(control_motor_t * controller,
                        const control_motor_config_t * config,
                        uint32_t now_ms)
{
    control_motor_config_t local_config;

    if (NULL == controller)
    {
        return;
    }

    if (NULL == config)
    {
        control_motor_default_config(&local_config);
        config = &local_config;
    }

    *controller = (control_motor_t) {0};
    controller->config = *config;
    controller->state = CONTROL_STATE_INIT;
    controller->reason = CONTROL_REASON_WAITING_FOR_VALID_DATA;
    controller->last_ai_seq_change_ms = now_ms;
    controller->last_tof_seq_change_ms = now_ms;

    if (0U == controller->config.good_frames_to_auto)
    {
        controller->config.good_frames_to_auto = 1U;
    }
    if (controller->config.tof_release_mm < controller->config.tof_stop_mm)
    {
        controller->config.tof_release_mm = controller->config.tof_stop_mm;
    }
    controller->config.steering_trim_limit =
        clampf_local(controller->config.steering_trim_limit, 0.0F, controller->config.max_steering);
    controller->steering_trim = clampf_local(controller->config.steering_trim_initial,
                                             -controller->config.steering_trim_limit,
                                             controller->config.steering_trim_limit);
}

void control_motor_update(control_motor_t * controller,
                          const ai_perception_result_t * perception,
                          const tof_safety_result_t * tof,
                          uint32_t now_ms,
                          control_motor_output_t * output)
{
    uint8_t i;
    uint8_t path_values_valid;
    uint8_t new_ai_frame = 0U;
    float maximum_obstacle_risk = 0.0F;
    float confidence_scale;
    float speed_command;
    float steering_command;
    float minimum_motor_command;

    if ((NULL == controller) || (NULL == output))
    {
        return;
    }

    if ((NULL == perception) || (NULL == tof))
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_SAFE_STOP,
                           CONTROL_REASON_WAITING_FOR_VALID_DATA,
                           MOTOR_STOP_BRAKE,
                           output);
        return;
    }

    if ((0U == controller->ai_seen) || (perception->seq != controller->last_ai_seq))
    {
        new_ai_frame = 1U;
        controller->ai_seen = 1U;
        controller->last_ai_seq = perception->seq;
        controller->last_ai_seq_change_ms = now_ms;
    }

    if ((0U == controller->tof_seen) || (tof->seq != controller->last_tof_seq))
    {
        controller->tof_seen = 1U;
        controller->last_tof_seq = tof->seq;
        controller->last_tof_seq_change_ms = now_ms;
    }

    if (0U == tof->valid)
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_SAFE_STOP,
                           CONTROL_REASON_TOF_INVALID,
                           MOTOR_STOP_BRAKE,
                           output);
        return;
    }

    if (time_is_stale(now_ms, controller->last_tof_seq_change_ms,
                      controller->config.tof_timeout_ms) ||
        time_is_stale(now_ms, tof->sample_timestamp_ms,
                      controller->config.tof_timeout_ms))
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_SAFE_STOP,
                           CONTROL_REASON_TOF_STALE,
                           MOTOR_STOP_BRAKE,
                           output);
        return;
    }

    if (tof->distance_mm <= controller->config.tof_stop_mm)
    {
        controller->tof_stop_latched = 1U;
    }
    else if ((0U != controller->tof_stop_latched) &&
             (tof->distance_mm >= controller->config.tof_release_mm))
    {
        controller->tof_stop_latched = 0U;
    }

    if (0U != controller->tof_stop_latched)
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_SAFE_STOP,
                           CONTROL_REASON_TOF_TOO_CLOSE,
                           MOTOR_STOP_BRAKE,
                           output);
        return;
    }

    if (AI_CONTROL_INTERFACE_VERSION != perception->interface_version)
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_TOR,
                           CONTROL_REASON_INTERFACE_VERSION,
                           MOTOR_STOP_COAST,
                           output);
        return;
    }

    if (time_is_stale(now_ms, controller->last_ai_seq_change_ms,
                      controller->config.perception_timeout_ms) ||
        time_is_stale(now_ms, perception->capture_timestamp_ms,
                      controller->config.perception_timeout_ms))
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_TOR,
                           CONTROL_REASON_AI_STALE,
                           MOTOR_STOP_COAST,
                           output);
        return;
    }

    if (perception->obstacle_count > AI_CONTROL_MAX_OBSTACLES)
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_TOR,
                           CONTROL_REASON_AI_DATA_INVALID,
                           MOTOR_STOP_COAST,
                           output);
        return;
    }

    /* Obstacle inference is advisory. A missing obstacle result contributes
     * no slowdown/alarm sample, but cannot interrupt valid road following.
     * Malformed payloads and path/ToF failures remain fail-safe below. */
    for (i = 0U; perception->obstacle_valid && i < perception->obstacle_count; ++i)
    {
        const ai_obstacle_result_t * obstacle = &perception->obstacles[i];
        float risk;

        if (!(value_in_range(obstacle->confidence, 0.0F, 1.0F) &&
              value_in_range(obstacle->corridor_overlap, 0.0F, 1.0F) &&
              value_in_range(obstacle->center_x, -1.0F, 1.0F) &&
              value_in_range(obstacle->bbox_bottom, 0.0F, 1.0F)))
        {
            set_stopped_output(controller, perception, tof,
                               CONTROL_STATE_TOR,
                               CONTROL_REASON_AI_DATA_INVALID,
                               MOTOR_STOP_COAST,
                               output);
            return;
        }

        risk = obstacle->confidence * obstacle->corridor_overlap * obstacle->bbox_bottom;
        if (risk > maximum_obstacle_risk)
        {
            maximum_obstacle_risk = risk;
        }

    }

    if (0U == perception->path_valid)
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_TOR,
                           CONTROL_REASON_PATH_INVALID,
                           MOTOR_STOP_COAST,
                           output);
        return;
    }

    path_values_valid = (uint8_t) (
        value_in_range(perception->lateral_error, -1.0F, 1.0F) &&
        value_in_range(perception->heading_error, -1.0F, 1.0F) &&
        value_in_range(perception->path_width, 0.0F, 1.0F) &&
        value_in_range(perception->path_confidence, 0.0F, 1.0F));

    if (0U == path_values_valid)
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_TOR,
                           CONTROL_REASON_AI_DATA_INVALID,
                           MOTOR_STOP_COAST,
                           output);
        return;
    }

    if ((CONTROL_STATE_AUTO == controller->state) &&
        (perception->path_confidence < controller->config.path_confidence_hold))
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_TOR,
                           CONTROL_REASON_PATH_LOW_CONFIDENCE,
                           MOTOR_STOP_COAST,
                           output);
        return;
    }

    if ((CONTROL_STATE_AUTO != controller->state) &&
        (perception->path_confidence < controller->config.path_confidence_enter))
    {
        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_TOR,
                           CONTROL_REASON_PATH_LOW_CONFIDENCE,
                           MOTOR_STOP_COAST,
                           output);
        return;
    }

    if (new_ai_frame &&
        (controller->good_frame_count < controller->config.good_frames_to_auto))
    {
        ++controller->good_frame_count;
    }

    if (controller->good_frame_count < controller->config.good_frames_to_auto)
    {
        uint8_t accumulated_good_frames = controller->good_frame_count;

        set_stopped_output(controller, perception, tof,
                           CONTROL_STATE_INIT,
                           CONTROL_REASON_WAITING_FOR_VALID_DATA,
                           MOTOR_STOP_COAST,
                           output);
        /* set_stopped_output resets this counter; preserve the valid streak. */
        controller->good_frame_count = accumulated_good_frames;
        return;
    }

    controller->state = CONTROL_STATE_AUTO;
    controller->reason = CONTROL_REASON_NONE;

    confidence_scale = 0.50F + (0.50F * perception->path_confidence);
    speed_command = controller->config.base_command * confidence_scale;
    if ((0U != controller->trim_learning) &&
        ((0U == controller->steering_valid) || (perception->seq != controller->steering_ai_seq)) &&
        (perception->heading_error <= controller->config.steering_trim_heading_gate) &&
        (perception->heading_error >= -controller->config.steering_trim_heading_gate))
    {
        /* Integral of the raw (no deadband) error, once per new frame.  A
         * constant motor imbalance leaves the car sitting just outside the
         * deadband on one side; this term moves that correction into the
         * trim so the error returns to zero instead of weaving. */
        float raw = (controller->config.lateral_gain * perception->lateral_error) +
                    (controller->config.heading_gain * perception->heading_error);
        controller->steering_trim = clampf_local(
            controller->steering_trim + (controller->config.steering_trim_rate * raw),
            -controller->config.steering_trim_limit,
            controller->config.steering_trim_limit);
    }
    steering_command =
        (controller->config.lateral_gain *
         deadband_local(perception->lateral_error, controller->config.lateral_deadband)) +
        (controller->config.heading_gain *
         deadband_local(perception->heading_error, controller->config.heading_deadband));
    steering_command = clampf_local(steering_command,
                                    -controller->config.max_steering,
                                    controller->config.max_steering);
    /* One smoothing step per new camera frame (this runs every 10 ms with the
     * same frame in between). */
    if ((0U == controller->steering_valid) ||
        (perception->seq != controller->steering_ai_seq))
    {
        float weight = clampf_local(controller->config.steering_filter_weight, 0.0F, 1.0F);
        float previous = (0U == controller->steering_valid) ? 0.0F : controller->steering_filtered;
        controller->steering_filtered = previous + weight * (steering_command - previous);
        controller->steering_valid = 1U;
        controller->steering_ai_seq = perception->seq;
    }
    steering_command = clampf_local(controller->steering_filtered + controller->steering_trim,
                                    -controller->config.max_steering,
                                    controller->config.max_steering);
    if (controller->config.max_steering > 0.0F)
    {
        float turn = steering_command < 0.0F ? -steering_command : steering_command;
        float slow = clampf_local(controller->config.curve_slowdown, 0.0F, 0.8F) *
                     clampf_local(turn / controller->config.max_steering, 0.0F, 1.0F);
        speed_command *= (1.0F - slow);
    }

    minimum_motor_command = (0U != controller->config.allow_reverse) ? -1.0F : 0.0F;

    output->command_seq = ++controller->command_seq;
    output->source_ai_seq = perception->seq;
    output->source_tof_seq = tof->seq;
    output->state = CONTROL_STATE_AUTO;
    output->reason = CONTROL_REASON_NONE;
    output->stop_action = MOTOR_STOP_COAST;
    output->motor_enable = 1U;
    output->reserved[0] = 0U;
    output->reserved[1] = 0U;
    output->reserved[2] = 0U;
    output->left_command = clampf_local(speed_command + steering_command,
                                        minimum_motor_command, 1.0F);
    output->right_command = clampf_local(speed_command - steering_command,
                                         minimum_motor_command, 1.0F);
    output->steering_command = steering_command;
    output->speed_scale = clampf_local(confidence_scale, 0.0F, 1.0F);
    output->obstacle_risk = clampf_local(maximum_obstacle_risk, 0.0F, 1.0F);
}
