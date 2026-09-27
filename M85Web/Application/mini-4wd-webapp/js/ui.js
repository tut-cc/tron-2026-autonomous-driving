import { StopReasonText, Config } from './constants.js';

const $ = (id) => document.getElementById(id);

export class UIManager {
    constructor(callbacks = {}) {
        this.cb         = callbacks;
        this.alertTimer = null;
        this.el = {
            status:     $('status-text'),
            dist:       $('distance-val'),
            btnMode:    $('btn-mode'),
            btnStop:    $('btn-stop'),
            btnEstop:   $('btn-estop'),
            stopReason: $('stop-reason-text'),
            alert:      $('alert-banner'),
            tor:        $('tor-countdown')
        };
        this.el.btnMode?.addEventListener('click', () => this.cb.onDriveModeClick?.());
        this.el.btnStop?.addEventListener('click', () => this.cb.onStopClick?.());
        this.el.btnEstop?.addEventListener('click', () => this.cb.onEstopClick?.());
        $('btn-takeover')?.addEventListener('click', () => this.cb.onTorTakeoverClick?.());
        $('camera')?.addEventListener('dragstart', (e) => e.preventDefault());
        try { navigator.wakeLock?.request('screen'); } catch (_) {}
    }

    renderState(state, mcu) {
        document.body.dataset.state = state;
        const stopReason = mcu?.stop_reason || 'NONE';
        const showStopReason = mcu?.armed === false && stopReason !== 'NONE';
        document.body.dataset.stopReasonActive = showStopReason ? 'true' : 'false';
        const isAbort = state.includes('ABORT'), isTor = state.includes('TOR');
        if (this.el.status)  this.el.status.textContent  = state === 'DISCONNECTED' ? '未接続' : '接続中';
        if (this.el.dist)    this.el.dist.textContent    = (mcu?.front_distance_mm ?? -1) >= 0 ? mcu.front_distance_mm : '--';
        if (this.el.btnMode) this.el.btnMode.textContent = state.startsWith('AUTO') ? 'AUTO MODE' : 'MANUAL MODE';
        if (this.el.btnStop) this.el.btnStop.textContent = isAbort ? 'RESET' : 'ABORT';
        if (isTor && mcu?.tor_remaining_ms != null && this.el.tor) this.el.tor.textContent = (mcu.tor_remaining_ms / 1000).toFixed(1);
        if (this.el.stopReason) {
            this.el.stopReason.textContent = showStopReason ?
                (StopReasonText[stopReason] || stopReason) : 'NONE';
        }
    }

    showError(msg) {
        if (!this.el.alert) return;
        this.el.alert.textContent = msg;
        try { this.el.alert.showPopover?.(); } catch (_) {}
        clearTimeout(this.alertTimer);
        this.alertTimer = setTimeout(() => { try { this.el.alert.hidePopover?.(); } catch (_) {} }, Config.ALERT_DISPLAY_DURATION_MS);
    }
}
