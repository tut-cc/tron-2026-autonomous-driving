/*
 * Web UI state machine.
 *
 * The M33 owns the vehicle state.  This UI only adds one thing on top of it:
 * at most ONE command the operator has asked for and M33 has not confirmed yet
 * (`pending`).  Everything the screen shows is derived from three values:
 *
 *   connected   is the HTTP heartbeat alive?
 *   pending     null | { kind, id, fromTor }      (kind: STOP / RESET / AUTO / MANUAL)
 *   mcu         last status received from M33
 *
 *   UI state = DISCONNECTED            if !connected
 *            = AUTO_PENDING            if pending AUTO
 *            = (TOR_|AUTO_)MANUAL_PENDING if pending MANUAL
 *            = mirror of mcu           otherwise (STOP/RESET only lock the input)
 *
 * A pending command is re-sent on every poll until a *fresh* status (web_seq
 * advanced past the value seen when that POST was built) shows it took effect:
 *
 *   kind    sent as                     done when fresh status says
 *   STOP    manual_abort_request=true   armed=false (M33 stop also selects MANUAL)
 *   RESET   reset_abort_request=true    armed=false (M33 Emergency stays latched)
 *   AUTO    mode_request=AUTO           mode=AUTO, or rejected -> MANUAL
 *   MANUAL  mode_request=MANUAL         mode=MANUAL and armed=false
 *
 * AUTO gives up waiting after MODE_SWITCH_TIMEOUT_MS and returns to the last
 * M33 state. It never synthesizes a MANUAL request: M33 owns AUTO safety, and
 * a delayed response may still confirm the original request. MANUAL never
 * gives up. An ABORT status from M33 cancels AUTO/MANUAL.
 */
import { UIState, MCUMode, ModeRequest, RejectReasonText, Config } from './constants.js';

export const Cmd = Object.freeze({ STOP: 'STOP', RESET: 'RESET', AUTO: 'AUTO', MANUAL: 'MANUAL' });

const isAbort = (mode) => mode === MCUMode.AUTO_ABORT || mode === MCUMode.MANUAL_ABORT;
const AUTO_FAMILY = new Set([UIState.AUTO_PENDING, UIState.AUTO, UIState.AUTO_TOR]);

/* M33 status -> UI state. */
export function mirror(mcu) {
    if (isAbort(mcu.mode)) return mcu.mode;
    if (mcu.mode === MCUMode.AUTO) return mcu.tor_active ? UIState.AUTO_TOR : UIState.AUTO;
    return UIState.MANUAL;
}

/* True when `current` web_seq is newer than `baseline` (uint32, wrap-safe). */
export function isFresh(current, baseline) {
    if (!Number.isInteger(current) || !Number.isInteger(baseline)) return false;
    const distance = ((current >>> 0) - (baseline >>> 0)) >>> 0;
    return distance !== 0 && distance < 0x80000000;
}

export class StateMachine {
    constructor(app) {
        this.app       = app;
        this.connected = false;
        this.pending   = null;
        this.mcu       = { mode: MCUMode.MANUAL, front_distance_mm: null, armed: false, web_seq: 0 };
        this.nextId    = 1;
        this.timer     = null;
    }

    /* ---- derived state ------------------------------------------------- */
    get state() {
        if (!this.connected) return UIState.DISCONNECTED;
        if (this.pending?.kind === Cmd.AUTO) return UIState.AUTO_PENDING;
        if (this.pending?.kind === Cmd.MANUAL) {
            return this.pending.fromTor ? UIState.TOR_MANUAL_PENDING : UIState.AUTO_MANUAL_PENDING;
        }
        return mirror(this.mcu);
    }

    get canDrive() { return this.state === UIState.MANUAL && !this.pending; }

    render() {
        this.app.input.setEnabled(this.canDrive);
        this.app.ui.renderState(this.state, this.mcu);
    }

    /* ---- pending command ----------------------------------------------- */
    setPending(kind, fromTor = false) {
        clearTimeout(this.timer);
        this.timer = null;
        this.pending = kind ? { kind, id: this.nextId++, fromTor } : null;
        if (kind === Cmd.AUTO || kind === Cmd.MANUAL) {
            this.timer = setTimeout(() => this.onTimeout(), Config.MODE_SWITCH_TIMEOUT_MS);
        }
        this.render();
    }

    onTimeout() {
        if (this.pending?.kind === Cmd.AUTO) {
            this.app.ui.showError('モード切替タイムアウト');
            this.setPending(null);
        } else if (this.pending?.kind === Cmd.MANUAL) {
            if (this.connected) this.app.ui.showError('手動復帰の応答待ち (再送中...)');
            this.timer = setTimeout(() => this.onTimeout(), Config.MODE_SWITCH_TIMEOUT_MS);
        }
    }

    clearTimer() { clearTimeout(this.timer); this.timer = null; }

    /* ---- operator actions ---------------------------------------------- */
    requestDriveModeToggle() {
        if (this.pending) return;
        const s = this.state;
        if (s === UIState.MANUAL) this.setPending(Cmd.AUTO);
        else if (s === UIState.AUTO || s === UIState.AUTO_TOR) this.setPending(Cmd.MANUAL, s === UIState.AUTO_TOR);
    }

    requestTorTakeover() { this.setPending(Cmd.MANUAL, true); }

    /* One button: ABORT normally, RESET on the abort screen. */
    requestAbortAction() {
        this.app.input.reset();
        this.setPending(isAbort(this.state) ? Cmd.RESET : Cmd.STOP);
    }

    /* ---- communication -------------------------------------------------- */
    /* Called once per POST.  `context` travels with that POST and comes back
     * with its response, so an old response can never confirm a newer click. */
    nextRequest() {
        const p = this.pending, drive = this.canDrive, s = this.state;
        const clientMode = isAbort(s) ? s :
            (p?.kind !== Cmd.MANUAL && AUTO_FAMILY.has(s)) ? MCUMode.AUTO : MCUMode.MANUAL;
        const modeRequest = (p?.kind === Cmd.AUTO || p?.kind === Cmd.MANUAL) ? p.kind : ModeRequest.NONE;
        return {
            payload: {
                client_mode:          clientMode,
                throttle:             drive ? this.app.input.getThrottle() : 0,
                steering:             drive ? this.app.input.getSteering() : 0,
                mode_request:         modeRequest,
                deadman:              drive ? this.app.input.getDeadman() : false,
                estop_request:        false,   /* no ESTOP button; field kept for the protocol */
                manual_abort_request: p?.kind === Cmd.STOP,
                reset_abort_request:  p?.kind === Cmd.RESET
            },
            context: { pendingId: p?.id ?? null, baselineWebSeq: this.mcu.web_seq }
        };
    }

    handleHeartbeat(mcu, context = {}) {
        this.mcu = mcu;
        const p = this.pending;

        if (p && isAbort(mcu.mode) && (p.kind === Cmd.AUTO || p.kind === Cmd.MANUAL)) {
            return this.setPending(null);               /* M33 aborted: nothing left to switch */
        }
        if (!p || context.pendingId !== p.id || !isFresh(mcu.web_seq, context.baselineWebSeq)) {
            return this.render();                       /* not an answer to the pending command */
        }

        switch (p.kind) {
        case Cmd.STOP:
        case Cmd.RESET:
            if (!mcu.armed) return this.setPending(null);
            break;
        case Cmd.MANUAL:
            if (mcu.mode === MCUMode.MANUAL && !mcu.armed) return this.setPending(null);
            break;
        case Cmd.AUTO: {
            if (mcu.mode === MCUMode.AUTO) return this.setPending(null);
            const reason = mcu.request_reject_reason;
            if (reason && reason !== 'NONE') {
                this.app.ui.showError(`切替拒否: ${RejectReasonText[reason] || reason}`);
                const safe = mcu.mode === MCUMode.MANUAL && !mcu.armed;
                return this.setPending(safe ? null : Cmd.MANUAL);
            }
            break;
        }
        }
        this.render();
    }

    handleConnect() {
        this.connected = true;
        this.render();
    }

    handleDisconnect() {
        this.connected = false;
        if (this.pending?.kind === Cmd.AUTO) {
            this.app.ui.showError('通信切断によりAUTO要求を中断');
            this.setPending(null);
        } else {
            this.render();
        }
    }
}
