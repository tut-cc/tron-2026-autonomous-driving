"""
Mini 4WD WebApp - プロトコルおよび制御定数定義
"""

class MCUMode:
    MANUAL       = "MANUAL"
    AUTO         = "AUTO"
    AUTO_ABORT   = "AUTO_ABORT"
    MANUAL_ABORT = "MANUAL_ABORT"

class StopReason:
    NONE                = "NONE"
    OBSTACLE            = "OBSTACLE"
    TOR_TIMEOUT         = "TOR_TIMEOUT"
    MANUAL_ABORT_BUTTON = "MANUAL_ABORT_BUTTON"
    COMM_TIMEOUT        = "COMM_TIMEOUT"
    SENSOR_ERROR        = "SENSOR_ERROR"

class RejectReason:
    NONE             = "NONE"
    SENSOR_NOT_READY = "SENSOR_NOT_READY"
    OBSTACLE_NEAR    = "OBSTACLE_NEAR"
    IN_TOR           = "IN_TOR"
    IN_MANUAL_ABORT  = "IN_MANUAL_ABORT"
    MODE_MISMATCH    = "MODE_MISMATCH"

DEFAULT_PORT           = 8765
DEFAULT_HOST           = "0.0.0.0"
HEARTBEAT_INTERVAL_SEC = 0.1  # 100ms
DEADMAN_TIMEOUT_SEC    = 0.3  # 300ms
COMM_TIMEOUT_SEC       = 1.5  # 1500ms (通信切断・フェイルセーフ判定)
