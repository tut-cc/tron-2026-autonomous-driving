#include "controller.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void controller_init(VehicleController *ctrl) {
    memset(ctrl, 0, sizeof(*ctrl));
    pthread_mutex_init(&ctrl->lock, NULL);

    /* 初期状態 */
    strncpy(ctrl->state.mode, MCU_MODE_MANUAL, sizeof(ctrl->state.mode) - 1);
    ctrl->state.front_distance_mm = 1200;
    ctrl->state.tor_active = 0;
    ctrl->state.tor_remaining_ms = 0;
    strncpy(ctrl->state.stop_reason, STOP_REASON_NONE, sizeof(ctrl->state.stop_reason) - 1);
    strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);

    ctrl->throttle = 0.0f;
    ctrl->steering = 0.0f;
    ctrl->last_command_time_ms = 0;
    ctrl->has_received_command = 0;
}

void controller_get_telemetry_json(VehicleController *ctrl, char *buf, size_t buf_size) {
    pthread_mutex_lock(&ctrl->lock);
    snprintf(buf, buf_size,
        "{\"mode\":\"%s\","
        "\"front_distance_mm\":%d,"
        "\"tor_active\":%s,"
        "\"tor_remaining_ms\":%d,"
        "\"stop_reason\":\"%s\","
        "\"request_reject_reason\":\"%s\"}",
        ctrl->state.mode,
        ctrl->state.front_distance_mm,
        ctrl->state.tor_active ? "true" : "false",
        ctrl->state.tor_remaining_ms,
        ctrl->state.stop_reason,
        ctrl->state.request_reject_reason
    );
    pthread_mutex_unlock(&ctrl->lock);
}

static void apply_motor(VehicleController *ctrl, float throttle, float steering) {
    ctrl->throttle = throttle;
    ctrl->steering = steering;
}

void controller_trigger_manual_abort(VehicleController *ctrl, const char *reason) {
    strncpy(ctrl->state.mode, MCU_MODE_MANUAL_ABORT, sizeof(ctrl->state.mode) - 1);
    strncpy(ctrl->state.stop_reason, reason, sizeof(ctrl->state.stop_reason) - 1);
    ctrl->state.tor_active = 0;
    ctrl->state.tor_remaining_ms = 0;
    strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
    apply_motor(ctrl, 0.0f, 0.0f);
}

void controller_trigger_auto_abort(VehicleController *ctrl, const char *reason) {
    strncpy(ctrl->state.mode, MCU_MODE_AUTO_ABORT, sizeof(ctrl->state.mode) - 1);
    strncpy(ctrl->state.stop_reason, reason, sizeof(ctrl->state.stop_reason) - 1);
    ctrl->state.tor_active = 0;
    ctrl->state.tor_remaining_ms = 0;
    apply_motor(ctrl, 0.0f, 0.0f);
}

void controller_trigger_tor(VehicleController *ctrl, int duration_ms) {
    if (strcmp(ctrl->state.mode, MCU_MODE_AUTO) == 0) {
        ctrl->state.tor_active = 1;
        ctrl->state.tor_remaining_ms = duration_ms;
    }
}

int controller_reset_abort(VehicleController *ctrl) {
    if (strcmp(ctrl->state.mode, MCU_MODE_AUTO_ABORT) != 0 &&
        strcmp(ctrl->state.mode, MCU_MODE_MANUAL_ABORT) != 0) {
        return 0;
    }

    if (ctrl->state.front_distance_mm > 200) {
        strncpy(ctrl->state.mode, MCU_MODE_MANUAL, sizeof(ctrl->state.mode) - 1);
        strncpy(ctrl->state.stop_reason, STOP_REASON_NONE, sizeof(ctrl->state.stop_reason) - 1);
        strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
        ctrl->state.tor_active = 0;
        ctrl->state.tor_remaining_ms = 0;
        apply_motor(ctrl, 0.0f, 0.0f);
        if (ctrl->has_received_command) {
            ctrl->last_command_time_ms = get_time_ms();
        }
        return 1;
    } else {
        strncpy(ctrl->state.request_reject_reason, REJECT_REASON_OBSTACLE_NEAR, sizeof(ctrl->state.request_reject_reason) - 1);
        return 0;
    }
}

int controller_request_mode(VehicleController *ctrl, const char *target_mode) {
    const char *current_mode = ctrl->state.mode;

    /* 中断中の切替要求は拒否 */
    if (strcmp(current_mode, MCU_MODE_MANUAL_ABORT) == 0) {
        strncpy(ctrl->state.request_reject_reason, REJECT_REASON_IN_MANUAL_ABORT, sizeof(ctrl->state.request_reject_reason) - 1);
        return 0;
    }
    if (strcmp(current_mode, MCU_MODE_AUTO_ABORT) == 0) {
        strncpy(ctrl->state.request_reject_reason, REJECT_REASON_MODE_MISMATCH, sizeof(ctrl->state.request_reject_reason) - 1);
        return 0;
    }

    if (strcmp(target_mode, MCU_MODE_AUTO) == 0) {
        if (ctrl->state.tor_active) {
            strncpy(ctrl->state.request_reject_reason, REJECT_REASON_IN_TOR, sizeof(ctrl->state.request_reject_reason) - 1);
            return 0;
        }
        if (strcmp(current_mode, MCU_MODE_MANUAL) == 0) {
            if (ctrl->state.front_distance_mm > 300) {
                strncpy(ctrl->state.mode, MCU_MODE_AUTO, sizeof(ctrl->state.mode) - 1);
                strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
                apply_motor(ctrl, 0.0f, 0.0f);
                return 1;
            } else {
                strncpy(ctrl->state.request_reject_reason, REJECT_REASON_OBSTACLE_NEAR, sizeof(ctrl->state.request_reject_reason) - 1);
                return 0;
            }
        } else {
            strncpy(ctrl->state.request_reject_reason, REJECT_REASON_MODE_MISMATCH, sizeof(ctrl->state.request_reject_reason) - 1);
            return 0;
        }
    } else if (strcmp(target_mode, MCU_MODE_MANUAL) == 0) {
        /* AUTOやTORからMANUALへの手動介入 (即時受諾) */
        strncpy(ctrl->state.mode, MCU_MODE_MANUAL, sizeof(ctrl->state.mode) - 1);
        ctrl->state.tor_active = 0;
        ctrl->state.tor_remaining_ms = 0;
        strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
        return 1;
    }

    return 0;
}

void controller_process_command(VehicleController *ctrl, const VehicleCommand *cmd) {
    pthread_mutex_lock(&ctrl->lock);

    ctrl->last_command_time_ms = get_time_ms();
    ctrl->has_received_command = 1;

    /* 1. 手動中断要求 (最優先) */
    if (cmd->manual_abort_request) {
        controller_trigger_manual_abort(ctrl, STOP_REASON_MANUAL_ABORT_BUTTON);
        pthread_mutex_unlock(&ctrl->lock);
        return;
    }

    /* 2. リセット要求 */
    if (cmd->reset_abort_request) {
        controller_reset_abort(ctrl);
        pthread_mutex_unlock(&ctrl->lock);
        return;
    }

    /* 3. モード切替要求 */
    if (strlen(cmd->mode_request) > 0 && strcmp(cmd->mode_request, "NONE") != 0) {
        controller_request_mode(ctrl, cmd->mode_request);
    }

    /* 4. 手動走行指示 */
    if (strcmp(ctrl->state.mode, MCU_MODE_MANUAL) == 0) {
        if (strlen(cmd->client_mode) > 0 && strcmp(cmd->client_mode, MCU_MODE_MANUAL) != 0) {
            apply_motor(ctrl, 0.0f, 0.0f);
        } else {
            float th = cmd->throttle;
            float st = cmd->steering;
            if (th < -1.0f) th = -1.0f; else if (th > 1.0f) th = 1.0f;
            if (st < -1.0f) st = -1.0f; else if (st > 1.0f) st = 1.0f;
            apply_motor(ctrl, th, st);
        }
    } else {
        apply_motor(ctrl, 0.0f, 0.0f);
    }

    pthread_mutex_unlock(&ctrl->lock);
}

void controller_tick(VehicleController *ctrl, int dt_ms) {
    pthread_mutex_lock(&ctrl->lock);

    uint64_t now = get_time_ms();

    /* TOR カウントダウン */
    if (ctrl->state.tor_active && ctrl->state.tor_remaining_ms > 0) {
        ctrl->state.tor_remaining_ms -= dt_ms;
        if (ctrl->state.tor_remaining_ms <= 0) {
            ctrl->state.tor_remaining_ms = 0;
            controller_trigger_auto_abort(ctrl, STOP_REASON_TOR_TIMEOUT);
        }
    }

    /* 通信途絶・デッドマン監視 (コマンドを1度でも受信した後) */
    if (ctrl->has_received_command) {
        uint64_t elapsed = now - ctrl->last_command_time_ms;

        /* 通信途絶監視 (1.5秒途絶で AUTO_ABORT) */
        if (strcmp(ctrl->state.mode, MCU_MODE_AUTO_ABORT) != 0 &&
            strcmp(ctrl->state.mode, MCU_MODE_MANUAL_ABORT) != 0) {
            if (elapsed > COMM_TIMEOUT_MS) {
                controller_trigger_auto_abort(ctrl, STOP_REASON_COMM_TIMEOUT);
            }
        }

        /* デッドマン監視 (MANUAL走行中に300ms途絶でモーター停止) */
        if (strcmp(ctrl->state.mode, MCU_MODE_MANUAL) == 0) {
            if (elapsed > DEADMAN_TIMEOUT_MS) {
                apply_motor(ctrl, 0.0f, 0.0f);
            }
        }
    }

    pthread_mutex_unlock(&ctrl->lock);
}

void controller_update_sensor(VehicleController *ctrl, int front_distance_mm) {
    pthread_mutex_lock(&ctrl->lock);
    ctrl->state.front_distance_mm = front_distance_mm;
    if (strcmp(ctrl->state.mode, MCU_MODE_AUTO) == 0 && front_distance_mm < 100) {
        controller_trigger_auto_abort(ctrl, STOP_REASON_OBSTACLE);
    }
    pthread_mutex_unlock(&ctrl->lock);
}

void controller_apply_scenario(VehicleController *ctrl, int scenario_id) {
    pthread_mutex_lock(&ctrl->lock);

    switch (scenario_id) {
        case 1: /* MANUAL Ready */
            strncpy(ctrl->state.mode, MCU_MODE_MANUAL, sizeof(ctrl->state.mode) - 1);
            ctrl->state.front_distance_mm = 1200;
            ctrl->state.tor_active = 0;
            ctrl->state.tor_remaining_ms = 0;
            strncpy(ctrl->state.stop_reason, STOP_REASON_NONE, sizeof(ctrl->state.stop_reason) - 1);
            strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
            apply_motor(ctrl, 0.0f, 0.0f);
            printf("[Scenario 1] MANUAL Ready applied.\n");
            break;

        case 2: /* AUTO Cruising */
            strncpy(ctrl->state.mode, MCU_MODE_AUTO, sizeof(ctrl->state.mode) - 1);
            ctrl->state.front_distance_mm = 850;
            ctrl->state.tor_active = 0;
            ctrl->state.tor_remaining_ms = 0;
            strncpy(ctrl->state.stop_reason, STOP_REASON_NONE, sizeof(ctrl->state.stop_reason) - 1);
            strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
            apply_motor(ctrl, 0.0f, 0.0f);
            printf("[Scenario 2] AUTO Cruising applied.\n");
            break;

        case 3: /* TOR Warning */
            strncpy(ctrl->state.mode, MCU_MODE_AUTO, sizeof(ctrl->state.mode) - 1);
            ctrl->state.front_distance_mm = 350;
            ctrl->state.tor_active = 1;
            ctrl->state.tor_remaining_ms = 3000;
            strncpy(ctrl->state.stop_reason, STOP_REASON_NONE, sizeof(ctrl->state.stop_reason) - 1);
            strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
            printf("[Scenario 3] TOR Warning applied (3000ms).\n");
            break;

        case 4: /* Obstacle Stop (AUTO_ABORT) */
            strncpy(ctrl->state.mode, MCU_MODE_AUTO_ABORT, sizeof(ctrl->state.mode) - 1);
            ctrl->state.front_distance_mm = 80;
            ctrl->state.tor_active = 0;
            ctrl->state.tor_remaining_ms = 0;
            strncpy(ctrl->state.stop_reason, STOP_REASON_OBSTACLE, sizeof(ctrl->state.stop_reason) - 1);
            strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
            apply_motor(ctrl, 0.0f, 0.0f);
            printf("[Scenario 4] Obstacle Stop applied.\n");
            break;

        case 5: /* Emergency Stop (MANUAL_ABORT) */
            strncpy(ctrl->state.mode, MCU_MODE_MANUAL_ABORT, sizeof(ctrl->state.mode) - 1);
            ctrl->state.front_distance_mm = 1200;
            ctrl->state.tor_active = 0;
            ctrl->state.tor_remaining_ms = 0;
            strncpy(ctrl->state.stop_reason, STOP_REASON_MANUAL_ABORT_BUTTON, sizeof(ctrl->state.stop_reason) - 1);
            strncpy(ctrl->state.request_reject_reason, REJECT_REASON_NONE, sizeof(ctrl->state.request_reject_reason) - 1);
            apply_motor(ctrl, 0.0f, 0.0f);
            printf("[Scenario 5] Emergency Stop applied.\n");
            break;

        default:
            break;
    }

    if (ctrl->has_received_command) {
        ctrl->last_command_time_ms = get_time_ms();
    }

    pthread_mutex_unlock(&ctrl->lock);
}
