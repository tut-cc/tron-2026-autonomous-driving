import { Config } from './constants.js';

/* Polls POST /api/control every POLLING_INTERVAL_MS.  Only one POST is in
 * flight at a time, so responses can never arrive out of order. */
export class CommManager {
    constructor(cb) {
        this.cb            = cb;   /* { nextRequest, onHeartbeat, onConnect, onDisconnect } */
        this.lastHeartbeat = 0;
        this.connected     = false;
        this.isRequesting  = false;
        this.apiUrl        = '/api/control';

        this.timer = setInterval(() => this.poll(), Config.POLLING_INTERVAL_MS);
        this.poll();
    }

    async poll() {
        this.checkTimeout();
        if (this.isRequesting) return;

        const { payload = {}, context = {} } = this.cb.nextRequest?.() || {};
        this.isRequesting = true;
        const abort   = new AbortController();
        const timeout = setTimeout(() => abort.abort(), Config.REQUEST_TIMEOUT_MS);
        try {
            const res = await fetch(this.apiUrl, {
                method:  'POST',
                headers: { 'Content-Type': 'application/json' },
                body:    JSON.stringify(payload),
                signal:  abort.signal
            });
            if (!res.ok) return;
            const telemetry    = await res.json();
            this.lastHeartbeat = Date.now();
            if (!this.connected) { this.connected = true; this.cb.onConnect?.(); }
            this.cb.onHeartbeat?.(telemetry, context);
        } catch (_) {
            /* network error / timeout: the pending command is simply re-sent next poll */
        } finally {
            clearTimeout(timeout);
            this.isRequesting = false;
            this.checkTimeout();
        }
    }

    checkTimeout() {
        if (this.connected && Date.now() - this.lastHeartbeat > Config.HEARTBEAT_TIMEOUT_MS) {
            this.connected = false;
            this.cb.onDisconnect?.();
        }
    }
}
