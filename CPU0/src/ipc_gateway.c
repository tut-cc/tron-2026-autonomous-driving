#include "ipc_gateway.h"
#include <string.h>
#if defined(M85_UKERNEL)
#include <tk/tkernel.h>
#elif !defined(IPC_GATEWAY_HOST_TEST)
#error "Production M85 gateway requires the µT-Kernel inherited-priority mutex"
#endif

static int lock(ipc_gateway_t *gateway)
{
#if defined(M85_UKERNEL)
    return tk_loc_mtx((ID)gateway->mutex_id, TMO_FEVR) == E_OK ? 0 : -1;
#else
    (void)gateway;
    return 0; /* Host contract tests are single-threaded. */
#endif
}

static void unlock(ipc_gateway_t *gateway)
{
#if defined(M85_UKERNEL)
    (void)tk_unl_mtx((ID)gateway->mutex_id);
#else
    (void)gateway;
#endif
}

int ipc_gateway_init(ipc_gateway_t *gateway, ipc_endpoint_t *endpoint)
{
    if (!gateway || !endpoint) {
        return -1;
    }
    memset(gateway, 0, sizeof(*gateway));
#if defined(M85_UKERNEL)
    T_CMTX mutex = {0};
    mutex.mtxatr = TA_INHERIT;
    ID mutex_id = tk_cre_mtx(&mutex);
    if (mutex_id <= 0) {
        return -1;
    }
    gateway->mutex_id = (uint32_t)mutex_id;
#endif
    if (producer_attach(&gateway->producer, endpoint)) {
#if defined(M85_UKERNEL)
        (void)tk_del_mtx((ID)gateway->mutex_id);   /* init may be retried */
        gateway->mutex_id = 0U;
#endif
        return -1;
    }
    gateway->attached = true;
    return 0;
}

int ipc_gateway_submit_ai(ipc_gateway_t *gateway,
                          const ai_perception_result_t *ai)
{
    if (!gateway || !gateway->attached || !ai) {
        return -1;
    }
    if (lock(gateway)) return -1;
    gateway->latest_ai = *ai;
    gateway->have_ai = true;
    ++gateway->ai_generation;
    unlock(gateway);
    return 0;
}

int ipc_gateway_submit_web(ipc_gateway_t *gateway, const vc_web_t *command)
{
    if (!gateway || !gateway->attached || !command) {
        return -1;
    }
    if (lock(gateway)) return -1;
    if (vc_web_action_is_stop(command->action)) {
        /* Urgent commands are not overwritten by a later DRIVE.  ESTOP has
         * higher rank than STOP, and a failed IPC write leaves this slot set. */
        if (!gateway->have_urgent || command->action == VC_WEB_ESTOP ||
            gateway->urgent_web.action != VC_WEB_ESTOP) {
            gateway->urgent_web = *command;
        }
        gateway->have_urgent = true;
        ++gateway->urgent_generation;
        /* A command queued before or during STOP must not be replayed after
         * the stop slot has been accepted.  Require a fresh operator input
         * only after M33 status confirms the output is disarmed. */
        gateway->have_web = false;
        ++gateway->web_generation;
        gateway->stop_barrier = true;
    } else {
        gateway->latest_web = *command;
        gateway->have_web = true;
        ++gateway->web_generation;
    }
    unlock(gateway);
    return 0;
}

/* Result of one IPC write: sent, busy (slot still pending -> retry next
 * tick, never a lost command) or failed. */
typedef enum { SEND_OK, SEND_BUSY, SEND_FAILED } send_result_t;

static send_result_t classify(int rc)
{
    return rc == IPC_OK ? SEND_OK : rc == IPC_BUSY ? SEND_BUSY : SEND_FAILED;
}

/* Clear a mailbox flag only if no newer value arrived while we were sending. */
static void consume(ipc_gateway_t *gateway, bool *flag,
                    const uint32_t *generation_now, uint32_t generation_sent)
{
    if (lock(gateway) == 0) {
        if (*generation_now == generation_sent) *flag = false;
        unlock(gateway);
    }
}

static void stop_sent(ipc_gateway_t *gateway, uint32_t generation,
                      uint32_t seq, uint32_t now_ms)
{
    if (lock(gateway) == 0) {
        if (gateway->urgent_generation == generation) {
            gateway->have_urgent = false;
            gateway->stop_sent_generation = generation;
            gateway->stop_sent_seq = seq;
            gateway->stop_sent_ms = now_ms;
        }
        unlock(gateway);
    }
}

static void release_stop_barrier_if_safe(ipc_gateway_t *gateway,
                                         const vc_status_t *status)
{
    if (!status || status->web_seq != gateway->stop_sent_seq ||
        status->armed || status->left_permille != 0 ||
        status->right_permille != 0 ||
        (uint32_t)(status->control_ms - gateway->stop_sent_ms) == 0U ||
        (uint32_t)(status->control_ms - gateway->stop_sent_ms) >= 0x80000000U) {
        return;
    }
    if (lock(gateway) == 0) {
        if (gateway->stop_barrier && !gateway->have_urgent &&
            gateway->stop_sent_generation == gateway->urgent_generation) {
            gateway->stop_barrier = false;
            gateway->have_web = false;
            ++gateway->web_generation;
        }
        unlock(gateway);
    }
}

#define HEARTBEAT_PERIOD_MS 50U

int ipc_gateway_step(ipc_gateway_t *gateway, uint32_t now_ms)
{
    ai_perception_result_t ai;
    vc_web_t web;
    vc_web_t urgent;
    bool have_ai, have_web, have_urgent, stop_barrier;
    uint32_t ai_generation, web_generation, urgent_generation;
    vc_status_t status;
    int result = 0;

    if (!gateway || !gateway->attached) {
        return -1;
    }
    gateway->have_step_status = false;
    /* Snapshot the mailboxes; IPC writes happen without holding the mutex. */
    if (lock(gateway)) return -1;
    ai = gateway->latest_ai;
    web = gateway->latest_web;
    urgent = gateway->urgent_web;
    have_ai = gateway->have_ai;
    have_web = gateway->have_web;
    have_urgent = gateway->have_urgent;
    stop_barrier = gateway->stop_barrier;
    ai_generation = gateway->ai_generation;
    web_generation = gateway->web_generation;
    urgent_generation = gateway->urgent_generation;
    unlock(gateway);

    /* 1. STOP/ESTOP first.  The same tick never forwards a normal Web
     *    command.  AI may resume on the next tick while Web stays inhibited
     *    until the M33 reports disarmed status. */
    bool urgent_pending = have_urgent || stop_barrier;
    if (have_urgent) {
        urgent.seq = ++gateway->tx_seq;
        urgent.timestamp_ms = now_ms;
        switch (classify(producer_send_web(&gateway->producer, &urgent))) {
        case SEND_OK:     stop_sent(gateway, urgent_generation, urgent.seq, now_ms); break;
        case SEND_BUSY:   urgent_pending = true; break;
        case SEND_FAILED: urgent_pending = true; result = -1; break;
        }
    }

    /* 2. Latest AI perception, then 3. latest Web DRIVE/MODE. */
    if (!have_urgent && have_ai) {
        if (ai.seq == 0U) ai.seq = ++gateway->tx_seq;
        switch (classify(producer_send_ai(&gateway->producer, &ai))) {
        case SEND_OK:     consume(gateway, &gateway->have_ai, &gateway->ai_generation, ai_generation); break;
        case SEND_BUSY:   break;
        case SEND_FAILED: result = -1; break;
        }
    }
    if (!urgent_pending && have_web && result == 0) {
        web.seq = ++gateway->tx_seq;
        web.timestamp_ms = now_ms;
        switch (classify(producer_send_web(&gateway->producer, &web))) {
        case SEND_OK:     consume(gateway, &gateway->have_web, &gateway->web_generation, web_generation); break;
        case SEND_BUSY:   break;
        case SEND_FAILED: result = -1; break;
        }
    }

    /* 4. Heartbeat, independent of the above.  A full slot only asks for a
     *    retry on the next tick; it must not turn into a false safety fault. */
    if ((uint32_t)(now_ms - gateway->last_heartbeat_ms) >= HEARTBEAT_PERIOD_MS) {
        switch (classify(producer_heartbeat(&gateway->producer))) {
        case SEND_OK:     gateway->last_heartbeat_ms = now_ms; break;
        case SEND_BUSY:   break;
        case SEND_FAILED: result = -1; break;
        }
    }
    /* Read M33 status exactly once.  The runtime still receives this same
     * fresh packet via ipc_gateway_get_status(); the stop barrier must not
     * consume telemetry behind its back. */
    if (producer_get_status(&gateway->producer, &status) == 0) {
        gateway->step_status = status;
        gateway->have_step_status = true;
        if (stop_barrier && !have_urgent) {
            release_stop_barrier_if_safe(gateway, &status);
        }
    }
    return result;
}

int ipc_gateway_get_status(ipc_gateway_t *gateway, vc_status_t *status)
{
    if (!gateway || !gateway->attached || !status) {
        return -1;
    }
    if (!gateway->have_step_status) return IPC_EMPTY;
    *status = gateway->step_status;
    gateway->have_step_status = false;
    return IPC_OK;
}
