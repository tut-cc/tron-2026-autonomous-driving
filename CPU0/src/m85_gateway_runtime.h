#ifndef M85_GATEWAY_RUNTIME_H
#define M85_GATEWAY_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include "ipc_gateway.h"
#include "control_protocol.h"

/* CPU0 owns the only ipc_gateway_step() caller.  Camera/AI and HTTP tasks
 * publish into its protected mailboxes through these non-blocking adapters. */
bool m85_gateway_runtime_init(void);
bool m85_gateway_runtime_submit_ai(ai_perception_result_t const * result);
/* Translate a decoded Web request (web_control_adapter.c) and queue it.
 * Returns false for an out-of-range request or before init. */
bool m85_gateway_runtime_submit_web(control_request_t const * request);
int m85_gateway_runtime_step(uint32_t now_ms);
bool m85_gateway_runtime_get_status(vc_status_t * status);
bool m85_gateway_task_start(void);

#endif
