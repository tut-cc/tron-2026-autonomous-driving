#include <tk/tkernel.h>
#include "control_runtime.h"
#include <string.h>

/* M33 control runtime: seven uT-Kernel tasks around one vc_t.
 *
 *   pri 1 emergency_task  STOP/ESTOP flag, internal faults, ToF/link watchdog
 *   pri 2 motor_task      final output: newest fresh motion of this generation
 *   pri 3 distance_task   ToF polling
 *   pri 4 decision_task   AI/Web queues -> vc_step() -> motion queue
 *   pri 5 web_task        IPC Web/STOP slots -> control_submit_web()
 *   pri 6 vision_task     IPC AI slot and M85 heartbeat
 *   pri 7 status_task     vc_status_t -> IPC status slot (every 50ms)
 *
 * Error handling rule: every kernel/driver failure stops the output at once.
 * A failure that makes the runtime itself untrustworthy is a fault
 * (fail_safe()): it latches `fatal`, records the first cause in
 * g_control_fault and ends in EMERGENCY/VC_INTERNAL until a board reset. */

typedef struct { control_motor_output_t output; uint32_t at, generation; } motion_t;

static control_runtime_config_t cfg;
static vc_t vehicle;                     /* protected by mutex_id */
static ID mutex_id, stop_flag, ai_queue, web_queue, motion_queue;
static uint32_t generation;              /* bumped by every stop and every arm */
static uint32_t started;
/* Protected by mutex_id.  The emergency task publishes this STOP sequence
 * only after it has disarmed the output, so M85 can distinguish an applied
 * STOP from an old, already-disarmed status packet. */
static uint32_t pending_web_stop_seq, pending_web_stop_valid;
static volatile uint32_t fatal;

/* Diagnostics for the debugger. */
volatile uint32_t g_control_fault;       /* control_fault_t, first cause only  */
volatile uint32_t g_start_count;         /* successful vc_start() calls        */
volatile uint32_t g_hb_max_gap_ms, g_hb_gap_over300; /* M85 heartbeat gaps > VC_LINK_FRESH_MS */

#define STOP_NORMAL        1U
#define STOP_EMERGENCY     2U
#define LOCK_TIMEOUT_MS    10
#define TASK_PERIOD_MS     10U
#define MOTION_MAX_AGE_MS  50U  /* older DecisionTask output is a fault */
#define QUEUE_DRAIN_MAX    4    /* messages taken per queue per period  */
#define STATUS_PERIOD_MS   50U  /* 20 Hz UI telemetry; not a safety input */

/* ---- error handling ------------------------------------------------------ */
static void note_fault(control_fault_t cause)
{
    if (g_control_fault == CONTROL_FAULT_NONE) g_control_fault = (uint32_t)cause;
}
static void fail_safe(control_fault_t cause)
{
    note_fault(cause);
    fatal = 1;
    control_hw_stop();
}
static int hw_healthy(void) { return !fatal && !control_hw_fault(); }

uint32_t control_now_ms(void)
{
    SYSTIM t;
    if (tk_get_otm(&t) != E_OK) { fail_safe(CONTROL_FAULT_CLOCK); return 0; }
    return (uint32_t)t.lo;
}
/* Take the state mutex.  On timeout the output is stopped at once; the
 * caller decides whether this is a fault (tasks) or just a missed read. */
static int lock_state(void)
{
    if (tk_loc_mtx(mutex_id, LOCK_TIMEOUT_MS) == E_OK) return 1;
    control_hw_stop();
    return 0;
}
static void unlock_state(void)
{
    if (tk_unl_mtx(mutex_id) != E_OK) fail_safe(CONTROL_FAULT_UNLOCK);
}
static int pause_task(uint32_t ms)
{
    if (tk_dly_tsk(ms) == E_OK) return 0;
    fail_safe(CONTROL_FAULT_DELAY);
    return -1;
}
/* Mutex held: disarm through the state machine and stop the output. */
static void stop_locked(uint32_t reason, int emergency)
{
    vc_stop(&vehicle, reason, emergency);
    ++generation;
    control_hw_stop();
}

/* ---- API for the IPC side ------------------------------------------------ */
void control_request_stop(int emergency)
{
    if (!started) return;
    if (tk_set_flg(stop_flag, emergency ? STOP_EMERGENCY : STOP_NORMAL) != E_OK)
        fail_safe(CONTROL_FAULT_STOP_FLAG);
}
int control_submit_ai(const ai_perception_result_t *p)
{
    if (!started || !p) return -1;
    return tk_snd_mbf(ai_queue, p, sizeof(*p), TMO_POL) == E_OK ? 0 : -1;
}
int control_submit_web(const vc_web_t *p)
{
    if (!started || !p) return -1;
    if (vc_web_action_is_stop(p->action)) {
        /* STOP/ESTOP never queue behind DRIVE: they go through the flag. */
        if (!lock_state()) { fail_safe(CONTROL_FAULT_LOCK); return -1; }
        pending_web_stop_seq = p->seq;
        pending_web_stop_valid = 1U;
        if (tk_set_flg(stop_flag, p->action == VC_WEB_ESTOP ? STOP_EMERGENCY : STOP_NORMAL) != E_OK) {
            fail_safe(CONTROL_FAULT_STOP_FLAG);
            unlock_state();
            return -1;
        }
        unlock_state();
        return 0;
    }
    if (tk_snd_mbf(web_queue, p, sizeof(*p), TMO_POL) != E_OK) {
        control_request_stop(0);   /* queue full: never drive on a dropped command */
        return -1;
    }
    return 0;
}
int control_get_status(vc_status_t *s)
{
    if (!started || !s || !lock_state()) return -1;
    *s = vehicle.status;
    unlock_state();
    return 0;
}

/* ---- pri 1: emergency ---------------------------------------------------- */
static void emergency_task(INT n, void *x)
{
    (void)n; (void)x;
    for (;;) {
        UINT bits = 0;
        ER rc = tk_wai_flg(stop_flag, STOP_NORMAL | STOP_EMERGENCY, TWF_ORW | TWF_CLR, &bits, 10);
        uint32_t now = control_now_ms();
        if (!lock_state()) { fail_safe(CONTROL_FAULT_LOCK); break; }
        int flag_failed = rc != E_OK && rc != E_TMOUT;
        int hw_fault = control_hw_fault();
        if (flag_failed) note_fault(CONTROL_FAULT_STOP_FLAG);
        if (hw_fault) note_fault(CONTROL_FAULT_HW);
        if (fatal || hw_fault || flag_failed) {
            stop_locked(VC_INTERNAL, 1);
        } else if (bits) {
            stop_locked((bits & STOP_EMERGENCY) != 0 ? VC_ESTOP : VC_OPERATOR,
                        (bits & STOP_EMERGENCY) != 0);
            if (pending_web_stop_valid) {
                /* Rejects Web messages queued before the STOP.  Only a
                 * strictly newer command may re-arm MANUAL. */
                vc_fence_web(&vehicle, pending_web_stop_seq, now);
                pending_web_stop_valid = 0U;
            }
        } else if (vehicle.status.armed) {
            uint32_t unsafe = vc_unsafe_reason(&vehicle, now); /* ToF or link */
            if (unsafe != VC_OK) stop_locked(unsafe, unsafe != VC_TOF_PRESTOP);
        }
        unlock_state();
    }
    tk_ext_tsk();
}

/* ---- pri 2: final motor output ------------------------------------------- */
/* Drive the motors only with a fresh command from the current arm generation.
 * A new arm has no valid motion until DecisionTask publishes it; the HW
 * watchdog limits even that waiting period to 100ms.  Mutex held. */
static void apply_motion_locked(const motion_t *m, uint32_t now)
{
    uint32_t unsafe;
    if (!vehicle.status.armed) { control_hw_stop(); return; }
    if (m->generation != generation) return;            /* waiting for first motion */
    if (now - m->at > MOTION_MAX_AGE_MS) {
        note_fault(CONTROL_FAULT_MOTION_STALE);
        stop_locked(VC_INTERNAL, 1);
        return;
    }
    unsafe = vc_unsafe_reason(&vehicle, now);
    if (unsafe != VC_OK) { stop_locked(unsafe, unsafe != VC_TOF_PRESTOP); return; }
    if (control_hw_apply(&m->output, now)) {
        note_fault(CONTROL_FAULT_HW);
        stop_locked(VC_INTERNAL, 1);
    }
}
static void motor_task(INT n, void *x)
{
    motion_t m = {0}, next;
    (void)n; (void)x;
    for (;;) {
        uint32_t now = control_now_ms();
        int count;
        for (count = 0; count < QUEUE_DRAIN_MAX; ++count) {   /* keep only the newest */
            INT rc = tk_rcv_mbf(motion_queue, &next, TMO_POL);
            if (rc == E_TMOUT) break;
            if (rc != (INT)sizeof(next)) { fail_safe(CONTROL_FAULT_QUEUE); break; }
            m = next;
        }
        if (!lock_state()) { fail_safe(CONTROL_FAULT_LOCK); break; }
        if (fatal) stop_locked(VC_INTERNAL, 1);
        apply_motion_locked(&m, now);
        unlock_state();
        if (pause_task(TASK_PERIOD_MS)) break;
    }
    control_hw_stop();
    tk_ext_tsk();
}

/* ---- pri 3: distance ----------------------------------------------------- */
static void distance_task(INT n, void *x)
{
    (void)n; (void)x;
    for (;;) {
        tof_safety_result_t t = {0};
        uint32_t now = control_now_ms();
        int rc = cfg.tof_poll(&t, now);          /* 0 new, 1 not ready, <0 error */
        if (!lock_state()) { fail_safe(CONTROL_FAULT_LOCK); break; }
        if (rc < 0) {
            (void)vc_tof(&vehicle, 0, now);      /* invalidates the reading, EMERGENCY/VC_TOF */
            stop_locked(VC_TOF, 1);              /* also bumps generation and stops the output */
        } else if (rc == 0) {
            uint32_t was_armed = vehicle.status.armed;
            (void)vc_tof(&vehicle, &t, now);
            /* The 100 mm pre-stop is an ordinary STOP, but like an emergency it
             * must inhibit the physical output at once and invalidate queued
             * motion (new generation). */
            if ((was_armed && !vehicle.status.armed) || vehicle.status.state == VC_EMERGENCY) {
                ++generation;
                control_hw_stop();
            }
        }
        unlock_state();
        if (pause_task(TASK_PERIOD_MS)) break;
    }
    tk_ext_tsk();
}

/* ---- pri 4: decision ----------------------------------------------------- */
/* vc_web() may start driving (D-pad / AUTO button).  Each new start gets a
 * new generation and re-arms the motor driver.  Mutex held. */
static void apply_start_locked(uint32_t *seen)
{
    if (vehicle.start_count == *seen) return;
    *seen = vehicle.start_count;
    g_start_count = *seen;
    if (!vehicle.status.armed) return;
    if (!hw_healthy()) { stop_locked(VC_INTERNAL, 1); return; }
    ++generation;
    if (control_hw_arm()) { note_fault(CONTROL_FAULT_HW); stop_locked(VC_INTERNAL, 1); }
}
static void drain_ai_locked(uint32_t now)
{
    ai_perception_result_t a;
    int i;
    for (i = 0; i < QUEUE_DRAIN_MAX; ++i) {
        INT rc = tk_rcv_mbf(ai_queue, &a, TMO_POL);
        if (rc == E_TMOUT) break;
        if (rc != (INT)sizeof(a)) { fail_safe(CONTROL_FAULT_QUEUE); break; }
        (void)vc_ai(&vehicle, &a, now);          /* a rejected frame is handled inside vc_ai() */
    }
}
static void drain_web_locked(uint32_t now)
{
    vc_web_t w;
    int i;
    for (i = 0; i < QUEUE_DRAIN_MAX; ++i) {
        INT rc = tk_rcv_mbf(web_queue, &w, TMO_POL);
        if (rc == E_TMOUT) break;
        if (rc != (INT)sizeof(w)) { fail_safe(CONTROL_FAULT_QUEUE); break; }
        (void)vc_web(&vehicle, &w, now);         /* a rejected command is handled inside vc_web() */
    }
}
static void decision_task(INT n, void *x)
{
    uint32_t starts_seen = 0;
    (void)n; (void)x;
    for (;;) {
        motion_t m;
        uint32_t now = control_now_ms();
        if (!lock_state()) { fail_safe(CONTROL_FAULT_LOCK); break; }
        drain_ai_locked(now);
        drain_web_locked(now);
        apply_start_locked(&starts_seen);
        vc_step(&vehicle, now, &m.output);
        m.at = now;
        m.generation = generation;
        if (!vehicle.status.armed) control_hw_stop();
        unlock_state();
        if (tk_snd_mbf(motion_queue, &m, sizeof(m), TMO_POL) != E_OK) control_request_stop(1);
        if (pause_task(TASK_PERIOD_MS)) break;
    }
    tk_ext_tsk();
}

/* ---- pri 5: Web IPC ------------------------------------------------------ */
static void web_task(INT n, void *x)
{
    (void)n; (void)x;
    for (;;) {
        ipc_packet_t p;
        vc_web_t w;
        /* A corrupt packet in the STOP slot is treated as ESTOP. */
        if (ipc_read(cfg.ipc, IPC_STOP, &p) == IPC_OK) {
            if (ipc_unpack_web(&p, &w) == IPC_OK && vc_web_action_is_stop(w.action))
                (void)control_submit_web(&w);
            else
                control_request_stop(1);
        }
        if (ipc_read(cfg.ipc, IPC_WEB, &p) == IPC_OK) {
            if (ipc_unpack_web(&p, &w) != IPC_OK) control_request_stop(0);
            else (void)control_submit_web(&w);
        }
        /* Lock contention is bounded; stale input is checked independently. */
        if (pause_task(TASK_PERIOD_MS)) break;
    }
    tk_ext_tsk();
}

/* ---- pri 6: AI IPC and M85 heartbeat ------------------------------------- */
static int seq_newer(uint32_t a, uint32_t b)
{
    uint32_t d = a - b;
    return d != 0U && d < 0x80000000U;
}
static void vision_task(INT n, void *x)
{
    uint32_t hb_seq = 0, seen = 0;
    (void)n; (void)x;
    for (;;) {
        ipc_packet_t p;
        ai_perception_result_t a;
        uint32_t now = control_now_ms();
        if (ipc_read(cfg.ipc, IPC_AI, &p) == IPC_OK) {
            if (ipc_unpack_ai(&p, &a) != IPC_OK) control_request_stop(0);
            else (void)control_submit_ai(&a);
        }
        if (ipc_read(cfg.ipc, IPC_HEARTBEAT, &p) == IPC_OK && p.words == IPC_HEARTBEAT_WORDS &&
            now - p.stamp <= VC_LINK_FRESH_MS && (!seen || seq_newer(p.seq, hb_seq))) {
            seen = 1;
            hb_seq = p.seq;
            if (!lock_state()) { fail_safe(CONTROL_FAULT_LOCK); break; }
            if (vehicle.in.link_seen) {
                uint32_t gap = p.stamp - vehicle.in.link_ms;
                if (gap > g_hb_max_gap_ms) g_hb_max_gap_ms = gap;
                if (gap > VC_LINK_FRESH_MS) ++g_hb_gap_over300;
            }
            vc_link(&vehicle, p.stamp);
            unlock_state();
        }
        if (pause_task(TASK_PERIOD_MS)) break;
    }
    tk_ext_tsk();
}

/* ---- pri 7: status IPC --------------------------------------------------- */
static void status_task(INT n, void *x)
{
    uint32_t seq = 0;
    (void)n; (void)x;
    for (;;) {
        vc_status_t s;
        ipc_packet_t p;
        (void)ipc_set_clock(cfg.ipc, control_now_ms());
        if (control_get_status(&s) == 0) {
            ipc_pack_status(&p, cfg.boot_session, ++seq, &s);
            (void)ipc_write(cfg.ipc, &p);        /* drop telemetry when the UI stalls */
        }
        if (pause_task(STATUS_PERIOD_MS)) break;
    }
    tk_ext_tsk();
}

/* ---- start --------------------------------------------------------------- */
typedef struct { void (*entry)(INT, void *); PRI priority; } task_spec_t;
static const task_spec_t task_specs[] = {
    { emergency_task, 1 }, { motor_task,  2 }, { distance_task, 3 }, { decision_task, 4 },
    { web_task,       5 }, { vision_task, 6 }, { status_task,   7 },
};
#define TASK_COUNT (sizeof(task_specs) / sizeof(task_specs[0]))
static ID tasks[TASK_COUNT];

static ID create_queue(INT size)
{
    T_CMBF q = {0};
    q.mbfatr = TA_TFIFO;
    q.maxmsz = size;
    q.bufsz = 4 * (size + 16);
    return tk_cre_mbf(&q);
}
int control_runtime_start(const control_runtime_config_t *c)
{
    T_CMTX mx = {0};
    T_CFLG fl = {0};
    unsigned i;
    if (started || !c || !c->ipc || !c->tof_poll || !c->boot_session) return -1;
    cfg = *c;
    if (control_hw_init()) { note_fault(CONTROL_FAULT_INIT); return -1; }
    vc_init(&vehicle, control_now_ms());
    pending_web_stop_seq = 0U;
    pending_web_stop_valid = 0U;
    if (fatal || ipc_control_init(cfg.ipc, cfg.boot_session, control_now_ms()) != IPC_OK) {
        note_fault(CONTROL_FAULT_INIT);
        return -1;
    }
    mx.mtxatr = TA_INHERIT;
    mutex_id = tk_cre_mtx(&mx);
    fl.flgatr = TA_TFIFO;
    stop_flag = tk_cre_flg(&fl);
    ai_queue = create_queue(sizeof(ai_perception_result_t));
    web_queue = create_queue(sizeof(vc_web_t));
    motion_queue = create_queue(sizeof(motion_t));
    if (mutex_id <= 0 || stop_flag <= 0 || ai_queue <= 0 || web_queue <= 0 || motion_queue <= 0) goto fail;
    for (i = 0; i < TASK_COUNT; ++i) {
        T_CTSK t = {0};
        t.tskatr = TA_HLNG | TA_RNG0;
        t.task = task_specs[i].entry;
        t.itskpri = task_specs[i].priority;
        t.stksz = 4096;
        tasks[i] = tk_cre_tsk(&t);
        if (tasks[i] <= 0) goto fail;
    }
    started = 1;
    for (i = 0; i < TASK_COUNT; ++i)
        if (tk_sta_tsk(tasks[i], 0) != E_OK) goto fail;
    return 0;
fail:
    /* No restart after partial initialization; the caller keeps the output
     * inhibited and resets.  Already started tasks see `fatal` and cannot arm. */
    fail_safe(CONTROL_FAULT_INIT);
    return -1;
}
