import { InputController } from './input.js';
import { UIManager       } from './ui.js';
import { CommManager     } from './comm.js';
import { StateMachine    } from './state-machine.js';
import { DebugManager    } from './debug-ui.js';
import { installSelectionGuard, installLandscape } from './screen.js';

export class Mini4WDApp {
    constructor() {
        this.input = new InputController();

        this.ui = new UIManager({
            onDriveModeClick:   () => this.stateMachine.requestDriveModeToggle(),
            onStopClick:        () => this.stateMachine.requestAbortAction(),
            onTorTakeoverClick: () => this.stateMachine.requestTorTakeover()
        });

        this.stateMachine = new StateMachine(this);

        this.comm = new CommManager({
            nextRequest:  ()              => this.stateMachine.nextRequest(),
            onHeartbeat:  (data, context) => this.stateMachine.handleHeartbeat(data, context),
            onConnect:    ()              => this.stateMachine.handleConnect(),
            onDisconnect: ()              => this.stateMachine.handleDisconnect()
        });

        this.debug = new DebugManager();
    }
}

const initApp = () => {
    if (window.app) return;
    installSelectionGuard();
    installLandscape();
    window.app = new Mini4WDApp();
};
if   (document.readyState === 'loading') window.addEventListener('DOMContentLoaded', initApp);
else                                     initApp();
