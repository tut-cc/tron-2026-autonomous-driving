#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <pthread.h>
#include "constants.h"

/* 車両の確定状態 (テレメトリ配信データ) */
typedef struct {
    char mode[16];                  /* MANUAL, AUTO, AUTO_ABORT, MANUAL_ABORT */
    int  front_distance_mm;         /* 前方測距値 (mm) */
    int  tor_active;                /* 1: TOR警告中, 0: なし */
    int  tor_remaining_ms;          /* TOR残り時間 (ms) */
    char stop_reason[32];           /* NONE, OBSTACLE, etc. */
    char request_reject_reason[32]; /* NONE, etc. */
} VehicleState;

/* WebApp から受信する操作コマンド */
typedef struct {
    char  client_mode[16];
    float throttle;                 /* -1.0 〜 1.0 */
    float steering;                 /* -1.0 〜 1.0 */
    char  mode_request[16];         /* NONE, MANUAL, AUTO */
    int   manual_abort_request;     /* 1: 手動中断要求 */
    int   reset_abort_request;      /* 1: リセット要求 */
} VehicleCommand;

/* 車両制御コア構造体 */
typedef struct {
    VehicleState state;
    float        throttle;
    float        steering;
    uint64_t     last_command_time_ms;
    int          has_received_command;
    pthread_mutex_t lock;
} VehicleController;

/* 初期化 */
void controller_init(VehicleController *ctrl);

/* テレメトリ JSON 文字列の生成 */
void controller_get_telemetry_json(VehicleController *ctrl, char *buf, size_t buf_size);

/* WebApp からのコマンド処理 */
void controller_process_command(VehicleController *ctrl, const VehicleCommand *cmd);

/* 周期タイマー監視 (100ms周期などで呼出: TOR・通信途絶・デッドマン) */
void controller_tick(VehicleController *ctrl, int dt_ms);

/* 中断・TOR トリガー */
void controller_trigger_manual_abort(VehicleController *ctrl, const char *reason);
void controller_trigger_auto_abort(VehicleController *ctrl, const char *reason);
void controller_trigger_tor(VehicleController *ctrl, int duration_ms);
int  controller_reset_abort(VehicleController *ctrl);
int  controller_request_mode(VehicleController *ctrl, const char *target_mode);

/* 測距センサー更新 */
void controller_update_sensor(VehicleController *ctrl, int front_distance_mm);

/* モックシナリオ適用 (1: MANUAL, 2: AUTO, 3: TOR, 4: AUTO_ABORT, 5: MANUAL_ABORT) */
void controller_apply_scenario(VehicleController *ctrl, int scenario_id);

#endif /* CONTROLLER_H */
