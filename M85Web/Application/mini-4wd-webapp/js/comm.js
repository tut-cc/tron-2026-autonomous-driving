import { Config } from './constants.js';

export class CommManager {
    constructor(cb) {
        this.cb            = cb;
        this.lastHeartbeat = 0 ;
        this.connected     = false;
        this.isRequesting  = false;
        this.apiUrl        = '/api/control';

        // 100ms周期でコマンド送信 & テレメトリ受信 (HTTP定期ポーリング)
        this.timer = setInterval(() => this.poll(), Config.POLLING_INTERVAL_MS);
        this.poll();
    }

    async poll() {
        // ハートビート途絶監視 (タイムアウト判定)
        if (this.connected && Date.now() - this.lastHeartbeat > Config.HEARTBEAT_TIMEOUT_MS) this.onLost();

        // 前のリクエストがまだ通信中ならスキップ (リクエスト滞留・逆転防止)
        if (this.isRequesting) return;

        const payload = this.cb.getTransmitPayload?.() || {};
        const context = this.cb.getTransmitContext?.() || {};

        this.isRequesting = true;
        try {
            const controller = new AbortController();
            const timeoutId  = setTimeout(() => controller.abort(), 800);

            const res = await fetch(this.apiUrl, {
                method:  'POST',
                headers: { 'Content-Type': 'application/json' },
                body:    JSON.stringify(payload),
                signal:  controller.signal
            });
            clearTimeout(timeoutId);

            if (res.ok) {
                const telemetry    = await res.json();
                this.lastHeartbeat = Date.now();
                if (!this.connected) { this.connected = true; this.cb.onConnect?.(); }
                this.cb.onHeartbeat?.(telemetry, context);
            } else if (this.connected && Date.now() - this.lastHeartbeat > Config.HEARTBEAT_TIMEOUT_MS) this.onLost();
        }
        catch (_) { if (this.connected && Date.now() - this.lastHeartbeat > Config.HEARTBEAT_TIMEOUT_MS) this.onLost(); }
        finally   { this.isRequesting = false; }
    }

    onLost() { if (this.connected) { this.connected = false; this.cb.onDisconnect?.(); } }
}

