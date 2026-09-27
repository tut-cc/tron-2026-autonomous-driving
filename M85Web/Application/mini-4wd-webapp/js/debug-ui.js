/**
 * デバッグUI & レイアウト調整マネージャー
 * 画面上の全ボタン・ウィンドウ・HUDパーツの位置をドラッグ＆数値で調整可能
 */

const STORAGE_KEY = 'mini4wd_ui_layout_v1';

export const ADJUSTABLE_ELEMENTS = [
    { id: 'pad-throttle',      selector: '#pad-throttle',                   label: '前進パッド（左手）' },
    { id: 'pad-steer',         selector: '#pad-steer',                      label: '左右パッド（右手）' },
    { id: 'btn-mode',          selector: '#btn-mode',                       label: 'モード切替ボタン' },
    { id: 'btn-stop',          selector: '#btn-stop',                       label: 'ABORT / RESET ボタン' },
    { id: 'bottom-bar',        selector: '.bottom-bar',                     label: '操作フッター全体' },
    { id: 'top-bar',           selector: '.top-bar',                        label: 'HUD ヘッダー全体' },
    { id: 'status-badge',      selector: '#status-badge',                   label: 'ステータスバッジ' },
    { id: 'distance-card',     selector: '#distance-card',                  label: '前方距離カード' },
    { id: 'btn-info',          selector: 'button[popovertarget="info-modal"]', label: 'INFOボタン' },
    { id: 'stop-reason',       selector: '#stop-reason',                    label: '中断理由表示' },
    { id: 'alert-banner',      selector: '#alert-banner',                   label: 'エラー通知バナー' },
    { id: 'info-modal',        selector: '#info-modal',                     label: 'INFOウィンドウ' },
    { id: 'tor-card',          selector: '#tor-overlay .card',              label: 'TOR引継ぎウィンドウ' },
    { id: 'disconnected-card', selector: '#disconnected-overlay .card',     label: '切断ウィンドウ' }
];

export class DebugManager {
    constructor() {
        this.isActive        = false;
        this.selectedElement = null;
        this.layoutData      = this.loadLayout();
        this.previewStates   = { tor: false, disconnected: false, info: false, alert: false };

        this.initShortcutListeners();
        this.applyAllLayouts();
        this.createDebugPanel();
    }

    /* ==========================================================================
       1. ショートカット & トリガー監視
       ========================================================================== */
    initShortcutListeners() {
        // キーボード: Ctrl+Shift+D, Backquote (`), F2
        window.addEventListener('keydown', (e) => {
            const isCtrlShiftD = (e.ctrlKey || e.metaKey) && e.shiftKey && (e.code === 'KeyD');
            const isBackquote  = e.code === 'Backquote' && !e.ctrlKey && !e.metaKey && !e.altKey;
            const isF2         = e.code === 'F2';

            const isInputActive = ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement?.tagName);
            if (isInputActive && !isCtrlShiftD && !isF2) return;

            if (isCtrlShiftD || isBackquote || isF2) {
                e.preventDefault();
                this.toggle();
            }
        });

        // モバイル・タッチ: ステータスバッジのトリプルタップ
        let tapCount = 0;
        let tapTimer = null;
        const statusBadge = document.getElementById('status-badge');
        if (statusBadge) {
            statusBadge.addEventListener('click', (e) => {
                tapCount++;
                clearTimeout(tapTimer);
                tapTimer = setTimeout(() => { tapCount = 0; }, 500);
                if (tapCount >= 3) {
                    tapCount = 0;
                    this.toggle();
                }
            });
        }
    }

    /* ==========================================================================
       2. デバッグモード ON / OFF 切り替え
       ========================================================================== */
    toggle() {
        this.isActive = !this.isActive;
        document.body.classList.toggle('debug-mode-active', this.isActive);

        if (this.panel) {
            this.panel.classList.toggle('visible', this.isActive);
        }

        if (this.isActive) {
            this.attachDraggables();
            // 初期選択
            if (!this.selectedElement) {
                this.selectElement('pad-throttle');
            }
            this.showNotification('🛠 デバッグUI有効 (Ctrl+Shift+D / ` で閉じる)');
        } else {
            this.detachDraggables();
            this.resetPreviews();
            this.showNotification('デバッグUI終了');
        }
    }

    /* ==========================================================================
       3. レイアウトデータの管理 (localStorage)
       ========================================================================== */
    loadLayout() {
        try {
            const saved = localStorage.getItem(STORAGE_KEY);
            return saved ? JSON.parse(saved) : {};
        } catch (_) {
            return {};
        }
    }

    saveLayout() {
        try {
            localStorage.setItem(STORAGE_KEY, JSON.stringify(this.layoutData));
        } catch (_) {}
    }

    applyLayout(item) {
        const el = document.querySelector(item.selector);
        if (!el) return;
        const data = this.layoutData[item.id];
        if (!data) {
            el.style.removeProperty('left');
            el.style.removeProperty('top');
            el.style.removeProperty('bottom');
            el.style.removeProperty('right');
            el.style.removeProperty('transform');
            el.style.removeProperty('scale');
            return;
        }

        if (data.left != null)   el.style.left   = `${data.left}px`;
        if (data.top != null)    el.style.top    = `${data.top}px`;
        if (data.bottom != null) el.style.bottom = `${data.bottom}px`;
        if (data.right != null)  el.style.right  = `${data.right}px`;
        if (data.scale != null)  el.style.scale  = `${data.scale}`;

        // transform/translateのリセット（位置ズレ防止）
        if (data.left != null || data.top != null) {
            el.style.translate = '0 0';
        }
    }

    applyAllLayouts() {
        ADJUSTABLE_ELEMENTS.forEach(item => this.applyLayout(item));
    }

    resetElement(id) {
        delete this.layoutData[id];
        this.saveLayout();
        const item = ADJUSTABLE_ELEMENTS.find(x => x.id === id);
        if (item) this.applyLayout(item);
        this.updatePanelInputs();
        this.showNotification(`「${item?.label || id}」の位置をリセットしました`);
    }

    resetAll() {
        if (!confirm('すべてのUI配置を初期状態にリセットしますか？')) return;
        this.layoutData = {};
        this.saveLayout();
        this.applyAllLayouts();
        this.updatePanelInputs();
        this.showNotification('全パーツの配置をリセットしました');
    }

    /* ==========================================================================
       4. ドラッグ＆ドロップ移動ハンドラ
       ========================================================================== */
    attachDraggables() {
        ADJUSTABLE_ELEMENTS.forEach(item => {
            const el = document.querySelector(item.selector);
            if (!el) return;

            el.classList.add('debug-draggable');
            el.dataset.debugId = item.id;

            if (!el._debugPointerDownHandler) {
                el._debugPointerDownHandler = (e) => this.handleDragStart(e, item, el);
                el.addEventListener('pointerdown', el._debugPointerDownHandler);
            }
        });
    }

    detachDraggables() {
        ADJUSTABLE_ELEMENTS.forEach(item => {
            const el = document.querySelector(item.selector);
            if (!el) return;

            el.classList.remove('debug-draggable', 'debug-selected');
            if (el._debugPointerDownHandler) {
                el.removeEventListener('pointerdown', el._debugPointerDownHandler);
                delete el._debugPointerDownHandler;
            }
        });
    }

    handleDragStart(e, item, el) {
        if (!this.isActive || e.button > 0) return;
        if (e.target.closest('#debug-panel')) return;

        e.stopPropagation();
        e.preventDefault();

        this.selectElement(item.id);

        const rect = el.getBoundingClientRect();
        const startX = e.clientX;
        const startY = e.clientY;
        const startLeft = rect.left;
        const startTop = rect.top;

        // 座標インジケータツールチップ
        const tooltip = document.createElement('div');
        tooltip.className = 'debug-coord-tooltip';
        document.body.appendChild(tooltip);

        const updateTooltip = (left, top) => {
            tooltip.textContent = `${item.label}  X: ${Math.round(left)}px, Y: ${Math.round(top)}px`;
            tooltip.style.left = `${Math.max(10, Math.min(window.innerWidth - 200, left))}px`;
            tooltip.style.top = `${Math.max(10, top - 28)}px`;
        };
        updateTooltip(startLeft, startTop);

        const onPointerMove = (moveEv) => {
            const dx = moveEv.clientX - startX;
            const dy = moveEv.clientY - startY;
            const newLeft = Math.round(startLeft + dx);
            const newTop = Math.round(startTop + dy);

            // 要素に直接反映
            el.style.left = `${newLeft}px`;
            el.style.top = `${newTop}px`;
            el.style.bottom = 'auto';
            el.style.right = 'auto';
            el.style.translate = '0 0';

            // 内部データ更新
            if (!this.layoutData[item.id]) this.layoutData[item.id] = {};
            this.layoutData[item.id].left = newLeft;
            this.layoutData[item.id].top = newTop;
            delete this.layoutData[item.id].bottom;
            delete this.layoutData[item.id].right;

            updateTooltip(newLeft, newTop);
            this.updatePanelInputs();
        };

        const onPointerUp = (upEv) => {
            window.removeEventListener('pointermove', onPointerMove);
            window.removeEventListener('pointerup', onPointerUp);
            window.removeEventListener('pointercancel', onPointerUp);
            try { el.releasePointerCapture(e.pointerId); } catch (_) {}
            tooltip.remove();

            this.saveLayout();
        };

        try { el.setPointerCapture(e.pointerId); } catch (_) {}
        window.addEventListener('pointermove', onPointerMove);
        window.addEventListener('pointerup', onPointerUp);
        window.addEventListener('pointercancel', onPointerUp);
    }

    selectElement(id) {
        this.selectedElement = id;
        document.querySelectorAll('.debug-selected').forEach(el => el.classList.remove('debug-selected'));

        const item = ADJUSTABLE_ELEMENTS.find(x => x.id === id);
        if (item) {
            const el = document.querySelector(item.selector);
            if (el) el.classList.add('debug-selected');
        }

        if (this.elemSelect) {
            this.elemSelect.value = id;
        }
        this.updatePanelInputs();
    }

    /* ==========================================================================
       5. デバッグコントロールパネルの構築
       ========================================================================== */
    createDebugPanel() {
        const panel = document.createElement('div');
        panel.id = 'debug-panel';
        panel.className = 'debug-panel';
        panel.innerHTML = `
            <div class="debug-panel-header" id="debug-panel-header">
                <span class="debug-panel-title">🛠 UI Layout Adjuster</span>
                <div class="debug-panel-actions">
                    <button type="button" id="debug-btn-collapse" class="debug-btn-icon" title="最小化">─</button>
                    <button type="button" id="debug-btn-close" class="debug-btn-icon" title="閉じる (Esc / \`)">✕</button>
                </div>
            </div>
            <div class="debug-panel-body" id="debug-panel-body">
                <div class="debug-field">
                    <label>調整対象パーツ:</label>
                    <select id="debug-elem-select" class="debug-select">
                        ${ADJUSTABLE_ELEMENTS.map(x => `<option value="${x.id}">${x.label}</option>`).join('')}
                    </select>
                </div>

                <div class="debug-field-group">
                    <div class="debug-field">
                        <label>X座標 (Left): <span id="debug-val-x">--</span>px</label>
                        <div class="debug-row">
                            <input type="range" id="debug-range-x" min="0" max="1200" step="1" class="debug-range">
                            <input type="number" id="debug-num-x" class="debug-num-input">
                        </div>
                    </div>
                    <div class="debug-field">
                        <label>Y座標 (Top): <span id="debug-val-y">--</span>px</label>
                        <div class="debug-row">
                            <input type="range" id="debug-range-y" min="0" max="1000" step="1" class="debug-range">
                            <input type="number" id="debug-num-y" class="debug-num-input">
                        </div>
                    </div>
                </div>

                <div class="debug-field">
                    <label>微調整 (Shiftキーで10px):</label>
                    <div class="debug-nudge-pad">
                        <button type="button" class="debug-btn-nudge" data-nudge-x="0" data-nudge-y="-1">▲</button>
                        <div class="debug-nudge-middle">
                            <button type="button" class="debug-btn-nudge" data-nudge-x="-1" data-nudge-y="0">◀</button>
                            <span class="debug-nudge-center">●</span>
                            <button type="button" class="debug-btn-nudge" data-nudge-x="1" data-nudge-y="0">▶</button>
                        </div>
                        <button type="button" class="debug-btn-nudge" data-nudge-x="0" data-nudge-y="1">▼</button>
                    </div>
                </div>

                <div class="debug-field">
                    <label>スケール (サイズ倍率): <span id="debug-val-scale">1.0</span>x</label>
                    <div class="debug-row">
                        <input type="range" id="debug-range-scale" min="0.5" max="2.0" step="0.05" value="1.0" class="debug-range">
                    </div>
                </div>

                <div class="debug-field">
                    <label>ウィンドウプレビュー (強制表示):</label>
                    <div class="debug-preview-toggles">
                        <label class="debug-chk"><input type="checkbox" id="debug-chk-tor"> TOR警告</label>
                        <label class="debug-chk"><input type="checkbox" id="debug-chk-disconnect"> 切断</label>
                        <label class="debug-chk"><input type="checkbox" id="debug-chk-info"> INFO</label>
                        <label class="debug-chk"><input type="checkbox" id="debug-chk-alert"> アラート</label>
                    </div>
                </div>

                <div class="debug-btn-row">
                    <button type="button" id="debug-btn-reset-item" class="debug-btn">選択を初期化</button>
                    <button type="button" id="debug-btn-reset-all" class="debug-btn debug-btn-danger">全初期化</button>
                </div>

                <div class="debug-btn-row">
                    <button type="button" id="debug-btn-export-css" class="debug-btn debug-btn-primary">📋 CSSをコピー</button>
                    <button type="button" id="debug-btn-export-json" class="debug-btn">📋 JSON</button>
                </div>

                <div class="debug-hint">
                    💡 画面上の要素を直接ドラッグ移動できます<br>
                    ショートカット: <code>Ctrl+Shift+D</code> / <code>\`</code> / <code>F2</code>
                </div>
            </div>
        `;
        document.body.appendChild(panel);
        this.panel = panel;

        // パネル自体のドラッグ移動
        this.makePanelDraggable();

        // 各UIパーツ参照
        this.elemSelect   = panel.querySelector('#debug-elem-select');
        this.rangeX       = panel.querySelector('#debug-range-x');
        this.numX         = panel.querySelector('#debug-num-x');
        this.valX         = panel.querySelector('#debug-val-x');
        this.rangeY       = panel.querySelector('#debug-range-y');
        this.numY         = panel.querySelector('#debug-num-y');
        this.valY         = panel.querySelector('#debug-val-y');
        this.rangeScale   = panel.querySelector('#debug-range-scale');
        this.valScale     = panel.querySelector('#debug-val-scale');

        // イベントリスナー
        this.elemSelect.addEventListener('change', (e) => this.selectElement(e.target.value));

        const handleCoordChange = (x, y) => {
            if (!this.selectedElement) return;
            const item = ADJUSTABLE_ELEMENTS.find(el => el.id === this.selectedElement);
            if (!item) return;
            const el = document.querySelector(item.selector);
            if (!el) return;

            if (!this.layoutData[item.id]) this.layoutData[item.id] = {};
            if (x != null) {
                this.layoutData[item.id].left = Math.round(x);
                delete this.layoutData[item.id].right;
            }
            if (y != null) {
                this.layoutData[item.id].top = Math.round(y);
                delete this.layoutData[item.id].bottom;
            }

            this.applyLayout(item);
            this.saveLayout();
            this.updatePanelInputs();
        };

        this.rangeX.addEventListener('input', (e) => handleCoordChange(parseFloat(e.target.value), null));
        this.numX.addEventListener('change', (e) => handleCoordChange(parseFloat(e.target.value), null));
        this.rangeY.addEventListener('input', (e) => handleCoordChange(null, parseFloat(e.target.value)));
        this.numY.addEventListener('change', (e) => handleCoordChange(null, parseFloat(e.target.value)));

        this.rangeScale.addEventListener('input', (e) => {
            if (!this.selectedElement) return;
            const item = ADJUSTABLE_ELEMENTS.find(el => el.id === this.selectedElement);
            if (!item) return;
            if (!this.layoutData[item.id]) this.layoutData[item.id] = {};
            const scale = parseFloat(e.target.value);
            this.layoutData[item.id].scale = scale;
            this.applyLayout(item);
            this.saveLayout();
            this.valScale.textContent = scale.toFixed(2);
        });

        // 微調整パッド
        panel.querySelectorAll('.debug-btn-nudge').forEach(btn => {
            btn.addEventListener('click', (e) => {
                if (!this.selectedElement) return;
                const item = ADJUSTABLE_ELEMENTS.find(el => el.id === this.selectedElement);
                if (!item) return;
                const el = document.querySelector(item.selector);
                if (!el) return;

                const step = e.shiftKey ? 10 : 1;
                const nx = parseInt(btn.dataset.nudgeX, 10) * step;
                const ny = parseInt(btn.dataset.nudgeY, 10) * step;

                const rect = el.getBoundingClientRect();
                handleCoordChange(rect.left + nx, rect.top + ny);
            });
        });

        // プレビュートグル
        this.setupPreviewToggles(panel);

        // ボタンアクション
        panel.querySelector('#debug-btn-reset-item').addEventListener('click', () => {
            if (this.selectedElement) this.resetElement(this.selectedElement);
        });
        panel.querySelector('#debug-btn-reset-all').addEventListener('click', () => this.resetAll());
        panel.querySelector('#debug-btn-export-css').addEventListener('click', () => this.exportCSS());
        panel.querySelector('#debug-btn-export-json').addEventListener('click', () => this.exportJSON());

        // 最小化 / 閉じる
        panel.querySelector('#debug-btn-collapse').addEventListener('click', () => {
            panel.classList.toggle('collapsed');
        });
        panel.querySelector('#debug-btn-close').addEventListener('click', () => {
            this.toggle();
        });
    }

    makePanelDraggable() {
        const header = this.panel.querySelector('#debug-panel-header');
        if (!header) return;

        let startX = 0, startY = 0, initialLeft = 0, initialTop = 0;
        header.addEventListener('pointerdown', (e) => {
            if (e.target.closest('button')) return;
            e.preventDefault();
            startX = e.clientX;
            startY = e.clientY;
            const rect = this.panel.getBoundingClientRect();
            initialLeft = rect.left;
            initialTop = rect.top;

            const onMove = (moveEv) => {
                const nx = Math.max(0, Math.min(window.innerWidth - rect.width, initialLeft + moveEv.clientX - startX));
                const ny = Math.max(0, Math.min(window.innerHeight - 40, initialTop + moveEv.clientY - startY));
                this.panel.style.left = `${nx}px`;
                this.panel.style.top = `${ny}px`;
                this.panel.style.right = 'auto';
                this.panel.style.bottom = 'auto';
            };

            const onUp = () => {
                window.removeEventListener('pointermove', onMove);
                window.removeEventListener('pointerup', onUp);
            };

            window.addEventListener('pointermove', onMove);
            window.addEventListener('pointerup', onUp);
        });
    }

    setupPreviewToggles(panel) {
        const torChk        = panel.querySelector('#debug-chk-tor');
        const disconnectChk = panel.querySelector('#debug-chk-disconnect');
        const infoChk       = panel.querySelector('#debug-chk-info');
        const alertChk      = panel.querySelector('#debug-chk-alert');

        const torOverlay  = document.getElementById('tor-overlay');
        const discOverlay = document.getElementById('disconnected-overlay');
        const infoModal   = document.getElementById('info-modal');
        const alertBanner = document.getElementById('alert-banner');

        torChk?.addEventListener('change', (e) => {
            torOverlay?.style.setProperty('display', e.target.checked ? 'flex' : '', 'important');
        });
        disconnectChk?.addEventListener('change', (e) => {
            discOverlay?.style.setProperty('display', e.target.checked ? 'flex' : '', 'important');
        });
        infoChk?.addEventListener('change', (e) => {
            if (e.target.checked) {
                try { infoModal?.showPopover?.(); } catch (_) { if (infoModal) infoModal.style.display = 'block'; }
            } else {
                try { infoModal?.hidePopover?.(); } catch (_) { if (infoModal) infoModal.style.display = ''; }
            }
        });
        alertChk?.addEventListener('change', (e) => {
            if (e.target.checked) {
                try { alertBanner?.showPopover?.(); } catch (_) { if (alertBanner) alertBanner.style.display = 'block'; }
            } else {
                try { alertBanner?.hidePopover?.(); } catch (_) { if (alertBanner) alertBanner.style.display = ''; }
            }
        });
    }

    resetPreviews() {
        if (!this.panel) return;
        ['tor', 'disconnect', 'info', 'alert'].forEach(k => {
            const chk = this.panel.querySelector(`#debug-chk-${k}`);
            if (chk && chk.checked) {
                chk.checked = false;
                chk.dispatchEvent(new Event('change'));
            }
        });
    }

    updatePanelInputs() {
        if (!this.isActive || !this.selectedElement || !this.rangeX) return;
        const item = ADJUSTABLE_ELEMENTS.find(x => x.id === this.selectedElement);
        if (!item) return;
        const el = document.querySelector(item.selector);
        if (!el) return;

        const rect = el.getBoundingClientRect();
        const curX = Math.round(rect.left);
        const curY = Math.round(rect.top);

        this.rangeX.max = `${Math.max(1200, window.innerWidth)}`;
        this.rangeY.max = `${Math.max(1000, window.innerHeight)}`;

        this.rangeX.value = curX;
        this.numX.value   = curX;
        this.valX.textContent = curX;

        this.rangeY.value = curY;
        this.numY.value   = curY;
        this.valY.textContent = curY;

        const data = this.layoutData[item.id] || {};
        const scale = data.scale ?? 1.0;
        this.rangeScale.value = scale;
        this.valScale.textContent = Number(scale).toFixed(2);
    }

    /* ==========================================================================
       6. CSS & JSON エクスポート
       ========================================================================== */
    generateCSS() {
        const lines = [
            '/* =================================================== */',
            '/*   Mini 4WD WebApp - カスタムUIレイアウト設定CSS      */',
            '/* =================================================== */'
        ];

        let hasData = false;
        ADJUSTABLE_ELEMENTS.forEach(item => {
            const data = this.layoutData[item.id];
            if (!data) return;
            hasData = true;

            lines.push(`/* ${item.label} */`);
            lines.push(`${item.selector} {`);
            if (data.left != null)   lines.push(`  left: ${data.left}px;`);
            if (data.top != null)    lines.push(`  top: ${data.top}px;`);
            if (data.bottom != null) lines.push(`  bottom: ${data.bottom}px;`);
            if (data.right != null)  lines.push(`  right: ${data.right}px;`);
            if (data.scale != null && data.scale !== 1.0) lines.push(`  scale: ${data.scale};`);
            lines.push('  translate: 0 0;');
            lines.push('}\n');
        });

        if (!hasData) {
            lines.push('/* 調整された要素はありません（すべてデフォルト） */');
        }

        return lines.join('\n');
    }

    exportCSS() {
        const css = this.generateCSS();
        navigator.clipboard.writeText(css).then(() => {
            this.showNotification('📋 CSSコードをクリップボードにコピーしました！');
        }).catch(() => {
            this.showExportModal('生成されたCSSコード', css);
        });
    }

    exportJSON() {
        const json = JSON.stringify(this.layoutData, null, 2);
        navigator.clipboard.writeText(json).then(() => {
            this.showNotification('📋 JSON設定をクリップボードにコピーしました！');
        }).catch(() => {
            this.showExportModal('レイアウトJSON', json);
        });
    }

    showExportModal(title, content) {
        let modal = document.getElementById('debug-export-modal');
        if (!modal) {
            modal = document.createElement('div');
            modal.id = 'debug-export-modal';
            modal.className = 'debug-modal';
            document.body.appendChild(modal);
        }
        modal.innerHTML = `
            <div class="debug-modal-content">
                <h3>${title}</h3>
                <textarea readonly class="debug-textarea">${content}</textarea>
                <div class="debug-btn-row">
                    <button type="button" class="debug-btn debug-btn-primary" id="debug-modal-copy">コピー</button>
                    <button type="button" class="debug-btn" id="debug-modal-close">閉じる</button>
                </div>
            </div>
        `;
        modal.style.display = 'flex';
        modal.querySelector('#debug-modal-copy').onclick = () => {
            const ta = modal.querySelector('textarea');
            ta.select();
            document.execCommand('copy');
            this.showNotification('コピーしました！');
        };
        modal.querySelector('#debug-modal-close').onclick = () => {
            modal.style.display = 'none';
        };
    }

    showNotification(msg) {
        let toast = document.getElementById('debug-toast');
        if (!toast) {
            toast = document.createElement('div');
            toast.id = 'debug-toast';
            toast.className = 'debug-toast';
            document.body.appendChild(toast);
        }
        toast.textContent = msg;
        toast.classList.add('show');
        clearTimeout(this._toastTimer);
        this._toastTimer = setTimeout(() => {
            toast.classList.remove('show');
        }, 2500);
    }
}
