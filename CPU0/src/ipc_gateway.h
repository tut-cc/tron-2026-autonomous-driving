#ifndef M85_IPC_GATEWAY_H
#define M85_IPC_GATEWAY_H

#include <stdbool.h>
#include <stdint.h>
#include "producer_api.h"

/* M85-side IPC scheduler.  Exactly one M85 task owns this object and calls
 * ipc_gateway_step().  HTTP, AI and camera tasks only publish latest-value
 * mailboxes.  In particular, they never call ipc_write() concurrently and
 * never access a motor pin.
 *
 * This module speaks only the M33 vocabulary (vc_web_t, vc_status_t,
 * ai_perception_result_t).  Translation from the Web API happens before
 * submit, in web_control_adapter.c. */
typedef struct {
    producer_t producer;
    uint32_t mutex_id;
    ai_perception_result_t latest_ai;
    vc_web_t latest_web;
    vc_web_t urgent_web;
    vc_status_t step_status;
    bool have_ai;
    bool have_web;
    bool have_urgent;
    bool stop_barrier;
    bool have_step_status;
    bool attached;
    uint32_t tx_seq;
    uint32_t ai_generation;
    uint32_t web_generation;
    uint32_t urgent_generation;
    uint32_t stop_sent_generation;
    uint32_t stop_sent_seq;
    uint32_t stop_sent_ms;
    uint32_t last_heartbeat_ms;
} ipc_gateway_t;

int ipc_gateway_init(ipc_gateway_t *gateway, ipc_endpoint_t *endpoint);
int ipc_gateway_submit_ai(ipc_gateway_t *gateway,
                          const ai_perception_result_t *ai);
/* STOP/ESTOP go to a retained urgent slot (ESTOP outranks STOP).  Pending
 * DRIVE/MODE is discarded, and later normal input is held until M33 reports
 * disarmed status after the stop; then a fresh input is required.  seq and
 * timestamp_ms are assigned by ipc_gateway_step(). */
int ipc_gateway_submit_web(ipc_gateway_t *gateway, const vc_web_t *command);
int ipc_gateway_step(ipc_gateway_t *gateway, uint32_t now_ms);
int ipc_gateway_get_status(ipc_gateway_t *gateway, vc_status_t *status);

#endif
