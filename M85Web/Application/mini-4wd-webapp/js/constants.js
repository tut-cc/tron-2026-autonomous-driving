export const UIState = Object.fromEntries([
    'DISCONNECTED', 'MANUAL', 'AUTO_PENDING', 'AUTO_MANUAL_PENDING',
    'TOR_MANUAL_PENDING', 'AUTO', 'AUTO_TOR', 'AUTO_ABORT', 'MANUAL_ABORT'
].map(k => [k, k]));

export const MCUMode     = Object.fromEntries(['MANUAL', 'AUTO', 'AUTO_ABORT', 'MANUAL_ABORT'].map(k => [k, k]));
export const ModeRequest = { NONE: 'NONE', MANUAL: 'MANUAL', AUTO: 'AUTO' };

export const StopReasonText = {
    OBSTACLE:            '障害物検知',
    AI_OBSTACLE:         '人物・車が接近：運転を引き継いでください',
    ROAD_UNAVAILABLE:    '走行経路を認識できず停止',
    DISTANCE_EMERGENCY:  '前方50 mmで距離による緊急停止（ToF）',
    DISTANCE_PRESTOP:    '前方100 mmで通常停止（ToF）',
    BUTTON_EMERGENCY:    'ボタンによる緊急停止（基板リセットで解除）',
    TOR_TIMEOUT:         '引継ぎ時間切れ',
    MANUAL_ABORT_BUTTON: 'ABORT で停止',
    COMM_TIMEOUT:        '通信切断',
    SENSOR_ERROR:        '距離センサー(ToF)の値が無効・途絶',
    INTERNAL_FAULT:      '内部異常で停止（基板リセットが必要）'
};

export const ObstacleKindText = {
    PERSON:         '人物',
    CAR:            '車',
    PERSON_AND_CAR: '人物と車'
};

export const RejectReasonText = {
    OBSTACLE_NEAR:    '前方に障害物があります',
    SENSOR_NOT_READY: '距離センサー(ToF)が準備できていません',
    LINK_NOT_READY:   'M85とM33の通信が途切れています',
    PATH_NOT_READY:   '走行路を認識できていません',
    IN_TOR:           '運転引継ぎ警告中です',
    IN_MANUAL_ABORT:  '手動中断中のため操作できません',
    MODE_MISMATCH:    '中断中のため切替できません'
};

export const Config = {
    POLLING_INTERVAL_MS:       100 ,
    REQUEST_TIMEOUT_MS:        800 ,
    HEARTBEAT_TIMEOUT_MS:      1500,
    MODE_SWITCH_TIMEOUT_MS:    1000,
    ALERT_DISPLAY_DURATION_MS: 3000,
    ALARM_LOG_MAX:             5
};
