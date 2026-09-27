const camera = document.getElementById('camera');
const image = document.getElementById('camera-image');
const status = document.getElementById('camera-status');

if (camera && image && status) {
    // Best-effort 10 Hz polling. Requests remain serialized below, so a slow
    // response never creates a queue of old frames; each request asks for the
    // newest snapshot available when the embedded server opens the URL.
    const refreshMs = 100;
    const staleAfterMs = 3000;
    const responseTimeoutMs = 3500;
    let sequence = 0;
    let lastFrameAt = 0;
    let lastProducerSequence = null;
    let producerSequenceChangedAt = 0;
    let nextRequestTimer = 0;
    let activeRequest = null;
    let displayedObjectUrl = null;
    let pageIsClosing = false;

    function setUnavailable(message) {
        camera.dataset.videoState = lastFrameAt ? 'stale' : 'waiting';
        status.textContent = message;
    }

    function scheduleNextFrame(delayMs = refreshMs) {
        window.clearTimeout(nextRequestTimer);
        nextRequestTimer = window.setTimeout(() => {
            nextRequestTimer = 0;
            void requestFrame();
        }, delayMs);
    }

    async function requestFrame() {
        if (activeRequest) return;

        const requestId = ++sequence;
        const controller = new AbortController();
        const startedAt = performance.now();
        const request = { id: requestId, controller };
        activeRequest = request;
        const timeoutId = window.setTimeout(() => controller.abort(), responseTimeoutMs);

        try {
            const response = await fetch(`/video_feed?frame=${requestId}`, {
                method: 'GET',
                cache: 'no-store',
                signal: controller.signal,
                headers: { 'Accept': 'image/bmp' }
            });
            const headersAt = performance.now();
            if (requestId !== sequence) return;
            if (!response.ok) {
                if (response.body) await response.body.cancel();
                if (response.status === 503) {
                    setUnavailable('映像データなし (HTTP 503)');
                } else {
                    setUnavailable(`映像取得エラー (HTTP ${response.status})`);
                }
                return;
            }

            const blob = await response.blob();
            if (requestId !== sequence || activeRequest !== request) return;
            const bodyReadyAt = performance.now();
            const headerMs = Math.max(0, Math.round(headersAt - startedAt));
            const bodyMs = Math.max(0, Math.round(bodyReadyAt - headersAt));
            const elapsedMs = Math.max(0, Math.round(bodyReadyAt - startedAt));

            const objectUrl = URL.createObjectURL(blob);
            const stagedImage = new Image();
            stagedImage.src = objectUrl;
            try {
                await stagedImage.decode();
            } catch (error) {
                URL.revokeObjectURL(objectUrl);
                throw error;
            }
            if (requestId !== sequence || activeRequest !== request) {
                URL.revokeObjectURL(objectUrl);
                return;
            }

            const previousObjectUrl = displayedObjectUrl;
            displayedObjectUrl = objectUrl;
            image.src = objectUrl;
            if (previousObjectUrl) URL.revokeObjectURL(previousObjectUrl);

            const displayedAt = performance.now();
            const previousDisplayAt = lastFrameAt;
            lastFrameAt = displayedAt;
            const frameSequence = response.headers?.get('X-Frame-Seq');
            let producerFrameIsNew = false;
            let producerFps = '--';
            if (frameSequence !== null && frameSequence !== undefined) {
                if (frameSequence !== lastProducerSequence) {
                    if (producerSequenceChangedAt > 0) {
                        producerFps = (1000 / (displayedAt - producerSequenceChangedAt)).toFixed(1);
                    }
                    lastProducerSequence = frameSequence;
                    producerSequenceChangedAt = displayedAt;
                    producerFrameIsNew = true;
                } else {
                    producerFps = '0.0';
                }
            }
            const pollFps = previousDisplayAt > 0 ?
                (1000 / (displayedAt - previousDisplayAt)).toFixed(1) : '--';
            const sequenceLabel = frameSequence === null || frameSequence === undefined ?
                '' : ` · frame #${frameSequence} ${producerFrameIsNew ? '(new)' :
                    `(same ${Math.round(displayedAt - producerSequenceChangedAt)}ms)`}`;
            camera.dataset.videoState = 'live';
            status.textContent = `LIVE · ${stagedImage.naturalWidth}×${stagedImage.naturalHeight} · GET ${elapsedMs}ms (header ${headerMs}/body ${bodyMs}) · poll ${pollFps}fps · producer ${producerFps}fps${sequenceLabel}`;
        } catch (error) {
            if (requestId !== sequence || activeRequest !== request) return;
            if (error?.name === 'AbortError') {
                setUnavailable('映像応答タイムアウト');
            } else {
                setUnavailable('映像取得エラー');
            }
        } finally {
            window.clearTimeout(timeoutId);
            if (activeRequest === request) {
                activeRequest = null;
                if (!pageIsClosing) {
                    const elapsedMs = performance.now() - startedAt;
                    scheduleNextFrame(Math.max(0, refreshMs - elapsedMs));
                }
            }
        }
    }

    window.setInterval(() => {
        const now = performance.now();
        if (lastProducerSequence !== null &&
            now - producerSequenceChangedAt >= staleAfterMs) {
            camera.dataset.videoState = 'stale';
            status.textContent = `producer frame #${lastProducerSequence} の更新停止 (${Math.round((now - producerSequenceChangedAt) / 1000)}s)`;
        } else if (lastFrameAt && now - lastFrameAt >= staleAfterMs) {
            camera.dataset.videoState = 'stale';
            status.textContent = '映像取得が止まっています';
        }
    }, 250);

    window.addEventListener('pagehide', () => {
        pageIsClosing = true;
        window.clearTimeout(nextRequestTimer);
        nextRequestTimer = 0;
        activeRequest?.controller.abort();
        if (displayedObjectUrl) {
            URL.revokeObjectURL(displayedObjectUrl);
            displayedObjectUrl = null;
        }
    });
    window.addEventListener('pageshow', () => {
        pageIsClosing = false;
        if (!activeRequest && !nextRequestTimer) void requestFrame();
    });

    void requestFrame();
}
