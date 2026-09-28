import { InputController } from './input.js';
import { UIManager       } from './ui.js';
import { CommManager     } from './comm.js';
import { StateMachine    } from './state-machine.js';
import { installSelectionGuard, installLandscape, installZoomGuard } from './screen.js';

export class Mini4WDApp {
    constructor() {
        this.input = new InputController();

        this.ui = new UIManager({
            onDriveModeClick:   () => this.stateMachine.requestDriveModeToggle(),
            onTorTakeoverClick: () => this.stateMachine.requestTorTakeover()
        });

        this.stateMachine = new StateMachine(this);

        this.comm = new CommManager({
            nextRequest:  ()              => this.stateMachine.nextRequest(),
            onHeartbeat:  (data, context) => this.stateMachine.handleHeartbeat(data, context),
            onConnect:    ()              => this.stateMachine.handleConnect(),
            onDisconnect: ()              => this.stateMachine.handleDisconnect()
        });
    }
}

const initApp = () => {
    if (window.app) return;
    installSelectionGuard();
    installZoomGuard();
    installLandscape();
    window.app = new Mini4WDApp();
};
if   (document.readyState === 'loading') window.addEventListener('DOMContentLoaded', initApp);
else                                     initApp();
