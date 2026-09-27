#ifndef VEHICLE_CONTROL_H
#define VEHICLE_CONTROL_H
#include "control_motor.h"
#ifdef __cplusplus
extern "C" {
#endif
/* ---- Vocabulary -----------------------------------------------------------
 * The struct fields below stay uint32_t because vc_web_t / vc_status_t are
 * copied word-for-word through the IPC ABI and uT-Kernel message buffers.
 * These typedefs name the value set each field may hold. */

/* Vehicle state (vc_status_t.state). */
typedef enum { VC_STOPPED, VC_AUTO, VC_MANUAL, VC_TOR, VC_EMERGENCY } vc_state_t;
/* Driving mode selected by the Web UI (vc_status_t.mode, vc_web_t.mode). */
typedef enum { VC_MODE_AUTO, VC_MODE_MANUAL } vc_mode_t;
/* Why the vehicle is stopped (vc_status_t.reason). */
typedef enum {
    VC_OK, VC_WAITING, VC_TOF, VC_AI, VC_WEB, VC_OPERATOR, VC_INTERNAL, VC_LINK,
    /* AUTO button refused; the vehicle stays stopped.  One value per failed
     * prerequisite so the Web UI can tell the operator what to fix.  These are
     * events, not running stops: see vc_reason_is_auto_refusal(). */
    VC_AUTO_REFUSED_TOF,  /* ToF invalid / stale / too near (VC_ARM_TOF_NOT_OK)   */
    VC_AUTO_REFUSED_LINK, /* M85 heartbeat stale (VC_ARM_LINK_STALE)              */
    VC_AUTO_REFUSED_PATH, /* AI path follower not ready (VC_ARM_PATH_NOT_READY)   */
    /* TOR (take-over request): AUTO can no longer continue (road lost, AI
     * stale, low confidence).  Output is 0 while the driver is asked to take
     * over; see VC_TOR_TIMEOUT_MS. */
    VC_TOR_REQUEST,       /* state VC_TOR, mode AUTO, waiting for the driver      */
    VC_TOR_TIMEOUT,       /* TOR unanswered -> safe stop (state STOPPED, MANUAL)  */
    VC_REASON_COUNT
} vc_reason_t;
static inline int vc_reason_is_auto_refusal(uint32_t reason)
{ return reason >= VC_AUTO_REFUSED_TOF && reason <= VC_AUTO_REFUSED_PATH; }
/* Web command action (vc_web_t.action).  STOP/ESTOP use the reserved IPC
 * stop slot; see vc_web_action_is_stop(). */
typedef enum { VC_WEB_DRIVE, VC_WEB_STOP, VC_WEB_MODE, VC_WEB_ESTOP } vc_web_action_t;

/* Result of vc_start() / vc_clear_emergency(): VC_ARM_OK or why it was
 * refused.  The last value is also mirrored in g_vc_start_result /
 * g_vc_clear_result for the debugger; the numeric values are kept stable for
 * existing debug notes (docs/DEVELOPMENT.md). */
typedef enum {
    VC_ARM_OK              = 0,
    VC_ARM_EMERGENCY       = 1, /* emergency latched                         */
    VC_ARM_TOF_NOT_OK      = 2, /* ToF invalid / stale / too near             */
    VC_ARM_LINK_STALE      = 3, /* M85 heartbeat stale                        */
    VC_ARM_PATH_NOT_READY  = 4, /* AUTO path follower not ready               */
    VC_ARM_WEB_STALE       = 5, /* MANUAL: web command missing / stale        */
    VC_ARM_WEB_NOT_NEUTRAL = 6, /* MANUAL: web command not neutral            */
    VC_ARM_TOF_NOT_CLEAR   = 7  /* clear: ToF closer than VC_TOF_CLEAR_MM     */
} vc_arm_result_t;

/* Result of the input functions vc_ai() / vc_tof() / vc_web().  Negative
 * values are rejections (the input was not stored); callers that only need
 * accepted/rejected may test `< 0`. */
typedef enum {
    VC_INPUT_ACCEPTED  =  0,
    VC_INPUT_INVALID   = -1, /* NULL, wrong version or a value out of range   */
    VC_INPUT_STALE     = -2, /* timestamp older than the freshness limit      */
    VC_INPUT_NOT_NEWER = -3  /* sequence not newer than the last accepted one */
} vc_input_result_t;

/* Web stick values are fixed-point permille: -1000 .. +1000. */
#define VC_PERMILLE_MAX 1000

static inline int vc_web_action_is_stop(uint32_t action)
{ return action == VC_WEB_STOP || action == VC_WEB_ESTOP; }
static inline int vc_permille_ok(int32_t value)
{ return value >= -VC_PERMILLE_MAX && value <= VC_PERMILLE_MAX; }

/* ---- Safety and tuning parameters (single source of truth) ---------------
 * These are provisional engineering values, not validated stopping distances. */
#define VC_TOF_STOP_MM        150U  /* <= this distance: emergency stop        */
#define VC_TOF_CLEAR_MM       220U  /* safety stop clears itself at >= this    */
#define VC_TOF_FRESH_MS       100U  /* ToF sample must be newer than this      */
#define VC_LINK_FRESH_MS      300U  /* M85 heartbeat must be newer than this   */
#define VC_AI_FRESH_MS        AI_FRAME_MAX_AGE_MS /* AI frame age limit (see ai_control_signals.h) */
#define VC_TOR_TIMEOUT_MS    3000U  /* TOR unanswered this long -> safe stop  */
/* MANUAL fail-safe: stop when no Web command arrived for this long.  300ms is
 * the design value (the Web UI sends every 100ms).  If phone->board HTTP gaps
 * exceed it the vehicle stops with COMM_TIMEOUT (safe side); a bench-only
 * build may override with -DVC_WEB_TIMEOUT_MS=1000U (2026-09-25 bench), never
 * for floor driving.  AUTO does not watch the phone at all. */
#ifndef VC_WEB_TIMEOUT_MS
#define VC_WEB_TIMEOUT_MS 300U
#endif
#define VC_AUTO_BASE_COMMAND   .45F /* AUTO base forward command              */
#define VC_AUTO_MAX_STEERING   .25F /* AUTO steering limit                    */
#define VC_AUTO_AI_TIMEOUT_MS  VC_AI_FRESH_MS /* path follower: max wait for the next AI frame */
#define VC_MANUAL_MAX_FORWARD  .45F /* MANUAL command at linear=+1000         */
#define VC_MANUAL_MAX_STEERING .25F /* MANUAL differential at steering=+-1000 */
#define VC_COMMAND_LIMIT       .60F /* per-wheel command cap                  */
#define VC_SLEW_PER_STEP       .02F /* max command change per vc_step()       */

/* Web command, M85 -> M33.  Field order is part of the IPC/test ABI. */
typedef struct {
    uint32_t seq, timestamp_ms;
    uint32_t action;          /* vc_web_action_t                         */
    uint32_t mode;            /* vc_mode_t                               */
    uint32_t deadman;         /* 0/1                                     */
    int32_t linear, steering; /* permille -VC_PERMILLE_MAX..+VC_PERMILLE_MAX */
} vc_web_t;
/* Vehicle status, M33 -> M85.  Field order is part of the IPC/test ABI. */
typedef struct {
    uint32_t state;           /* vc_state_t                              */
    uint32_t reason;          /* vc_reason_t                             */
    uint32_t mode;            /* vc_mode_t                               */
    uint32_t armed, tof_mm, tof_valid;
    uint32_t ai_seq, web_seq, control_ms, control_max_gap_ms;
    int32_t left_permille, right_permille;
} vc_status_t;
/* Diagnostics for the debugger (see vc_arm_result_t). */
extern volatile uint32_t g_vc_start_result;
extern volatile uint32_t g_vc_clear_result;
/* Latest accepted inputs.  `have_*` is 0 until the first accepted sample;
 * the M85 link has no payload, only the time of the last heartbeat.  Local
 * to the M33 (not part of the IPC ABI). */
typedef struct {
    ai_perception_result_t ai;
    tof_safety_result_t tof;
    vc_web_t web;
    uint32_t have_ai, have_tof, have_web;
    uint32_t link_seen, link_ms;
} vc_inputs_t;
/* Vehicle controller.  Owned by the M33 control runtime, accessed under its
 * mutex only.  Local to the M33 (not part of the IPC ABI). */
typedef struct {
    control_motor_t path;      /* AUTO path follower (owned by vc_step())     */
    vc_inputs_t in;
    vc_status_t status;        /* published to the M85 every 10ms             */
    uint32_t last_step_ms;     /* previous vc_step() (control_max_gap_ms)     */
    uint32_t tor_ms;           /* time TOR was entered (VC_TOR_TIMEOUT_MS)    */
    uint32_t start_count;      /* incremented by every successful vc_start()  */
    float left, right;         /* slew-limited wheel commands                 */
} vc_t;
void vc_init(vc_t *, uint32_t now);
vc_input_result_t vc_ai(vc_t *, const ai_perception_result_t *, uint32_t now);
vc_input_result_t vc_tof(vc_t *, const tof_safety_result_t *, uint32_t now);
vc_input_result_t vc_web(vc_t *, const vc_web_t *, uint32_t now);
void vc_link(vc_t *, uint32_t now);
/* Web STOP/ESTOP applied by the urgent path: remember its sequence as the
 * newest Web command with a neutral stick, so a DRIVE/MODE queued before the
 * STOP (older sequence) can never re-arm MANUAL.  Call after the output has
 * been disarmed. */
void vc_fence_web(vc_t *, uint32_t stop_seq, uint32_t now);
void vc_stop(vc_t *, uint32_t reason, int emergency);
/* Start in the current mode: Web AUTO button (AUTO) or D-pad press (MANUAL). */
vc_arm_result_t vc_start(vc_t *, uint32_t now);
vc_arm_result_t vc_clear_emergency(vc_t *, uint32_t now);
void vc_step(vc_t *, uint32_t now, control_motor_output_t *out);
/* VC_OK when ToF and M85 link are both healthy, otherwise VC_TOF or VC_LINK. */
uint32_t vc_unsafe_reason(const vc_t *, uint32_t now);
#ifdef __cplusplus
}
#endif
#endif
