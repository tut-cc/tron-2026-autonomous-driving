#include "m85_gateway_runtime.h"
#include "web_control_adapter.h"
#include "dualcore_board.h"
#include "hal_data.h"
#include <tk/tkernel.h>

static ipc_endpoint_t g_endpoint;
static ipc_gateway_t g_gateway;
static vc_status_t g_status;
static bool g_initialized;
static bool g_bound;
static bool g_status_valid;
static ID g_status_mutex;

bool m85_gateway_runtime_init(void)
{
    if (g_initialized)
    {
        return true;
    }
    if (!g_bound && dc_bind(&g_endpoint))
    {
        return false;
    }
    g_bound = true;
    /* M33 initializes the shared ABI before producers are admitted. */
    for (uint32_t retry = 0U; retry < 10000U; ++retry)
    {
        if (DC_SHARED->ready == DC_READY &&
            ipc_gateway_init(&g_gateway, &g_endpoint) == 0)
        {
            T_CMTX config = {0};
            config.mtxatr = TA_INHERIT;
            g_status_mutex = tk_cre_mtx(&config);
            if (g_status_mutex <= 0)
            {
                return false;
            }
            g_initialized = true;
            return true;
        }
        /* This runs in a task; yield so lwIP and HTTP remain schedulable while
         * the other core is still initializing the shared IPC block. */
        (void) tk_dly_tsk(1U);
    }
    return false;
}

bool m85_gateway_runtime_submit_ai(ai_perception_result_t const * result)
{
    return g_initialized && ipc_gateway_submit_ai(&g_gateway, result) == 0;
}

bool m85_gateway_runtime_submit_web(control_request_t const * request)
{
    vc_web_t command;
    return g_initialized && web_adapter_request_to_command(request, &command) == 0 &&
           ipc_gateway_submit_web(&g_gateway, &command) == 0;
}

int m85_gateway_runtime_step(uint32_t now_ms)
{
    if (!g_initialized)
    {
        return -1;
    }
    int result = ipc_gateway_step(&g_gateway, now_ms);
    vc_status_t status;
    if (ipc_gateway_get_status(&g_gateway, &status) == 0)
    {
        (void) tk_loc_mtx(g_status_mutex, TMO_FEVR);
        g_status = status;
        g_status_valid = true;
        (void) tk_unl_mtx(g_status_mutex);
    }
    return result;
}

bool m85_gateway_runtime_get_status(vc_status_t * status)
{
    if (!status || g_status_mutex <= 0)
    {
        return false;
    }
    if (tk_loc_mtx(g_status_mutex, TMO_FEVR) != E_OK)
    {
        return false;
    }
    if (!g_status_valid)
    {
        (void) tk_unl_mtx(g_status_mutex);
        return false;
    }
    *status = g_status;
    (void) tk_unl_mtx(g_status_mutex);
    return true;
}
