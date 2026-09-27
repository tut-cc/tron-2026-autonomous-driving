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
    control_motor_init(&controller, &config, 0U);
    reach_auto(&controller, &perception, &tof, &output);
    assert(MOTOR_STOP_COAST == output.stop_action);
    assert(output.steering_command > 0.0F);
    assert(output.left_command > output.right_command);

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
    tof = make_tof(4U, 40U, 500U);
    control_motor_update(&controller, &perception, &tof, 40U, &output);
    assert(CONTROL_STATE_SAFE_STOP == output.state);
    assert(CONTROL_REASON_OBSTACLE_IN_CORRIDOR == output.reason);
    assert(MOTOR_STOP_BRAKE == output.stop_action);

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
