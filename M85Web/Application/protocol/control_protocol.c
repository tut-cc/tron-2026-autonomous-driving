#include "control_protocol.h"
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
const char *const control_modes[VEHICLE_STATE_COUNT] = {"MANUAL", "AUTO", "AUTO_ABORT",
                                                        "MANUAL_ABORT"};
const char *const control_requests[MODE_REQUEST_COUNT] = {"NONE", "MANUAL", "AUTO"};
const char *const control_stops[STOP_REASON_COUNT] = {
    "NONE", "OBSTACLE", "TOR_TIMEOUT", "MANUAL_ABORT_BUTTON", "COMM_TIMEOUT", "SENSOR_ERROR",
    "DISTANCE_EMERGENCY", "BUTTON_EMERGENCY", "AI_OBSTACLE", "DISTANCE_PRESTOP",
    "ROAD_UNAVAILABLE"};
const char *const control_rejects[REQUEST_REJECT_COUNT] = {
    "NONE", "SENSOR_NOT_READY", "OBSTACLE_NEAR", "IN_TOR", "IN_MANUAL_ABORT", "MODE_MISMATCH",
    "LINK_NOT_READY", "PATH_NOT_READY"};
/* A missing initializer would leave a NULL entry; catch it at compile time. */
_Static_assert(COUNT_OF(control_modes) == 4 && COUNT_OF(control_requests) == 3 &&
                   COUNT_OF(control_stops) == 11 && COUNT_OF(control_rejects) == 8,
               "wire-name tables must match the enums");
static const char *lookup(const char *const *names, unsigned count, unsigned value)
{
    return value < count ? names[value] : "UNKNOWN";
}
const char *control_vehicle_state_name(vehicle_state_t value)
{
    return lookup(control_modes, VEHICLE_STATE_COUNT, (unsigned)value);
}
const char *control_mode_request_name(mode_request_t value)
{
    return lookup(control_requests, MODE_REQUEST_COUNT, (unsigned)value);
}
const char *control_stop_reason_name(stop_reason_t value)
{
    return lookup(control_stops, STOP_REASON_COUNT, (unsigned)value);
}
const char *control_reject_reason_name(request_reject_reason_t value)
{
    return lookup(control_rejects, REQUEST_REJECT_COUNT, (unsigned)value);
}
