import test from 'node:test';
import assert from 'node:assert/strict';
import { StateMachine } from '../js/state-machine.js';
import { UIState, ModeRequest } from '../js/constants.js';

function createMachine() {
    const rendered = [];
    const errors = [];
    const app = {
        input: {
            enabled: false,
            setEnabled(enabled) { this.enabled = enabled; },
            getThrottle() { return 0.8; },
            getSteering() { return 0.2; },
            getDeadman() { return true; },
            reset() {}
        },
        ui: {
            renderState(state, data) { rendered.push({ state, data }); },
            showError(message) { errors.push(message); }
        }
    };
    const machine = new StateMachine(app);
    machine.transitionTo(UIState.MANUAL);
    return { machine, app, rendered, errors };
}

const manual = (web_seq, extra = {}) => ({
    mode: 'MANUAL', armed: false, web_seq, tor_active: false,
    request_reject_reason: 'NONE', ...extra
});

test('AUTO stays pending through stale/neutral ACKs and retries the mode request', () => {
    const { machine } = createMachine();
    try {
        machine.requestDriveModeToggle();
        const first = machine.getTransmitPayload();
        const firstContext = machine.getTransmitContext();
        assert.equal(first.mode_request, ModeRequest.AUTO);

        machine.handleHeartbeat(manual(0), firstContext);
        assert.equal(machine.state, UIState.AUTO_PENDING);
        assert.equal(machine.getTransmitPayload().mode_request, ModeRequest.AUTO);

        const retryContext = machine.getTransmitContext();
        machine.handleHeartbeat(manual(1), retryContext);
        assert.equal(machine.state, UIState.AUTO_PENDING, 'web_seq progress alone is not AUTO success');
        assert.equal(machine.getTransmitPayload().mode_request, ModeRequest.AUTO);
    } finally { machine.clearTimer(); }
});

test('fresh status mode AUTO completes; a fresh explicit rejection returns to MANUAL', () => {
    const success = createMachine().machine;
    try {
        success.requestDriveModeToggle();
        success.getTransmitPayload();
        const context = success.getTransmitContext();
        success.handleHeartbeat({ ...manual(1), mode: 'AUTO', armed: true }, context);
        assert.equal(success.state, UIState.AUTO);
        assert.equal(success.pendingModeRequest, ModeRequest.NONE);
    } finally { success.clearTimer(); }

    const rejected = createMachine();
    try {
        rejected.machine.requestDriveModeToggle();
        rejected.machine.getTransmitPayload();
        const context = rejected.machine.getTransmitContext();
        rejected.machine.handleHeartbeat(manual(1, { request_reject_reason: 'SENSOR_NOT_READY' }), context);
        assert.equal(rejected.machine.state, UIState.MANUAL);
        assert.equal(rejected.machine.pendingModeRequest, ModeRequest.NONE);
        assert.equal(rejected.app.input.enabled, true);
        assert.match(rejected.errors.at(-1), /切替拒否/);
        assert.match(rejected.errors.at(-1), /ToF/);
    } finally { rejected.machine.clearTimer(); }

    for (const [reason, text] of [['LINK_NOT_READY', /通信/], ['PATH_NOT_READY', /走行路/]]) {
        const r = createMachine();
        try {
            r.machine.requestDriveModeToggle();
            r.machine.getTransmitPayload();
            const ctx = r.machine.getTransmitContext();
            r.machine.handleHeartbeat(manual(1, { request_reject_reason: reason }), ctx);
            assert.equal(r.machine.state, UIState.MANUAL);
            assert.match(r.errors.at(-1), text);
        } finally { r.machine.clearTimer(); }
    }
});

test('an old in-flight response cannot acknowledge a mode request clicked later', () => {
    const { machine } = createMachine();
    try {
        machine.getTransmitPayload();
        const oldContext = machine.getTransmitContext();
        machine.requestDriveModeToggle();
        machine.handleHeartbeat({ ...manual(0), mode: 'AUTO', armed: true }, oldContext);
        assert.equal(machine.state, UIState.AUTO_PENDING);
        assert.equal(machine.pendingModeRequest, ModeRequest.AUTO);
    } finally { machine.clearTimer(); }
});

test('timeout cancels AUTO and only unlocks after fresh MANUAL + unarmed status', () => {
    const { machine, app } = createMachine();
    try {
        machine.requestDriveModeToggle();
        machine.getTransmitPayload();
        machine.expireAutoRequest();
        assert.equal(machine.state, UIState.AUTO_MANUAL_PENDING);

        let payload = machine.getTransmitPayload();
        let context = machine.getTransmitContext();
        assert.equal(payload.mode_request, ModeRequest.MANUAL);
        assert.equal(payload.client_mode, 'MANUAL');
        assert.equal(payload.deadman, false);
        assert.equal(payload.throttle, 0);

        machine.handleHeartbeat(manual(0), context);
        assert.equal(machine.state, UIState.AUTO_MANUAL_PENDING, 'pre-request MANUAL snapshot is stale');
        payload = machine.getTransmitPayload();
        context = machine.getTransmitContext();
        machine.handleHeartbeat(manual(1, { armed: true }), context);
        assert.equal(machine.state, UIState.AUTO_MANUAL_PENDING, 'MANUAL while armed is not safe to unlock');

        payload = machine.getTransmitPayload();
        context = machine.getTransmitContext();
        machine.handleHeartbeat(manual(2), context);
        assert.equal(machine.state, UIState.MANUAL);
        assert.equal(machine.pendingModeRequest, ModeRequest.NONE);
        assert.equal(app.input.enabled, true);
    } finally { machine.clearTimer(); }
});

test('STOP/ESTOP remain latched, suppress MODE, and Web reset cannot clear Emergency', () => {
    const { machine } = createMachine();
    try {
        machine.requestDriveModeToggle();
        machine.getTransmitPayload();
        machine.requestEstop();
        let payload = machine.getTransmitPayload();
        let context = machine.getTransmitContext();
        assert.equal(payload.estop_request, true);
        assert.equal(payload.mode_request, ModeRequest.NONE);
        assert.equal(payload.throttle, 0);

        machine.handleHeartbeat(manual(0), context);
        assert.equal(machine.pendingEstop, true, 'manual/unarmed is not proof of ESTOP');
        payload = machine.getTransmitPayload();
        context = machine.getTransmitContext();
        assert.equal(payload.estop_request, true, 'failed/stale response leaves ESTOP for retry');
        machine.handleHeartbeat({ ...manual(0), mode: 'MANUAL_ABORT' }, context);
        assert.equal(machine.pendingEstop, true, 'an old ABORT snapshot cannot acknowledge this ESTOP');
        payload = machine.getTransmitPayload();
        context = machine.getTransmitContext();
        assert.equal(payload.estop_request, true, 'ESTOP still retries after an old ABORT snapshot');
        machine.handleHeartbeat(manual(1), context); // M33 may auto-clear an obstacle-free ESTOP on its next tick.
        assert.equal(machine.pendingEstop, false);
        assert.equal(machine.state, UIState.MANUAL, 'fresh MANUAL + unarmed status confirms the stop was applied');

        machine.transitionTo(UIState.MANUAL_ABORT);
        machine.requestAbortAction();
        payload = machine.getTransmitPayload();
        context = machine.getTransmitContext();
        assert.equal(payload.reset_abort_request, true);
        machine.handleHeartbeat({ ...manual(1), mode: 'MANUAL_ABORT' }, context);
        assert.equal(machine.pendingResetAbort, true, 'old Emergency snapshot cannot acknowledge a new Web reset');
        payload = machine.getTransmitPayload();
        context = machine.getTransmitContext();
        machine.handleHeartbeat({ ...manual(2), mode: 'MANUAL_ABORT' }, context);
        assert.equal(machine.state, UIState.MANUAL_ABORT, 'reset request does not clear the M33 Emergency latch');
        assert.equal(machine.pendingResetAbort, false);
    } finally { machine.clearTimer(); }
});

test('manual abort preempts MODE and is retained until a safe response', () => {
    const { machine } = createMachine();
    try {
        machine.requestDriveModeToggle();
        machine.requestAbortAction();
        let payload = machine.getTransmitPayload();
        let context = machine.getTransmitContext();
        assert.equal(payload.manual_abort_request, true);
        assert.equal(payload.mode_request, ModeRequest.NONE);

        machine.handleHeartbeat(manual(0), { modeRequest: ModeRequest.NONE });
        assert.equal(machine.pendingManualAbort, true, 'a response to an older flight cannot clear STOP');
        payload = machine.getTransmitPayload();
        context = machine.getTransmitContext();
        machine.handleHeartbeat(manual(0), context);
        assert.equal(machine.pendingManualAbort, true, 'safe but unchanged sequence may be a stale snapshot');
        payload = machine.getTransmitPayload();
        context = machine.getTransmitContext();
        assert.equal(payload.manual_abort_request, true, 'STOP is retried until status advances');
        machine.handleHeartbeat(manual(1), context);
        assert.equal(machine.pendingManualAbort, false);
        assert.equal(machine.pendingModeRequest, ModeRequest.MANUAL);
        payload = machine.getTransmitPayload();
        assert.equal(payload.mode_request, ModeRequest.MANUAL);
        assert.equal(payload.manual_abort_request, false);
    } finally { machine.clearTimer(); }
});

test('web_seq comparison handles uint32 wraparound', () => {
    const { machine } = createMachine();
    try {
        assert.equal(machine.hasFreshWebStatus({ web_seq: 0 }, { baselineWebSeq: 0xffffffff }), true);
        assert.equal(machine.hasFreshWebStatus({ web_seq: 0xffffffff }, { baselineWebSeq: 0 }), false);
    } finally { machine.clearTimer(); }
});
