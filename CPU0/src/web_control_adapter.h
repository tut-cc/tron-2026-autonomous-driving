#ifndef M85_WEB_CONTROL_ADAPTER_H
#define M85_WEB_CONTROL_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>
#include "control_protocol.h"   /* Web vocabulary: control_request_t / control_response_t */
#include "vehicle_control.h"    /* M33 vocabulary: vc_web_t / vc_status_t */

/* ---- Web <-> M33 translation (the only place the two vocabularies meet) ----
 *
 *   browser --JSON--> control_request_t --web_adapter_request_to_command()--> vc_web_t --IPC--> M33
 *   browser <--JSON-- control_response_t <--web_adapter_status_to_response()-- vc_status_t <--IPC-- M33
 *
 * Pure functions: no RTOS, GPIO or FSP call is reachable from this module, so
 * both directions are covered by host tests (control/tests/test_web_adapter.c).
 */

/* Request -> fixed-width M33 command.  Returns 0, or -1 when a field is out
 * of range (throttle/steering outside -1..+1 or non-finite, unknown enum).
 * timestamp_ms/seq are left 0; the IPC gateway stamps them when sending. */
int web_adapter_request_to_command(const control_request_t *request, vc_web_t *command);

/* M33 status -> Web response.  Never fails for a valid pointer. */
void web_adapter_status_to_response(const vc_status_t *status, control_response_t *response);

/* TOR countdown for the UI.  The M33 status packet carries only the state, so
 * the M85 remembers the M33 clock (status.control_ms) at which it first saw
 * VC_TOR and reports VC_TOR_TIMEOUT_MS minus the elapsed M33 time (clamped at
 * 0).  Returns 0 outside TOR and resets the tracker. */
typedef struct { bool in_tor; uint32_t tor_since_ms; } web_tor_tracker_t;
uint32_t web_adapter_tor_remaining_ms(const vc_status_t *status, web_tor_tracker_t *tracker);

#endif
