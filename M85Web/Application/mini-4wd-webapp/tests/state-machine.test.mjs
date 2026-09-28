import test from 'node:test';
import assert from 'node:assert/strict';
import { StateMachine, Cmd, isFresh } from '../js/state-machine.js';
import { UIState, ModeRequest } from '../js/constants.js';

const status = (web_seq, extra = {}) => ({
    mode: 'MANUAL', armed: false, web_seq, tor_active: false, request_reject_reason: 'NONE', ...extra
});

/* A connected machine showing MANUAL at web_seq 0. */
function setup() {
    const errors = [];
    const app = {
        input: {
            enabled: false,
            setEnabled(v) { this.enabled = v; },
            getThrottle: () => 0.8, getSteering: () => 0.2, getDeadman: () => true,
            reset() {}
        },
        ui: { renderState() {}, showError: (m) => errors.push(m) }
    };
    const m = new StateMachine(app);
    m.handleConnect();
    m.handleHeartbeat(status(0), {});
    /* Send one POST and answer it with `reply` (a function of the payload or a status). */
    const roundTrip = (reply) => {
        const { payload, context } = m.nextRequest();
        m.handleHeartbeat(typeof reply === 'function' ? reply(payload) : reply, context);
        return payload;
    };
    return { m, app, errors, roundTrip };
}

function withMachine(fn) {
    const t = setup();
    try { fn(t); } finally { t.m.clearTimer(); }
}

test('MANUAL drives; any pending command locks the input', () => withMachine(({ m, app }) => {
    assert.equal(m.state, UIState.MANUAL);
    assert.equal(app.input.enabled, true);
    let { payload } = m.nextRequest();
    assert.deepEqual([payload.throttle, payload.steering, payload.deadman], [0.8, 0.2, true]);
    assert.equal(payload.estop_request, false, 'no ESTOP button: always false');

    m.requestAbortAction();
    assert.equal(app.input.enabled, false);
    ({ payload } = m.nextRequest());
    assert.deepEqual([payload.throttle, payload.steering, payload.deadman], [0, 0, false]);
}));

test('AUTO stays pending through stale and neutral replies, then completes on fresh mode=AUTO', () => withMachine(({ m, roundTrip }) => {
    m.requestDriveModeToggle();
    assert.equal(m.state, UIState.AUTO_PENDING);
    assert.equal(roundTrip(status(0)).mode_request, ModeRequest.AUTO);           // stale
    assert.equal(m.state, UIState.AUTO_PENDING);
    roundTrip(status(1));                                                         // fresh, still MANUAL
    assert.equal(m.state, UIState.AUTO_PENDING, 'web_seq progress alone is not AUTO success');
    assert.equal(roundTrip(status(2, { mode: 'AUTO', armed: true })).mode_request, ModeRequest.AUTO);
    assert.equal(m.state, UIState.AUTO);
    assert.equal(m.pending, null);
    assert.equal(m.nextRequest().payload.client_mode, 'AUTO');
}));

test('a fresh rejection shows the reason and returns to MANUAL', () => {
    for (const [reason, text] of [['SENSOR_NOT_READY', /ToF/], ['LINK_NOT_READY', /通信/], ['PATH_NOT_READY', /走行路/]]) {
        withMachine(({ m, app, errors, roundTrip }) => {
            m.requestDriveModeToggle();
            roundTrip(status(1, { request_reject_reason: reason }));
            assert.equal(m.state, UIState.MANUAL);
            assert.equal(app.input.enabled, true);
            assert.match(errors.at(-1), /切替拒否/);
            assert.match(errors.at(-1), text);
        });
    }
    withMachine(({ m, roundTrip }) => {
        m.requestDriveModeToggle();
        roundTrip(status(1, { request_reject_reason: 'PATH_NOT_READY', armed: true }));
        assert.equal(m.state, UIState.AUTO_MANUAL_PENDING, 'rejected but still armed: ask for MANUAL');
    });
});

test('a response to a POST sent before the click cannot confirm it', () => withMachine(({ m }) => {
    const { context } = m.nextRequest();
    m.requestDriveModeToggle();
    m.handleHeartbeat(status(5, { mode: 'AUTO', armed: true }), context);
    assert.equal(m.pending?.kind, Cmd.AUTO);
}));

test('AUTO timeout cancels only the pending UI request; delayed acknowledgement remains authoritative', () => withMachine(({ m, app, errors }) => {
    m.requestDriveModeToggle();
    const { context } = m.nextRequest();
    m.onTimeout();
    assert.match(errors.at(-1), /タイムアウト/);
    assert.equal(m.pending, null);
    assert.equal(m.state, UIState.MANUAL);
    assert.deepEqual(m.nextRequest().payload.mode_request, ModeRequest.NONE,
                     'timeout must not synthesize a MANUAL request');

    m.handleHeartbeat(status(1, { mode: 'AUTO', armed: true }), context);
    assert.equal(m.state, UIState.AUTO, 'a late M33 acknowledgement mirrors actual vehicle state');
    assert.equal(m.pending, null);
    assert.equal(app.input.enabled, false);
}));

test('TOR take-over and the mode button in AUTO ask for MANUAL', () => withMachine(({ m, roundTrip }) => {
    m.handleHeartbeat(status(0, { mode: 'AUTO', armed: true, tor_active: true }), {});
    assert.equal(m.state, UIState.AUTO_TOR);
    m.requestTorTakeover();
    assert.equal(m.state, UIState.TOR_MANUAL_PENDING);
    roundTrip(status(1));
    assert.equal(m.state, UIState.MANUAL);

    m.handleHeartbeat(status(1, { mode: 'AUTO', armed: true }), {});
    m.requestDriveModeToggle();
    assert.equal(m.state, UIState.AUTO_MANUAL_PENDING);
}));

test('ABORT preempts a mode request and enters MANUAL_ABORT', () => withMachine(({ m, roundTrip }) => {
    m.requestDriveModeToggle();
    m.requestAbortAction();
    let p = roundTrip(status(0, { mode: 'AUTO', armed: true }));
    assert.deepEqual([p.manual_abort_request, p.mode_request], [true, ModeRequest.NONE]);
    p = roundTrip(status(0));
    assert.equal(p.manual_abort_request, true);
    assert.equal(m.pending?.kind, Cmd.STOP, 'unchanged web_seq may be a stale snapshot');
    p = roundTrip(status(1, { mode: 'MANUAL_ABORT' }));
    assert.equal(p.manual_abort_request, true);
    assert.equal(m.pending, null);
    assert.equal(m.state, UIState.MANUAL_ABORT);
    assert.equal(m.nextRequest().payload.manual_abort_request, false);
}));

test('ABORT enters MANUAL_ABORT, and RESET returns to MANUAL', () => withMachine(({ m, app, roundTrip }) => {
    assert.equal(m.state, UIState.MANUAL);
    assert.equal(app.input.enabled, true);
    m.requestAbortAction();
    assert.equal(app.input.enabled, false);
    roundTrip(status(1, { mode: 'MANUAL_ABORT', stop_reason: 'MANUAL_ABORT_BUTTON' }));
    assert.equal(m.state, UIState.MANUAL_ABORT);
    assert.equal(app.input.enabled, false);

    /* Click RESET button on the abort screen */
    m.requestAbortAction();
    assert.equal(m.pending?.kind, Cmd.RESET);
    roundTrip(status(2, { mode: 'MANUAL', stop_reason: 'NONE' }));
    assert.equal(m.state, UIState.MANUAL);
    assert.equal(app.input.enabled, true);
}));

test('RESET on the abort screen does not clear an M33 Emergency', () => withMachine(({ m, roundTrip }) => {
    m.handleHeartbeat(status(0, { mode: 'MANUAL_ABORT' }), {});
    assert.equal(m.state, UIState.MANUAL_ABORT);
    m.requestAbortAction();
    let p = roundTrip(status(0, { mode: 'MANUAL_ABORT' }));
    assert.deepEqual([p.reset_abort_request, p.client_mode], [true, 'MANUAL_ABORT']);
    assert.equal(m.pending?.kind, Cmd.RESET, 'stale snapshot cannot confirm RESET');
    roundTrip(status(1, { mode: 'MANUAL_ABORT' }));
    assert.equal(m.pending, null);
    assert.equal(m.state, UIState.MANUAL_ABORT);
}));

test('an ABORT status from M33 cancels a pending mode switch', () => withMachine(({ m }) => {
    m.requestDriveModeToggle();
    m.handleHeartbeat(status(0, { mode: 'AUTO_ABORT' }), {});
    assert.equal(m.pending, null);
    assert.equal(m.state, UIState.AUTO_ABORT);
}));

test('disconnect during AUTO request cancels pending UI state without synthesizing MANUAL', () => withMachine(({ m, errors }) => {
    m.requestDriveModeToggle();
    m.handleDisconnect();
    assert.equal(m.state, UIState.DISCONNECTED);
    assert.match(errors.at(-1), /通信切断/);
    assert.equal(m.pending, null);
    m.handleConnect();
    assert.equal(m.state, UIState.MANUAL);
    assert.equal(m.nextRequest().payload.mode_request, ModeRequest.NONE);
}));

test('web_seq comparison handles uint32 wraparound', () => {
    assert.equal(isFresh(0, 0xffffffff), true);
    assert.equal(isFresh(0xffffffff, 0), false);
    assert.equal(isFresh(3, 3), false);
    assert.equal(isFresh(undefined, 0), false);
});
