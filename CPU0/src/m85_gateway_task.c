#include "m85_gateway_runtime.h"
#include "interface/controller_if.h"
#include "protocol/control_protocol.h"
#include "web_control_adapter.h"
#include "autonomy_controller.h"
#include <tk/tkernel.h>

/* The M85 gateway task: the bridge between the HTTP mailbox (controller_if)
 * and the M33 IPC (ipc_gateway).  Translation lives in web_control_adapter.c;
 * this file only schedules it.
 *
 *   controller_if (urgent slot)  --+
 *   controller_if (latest value) --+--> m85_gateway_runtime_submit_web() --> IPC --> M33
 *   controller_if response <-- web_adapter_status_to_response() <-- M33 status
 */

static ID g_gateway_task;

/* Capture urgent HTTP commands before the latest-value request.  The
 * controller_if urgent slot is acknowledged only after this gateway has
 * retained it; ipc_gateway_step() then retries IPC_BUSY without dropping
 * STOP/ESTOP. */
static void forward_urgent_request(void)
{
    control_request_t request;
    uint32_t received, revision;
    if (control_if_get_urgent_snapshot(&request, &received, &revision) &&
        m85_gateway_runtime_submit_web(&request))
    {
        (void) control_if_ack_urgent(revision);
    }
}

/* Forward the latest normal request once per revision.  Urgent requests are
 * skipped here: they were already taken from the urgent slot above. */
static void forward_latest_request(uint32_t * forwarded_revision)
{
    control_request_t request;
    uint32_t received, revision;
    if (control_if_get_request_snapshot(&request, &received, &revision) &&
        revision != *forwarded_revision &&
        !control_if_request_is_urgent(&request) &&
        m85_gateway_runtime_submit_web(&request))
    {
        *forwarded_revision = revision;
    }
}

static web_tor_tracker_t g_tor_tracker;

static void publish_status(void)
{
    vc_status_t status;
    control_response_t response;
    if (m85_gateway_runtime_get_status(&status))
    {
        web_adapter_status_to_response(&status, &response);
        response.tor_remaining_ms = web_adapter_tor_remaining_ms(&status, &g_tor_tracker);
        web_adapter_obstacle_alarm(autonomy_controller_obstacle_alarm(), &response);
        (void) control_if_set_response(&response);
    }
}

static void gateway_task(INT stacd, void * exinf)
{
    (void) stacd;
    (void) exinf;
    while (!m85_gateway_runtime_init())
    {
        tk_dly_tsk(1);
    }

    /* HTTP may have accepted normal commands while M33 was still starting.
     * Fence the latest-value mailbox at startup so an old DRIVE/MODE is not
     * replayed with a fresh IPC timestamp. Urgent STOP/ESTOP uses its separate
     * retained slot and is still forwarded below. */
    control_request_t startup_request;
    uint32_t startup_received;
    uint32_t forwarded_revision = 0U;
    (void) control_if_get_request_snapshot(&startup_request, &startup_received,
                                           &forwarded_revision);
    for (;;)
    {
        forward_urgent_request();
        forward_latest_request(&forwarded_revision);

        uint32_t now = autonomy_controller_now_ms();
        if (now != UINT32_MAX)
        {
            (void) m85_gateway_runtime_step(now);
            publish_status();
        }
        /* The M33 status cadence is 50 ms.  A 5 ms gateway period keeps
         * retained STOP/ESTOP retries responsive while avoiding a 1 kHz
         * high-priority poll of unchanged mailboxes and status. */
        tk_dly_tsk(5);
    }
}

bool m85_gateway_task_start(void)
{
    if (g_gateway_task > 0)
    {
        return true;
    }
    T_CTSK config = {0};
    config.tskatr = TA_HLNG | TA_RNG3;
    config.task = gateway_task;
    config.itskpri = 8; /* control/IPC above low-priority video draining */
    config.stksz = 4096;
    g_gateway_task = tk_cre_tsk(&config);
    if (g_gateway_task <= 0)
    {
        return false;
    }
    if (tk_sta_tsk(g_gateway_task, 0) < E_OK)
    {
        tk_del_tsk(g_gateway_task);
        g_gateway_task = 0;
        return false;
    }
    return true;
}
