#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
/* ---- Web API vocabulary (POST /api/control) --------------------------------
 * This header is the single C definition of the browser <-> M85 request and
 * response.  It has no HTTP/lwIP dependency, so the JSON codec, the HTTP task
 * and the M85 gateway (web_control_adapter.c) all share these types.
 * Wire names match sample_server/constants.h and docs/protocol.md; the
 * numeric enum values are internal and never sent on the wire.  Each enum
 * ends with a *_COUNT sentinel that sizes its name table. */
typedef enum
{
    VEHICLE_MANUAL,
    VEHICLE_AUTO,
    VEHICLE_AUTO_ABORT,
    VEHICLE_MANUAL_ABORT,
    VEHICLE_STATE_COUNT
} vehicle_state_t;
typedef enum
{
    MODE_REQUEST_NONE,
    MODE_REQUEST_MANUAL,
    MODE_REQUEST_AUTO,
    MODE_REQUEST_COUNT
} mode_request_t;
typedef enum
{
    STOP_REASON_NONE,
    STOP_REASON_OBSTACLE,
    STOP_REASON_TOR_TIMEOUT,
    STOP_REASON_MANUAL_ABORT_BUTTON,
    STOP_REASON_COMM_TIMEOUT,
    STOP_REASON_SENSOR_ERROR,
    STOP_REASON_DISTANCE_EMERGENCY, /* ToF <= emergency distance (50 mm)      */
    STOP_REASON_BUTTON_EMERGENCY,   /* Web ESTOP (latched until board reset)  */
    STOP_REASON_AI_OBSTACLE,        /* person/car close ahead: TOR (AUTO)     */
    STOP_REASON_DISTANCE_PRESTOP,   /* ToF <= ordinary stop distance (100 mm) */
    STOP_REASON_ROAD_UNAVAILABLE,   /* AI frame invalid/stale in AUTO         */
    STOP_REASON_COUNT
} stop_reason_t;
/* Person/car alarm raised by the M85 camera (independent of the drive mode). */
typedef enum
{
    OBSTACLE_KIND_NONE,
    OBSTACLE_KIND_PERSON,
    OBSTACLE_KIND_CAR,
    OBSTACLE_KIND_PERSON_AND_CAR,
    OBSTACLE_KIND_COUNT
} obstacle_kind_t;
typedef enum
{
    REQUEST_REJECT_NONE,
    REQUEST_REJECT_SENSOR_NOT_READY,
    REQUEST_REJECT_OBSTACLE_NEAR,
    REQUEST_REJECT_IN_TOR,
    REQUEST_REJECT_IN_MANUAL_ABORT,
    REQUEST_REJECT_MODE_MISMATCH,
    REQUEST_REJECT_LINK_NOT_READY, /* AUTO refused: M85->M33 heartbeat stale */
    REQUEST_REJECT_PATH_NOT_READY, /* AUTO refused: AI road path not ready   */
    REQUEST_REJECT_COUNT
} request_reject_reason_t;

/* Browser -> M85.  Decoded and range-checked by control_json_decode(). */
typedef struct
{
    vehicle_state_t client_mode; /* mode the UI currently shows (mirrors M33)  */
    float throttle, steering;    /* -1.0 .. +1.0                               */
    mode_request_t mode_request; /* explicit mode switch; NONE = keep          */
    bool deadman;                /* explicit hold input; never inferred        */
    bool estop_request;          /* explicit emergency button                  */
    bool manual_abort_request, reset_abort_request;
} control_request_t;
/* M85 -> browser.  Built from the M33 status by web_control_adapter.c. */
typedef struct
{
    vehicle_state_t mode;
    int32_t front_distance_mm;   /* -1 when ToF is invalid                     */
    bool tor_active;
    uint32_t tor_remaining_ms;
    stop_reason_t stop_reason;
    request_reject_reason_t request_reject_reason;
    bool armed;                  /* M33 motor authorization state              */
    uint32_t web_seq;             /* latest M33-accepted web sequence           */
    bool obstacle_alarm;          /* person/car in the corridor right now       */
    obstacle_kind_t obstacle_kind;
    uint32_t alarm_seq;           /* +1 on every new alarm (one log line each)  */
} control_response_t;

/* Wire-name tables for JSON and debug logs, indexed by the enum value. */
extern const char *const control_modes[VEHICLE_STATE_COUNT];
extern const char *const control_requests[MODE_REQUEST_COUNT];
extern const char *const control_stops[STOP_REASON_COUNT];
extern const char *const control_rejects[REQUEST_REJECT_COUNT];
extern const char *const control_obstacle_kinds[OBSTACLE_KIND_COUNT];
/* Bounds-checked lookups; out-of-range values return "UNKNOWN". */
const char *control_vehicle_state_name(vehicle_state_t value);
const char *control_mode_request_name(mode_request_t value);
const char *control_stop_reason_name(stop_reason_t value);
const char *control_reject_reason_name(request_reject_reason_t value);
#endif
