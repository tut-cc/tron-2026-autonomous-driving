#ifndef CONTROLLER_IF_H
#define CONTROLLER_IF_H
#include "../protocol/control_protocol.h"
/* HTTP task <-> gateway task mailbox (M85, same core).
 *   HTTP (tcpip_thread) : control_if_set_request(), control_if_get_response()
 *   gateway task        : control_if_get_*_snapshot(), control_if_ack_urgent(),
 *                         control_if_set_response()
 * Initialize once before starting HTTP/Controller tasks. All access is task
 * context only. Setters report failure so a failed lock cannot look accepted. */
bool control_if_init(void);
/* Requests that map to M33 STOP/ESTOP are retained outside the overwriteable
 * latest-value mailbox until the gateway has accepted them. */
bool control_if_request_is_urgent(const control_request_t *request);
bool control_if_set_request(const control_request_t *request, uint32_t receive_time_ms);
/* Atomic latest-value snapshot with a generic update counter. This detects
 * multiple accepted requests in the same millisecond; no wire fields change. */
bool control_if_get_request_snapshot(control_request_t *request, uint32_t *receive_time_ms,
                                     uint32_t *revision);
/* STOP/ESTOP bypass the normal latest-value request.  The urgent slot is
 * retained until the gateway explicitly acknowledges local capture; an HTTP
 * 2xx therefore cannot erase a safety command before IPC submission. */
bool control_if_get_urgent_snapshot(control_request_t *request, uint32_t *receive_time_ms,
                                    uint32_t *revision);
bool control_if_ack_urgent(uint32_t revision);
bool control_if_set_response(const control_response_t *response);
bool control_if_get_response(control_response_t *response);
#endif
