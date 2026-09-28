// スマホ向けの画面制御
//  1. 長押しでボタンの文字が範囲選択される／コールアウトが出るのを防ぐ
//  2. 横画面で表示する（可能なら全画面＋向きロック、無理なら CSS で 90° 回転）
//  3. 画面の拡大（ピンチ・ダブルタップ・Ctrl+ホイール・Ctrl+/-）を防ぐ

const isEditable = (el) => !!el?.closest?.('input, textarea, select, [contenteditable="true"]');

export function installSelectionGuard() {
    // iOS Safari / Android Chrome の長押し選択とコンテキストメニューを抑止
    document.addEventListener('selectstart', (e) => { if (!isEditable(e.target)) e.preventDefault(); });
    document.addEventListener('contextmenu', (e) => { if (!isEditable(e.target)) e.preventDefault(); });
    // 既に付いてしまった選択範囲はタッチ開始時に解除する
    document.addEventListener('pointerdown', (e) => {
        if (isEditable(e.target)) return;
        const sel = window.getSelection?.();
        if (sel && sel.rangeCount > 0) sel.removeAllRanges();
    }, { capture: true });
}

export function installZoomGuard() {
    // viewport の user-scalable=no と CSS の touch-action で大半は止まるが、
    // iOS Safari は user-scalable を無視するので gesture* イベントも止める。
    const block = (e) => e.preventDefault();
    ['gesturestart', 'gesturechange', 'gestureend'].forEach((t) =>
        document.addEventListener(t, block, { passive: false }));
    // 2本指以上のタッチ移動（ピンチ）はブラウザ既定動作だけ止める。
    // 操作パッドは pointer イベントで動くので、左右同時押しには影響しない。
    document.addEventListener('touchmove', (e) => {
        if (e.touches.length > 1) e.preventDefault();
    }, { passive: false });
    // PC: Ctrl(⌘)+ホイール / トラックパッドのピンチ、Ctrl(⌘) + '+' '-' '0'
    document.addEventListener('wheel', (e) => {
        if (e.ctrlKey || e.metaKey) e.preventDefault();
    }, { passive: false });
    document.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && ['+', '-', '=', '0', ';', '_'].includes(e.key)) e.preventDefault();
    });
}

export function installLandscape() {
    const coarse = window.matchMedia?.('(pointer: coarse)').matches;
    if (!coarse) return; // PC では何もしない

    // Android Chrome などは全画面にすると向きをロックできる。
    // iPhone Safari は未対応なので、縦持ちの間は style.css の
    // @media (orientation: portrait) and (pointer: coarse) が画面を 90° 回転させる。
    let tried = false;
    const tryLock = async (e) => {
        // 操作パッドでの最初のタッチは運転操作なので、全画面遷移で邪魔しない
        if (tried || e.target?.closest?.('.pad')) return;
        tried = true;
        try {
            if (!document.fullscreenElement && document.documentElement.requestFullscreen) {
                await document.documentElement.requestFullscreen({ navigationUI: 'hide' });
            }
            await screen.orientation?.lock?.('landscape');
        } catch (_) {
            // 未対応ブラウザは CSS 回転で表示する
        }
    };
    document.addEventListener('pointerup', tryLock, { capture: true });
}
