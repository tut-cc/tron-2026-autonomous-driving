export class InputController {
    constructor() {
        // ラジコン式：左手の前進パッドと右手の左右パッド。同時押し可。
        this.pads        = [...document.querySelectorAll('.pad')];
        this.buttons     = {};
        this.pointerDirs = new Map(); // pointerId -> dir
        this.keyDirs     = new Set(); // dir
        this.throttle    = 0;
        this.steering    = 0;
        this.deadman     = false;
        this.enabled     = true;

        this.initButtons();
        this.initPointerEvents();
        this.initKeyboardEvents();
    }

    initButtons() {
        ['up', 'left', 'right'].forEach(dir => {
            const btn = document.querySelector(`.pad [data-dir="${dir}"]`);
            if (btn) this.buttons[dir] = btn;
        });
    }

    initPointerEvents() {
        // 各パッドで押した指は、そのパッドのボタンだけを操作する
        // （左手の指が右のパッドへ滑っても左右操作にならない）。
        const getDirFromPoint = (pad, x, y) => {
            const el = document.elementFromPoint(x, y);
            const btn = el ? el.closest('.pad-btn') : null;
            return btn && pad.contains(btn) ? btn.dataset.dir : null;
        };

        const updatePointer = (pointerId, dir) => {
            if (!this.enabled) return;
            const prev = this.pointerDirs.get(pointerId);
            if (prev !== dir) {
                if (dir) {
                    this.pointerDirs.set(pointerId, dir);
                } else {
                    this.pointerDirs.delete(pointerId);
                }
                this.updateState();
            }
        };

        this.pads.forEach(pad => {
            pad.addEventListener('pointerdown', (e) => {
                if (!this.enabled || e.button > 0) return;
                try { pad.setPointerCapture(e.pointerId); } catch (_) {}
                const dir = getDirFromPoint(pad, e.clientX, e.clientY);
                if (dir) updatePointer(e.pointerId, dir);
            });

            pad.addEventListener('pointermove', (e) => {
                if (!this.pointerDirs.has(e.pointerId)) return;
                updatePointer(e.pointerId, getDirFromPoint(pad, e.clientX, e.clientY));
            });

            const releasePointer = (e) => {
                if (this.pointerDirs.has(e.pointerId)) {
                    this.pointerDirs.delete(e.pointerId);
                    try { pad.releasePointerCapture(e.pointerId); } catch (_) {}
                    this.updateState();
                }
            };

            pad.addEventListener('pointerup', releasePointer);
            pad.addEventListener('pointercancel', releasePointer);
        });
    }

    initKeyboardEvents() {
        const keyMap = {
            ArrowUp:    'up',
            KeyW:       'up',
            ArrowLeft:  'left',
            KeyA:       'left',
            ArrowRight: 'right',
            KeyD:       'right'
        };

        window.addEventListener('keydown', (e) => {
            if (!this.enabled || e.repeat) return;
            // 入力要素フォーカス中は無効
            const tag = document.activeElement?.tagName;
            if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;

            const dir = keyMap[e.code];
            if (dir) {
                this.keyDirs.add(dir);
                this.updateState();
            }
        });

        window.addEventListener('keyup', (e) => {
            const dir = keyMap[e.code];
            if (dir && this.keyDirs.has(dir)) {
                this.keyDirs.delete(dir);
                this.updateState();
            }
        });

        window.addEventListener('blur', () => {
            this.reset();
        });
    }

    updateState() {
        const activeDirs = new Set([...this.pointerDirs.values(), ...this.keyDirs]);
        this.deadman = this.enabled && activeDirs.size > 0;

        // UIボタンの押下表示を更新
        Object.entries(this.buttons).forEach(([dir, btn]) => {
            btn.classList.toggle('active', activeDirs.has(dir));
        });

        if (!this.enabled) {
            this.throttle = 0;
            this.steering = 0;
            this.deadman = false;
            return;
        }

        const up    = activeDirs.has('up');
        const left  = activeDirs.has('left');
        const right = activeDirs.has('right');

        this.throttle = up ? 1.0 : 0.0;
        this.steering = (right && !left) ? 1.0 : (left && !right) ? -1.0 : 0.0;
    }

    setEnabled(enabled) {
        this.enabled = enabled;
        if (!enabled) this.reset();
    }

    reset() {
        this.pointerDirs.clear();
        this.keyDirs.clear();
        this.throttle = 0;
        this.steering = 0;
        this.deadman = false;
        Object.values(this.buttons).forEach(btn => btn.classList.remove('active'));
    }

    getThrottle() { return this.enabled ? this.throttle : 0; }
    getSteering() { return this.enabled ? this.steering : 0; }
    getDeadman() { return this.enabled && this.deadman; }
}
