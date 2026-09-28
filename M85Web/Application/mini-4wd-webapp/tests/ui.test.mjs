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

test('manual advisory appears at 300 mm for an active camera obstacle, once per encounter', () => {
    const ui = advisoryHarness();
    const mcu = { obstacle_alarm: true, front_distance_mm: 300 };

    ui.renderManualAdvisory('AUTO', mcu, false);
    ui.renderManualAdvisory('AUTO', mcu, false);

    assert.equal(document.body.dataset.manualAdvisory, 'true');
    assert.equal(ui.logs.length, 1);
    assert.match(ui.logs[0], /手動操作への切り替えをお勧めします/);
});

test('manual advisory is absent at 301 mm, without an obstacle, or without a valid ToF value', () => {
    const ui = advisoryHarness();
    for (const mcu of [
        { obstacle_alarm: true, front_distance_mm: 301 },
        { obstacle_alarm: false, front_distance_mm: 200 },
        { obstacle_alarm: true, front_distance_mm: -1 },
        { obstacle_alarm: true, front_distance_mm: null },
        null
    ]) {
        ui.renderManualAdvisory('AUTO', mcu, false);
        assert.equal(document.body.dataset.manualAdvisory, 'false');
    }
    assert.equal(ui.logs.length, 0);
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

test('STOP/TOR display priority is preserved and a cleared threshold re-arms the advisory', () => {
    const ui = advisoryHarness();
    const closeObstacle = { obstacle_alarm: true, front_distance_mm: 250 };
    ui.renderManualAdvisory('AUTO', closeObstacle, true);
    assert.equal(document.body.dataset.manualAdvisory, 'false');
    ui.renderManualAdvisory('AUTO_TOR', closeObstacle, false);
    assert.equal(document.body.dataset.manualAdvisory, 'false');
    ui.renderManualAdvisory('AUTO', closeObstacle, false);
    assert.equal(document.body.dataset.manualAdvisory, 'true');
    assert.equal(ui.logs.length, 1);
    ui.renderManualAdvisory('AUTO', { obstacle_alarm: true, front_distance_mm: 301 }, false);
    ui.renderManualAdvisory('AUTO', closeObstacle, false);
    assert.equal(ui.logs.length, 2);
});
