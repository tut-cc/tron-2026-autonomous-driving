import test from 'node:test';
import assert from 'node:assert/strict';
import { CommManager } from '../js/comm.js';

test('CommManager retries a retained request after an HTTP failure and serializes POSTs', async () => {
    const saved = {
        setInterval: globalThis.setInterval,
        clearInterval: globalThis.clearInterval,
        fetch: globalThis.fetch
    };
    const bodies = [];
    const heartbeats = [];
    let resolveFirst;
    let calls = 0;
    globalThis.setInterval = () => 1;
    globalThis.clearInterval = () => {};
    globalThis.fetch = async (_url, options) => {
        calls += 1;
        bodies.push(JSON.parse(options.body));
        if (calls === 1) return new Promise(resolve => { resolveFirst = resolve; });
        return { ok: false, status: 503 };
    };

    try {
        const manager = new CommManager({
            nextRequest: () => ({ payload: { mode_request: 'AUTO' }, context: { pendingId: 1 } }),
            onHeartbeat: (data, context) => heartbeats.push({ data, context })
        });
        await new Promise(resolve => setImmediate(resolve));
        assert.equal(calls, 1);
        await manager.poll();
        assert.equal(calls, 1, 'a second POST must not overlap the in-flight first POST');
        resolveFirst({ ok: false, status: 503 });
        await new Promise(resolve => setImmediate(resolve));
        await manager.poll();
        assert.equal(calls, 2);
        assert.deepEqual(bodies.map(body => body.mode_request), ['AUTO', 'AUTO']);
        assert.equal(heartbeats.length, 0, 'failed HTTP responses are not telemetry ACKs');
    } finally {
        globalThis.setInterval = saved.setInterval;
        globalThis.clearInterval = saved.clearInterval;
        globalThis.fetch = saved.fetch;
    }
});
