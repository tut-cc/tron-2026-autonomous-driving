import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

const source = await readFile(new URL('../js/video.js', import.meta.url), 'utf8');

function createVideoHarness(fetchImpl) {
    const camera = { dataset: {} };
    const image = { src: '' };
    const status = { textContent: '' };
    const timers = new Map();
    const listeners = new Map();
    const intervals = new Map();
    const revokedUrls = [];
    let nextId = 0;
    let objectUrlId = 0;
    let now = 1000;

    const window = {
        setTimeout(callback, delay) {
            const id = ++nextId;
            timers.set(id, { callback, delay });
            return id;
        },
        clearTimeout(id) { timers.delete(id); },
        setInterval(callback, delay) {
            const id = ++nextId;
            intervals.set(id, { callback, delay });
            return id;
        },
        addEventListener(name, callback) { listeners.set(name, callback); }
    };
    const URL = {
        createObjectURL() { return `blob:test-${++objectUrlId}`; },
        revokeObjectURL(url) { revokedUrls.push(url); }
    };
    class StagedImage {
        constructor() {
            this.naturalWidth = 240;
            this.naturalHeight = 180;
        }
        set src(value) { this.url = value; }
        decode() { return Promise.resolve(); }
    }

    const context = {
        document: {
            getElementById: id => ({ camera, 'camera-image': image, 'camera-status': status })[id]
        },
        window,
        URL,
        Image: StagedImage,
        AbortController,
        Date,
        performance: { now: () => now },
        fetch: fetchImpl
    };
    vm.runInNewContext(source, context, { filename: 'video.js' });

    return {
        camera, image, status, timers, listeners, intervals, revokedUrls,
        advance(ms) { now += ms; },
        fireInterval() { [...intervals.values()][0]?.callback(); },
        fireTimer(delay) {
            const match = [...timers.entries()].find(([, timer]) =>
                delay === 100 ? timer.delay <= 100 : timer.delay === delay);
            assert.ok(match, `expected an active ${delay}ms timer`);
            timers.delete(match[0]);
            now += match[1].delay;
            match[1].callback();
        }
    };
}

test('video fetch publishes decoded image and reports GET/body duration', async () => {
    let requestedUrl;
    let requestedOptions;
    const harness = createVideoHarness(async (url, options) => {
        requestedUrl = url;
        requestedOptions = options;
        return {
            ok: true,
            headers: { get: name => name === 'X-Frame-Seq' ? '42' : null },
            blob: async () => new Blob(['bmp'])
        };
    });
    await new Promise(resolve => setImmediate(resolve));

    assert.equal(requestedUrl, '/video_feed?frame=1');
    assert.equal(requestedOptions.cache, 'no-store');
    assert.ok(requestedOptions.signal instanceof AbortSignal);
    assert.equal(harness.image.src, 'blob:test-1');
    assert.match(harness.status.textContent, /^LIVE · 240×180 · GET \d+ms \(header \d+\/body \d+\) · poll --fps · producer --fps · frame #42 \(new\)$/);
    assert.equal(harness.camera.dataset.videoState, 'live');
    assert.ok([...harness.timers.values()].some(timer => timer.delay <= 100));
});

test('the previous image object URL is revoked after its replacement is decoded', async () => {
    let frameSequence = 1;
    const harness = createVideoHarness(async () => ({
        ok: true,
        headers: { get: name => name === 'X-Frame-Seq' ? String(frameSequence) : null },
        blob: async () => new Blob(['bmp'])
    }));
    await new Promise(resolve => setImmediate(resolve));
    harness.fireTimer(100);
    await new Promise(resolve => setImmediate(resolve));

    assert.equal(harness.image.src, 'blob:test-2');
    assert.deepEqual(harness.revokedUrls, ['blob:test-1']);
    assert.match(harness.status.textContent, /poll 10\.0fps · producer 0\.0fps · frame #1 \(same 100ms\)$/);
});

test('repeated producer sequence is marked stale even while HTTP polling continues', async () => {
    const harness = createVideoHarness(async () => ({
        ok: true,
        headers: { get: name => name === 'X-Frame-Seq' ? '7' : null },
        blob: async () => new Blob(['bmp'])
    }));
    await new Promise(resolve => setImmediate(resolve));
    harness.fireTimer(100);
    await new Promise(resolve => setImmediate(resolve));

    assert.equal(harness.camera.dataset.videoState, 'live');
    harness.advance(3000);
    harness.fireInterval();
    assert.equal(harness.camera.dataset.videoState, 'stale');
    assert.match(harness.status.textContent, /producer frame #7 の更新停止 \(3s\)/);
});

test('HTTP 503 is surfaced distinctly and retried after the normal interval', async () => {
    let bodyCancelled = false;
    const harness = createVideoHarness(async () => ({
        ok: false,
        status: 503,
        body: { cancel: async () => { bodyCancelled = true; } }
    }));
    await new Promise(resolve => setImmediate(resolve));

    assert.equal(bodyCancelled, true);
    assert.equal(harness.status.textContent, '映像データなし (HTTP 503)');
    assert.equal(harness.camera.dataset.videoState, 'waiting');
    assert.ok([...harness.timers.values()].some(timer => timer.delay <= 100));
});

test('the 3.5 second timeout aborts the in-flight GET and reports timeout', async () => {
    let requestSignal;
    const harness = createVideoHarness((_url, options) => {
        requestSignal = options.signal;
        return new Promise((_resolve, reject) => {
            options.signal.addEventListener('abort', () => {
                reject(Object.assign(new Error('aborted'), { name: 'AbortError' }));
            }, { once: true });
        });
    });
    assert.ok([...harness.timers.values()].some(timer => timer.delay === 3500));
    harness.fireTimer(3500);
    await new Promise(resolve => setImmediate(resolve));

    assert.equal(requestSignal.aborted, true);
    assert.equal(harness.status.textContent, '映像応答タイムアウト');
    assert.equal(harness.camera.dataset.videoState, 'waiting');
    assert.ok([...harness.timers.values()].some(timer => timer.delay <= 100));
});
