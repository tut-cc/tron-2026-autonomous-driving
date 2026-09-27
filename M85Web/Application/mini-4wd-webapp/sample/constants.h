#ifndef CONSTANTS_H
#define CONSTANTS_H

/* サーバー設定 */
#define DEFAULT_PORT            8765
#define DEFAULT_HOST            "0.0.0.0"

/* 通信・制御周期およびタイムアウト閾値 (ms) */
#define HEARTBEAT_INTERVAL_MS   100  /* 100ms 周期 */
#define DEADMAN_TIMEOUT_MS      300  /* 300ms 指示途絶でモーター停止 */
#define COMM_TIMEOUT_MS         1500 /* 1500ms 通信切断で自律中断 */

/* MCU 運転モード */
#define MCU_MODE_MANUAL         "MANUAL"
#define MCU_MODE_AUTO           "AUTO"
#define MCU_MODE_AUTO_ABORT     "AUTO_ABORT"
#define MCU_MODE_MANUAL_ABORT   "MANUAL_ABORT"

/* 中断要因 (Stop Reason) */
#define STOP_REASON_NONE                "NONE"
#define STOP_REASON_OBSTACLE            "OBSTACLE"
#define STOP_REASON_TOR_TIMEOUT         "TOR_TIMEOUT"
#define STOP_REASON_MANUAL_ABORT_BUTTON "MANUAL_ABORT_BUTTON"
#define STOP_REASON_COMM_TIMEOUT        "COMM_TIMEOUT"
#define STOP_REASON_SENSOR_ERROR        "SENSOR_ERROR"

/* 要求拒否理由 (Reject Reason) */
#define REJECT_REASON_NONE             "NONE"
#define REJECT_REASON_SENSOR_NOT_READY "SENSOR_NOT_READY"
#define REJECT_REASON_OBSTACLE_NEAR    "OBSTACLE_NEAR"
#define REJECT_REASON_IN_TOR           "IN_TOR"
#define REJECT_REASON_IN_MANUAL_ABORT  "IN_MANUAL_ABORT"
#define REJECT_REASON_MODE_MISMATCH    "MODE_MISMATCH"
#define REJECT_REASON_LINK_NOT_READY   "LINK_NOT_READY"
#define REJECT_REASON_PATH_NOT_READY   "PATH_NOT_READY"

#endif /* CONSTANTS_H */
