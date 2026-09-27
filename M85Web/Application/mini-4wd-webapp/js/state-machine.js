import { UIState, MCUMode, ModeRequest, RejectReasonText, Config } from './constants.js';

export class StateMachine {
    constructor(app) {
        this.app                = app;
        this.state              = UIState.DISCONNECTED;
        this.timer              = null;
        this.mcuData            = { mode: MCUMode.MANUAL, front_distance_mm: null, armed: false, web_seq: 0 };
        this.pendingModeRequest = ModeRequest.NONE;
        this.pendingManualAbort = false;
        this.pendingResetAbort  = false;
        this.pendingEstop       = false;
        this.returningToManual  = false;
        this.lastTransmitContext = {};
    }

    getTransmitPayload() {
        const urgent = this.pendingEstop || this.pendingManualAbort || this.pendingResetAbort;
        const modeRequest = urgent ? ModeRequest.NONE : this.pendingModeRequest;
        const clientMode = this.returningToManual ? MCUMode.MANUAL :
            (this.state.includes('ABORT') ? this.state : (this.state.startsWith('AUTO') ? MCUMode.AUTO : MCUMode.MANUAL));
        const isManual = this.state === UIState.MANUAL && !urgent && modeRequest === ModeRequest.NONE;
        const payload = {
            client_mode:          clientMode,
            throttle:             isManual ? this.app.input.getThrottle() : 0,
            steering:             isManual ? this.app.input.getSteering() : 0,
            mode_request:         modeRequest,
            deadman:              isManual ? this.app.input.getDeadman() : false,
            estop_request:        this.pendingEstop,
            manual_abort_request: this.pendingManualAbort,
            reset_abort_request:  this.pendingResetAbort
        };
        /* CommManager serializes POSTs. Keep commands latched until a response
         * proves that M33 reached the requested safe state; a failed fetch or
         * stale snapshot must not consume STOP/ESTOP/MODE. */
        this.lastTransmitContext = {
            modeRequest,
            baselineWebSeq: this.mcuData.web_seq,
            estopRequest: payload.estop_request,
            manualAbortRequest: payload.manual_abort_request,
            resetAbortRequest: payload.reset_abort_request
        };
        return payload;
    }

    getTransmitContext() { return this.lastTransmitContext; }

    handleHeartbeat(data, context = {}) {
        this.mcuData = data;

        /* M33 Emergency is authoritative and Web reset never clears it. */
        if (data.mode === MCUMode.MANUAL_ABORT || data.mode === MCUMode.AUTO_ABORT) {
            this.pendingModeRequest = ModeRequest.NONE;
            this.returningToManual = false;
            this.clearTimer();
            const freshStopStatus = this.hasFreshWebStatus(data, context) && data.armed === false;
            if (freshStopStatus && context.estopRequest) this.pendingEstop = false;
            if (freshStopStatus && context.manualAbortRequest) this.pendingManualAbort = false;
            if (freshStopStatus && context.resetAbortRequest) this.pendingResetAbort = false;
            this.transitionTo(data.mode);
            return;
        }

        if (this.state === UIState.AUTO_PENDING) {
            /* web_seq only tells us M33 processed some newer web input. It is
             * not itself an ACK for AUTO: require the reported mode or a
             * rejection, and only inspect those after the sequence advances. */
            if (context.modeRequest === ModeRequest.AUTO && this.hasFreshWebStatus(data, context)) {
                if (data.mode === MCUMode.AUTO) {
                    this.pendingModeRequest = ModeRequest.NONE;
                    this.clearTimer();
                    return this.transitionTo(data.tor_active ? UIState.AUTO_TOR : UIState.AUTO);
                }
                if (data.request_reject_reason && data.request_reject_reason !== 'NONE') {
                    this.app.ui.showError(`切替拒否: ${RejectReasonText[data.request_reject_reason] || data.request_reject_reason}`);
                    this.pendingModeRequest = ModeRequest.NONE;
                    this.clearTimer();
                    if (data.mode === MCUMode.MANUAL && data.armed === false) {
                        return this.transitionTo(UIState.MANUAL);
                    }
                    return this.beginManualReturn();
                }
            }
            return;
        }

        if (this.pendingEstop || this.pendingManualAbort || this.pendingResetAbort) {
            const matchingUrgent = context.estopRequest || context.manualAbortRequest || context.resetAbortRequest;
            const safeManual = data.mode === MCUMode.MANUAL && data.armed === false;
            const freshUrgentStatus = matchingUrgent && this.hasFreshWebStatus(data, context);
            if (freshUrgentStatus && safeManual) {
                if (context.estopRequest) this.pendingEstop = false;
                if (context.manualAbortRequest) this.pendingManualAbort = false;
                if (context.resetAbortRequest) this.pendingResetAbort = false;
                if (this.returningToManual) this.pendingModeRequest = ModeRequest.MANUAL;
            }
            if (this.pendingEstop || this.pendingManualAbort || this.pendingResetAbort) {
                this.syncToMCU();
                return;
            }
        }

        if (this.pendingModeRequest === ModeRequest.MANUAL) {
            /* A manual request is not complete until a newer M33 status says
             * MANUAL and unarmed. This prevents stale pre-request snapshots
             * from unlocking D-pad input after an AUTO cancellation. */
            if (context.modeRequest === ModeRequest.MANUAL && this.hasFreshWebStatus(data, context) &&
                data.mode === MCUMode.MANUAL && data.armed === false) {
                this.pendingModeRequest = ModeRequest.NONE;
                this.returningToManual = false;
                this.clearTimer();
                return this.transitionTo(UIState.MANUAL);
            }
            return;
        }

        this.syncToMCU();
    }

    hasFreshWebStatus(data, context) {
        const baseline = context.baselineWebSeq;
        const current = data.web_seq;
        if (!Number.isInteger(baseline) || !Number.isInteger(current)) return false;
        const distance = ((current >>> 0) - (baseline >>> 0)) >>> 0;
        return distance !== 0 && distance < 0x80000000;
    }

    syncToMCU() {
        const { mode, tor_active } = this.mcuData;
        this.transitionTo(
            mode === MCUMode.MANUAL_ABORT ? UIState.MANUAL_ABORT :
            mode === MCUMode.AUTO_ABORT   ? UIState.AUTO_ABORT   :
            mode === MCUMode.AUTO ? (tor_active ? UIState.AUTO_TOR : UIState.AUTO) : UIState.MANUAL
        );
    }

    transitionTo(newState) {
        this.state = newState;
        this.app.input.setEnabled(newState === UIState.MANUAL &&
            !this.pendingEstop && !this.pendingManualAbort && !this.pendingResetAbort &&
            this.pendingModeRequest === ModeRequest.NONE);
        this.app.ui.renderState(this.state, this.mcuData);
    }

    clearTimer() {
        clearTimeout(this.timer);
        this.timer = null;
    }

    startManualSwitch(fromTor = false) {
        this.pendingModeRequest = ModeRequest.MANUAL;
        this.returningToManual = true;
        const target = fromTor ? UIState.TOR_MANUAL_PENDING : UIState.AUTO_MANUAL_PENDING;
        this.transitionTo(target);
        this.clearTimer();
        this.timer = setTimeout(function retry() {
            if (this.state === target) {
                this.app.ui.showError('切替応答待ち (手動復帰を再送中...)');
                this.timer = setTimeout(retry.bind(this), Config.MODE_SWITCH_TIMEOUT_MS);
            }
        }.bind(this), Config.MODE_SWITCH_TIMEOUT_MS);
    }

    beginManualReturn() {
        this.pendingModeRequest = ModeRequest.MANUAL;
        this.returningToManual = true;
        this.clearTimer();
        this.transitionTo(UIState.AUTO_MANUAL_PENDING);
        this.timer = setTimeout(() => {
            if (this.state === UIState.AUTO_MANUAL_PENDING) {
                this.app.ui.showError('手動復帰の応答待ち (再送中...)');
                this.timer = setTimeout(() => this.beginManualReturnTimer(), Config.MODE_SWITCH_TIMEOUT_MS);
            }
        }, Config.MODE_SWITCH_TIMEOUT_MS);
    }

    beginManualReturnTimer() {
        if (this.state !== UIState.AUTO_MANUAL_PENDING) return;
        this.app.ui.showError('手動復帰の応答待ち (再送中...)');
        this.timer = setTimeout(() => this.beginManualReturnTimer(), Config.MODE_SWITCH_TIMEOUT_MS);
    }

    expireAutoRequest() {
        if (this.state !== UIState.AUTO_PENDING) return;
        this.app.ui.showError('モード切替タイムアウト');
        /* Stop retrying AUTO immediately. A MANUAL request is repeated until
         * M33 confirms MANUAL + unarmed; later sensor recovery cannot restart. */
        this.beginManualReturn();
    }

    requestDriveModeToggle() {
        if (this.pendingEstop || this.pendingManualAbort || this.pendingResetAbort ||
            this.pendingModeRequest !== ModeRequest.NONE) return;
        if (this.state === UIState.MANUAL) {
            this.pendingModeRequest = ModeRequest.AUTO;
            this.transitionTo(UIState.AUTO_PENDING);
            this.clearTimer();
            this.timer = setTimeout(() => this.expireAutoRequest(), Config.MODE_SWITCH_TIMEOUT_MS);
        } else if (this.state.startsWith('AUTO')) this.startManualSwitch(this.state === UIState.AUTO_TOR);
    }

    requestAbortAction() {
        if (this.pendingEstop) return;
        if (this.state.includes('ABORT')) {
            this.pendingResetAbort = true;
            this.app.input.reset();
            return;
        }
        this.pendingManualAbort = true;
        this.pendingModeRequest = this.state.startsWith('AUTO') ? ModeRequest.MANUAL : ModeRequest.NONE;
        this.returningToManual = this.pendingModeRequest === ModeRequest.MANUAL;
        this.clearTimer();
        this.app.input.reset();
        if (this.returningToManual) this.transitionTo(UIState.AUTO_MANUAL_PENDING);
        else this.transitionTo(this.state);
    }

    requestEstop() {
        this.pendingEstop = true;
        this.pendingManualAbort = false;
        this.pendingResetAbort = false;
        this.pendingModeRequest = ModeRequest.NONE;
        this.returningToManual = false;
        this.clearTimer();
        this.app.input.reset();
        this.transitionTo(this.state);
    }

    requestTorTakeover() { this.startManualSwitch(true); }
    handleDisconnect() {
        this.clearTimer();
        if (this.state === UIState.AUTO_PENDING) {
            this.app.ui.showError('通信切断によりAUTO要求を中断');
            this.pendingModeRequest = ModeRequest.MANUAL;
            this.returningToManual = true;
        }
        this.transitionTo(UIState.DISCONNECTED);
    }
    handleConnect() {
        if (this.returningToManual || this.pendingModeRequest === ModeRequest.MANUAL) {
            this.transitionTo(UIState.AUTO_MANUAL_PENDING);
            return;
        }
        this.syncToMCU();
    }
}
