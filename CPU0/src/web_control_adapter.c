#include "web_control_adapter.h"
#include <math.h>

/* ---- request -> command --------------------------------------------------- */
static int in_unit(float value)
{
    return isfinite(value) && value >= -1.0F && value <= 1.0F;
}

static int32_t to_permille(float value)
{
    float scaled = value * (float)VC_PERMILLE_MAX;
    return (int32_t)(scaled >= 0.0F ? scaled + 0.5F : scaled - 0.5F);
}

static int is_abort_mirror(vehicle_state_t mode)
{
    return mode == VEHICLE_AUTO_ABORT || mode == VEHICLE_MANUAL_ABORT;
}

static uint32_t selected_mode(const control_request_t *request)
{
    /* An explicit mode_request wins over the client's current mode: the UI
     * sends mode_request=MANUAL while it still reports client_mode=AUTO. */
    if (request->mode_request == MODE_REQUEST_MANUAL) return VC_MODE_MANUAL;
    if (request->mode_request == MODE_REQUEST_AUTO) return VC_MODE_AUTO;
    return (request->client_mode == VEHICLE_AUTO || request->client_mode == VEHICLE_AUTO_ABORT) ?
           VC_MODE_AUTO : VC_MODE_MANUAL;
}

int web_adapter_request_to_command(const control_request_t *request, vc_web_t *command)
{
    if (!request || !command || !in_unit(request->throttle) || !in_unit(request->steering) ||
        (unsigned)request->client_mode >= VEHICLE_STATE_COUNT ||
        (unsigned)request->mode_request >= MODE_REQUEST_COUNT) {
        return -1;
    }

    *command = (vc_web_t){0};
    command->mode = selected_mode(request);

    /* Only an explicit button press stops the vehicle.  A web reset is
     * intentionally downgraded to STOP: only the local SW2/cause-clear path
     * may clear M33 Emergency. */
    if (request->estop_request) {
        command->action = VC_WEB_ESTOP;
        return 0;
    }
    if (request->manual_abort_request || request->reset_abort_request) {
        command->action = VC_WEB_STOP;
        return 0;
    }
    if (request->mode_request != MODE_REQUEST_NONE) {
        command->action = VC_WEB_MODE;
        return 0;
    }

    command->action = VC_WEB_DRIVE;
    if (is_abort_mirror(request->client_mode)) {
        /* The UI only mirrors the M33 abort/emergency state here.  Forward a
         * neutral keep-alive (never a drive request) instead of a STOP, so the
         * M33 sees a fresh neutral command and SW1 can re-arm after SW2. */
        return 0;
    }
    /* Mode selection is not a drive authorization.  Only the explicit
     * deadman field can set the M33 deadman bit. */
    command->deadman = request->deadman ? 1U : 0U;
    command->linear = to_permille(request->throttle);
    command->steering = to_permille(request->steering);
    return 0;
}

/* ---- status -> response --------------------------------------------------- */
static vehicle_state_t response_mode(const vc_status_t *status)
{
    switch (status->state) {
    case VC_EMERGENCY:
        /* The HTTP vocabulary has no separate emergency enum.  MANUAL_ABORT
         * keeps the UI stopped; Web reset still never clears M33's latch. */
        return VEHICLE_MANUAL_ABORT;
    case VC_AUTO:
    case VC_TOR:
        return VEHICLE_AUTO;
    case VC_MANUAL:
        return VEHICLE_MANUAL;
    default:
        /* TOR unanswered: AUTO was aborted (UI shows the TOR_TIMEOUT reason
         * until the operator resets with STOP). */
        if (status->reason == VC_TOR_TIMEOUT) return VEHICLE_AUTO_ABORT;
        /* Stopped: report the M33-selected mode.  The current RC firmware
         * boots in MANUAL; preserve the field rather than assuming it. */
        return (status->mode == VC_MODE_AUTO) ? VEHICLE_AUTO : VEHICLE_MANUAL;
    }
}

static int is_mode_start_rejection(const vc_status_t *status)
{
    return status->state == VC_STOPPED && !status->armed &&
           status->mode == VC_MODE_MANUAL &&
           vc_reason_is_auto_refusal(status->reason);
}

static request_reject_reason_t mode_start_reject_reason(const vc_status_t *status)
{
    if (!is_mode_start_rejection(status)) return REQUEST_REJECT_NONE;
    switch (status->reason) {
    case VC_AUTO_REFUSED_TOF:  return REQUEST_REJECT_SENSOR_NOT_READY;
    case VC_AUTO_REFUSED_LINK: return REQUEST_REJECT_LINK_NOT_READY;
    default:                   return REQUEST_REJECT_PATH_NOT_READY;
    }
}

static stop_reason_t response_stop_reason(const vc_status_t *status)
{
    /* An AUTO start refusal uses the existing reason word as an event; do not
     * also mislabel it as a running obstacle/communication stop. */
    if (is_mode_start_rejection(status)) return STOP_REASON_NONE;
    switch (status->reason) {
    case VC_TOF_NEAR:    return STOP_REASON_DISTANCE_EMERGENCY;
    case VC_TOF_PRESTOP: return STOP_REASON_DISTANCE_PRESTOP;
    case VC_ESTOP:       return STOP_REASON_BUTTON_EMERGENCY;
    case VC_AI_OBSTACLE:
    case VC_TOR_OBSTACLE: return STOP_REASON_AI_OBSTACLE; /* TOR: close person/car */
    case VC_OPERATOR: return STOP_REASON_MANUAL_ABORT_BUTTON;
    case VC_LINK:     /* M85 heartbeat to M33 lost */
    case VC_WEB:      return STOP_REASON_COMM_TIMEOUT; /* Web command stale/invalid */
    case VC_TOF:      return STOP_REASON_SENSOR_ERROR;
    case VC_AI:       return STOP_REASON_ROAD_UNAVAILABLE; /* bad/stale AI frame in AUTO */
    case VC_TOR_TIMEOUT: return STOP_REASON_TOR_TIMEOUT;
    case VC_TOR_REQUEST: return STOP_REASON_NONE; /* tor_active carries it */
    case VC_INTERNAL:    return STOP_REASON_INTERNAL_FAULT; /* latched until board reset */
    default:          return STOP_REASON_NONE;
    }
}

void web_adapter_obstacle_alarm(uint32_t word, control_response_t *response)
{
    if (!response) return;
    bool person = (word & AUTONOMY_ALARM_PERSON) != 0U;
    bool car = (word & AUTONOMY_ALARM_CAR) != 0U;
    response->obstacle_alarm = (word & AUTONOMY_ALARM_ACTIVE) != 0U;
    response->obstacle_kind = !response->obstacle_alarm ? OBSTACLE_KIND_NONE :
                              (person && car) ? OBSTACLE_KIND_PERSON_AND_CAR :
                              person ? OBSTACLE_KIND_PERSON :
                              car ? OBSTACLE_KIND_CAR : OBSTACLE_KIND_NONE;
    response->alarm_seq = word >> AUTONOMY_ALARM_SEQ_SHIFT;
}

uint32_t web_adapter_tor_remaining_ms(const vc_status_t *status, web_tor_tracker_t *tracker)
{
    if (!status || !tracker) return 0U;
    if (status->state != VC_TOR) {
        tracker->in_tor = false;
        return 0U;
    }
    if (!tracker->in_tor) {
        tracker->in_tor = true;
        tracker->tor_since_ms = status->control_ms;
    }
    uint32_t elapsed = status->control_ms - tracker->tor_since_ms;
    return (elapsed >= VC_TOR_TIMEOUT_MS) ? 0U : (VC_TOR_TIMEOUT_MS - elapsed);
}

void web_adapter_status_to_response(const vc_status_t *status, control_response_t *response)
{
    if (!status || !response) {
        return;
    }
    *response = (control_response_t){0};
    response->mode = response_mode(status);
    response->front_distance_mm = status->tof_valid ? (int32_t)status->tof_mm : -1;
    response->tor_active = status->state == VC_TOR;
    response->tor_remaining_ms = 0U; /* set by the gateway: web_adapter_tor_remaining_ms() */
    response->stop_reason = response_stop_reason(status);
    response->request_reject_reason = mode_start_reject_reason(status);
    response->armed = status->armed != 0U;
    response->web_seq = status->web_seq;
}
