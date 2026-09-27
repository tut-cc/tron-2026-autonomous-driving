#include "controller_if.h"
#include <tk/tkernel.h>
static ID mailbox_mutex;
static control_request_t latest_request;
static control_response_t latest_response;
static control_request_t urgent_request;
static uint32_t latest_receive_time, request_revision;
static uint32_t urgent_receive_time, urgent_revision;
static bool request_valid, urgent_valid;
/* Urgent = an explicit STOP/ESTOP/reset button press.  client_mode only
 * mirrors the state the M33 reported, so an ABORT client_mode alone is not a
 * new stop request (it would otherwise resend STOP on every 100ms poll). */
bool control_if_request_is_urgent(const control_request_t *request)
{
    return request && (request->estop_request || request->manual_abort_request ||
                       request->reset_abort_request);
}

bool control_if_init(void)
{
    T_CMTX cfg = {0};
    if (mailbox_mutex > 0)
        return true;
    cfg.mtxatr = TA_INHERIT;
    mailbox_mutex = tk_cre_mtx(&cfg);
    if (mailbox_mutex <= 0)
        return false;
    /* Until the first M33 status arrives the distance is unknown (-1), not a
     * plausible reading: the UI shows "--" for negative values. */
    latest_response = (control_response_t){
        .mode = VEHICLE_MANUAL,
        .front_distance_mm = -1,
        .stop_reason = STOP_REASON_NONE,
        .request_reject_reason = REQUEST_REJECT_NONE,
        .armed = false,
        .web_seq = 0U
    };
    request_valid = false;
    urgent_valid = false;
    urgent_revision = 0;
    return true;
}
static bool lock(void) { return mailbox_mutex > 0 && tk_loc_mtx(mailbox_mutex, TMO_FEVR) == E_OK; }
bool control_if_set_request(const control_request_t *request, uint32_t receive_time_ms)
{
    if (!request || !lock())
        return false;
    latest_request = *request;
    latest_receive_time = receive_time_ms;
    ++request_revision;
    request_valid = true;
    if (control_if_request_is_urgent(request))
    {
        /* ESTOP outranks STOP and cannot be replaced by a later normal or
         * STOP request.  A new ESTOP refreshes the retained urgent value. */
        if (!urgent_valid || request->estop_request || !urgent_request.estop_request)
        {
            urgent_request = *request;
            urgent_receive_time = receive_time_ms;
            ++urgent_revision;
            urgent_valid = true;
        }
    }
    tk_unl_mtx(mailbox_mutex);
    return true;
}
bool control_if_get_urgent_snapshot(control_request_t *request, uint32_t *receive_time_ms,
                                    uint32_t *revision)
{
    if (!request || !receive_time_ms || !revision || !lock())
        return false;
    bool valid = urgent_valid;
    if (valid)
    {
        *request = urgent_request;
        *receive_time_ms = urgent_receive_time;
        *revision = urgent_revision;
    }
    tk_unl_mtx(mailbox_mutex);
    return valid;
}
bool control_if_ack_urgent(uint32_t revision)
{
    if (!lock())
        return false;
    bool acknowledged = urgent_valid && urgent_revision == revision;
    if (acknowledged)
        urgent_valid = false;
    tk_unl_mtx(mailbox_mutex);
    return acknowledged;
}
bool control_if_get_request_snapshot(control_request_t *request, uint32_t *receive_time_ms,
                                     uint32_t *revision)
{
    if (!request || !receive_time_ms || !revision || !lock())
        return false;
    bool valid = request_valid;
    if (valid)
    {
        *request = latest_request;
        *receive_time_ms = latest_receive_time;
        *revision = request_revision;
    }
    tk_unl_mtx(mailbox_mutex);
    return valid;
}
bool control_if_set_response(const control_response_t *response)
{
    if (!response || !lock())
        return false;
    latest_response = *response;
    tk_unl_mtx(mailbox_mutex);
    return true;
}
bool control_if_get_response(control_response_t *response)
{
    if (!response || !lock())
        return false;
    *response = latest_response;
    tk_unl_mtx(mailbox_mutex);
    return true;
}
