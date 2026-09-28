#include "vehicle_control.h"
#include <string.h>

/* ---- small helpers ------------------------------------------------------ */
static int fresh(uint32_t now, uint32_t stamp, uint32_t limit)
{ return (uint32_t)(now - stamp) <= limit; }
/* Sequence a is newer than b (wrap-safe). */
static int newer(uint32_t a, uint32_t b)
{ uint32_t d = a - b; return d != 0U && d < 0x80000000U; }
static int range(float x, float low, float high) { return x >= low && x <= high; }
static float limit_command(float x)
{ return x > VC_COMMAND_LIMIT ? VC_COMMAND_LIMIT : x < -VC_COMMAND_LIMIT ? -VC_COMMAND_LIMIT : x; }
static float slew(float prev, float next)
{
    float d = next - prev;
    return prev + (d > VC_SLEW_PER_STEP ? VC_SLEW_PER_STEP : d < -VC_SLEW_PER_STEP ? -VC_SLEW_PER_STEP : d);
}

/* ---- safety predicates (the only place these rules are written) --------- */
/* Latest ToF sample exists, is valid, is fresh and is beyond the ordinary
 * (pre-)stop distance. */
static int tof_ok(const vc_t *v, uint32_t now)
{
    return v->in.have_tof && v->in.tof.valid && v->in.tof.distance_mm > VC_TOF_PRESTOP_MM &&
           fresh(now, v->in.tof.sample_timestamp_ms, VC_TOF_FRESH_MS);
}
/* M85 heartbeat has been seen and is fresh. */
static int link_ok(const vc_t *v, uint32_t now)
{ return v->in.link_seen && fresh(now, v->in.link_ms, VC_LINK_FRESH_MS); }
/* MANUAL: a fresh Web command exists. */
static int web_fresh(const vc_t *v, uint32_t now)
{ return v->in.have_web && fresh(now, v->in.web.timestamp_ms, VC_WEB_TIMEOUT_MS); }

uint32_t vc_unsafe_reason(const vc_t *v, uint32_t now)
{
    if (!v->in.have_tof || !v->in.tof.valid ||
        !fresh(now, v->in.tof.sample_timestamp_ms, VC_TOF_FRESH_MS)) return VC_TOF;
    if (v->in.tof.distance_mm <= VC_TOF_STOP_MM) return VC_TOF_NEAR;
    if (v->in.tof.distance_mm <= VC_TOF_PRESTOP_MM) return VC_TOF_PRESTOP;
    if (!link_ok(v, now)) return VC_LINK;
    return VC_OK;
}
/* AUTO cannot continue: ask the driver to take over (TOR).  Output goes to 0
 * immediately; the vehicle never keeps driving on stale perception.  Only a
 * MANUAL request (take-over), STOP/ESTOP, a hazard, or the timeout leaves TOR,
 * and a recovered AI path does not resume AUTO by itself. */
static void enter_tor(vc_t *v, uint32_t now, uint32_t reason)
{
    v->status.armed = 0;
    v->left = v->right = 0;
    v->status.left_permille = v->status.right_permille = 0;
    v->status.state = VC_TOR;
    v->status.reason = reason;           /* mode stays AUTO while waiting */
    v->tor_ms = now;
}

/* vc_start() refusal -> status reason shown to the operator (VC_OK = none). */
static uint32_t auto_refusal_reason(uint32_t arm_result)
{
    switch (arm_result) {
    case VC_ARM_TOF_NOT_OK:     return VC_AUTO_REFUSED_TOF;
    case VC_ARM_LINK_STALE:     return VC_AUTO_REFUSED_LINK;
    case VC_ARM_PATH_NOT_READY: return VC_AUTO_REFUSED_PATH;
    default:                    return VC_OK;
    }
}

/* ---- lifecycle ---------------------------------------------------------- */
void vc_init(vc_t *v, uint32_t now)
{
    control_motor_config_t c;
    memset(v, 0, sizeof(*v));
    control_motor_default_config(&c);
    c.base_command = VC_AUTO_BASE_COMMAND;
    c.max_steering = VC_AUTO_MAX_STEERING;
    c.perception_timeout_ms = VC_AUTO_AI_TIMEOUT_MS;
    c.tof_timeout_ms = VC_TOF_FRESH_MS;
    c.tof_stop_mm = VC_TOF_STOP_MM;
    c.tof_release_mm = VC_TOF_CLEAR_MM;
    c.steering_trim_initial = VC_STEERING_TRIM_INITIAL;
    control_motor_init(&v->path, &c, now);
    v->status.reason = VC_WAITING;
    v->status.mode = VC_MODE_MANUAL;   /* RC build: always boot in MANUAL */
    v->last_step_ms = now;
}

/* Disarm, zero the output and fall back to MANUAL.  A sensor/link/AI
 * EMERGENCY clears itself in vc_step() once the cause is gone (never
 * re-arming by itself); an explicit ESTOP and VC_INTERNAL (driver/kernel
 * fault) stay latched until a board reset. */
void vc_stop(vc_t *v, uint32_t why, int emergency)
{
    /* An explicit ESTOP always upgrades an earlier sensor/AI emergency.
     * Lesser stop causes never downgrade an existing emergency. */
    if (v->status.state == VC_EMERGENCY && why != VC_ESTOP) return;
    v->status.armed = 0;
    v->status.mode = VC_MODE_MANUAL;
    v->left = v->right = 0;
    v->status.left_permille = v->status.right_permille = 0;
    v->status.state = emergency ? VC_EMERGENCY : VC_STOPPED;
    v->status.reason = why;
}

/* ---- inputs ------------------------------------------------------------- */
vc_input_result_t vc_ai(vc_t *v, const ai_perception_result_t *p, uint32_t now)
{
    unsigned i;
    vc_input_result_t why = VC_INPUT_INVALID;
    if (p && p->interface_version == AI_CONTROL_INTERFACE_VERSION &&
        !fresh(now, p->capture_timestamp_ms, VC_AI_FRESH_MS)) {
        why = VC_INPUT_STALE;
        goto bad;
    }
    if (!p || p->interface_version != AI_CONTROL_INTERFACE_VERSION ||
        p->path_valid > 1 || p->obstacle_valid > 1 || p->obstacle_count > AI_CONTROL_MAX_OBSTACLES ||
        !range(p->lateral_error, -1, 1) || !range(p->heading_error, -1, 1) ||
        !range(p->path_width, 0, 1) || !range(p->path_confidence, 0, 1)) goto bad;
    for (i = 0; i < p->obstacle_count; ++i) {
        const ai_obstacle_result_t *o = &p->obstacles[i];
        if (!range(o->confidence, 0, 1) || !range(o->corridor_overlap, 0, 1) ||
            !range(o->bbox_bottom, 0, 1) || !range(o->center_x, -1, 1)) goto bad;
    }
    if (v->in.have_ai && !newer(p->seq, v->in.ai.seq)) return VC_INPUT_NOT_NEWER;
    v->in.ai = *p; v->in.have_ai = 1; v->status.ai_seq = p->seq;
    return VC_INPUT_ACCEPTED;
bad:
    /* Preserve sequence history but never re-arm using the last good frame
     * after receiving malformed data. Require new healthy frames. */
    v->in.ai.path_valid = 0; v->in.ai.obstacle_valid = 0;
    v->path.good_frame_count = 0;
    if (v->status.mode == VC_MODE_AUTO) vc_stop(v, VC_AI, 0);
    return why;
}

/* While driving (or waiting in TOR) a missing, malformed or stale ToF
 * sample is an emergency: the front distance is unknown.  While stopped it is
 * only recorded: tof_ok() already refuses any start, and latching EMERGENCY
 * here made the Web UI flicker MANUAL <-> MANUAL_ABORT on every odd sample
 * (sigma fail, open space) with nothing moving (2026-09-28). */
static int tof_guarded(const vc_t *v)
{ return v->status.armed || v->status.state == VC_TOR; }
vc_input_result_t vc_tof(vc_t *v, const tof_safety_result_t *p, uint32_t now)
{
    if (!p || p->valid > 1 || !fresh(now, p->sample_timestamp_ms, VC_TOF_FRESH_MS)) {
        v->in.tof.valid = 0; v->status.tof_valid = 0;
        if (tof_guarded(v)) vc_stop(v, VC_TOF, 1);
        return (p && p->valid <= 1) ? VC_INPUT_STALE : VC_INPUT_INVALID;
    }
    if (v->in.have_tof && !newer(p->seq, v->in.tof.seq)) return VC_INPUT_NOT_NEWER;
    v->in.tof = *p; v->in.have_tof = 1;
    v->status.tof_mm = p->distance_mm; v->status.tof_valid = p->valid;
    if (!tof_guarded(v)) return VC_INPUT_ACCEPTED;
    if (!p->valid) vc_stop(v, VC_TOF, 1);
    else if (p->distance_mm <= VC_TOF_STOP_MM) vc_stop(v, VC_TOF_NEAR, 1);
    else if (p->distance_mm <= VC_TOF_PRESTOP_MM && v->status.armed)
        vc_stop(v, VC_TOF_PRESTOP, 0);   /* ordinary stop, no EMERGENCY */
    return VC_INPUT_ACCEPTED;
}

void vc_link(vc_t *v, uint32_t now) { v->in.link_seen = 1; v->in.link_ms = now; }

void vc_fence_web(vc_t *v, uint32_t stop_seq, uint32_t now)
{
    v->in.web.seq = stop_seq;
    v->in.web.deadman = 0U;
    v->in.web.linear = v->in.web.steering = 0;
    v->in.web.timestamp_ms = now;
    v->in.have_web = 1U;
    v->status.web_seq = stop_seq;
}

vc_input_result_t vc_web(vc_t *v, const vc_web_t *w, uint32_t now)
{
    vc_input_result_t why = VC_INPUT_ACCEPTED;
    if (!w || w->action > VC_WEB_ESTOP || w->mode > VC_MODE_MANUAL ||
        w->deadman > 1 || !vc_permille_ok(w->linear) || !vc_permille_ok(w->steering))
        why = VC_INPUT_INVALID;
    else if (!fresh(now, w->timestamp_ms, VC_WEB_TIMEOUT_MS))
        why = VC_INPUT_STALE;
    if (why != VC_INPUT_ACCEPTED) {
        if (v->status.mode == VC_MODE_MANUAL) vc_stop(v, VC_WEB, 0); /* AUTO ignores the phone */
        return why;
    }
    /* Stop must not get stuck behind an old sequence or a full queue. */
    if (vc_web_action_is_stop(w->action)) {
        vc_stop(v, w->action == VC_WEB_ESTOP ? VC_ESTOP : VC_OPERATOR,
                w->action == VC_WEB_ESTOP);
        /* Fence off any older DRIVE/MODE already waiting in a mailbox.
         * The production urgent task applies the same fence after the
         * hardware output has been disarmed, then publishes web_seq. */
        if (!v->in.have_web || newer(w->seq, v->in.web.seq)) {
            v->in.web = *w;
            v->in.have_web = 1U;
            v->status.web_seq = w->seq;
        }
        return VC_INPUT_ACCEPTED;
    }
    if (v->in.have_web && !newer(w->seq, v->in.web.seq)) return VC_INPUT_NOT_NEWER;
    v->in.web = *w; v->in.have_web = 1; v->status.web_seq = w->seq;
    /* AUTO button: start autonomous driving (if ToF/link/AI path are OK).
     * MANUAL button (or any stop): back to MANUAL. */
    if (w->action == VC_WEB_MODE && w->mode != v->status.mode) {
        vc_stop(v, VC_OK, 0);            /* a mode switch is not an abort */
        if (w->mode == VC_MODE_AUTO) {
            v->status.mode = VC_MODE_AUTO;
            vc_arm_result_t armed = vc_start(v, now);
            if (armed != VC_ARM_OK) {
                /* Report which prerequisite refused AUTO, distinct from a real
                 * ToF/link/AI stop, without changing the status packet layout. */
                if (v->status.state != VC_EMERGENCY) {
                    uint32_t refused = auto_refusal_reason(armed);
                    if (refused != VC_OK) v->status.reason = refused;
                }
                v->status.mode = VC_MODE_MANUAL;
            }
        }
        return VC_INPUT_ACCEPTED;
    }
    /* AUTO_PENDING keeps reporting client_mode=AUTO.  Retain a refusal until
     * the UI synchronizes back to MANUAL, which prevents it from being lost
     * among those neutral polling commands. */
    if (w->action == VC_WEB_DRIVE && w->mode == VC_MODE_MANUAL &&
        v->status.state == VC_STOPPED && !v->status.armed &&
        v->status.mode == VC_MODE_MANUAL &&
        vc_reason_is_auto_refusal(v->status.reason)) {
        /* Consume a refusal when the UI has seen it and synchronized to MANUAL.
         * AUTO_PENDING polls keep client_mode=AUTO, so they cannot erase it. */
        v->status.reason = VC_OK;
    }
    /* MANUAL: the vehicle drives only while a D-pad button is held. */
    if (v->status.mode == VC_MODE_MANUAL) {
        if (!v->status.armed && w->action == VC_WEB_DRIVE && w->deadman)
            (void)vc_start(v, now);
        else if (v->status.armed && !w->deadman)
            vc_stop(v, VC_OK, 0);        /* D-pad released: normal idle, no stop cause */
    }
    return VC_INPUT_ACCEPTED;
}

/* ---- start ------------------------------------------------------------ */
/* Start driving in the current mode: AUTO (Web AUTO button) or MANUAL (D-pad
 * pressed).  g_vc_start_result mirrors the last result for the debugger. */
volatile uint32_t g_vc_start_result;
static vc_arm_result_t start_check(const vc_t *v, uint32_t now)
{
    if (v->status.state == VC_EMERGENCY) return VC_ARM_EMERGENCY;
    if (!tof_ok(v, now))  return VC_ARM_TOF_NOT_OK;
    if (!link_ok(v, now)) return VC_ARM_LINK_STALE;
    if (v->status.mode == VC_MODE_AUTO) {
        /* Ask the path follower on a copy: this check must not change its
         * state (vc_step() is the only owner of v->path). */
        control_motor_t probe = v->path;
        control_motor_output_t p;
        control_motor_update(&probe, v->in.have_ai ? &v->in.ai : 0, &v->in.tof, now, &p);
        if (!p.motor_enable) return VC_ARM_PATH_NOT_READY;
    } else if (!web_fresh(v, now)) {
        return VC_ARM_WEB_STALE;
    }
    return VC_ARM_OK;
}
vc_arm_result_t vc_start(vc_t *v, uint32_t now)
{
    vc_arm_result_t result = start_check(v, now);
    g_vc_start_result = result;
    if (result != VC_ARM_OK) return result;
    v->status.armed = 1;
    v->status.reason = VC_OK;
    v->status.state = v->status.mode == VC_MODE_AUTO ? VC_AUTO : VC_MANUAL;
    ++v->start_count;
    return VC_ARM_OK;
}

/* Result: VC_ARM_OK, EMERGENCY (ESTOP / internal fault: board reset only),
 * TOF_NOT_OK, LINK_STALE or TOF_NOT_CLEAR (mirrored in g_vc_clear_result for
 * the debugger). */
volatile uint32_t g_vc_clear_result;
static vc_arm_result_t clear_check(const vc_t *v, uint32_t now)
{
    /* Neither a Web reset nor recovered sensors can clear these. */
    if (v->status.reason == VC_ESTOP || v->status.reason == VC_INTERNAL) return VC_ARM_EMERGENCY;
    if (!tof_ok(v, now))  return VC_ARM_TOF_NOT_OK;
    if (!link_ok(v, now)) return VC_ARM_LINK_STALE;
    if (v->in.tof.distance_mm < VC_TOF_CLEAR_MM) return VC_ARM_TOF_NOT_CLEAR;
    return VC_ARM_OK;
}
vc_arm_result_t vc_clear_emergency(vc_t *v, uint32_t now)
{
    vc_arm_result_t result = clear_check(v, now);
    g_vc_clear_result = result;
    if (result != VC_ARM_OK) return result;
    v->status.state = VC_STOPPED;
    v->status.armed = 0;
    /* status.reason keeps the cause (e.g. VC_TOF_NEAR) so the operator still
     * sees why the car stopped; the next start sets VC_OK. */
    v->left = v->right = 0;
    return VC_ARM_OK;
}

/* ---- periodic decision -------------------------------------------------- */
volatile int32_t g_vc_steering_trim_permille;
/* MANUAL wheel targets from the latest Web command.  Forward only: the rear is
 * unobserved, so reverse stays disabled pending rear safety validation.  The
 * wheel-balance trim learned in AUTO is applied while moving forward, so ▲
 * alone also drives straight. */
static void manual_targets(const vc_t *v, float *l, float *r)
{
    float forward = v->in.web.linear > 0 ? VC_MANUAL_MAX_FORWARD * v->in.web.linear / (float)VC_PERMILLE_MAX : 0;
    float turn = VC_MANUAL_MAX_STEERING * v->in.web.steering / (float)VC_PERMILLE_MAX +
                 (forward > 0 ? v->path.steering_trim : 0);
    *l = limit_command(forward + turn);
    *r = limit_command(forward - turn);
    if (*l < 0) *l = 0;
    if (*r < 0) *r = 0;
}

void vc_step(vc_t *v, uint32_t now, control_motor_output_t *out)
{
    control_motor_output_t p;
    float l = 0, r = 0;
    uint32_t gap = now - v->last_step_ms;
    uint32_t unsafe;

    memset(out, 0, sizeof(*out));
    out->stop_action = MOTOR_STOP_BRAKE;
    v->last_step_ms = now;
    v->status.control_ms = now;
    if (gap > v->status.control_max_gap_ms) v->status.control_max_gap_ms = gap;

    v->path.trim_learning = (uint8_t)(v->status.armed && v->status.state == VC_AUTO);
    control_motor_update(&v->path, v->in.have_ai ? &v->in.ai : 0, v->in.have_tof ? &v->in.tof : 0, now, &p);
    g_vc_steering_trim_permille = (int32_t)(VC_PERMILLE_MAX * v->path.steering_trim);

    /* ToF (wall) and link are stops in every mode. Person/car detections are
     * advisory only: the M85 logs them, and the M33 does not alter motor output. */
    unsafe = vc_unsafe_reason(v, now);
    if (v->status.armed && unsafe != VC_OK)
        vc_stop(v, unsafe, unsafe != VC_TOF_PRESTOP);
    /* Sensor/link/AI emergencies clear themselves once the cause is gone (ToF
     * beyond VC_TOF_CLEAR_MM, link fresh, AI path healthy again).  ESTOP and
     * internal faults stay latched.  Driving again still needs a new D-pad
     * press or AUTO button. */
    if (v->status.state == VC_EMERGENCY &&
        v->status.reason != VC_INTERNAL && v->status.reason != VC_ESTOP &&
        (v->status.reason != VC_AI_OBSTACLE || p.motor_enable))
        (void)vc_clear_emergency(v, now);

    /* TOR unanswered: safe stop.  The driver sees TOR_TIMEOUT and must reset
     * (Web STOP) or start again explicitly. */
    if (v->status.state == VC_TOR && (uint32_t)(now - v->tor_ms) >= VC_TOR_TIMEOUT_MS) {
        v->status.state = VC_STOPPED;
        v->status.mode = VC_MODE_MANUAL;
        v->status.reason = VC_TOR_TIMEOUT;
    }

    if (v->status.armed && v->status.mode == VC_MODE_AUTO) {
        /* AUTO ignores the phone: ToF / M85 link are stops (above); a path the
         * follower can no longer drive is a TOR. Obstacle alarms don't affect
         * the mode or motor output; the Web UI prompts the operator. */
        if (!p.motor_enable)
            enter_tor(v, now, VC_TOR_REQUEST);
        else { l = p.left_command; r = p.right_command; }
    } else if (v->status.armed) {
        /* MANUAL fail-safe: lost phone link must not keep the last command. */
        if (!web_fresh(v, now)) vc_stop(v, VC_WEB, 0);
        else if (v->in.web.action == VC_WEB_DRIVE && v->in.web.deadman) manual_targets(v, &l, &r);
    }

    if (v->status.armed) {
        v->left = slew(v->left, l); v->right = slew(v->right, r);
        out->motor_enable = 1; out->state = CONTROL_STATE_AUTO;
        out->left_command = v->left; out->right_command = v->right;
    } else {
        v->left = v->right = 0;
        out->state = CONTROL_STATE_SAFE_STOP;
    }
    v->status.left_permille = (int32_t)(VC_PERMILLE_MAX * v->left);
    v->status.right_permille = (int32_t)(VC_PERMILLE_MAX * v->right);
}
