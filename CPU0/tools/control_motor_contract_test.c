#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "control_motor.h"

static ai_perception_result_t make_perception(uint32_t seq, uint32_t now_ms)
{
    ai_perception_result_t result;
    memset(&result, 0, sizeof(result));
    result.interface_version = AI_CONTROL_INTERFACE_VERSION;
    result.seq = seq;
    result.capture_timestamp_ms = now_ms;
    result.path_valid = 1U;
    result.obstacle_valid = 1U;
    result.lateral_error = 0.10F;
    result.heading_error = 0.10F;
    result.path_width = 0.50F;
    result.path_confidence = 0.90F;
    return result;
}

static tof_safety_result_t make_tof(uint32_t seq, uint32_t now_ms, uint16_t distance_mm)
{
    tof_safety_result_t result;
    memset(&result, 0, sizeof(result));
    result.seq = seq;
    result.sample_timestamp_ms = now_ms;
    result.distance_mm = distance_mm;
    result.valid = 1U;
    return result;
}

static void reach_auto(control_motor_t * controller,
                       ai_perception_result_t * perception,
                       tof_safety_result_t * tof,
                       control_motor_output_t * output)
{
    for (uint32_t frame = 1U; frame <= 3U; frame++)
    {
        *perception = make_perception(frame, frame * 10U);
        *tof = make_tof(frame, frame * 10U, 500U);
        control_motor_update(controller, perception, tof, frame * 10U, output);
    }
    assert(CONTROL_STATE_AUTO == output->state);
    assert(1U == output->motor_enable);
}

int main(void)
{
    control_motor_config_t config;
    control_motor_t controller;
    control_motor_output_t output;
    ai_perception_result_t perception;
    tof_safety_result_t tof;

    control_motor_default_config(&config);
    assert(config.lateral_gain == 0.22F);
    assert(config.heading_gain == 0.18F);
    assert(config.lateral_deadband == 0.06F && config.heading_deadband == 0.08F);
    assert(config.steering_filter_weight == 0.50F);
    control_motor_init(&controller, &config, 0U);
    reach_auto(&controller, &perception, &tof, &output);
    assert(MOTOR_STOP_COAST == output.stop_action);
    assert(output.steering_command > 0.0F);
    assert(output.left_command > output.right_command);

    /* Anti-weaving: small errors on a straight strip give exactly zero
     * steering; a real offset converges to gain * (error - deadband), half
     * way per camera frame, and repeated updates of the same frame do not
     * advance the filter. */
    {
        uint32_t seq = 10U;
        for (uint32_t i = 0U; i < 12U; i++, seq++)
        {
            perception = make_perception(seq, seq * 10U);
            perception.lateral_error = 0.05F;
            perception.heading_error = -0.07F;
            tof = make_tof(seq, seq * 10U, 500U);
            control_motor_update(&controller, &perception, &tof, seq * 10U, &output);
        }
        assert(output.steering_command > -0.001F && output.steering_command < 0.001F);
        assert(output.left_command - output.right_command < 0.002F && output.right_command - output.left_command < 0.002F);

        perception = make_perception(seq, seq * 10U);
        perception.lateral_error = 0.56F;
        perception.heading_error = 0.0F;
        tof = make_tof(seq, seq * 10U, 500U);
        control_motor_update(&controller, &perception, &tof, seq * 10U, &output);
        float first = output.steering_command;
        assert(first > 0.054F && first < 0.056F);           /* 0.5 * 0.22 * 0.50 */
        tof = make_tof(seq + 1U, seq * 10U + 5U, 500U);
        control_motor_update(&controller, &perception, &tof, seq * 10U + 5U, &output);
        assert(output.steering_command == first);           /* same frame: held */
        for (uint32_t i = 0U; i < 12U; i++)
        {
            seq++;
            perception = make_perception(seq, seq * 10U);
            perception.lateral_error = 0.56F;
            perception.heading_error = 0.0F;
            tof = make_tof(seq + 100U, seq * 10U, 500U);
            control_motor_update(&controller, &perception, &tof, seq * 10U, &output);
        }
        assert(output.steering_command > 0.109F && output.steering_command < 0.111F);
        /* Curve slowdown: the forward part (average of both wheels) is lower
        * while steering than on the straight above. */
        float turning_speed = (output.left_command + output.right_command) * 0.5F;
        float expected = config.base_command * (0.50F + 0.50F * 0.90F) *
                         (1.0F - config.curve_slowdown * output.steering_command / config.max_steering);
        assert(turning_speed > expected - 0.001F && turning_speed < expected + 0.001F);
    }

    perception = make_perception(4U, 40U);
    tof = make_tof(4U, 40U, 100U);
    control_motor_update(&controller, &perception, &tof, 40U, &output);
    assert(CONTROL_STATE_SAFE_STOP == output.state);
    assert(CONTROL_REASON_TOF_TOO_CLOSE == output.reason);
    assert(MOTOR_STOP_BRAKE == output.stop_action);
    assert(0U == output.motor_enable);

    control_motor_init(&controller, &config, 0U);
    reach_auto(&controller, &perception, &tof, &output);
    perception = make_perception(4U, 40U);
    perception.obstacle_count = 1U;
    perception.obstacles[0].confidence = 0.90F;
    perception.obstacles[0].corridor_overlap = 0.90F;
    perception.obstacles[0].center_x = 0.0F;
    perception.obstacles[0].bbox_bottom = 0.90F;
    /* A person/car detection is diagnostic only; road following remains active. */
    tof = make_tof(4U, 40U, 500U);
    control_motor_update(&controller, &perception, &tof, 40U, &output);
    assert(1U == output.motor_enable);
    assert(output.speed_scale > 0.0F);
    /* Close/flickering obstacle detections still don't change mode or stop. */
    perception.seq = 5U;
    perception.capture_timestamp_ms = 50U;
    tof = make_tof(5U, 50U, 200U);
    control_motor_update(&controller, &perception, &tof, 50U, &output);
    assert(CONTROL_STATE_AUTO == output.state);
    assert(1U == output.motor_enable);

    /* Detector unavailable: a valid road path remains usable. */
    perception.seq = 6U;
    perception.capture_timestamp_ms = 60U;
    perception.obstacle_valid = 0U;
    tof = make_tof(6U, 60U, 500U);
    control_motor_update(&controller, &perception, &tof, 60U, &output);
    assert(CONTROL_STATE_AUTO == output.state);
    assert(1U == output.motor_enable);

    control_motor_init(&controller, &config, 0U);
    reach_auto(&controller, &perception, &tof, &output);
    tof = make_tof(4U, 600U, 500U);
    control_motor_update(&controller, &perception, &tof, 600U, &output);
    assert(CONTROL_STATE_TOR == output.state);
    assert(CONTROL_REASON_AI_STALE == output.reason);
    assert(MOTOR_STOP_COAST == output.stop_action);

    puts("control_motor contract tests: PASS");
    return 0;
}
