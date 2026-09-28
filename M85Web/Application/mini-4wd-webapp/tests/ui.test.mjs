import test from 'node:test';
import assert from 'node:assert/strict';
import { UIManager } from '../js/ui.js';

function advisoryHarness() {
    globalThis.document = { body: { dataset: {} } };
    const ui = Object.create(UIManager.prototype);
    ui.manualAdvisoryShown = false;
    ui.logs = [];
    ui.addAlarmLog = (message) => ui.logs.push(message);
    return ui;
}

test('manual advisory appears for an active camera obstacle at any distance, once per encounter', () => {
    const ui = advisoryHarness();
    const mcu = { obstacle_alarm: true, obstacle_kind: 'PERSON', front_distance_mm: 301 };

    ui.renderManualAdvisory('AUTO', mcu, false);
    ui.renderManualAdvisory('AUTO', mcu, false);

    assert.equal(document.body.dataset.manualAdvisory, 'true');
    assert.equal(ui.logs.length, 1);
    assert.match(ui.logs[0], /人物を検知しました。手動操作への切り替えをお勧めします/);
});

test('manual advisory does not need valid ToF, and adds neutral ToF wording at 300 mm or closer', () => {
    const ui = advisoryHarness();
    for (const mcu of [{ obstacle_alarm: false, front_distance_mm: 200 }, null]) {
        ui.renderManualAdvisory('AUTO', mcu, false);
        assert.equal(document.body.dataset.manualAdvisory, 'false');
    }
    assert.equal(ui.logs.length, 0);

    let message = '';
    ui.el = { manualAdvisory: { set textContent(value) { message = value; } } };
    ui.renderManualAdvisory('AUTO', { obstacle_alarm: true, obstacle_kind: 'CAR', front_distance_mm: -1 }, false);
    assert.equal(document.body.dataset.manualAdvisory, 'true');
    assert.match(message, /車を検知しました。安全のため、手動操作への切り替えをお勧めします。$/);
    assert.doesNotMatch(message, /測定値/);

    ui.renderManualAdvisory('AUTO', { obstacle_alarm: true, obstacle_kind: 'CAR', front_distance_mm: 300 }, false);
    assert.match(message, /車を検知しました。安全のため、手動操作への切り替えをお勧めします。/);
    assert.match(message, /前方距離センサーの測定値は300 mmです/);
});

test('manual advisory only appears in active AUTO, never MANUAL, pending, TOR, or abort states', () => {
    const ui = advisoryHarness();
    const closeObstacle = { obstacle_alarm: true, front_distance_mm: 250 };
    for (const state of ['MANUAL', 'AUTO_PENDING', 'AUTO_TOR', 'AUTO_ABORT', 'MANUAL_ABORT', 'AUTO_MANUAL_PENDING', 'DISCONNECTED']) {
        ui.renderManualAdvisory(state, closeObstacle, false);
        assert.equal(document.body.dataset.manualAdvisory, 'false', state);
    }
    assert.equal(ui.logs.length, 0);

    ui.renderManualAdvisory('AUTO', closeObstacle, false);
    assert.equal(document.body.dataset.manualAdvisory, 'true');
    assert.equal(ui.logs.length, 1);
});

test('STOP/TOR display priority is preserved and a cleared detection re-arms the advisory', () => {
    const ui = advisoryHarness();
    const closeObstacle = { obstacle_alarm: true, front_distance_mm: 250 };
    ui.renderManualAdvisory('AUTO', closeObstacle, true);
    assert.equal(document.body.dataset.manualAdvisory, 'false');
    ui.renderManualAdvisory('AUTO_TOR', closeObstacle, false);
    assert.equal(document.body.dataset.manualAdvisory, 'false');
    ui.renderManualAdvisory('AUTO', closeObstacle, false);
    assert.equal(document.body.dataset.manualAdvisory, 'true');
    assert.equal(ui.logs.length, 1);
    ui.renderManualAdvisory('AUTO', { obstacle_alarm: false, front_distance_mm: 250 }, false);
    ui.renderManualAdvisory('AUTO', closeObstacle, false);
    assert.equal(ui.logs.length, 2);
});

test('TOR explains a possible blocked road after recent AUTO obstacle detection', () => {
    const torMessage = { textContent: '' };
    globalThis.document = { body: { dataset: {} } };
    const ui = Object.create(UIManager.prototype);
    ui.lastAutoObstacleAtMs = Date.now();
    ui.el = { torMessage };

    ui.renderState('AUTO_TOR', { armed: true, tor_remaining_ms: 2500 });
    assert.equal(torMessage.textContent, '障害物で走行路が見えなくなった可能性があります。停止しました。手動操作に切り替えてください。');

    ui.lastAutoObstacleAtMs = Date.now() - 5001;
    ui.renderState('AUTO_TOR', { armed: true, tor_remaining_ms: 2500 });
    assert.equal(torMessage.textContent, '自動運転の継続が困難です。手動操作へ引き継いでください。');

    ui.lastAutoObstacleAtMs = null;
    ui.renderState('AUTO_TOR', { armed: true, obstacle_alarm: true, tor_remaining_ms: 2500 });
    assert.equal(torMessage.textContent, '障害物で走行路が見えなくなった可能性があります。停止しました。手動操作に切り替えてください。');
    assert.notEqual(ui.lastAutoObstacleAtMs, null);
});

test('abort action is labeled RESET after a TOR timeout and STOP in normal MANUAL', () => {
    const button = {
        textContent: '', disabled: false, attributes: {},
        addEventListener() {},
        setAttribute(name, value) { this.attributes[name] = value; }
    };
    globalThis.document = {
        body: { dataset: {} },
        getElementById(id) { return id === 'btn-abort-action' ? button : null; }
    };
    const ui = new UIManager();

    ui.renderState('AUTO_ABORT', { armed: false, stop_reason: 'TOR_TIMEOUT' });
    assert.equal(button.textContent, 'RESET');
    assert.equal(button.attributes['aria-label'], '停止状態をリセット');
    assert.equal(button.disabled, false);

    ui.renderState('MANUAL', { armed: false, stop_reason: 'NONE' });
    assert.equal(button.textContent, 'STOP');
    assert.equal(button.attributes['aria-label'], '停止');
});
