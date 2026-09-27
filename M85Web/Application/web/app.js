'use strict';
const byId = id => document.getElementById(id);
let running = false;
let inFlight = false;
let pendingEstop = false;
for (const id of ['throttle', 'steering']) {
  byId(id).addEventListener('input', () => { byId(`${id}_value`).value = byId(id).value; });
}
byId('toggle').addEventListener('click', () => {
  running = !running;
  byId('toggle').textContent = running ? '通信停止' : '通信開始';
  byId('status').textContent = running ? '通信中' : '停止中';
  if (running && !inFlight) void controlLoop();
});
byId('estop').addEventListener('click', () => {
  /* Keep the one-shot request pending until an HTTP 2xx acknowledges it. */
  pendingEstop = true;
  byId('status').textContent = 'ESTOP送信待ち';
});
async function controlLoop() {
  if (inFlight) return;
  inFlight = true;
  while (running) {
    const started = performance.now();
    const command = {
      client_mode: byId('client_mode').value,
      throttle: Number(byId('throttle').value),
      steering: Number(byId('steering').value),
      mode_request: byId('mode_request').value,
      deadman: byId('deadman').checked,
      estop_request: pendingEstop,
      manual_abort_request: byId('manual_abort_request').checked,
      reset_abort_request: byId('reset_abort_request').checked
    };
    const abort = new AbortController();
    const timeout = setTimeout(() => abort.abort(), 2000);
    try {
      const response = await fetch('/api/control', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(command), signal: abort.signal, cache: 'no-store'
      });
      const state = await response.json();
      if (!response.ok) throw new Error(`HTTP ${response.status}: ${JSON.stringify(state)}`);
      pendingEstop = false;
      byId('state').textContent = JSON.stringify(state, null, 2);
      byId('status').textContent = running ? '通信正常 (10 Hz)' : '停止中';
    } catch (error) {
      byId('status').textContent = running ? `通信エラー: ${error.message}` : '停止中';
    } finally { clearTimeout(timeout); }
    await new Promise(resolve => setTimeout(resolve, Math.max(0, 100 - (performance.now() - started))));
  }
  inFlight = false;
}
