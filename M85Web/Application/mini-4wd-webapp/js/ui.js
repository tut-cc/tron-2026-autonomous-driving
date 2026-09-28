import { StopReasonText, ObstacleKindText, Config } from './constants.js';

const $ = (id) => document.getElementById(id);

export class UIManager {
    constructor(callbacks = {}) {
        this.cb           = callbacks;
        this.alertTimer   = null;
        this.lastAlarmSeq = null;
        this.manualAdvisoryShown = false;
        this.el = {
            status:     $('status-text'),
            dist:       $('distance-val'),
            btnMode:    $('btn-mode'),
            btnAbort:   $('btn-abort-action'),
            stopReason: $('stop-reason-text'),
            alert:      $('alert-banner'),
            tor:        $('tor-countdown'),
            alarmKind:  $('alarm-kind'),
            torMessage: $('tor-message'),
            alarmLog:   $('alarm-log')
        };
        this.el.btnMode?.addEventListener('click', () => this.cb.onDriveModeClick?.());
        this.el.btnAbort?.addEventListener('click', () => this.cb.onAbortActionClick?.());
        $('btn-takeover')?.addEventListener('click', () => this.cb.onTorTakeoverClick?.());
        $('camera')?.addEventListener('dragstart', (e) => e.preventDefault());
        try { navigator.wakeLock?.request('screen'); } catch (_) {}
    }

    renderState(state, mcu) {
        document.body.dataset.state = state;
        const stopReason = mcu?.stop_reason || 'NONE';
        const showStopReason = mcu?.armed === false && stopReason !== 'NONE';
        document.body.dataset.stopReasonActive = showStopReason ? 'true' : 'false';
        const isTor = state.includes('TOR');
        if (this.el.status)  this.el.status.textContent  = state === 'DISCONNECTED' ? '未接続' : '接続中';
        if (this.el.dist)    this.el.dist.textContent    = (mcu?.front_distance_mm ?? -1) >= 0 ? mcu.front_distance_mm : '--';
        if (this.el.btnMode) this.el.btnMode.textContent = state.startsWith('AUTO') ? 'AUTO MODE' : 'MANUAL MODE';
        if (this.el.btnAbort) {
            const abort = state === 'AUTO_ABORT' || state === 'MANUAL_ABORT';
            this.el.btnAbort.textContent = abort ? 'RESET' : 'STOP';
            this.el.btnAbort.setAttribute('aria-label', abort ? '停止状態をリセット' : '停止');
            this.el.btnAbort.disabled = state === 'DISCONNECTED' || state.includes('PENDING') || state === 'AUTO_TOR';
        }
        if (isTor && mcu?.tor_remaining_ms != null && this.el.tor) this.el.tor.textContent = (mcu.tor_remaining_ms / 1000).toFixed(1);
        if (isTor && this.el.torMessage) {
            this.el.torMessage.textContent = stopReason === 'AI_OBSTACLE' ?
                '前方に人物・車が近づいています。手動操作へ引き継いでください。' :
                '自動運転の継続が困難です。手動操作へ引き継いでください。';
        }
        if (this.el.stopReason) {
            this.el.stopReason.textContent = showStopReason ?
                (StopReasonText[stopReason] || stopReason) : 'NONE';
        }
        this.renderAlarm(state === 'DISCONNECTED' ? null : mcu);
        this.renderManualAdvisory(state, mcu, showStopReason);
    }

    /* Person/car alarm from the M85 camera: a badge while it lasts and one
     * log line per new alarm (alarm_seq).  Not a stop by itself. */
    renderAlarm(mcu) {
        const active = !!mcu?.obstacle_alarm;
        const kind = ObstacleKindText[mcu?.obstacle_kind] || '障害物';
        document.body.dataset.obstacleAlarm = active ? 'true' : 'false';
        if (this.el.alarmKind) this.el.alarmKind.textContent = `${kind}を検知`;
        const seq = mcu?.alarm_seq;
        if (!Number.isInteger(seq) || seq === this.lastAlarmSeq) return;
        const first = this.lastAlarmSeq === null;
        this.lastAlarmSeq = seq;
        if (first && !active) return;          /* page opened: older alarms are not replayed */
        this.addAlarmLog(`${new Date().toLocaleTimeString('ja-JP', { hour12: false })} ${kind}を検知`);
    }

    /* A camera-detected person/car plus a valid close ToF reading is an
     * advisory only while AUTO is active. Log once per encounter. */
    renderManualAdvisory(state, mcu, showStopReason) {
        const distance = mcu?.front_distance_mm;
        const closeObstacle = !!mcu?.obstacle_alarm && Number.isFinite(distance) &&
            distance >= 0 && distance <= 300;
        const show = closeObstacle && state === 'AUTO' && !showStopReason;
        document.body.dataset.manualAdvisory = show ? 'true' : 'false';

        if (!closeObstacle) {
            this.manualAdvisoryShown = false;
            return;
        }
        if (show && !this.manualAdvisoryShown) {
            this.addAlarmLog(`${new Date().toLocaleTimeString('ja-JP', { hour12: false })} 手動操作への切り替えをお勧めします`);
            this.manualAdvisoryShown = true;
        }
    }

    addAlarmLog(text) {
        const log = this.el.alarmLog;
        if (!log) return;
        const item = document.createElement('li');
        item.textContent = text;
        log.prepend(item);
        while (log.children.length > Config.ALARM_LOG_MAX) log.lastElementChild.remove();
    }

    showError(msg) {
        if (!this.el.alert) return;
        this.el.alert.textContent = msg;
        try { this.el.alert.showPopover?.(); } catch (_) {}
        clearTimeout(this.alertTimer);
        this.alertTimer = setTimeout(() => { try { this.el.alert.hidePopover?.(); } catch (_) {} }, Config.ALERT_DISPLAY_DURATION_MS);
    }
}
