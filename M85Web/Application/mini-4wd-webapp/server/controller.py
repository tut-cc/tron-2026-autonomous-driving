"""
車両制御コア (VehicleController)
マイコン実機・モック共通の Source of Truth (状態管理) & 安全判定ロジック
"""
import time
from typing     import Callable, Optional
from .constants import MCUMode, StopReason, RejectReason, DEADMAN_TIMEOUT_SEC, COMM_TIMEOUT_SEC

class VehicleController:
    def __init__(self, on_motor_update: Optional[Callable[[float, float], None]] = None):
        """
        :param on_motor_update: モーター出力更新時コールバック (throttle, steering)
        """
        self.on_motor_update = on_motor_update

        # 車両の確定状態 (Heartbeatとして配信)
        self.state = {
            "mode":                  MCUMode.MANUAL,
            "front_distance_mm":     1200,
            "tor_active":            False,
            "tor_remaining_ms":      0,
            "stop_reason":           StopReason.NONE,
            "request_reject_reason": RejectReason.NONE,
        }

        # 出力指示値
        self.throttle          = 0.0
        self.steering          = 0.0
        self.last_command_time = None

    def get_telemetry(self) -> dict:
        """WebAppに配信する最新状態辞書"""
        return dict(self.state)

    def get_motor_output(self) -> tuple[float, float]:
        """現在の実質出力 (throttle, steering)"""
        return self.throttle, self.steering

    def update_sensor(self, front_distance_mm: int):
        """測距センサー値の更新 (実機ToF / 超音波から呼出)"""
        self.state["front_distance_mm"] = front_distance_mm

        # AUTO走行中の前方障害物自動検知 -> 自律安全中断
        if self.state["mode"] == MCUMode.AUTO and front_distance_mm < 100:
            self.trigger_auto_abort(StopReason.OBSTACLE)

    def trigger_manual_abort(self, reason: str = StopReason.MANUAL_ABORT_BUTTON):
        """手動中断 (最優先)"""
        self.state["mode"                 ] = MCUMode.MANUAL_ABORT
        self.state["stop_reason"          ] = reason
        self.state["tor_active"           ] = False
        self.state["tor_remaining_ms"     ] = 0
        self.state["request_reject_reason"] = RejectReason.NONE
        self._apply_motor(0.0, 0.0)

    def trigger_auto_abort(self, reason: str = StopReason.OBSTACLE):
        """自律安全中断"""
        self.state["mode"            ] = MCUMode.AUTO_ABORT
        self.state["stop_reason"     ] = reason
        self.state["tor_active"      ] = False
        self.state["tor_remaining_ms"] = 0
        self._apply_motor(0.0, 0.0)

    def trigger_tor(self, duration_ms: int = 3000):
        """TOR (運転引継ぎ要求) 発動"""
        if self.state["mode"] == MCUMode.AUTO:
            self.state["tor_active"      ] = True
            self.state["tor_remaining_ms"] = duration_ms

    def reset_abort(self) -> bool:
        """中断状態 (AUTO_ABORT / MANUAL_ABORT) からの復帰要求"""
        if self.state["mode"] not in (MCUMode.AUTO_ABORT, MCUMode.MANUAL_ABORT):
            return False

        if self.state["front_distance_mm"] > 200:
            self.state["mode"                 ] = MCUMode.MANUAL
            self.state["stop_reason"          ] = StopReason.NONE
            self.state["request_reject_reason"] = RejectReason.NONE
            self.state["tor_active"           ] = False
            self.state["tor_remaining_ms"     ] = 0
            self._apply_motor(0.0, 0.0)
            if self.last_command_time is not None:
                self.last_command_time = time.monotonic()
            return True
        else:
            self.state["request_reject_reason"] = RejectReason.OBSTACLE_NEAR
            return False

    def request_mode(self, target_mode: str) -> bool:
        """モード切替要求の処理"""
        current_mode = self.state["mode"]

        # 中断中の切替要求は拒否 (要RESET)
        if current_mode == MCUMode.MANUAL_ABORT:
            self.state["request_reject_reason"] = RejectReason.IN_MANUAL_ABORT
            return False
        if current_mode == MCUMode.AUTO_ABORT:
            self.state["request_reject_reason"] = RejectReason.MODE_MISMATCH
            return False

        if target_mode == MCUMode.AUTO:
            if self.state["tor_active"]:
                self.state["request_reject_reason"] = RejectReason.IN_TOR
                return False
            if current_mode == MCUMode.MANUAL:
                if self.state["front_distance_mm"] > 300:
                    self.state["mode"                 ] = MCUMode.AUTO
                    self.state["request_reject_reason"] = RejectReason.NONE
                    self._apply_motor(0.0, 0.0)
                    return True
                else:
                    self.state["request_reject_reason"] = RejectReason.OBSTACLE_NEAR
                    return False
            else:
                self.state["request_reject_reason"] = RejectReason.MODE_MISMATCH
                return False

        elif target_mode == MCUMode.MANUAL:
            # AUTOやTOR等からMANUALへの手動介入 (即時受諾)
            self.state["mode"                 ] = MCUMode.MANUAL
            self.state["tor_active"           ] = False
            self.state["tor_remaining_ms"     ] = 0
            self.state["request_reject_reason"] = RejectReason.NONE
            return True

        return False

    def process_command(self, cmd: dict):
        """WebAppからの操作コマンド処理 (安全優先順位に準拠)"""
        if not isinstance(cmd, dict):
            return

        self.last_command_time = time.monotonic()
        client_mode            = cmd.get("client_mode")

        # 1. 手動中断要求 (最優先)
        if cmd.get("manual_abort_request"):
            self.trigger_manual_abort(StopReason.MANUAL_ABORT_BUTTON)
            return

        # 2. 中断解除 (RESET) 要求
        if cmd.get("reset_abort_request"):
            self.reset_abort()
            return

        # 3. モード切替要求
        mode_req = cmd.get("mode_request", "NONE")
        if mode_req in (MCUMode.AUTO, MCUMode.MANUAL):
            self.request_mode(mode_req)

        # 4. 手動走行指示 (MANUALモードかつ状態一致時のみ適用)
        if self.state["mode"] == MCUMode.MANUAL:
            if client_mode and client_mode != MCUMode.MANUAL:
                th, st = 0.0, 0.0
            else:
                try:
                    th = max(-1.0, min(1.0, float(cmd.get("throttle", 0.0))))
                    st = max(-1.0, min(1.0, float(cmd.get("steering", 0.0))))
                except (ValueError, TypeError):
                    th, st = 0.0, 0.0
            self._apply_motor(th, st)
        else:
            self._apply_motor(0.0, 0.0)

    def tick(self, dt_ms: int):
        """周期タイマー監視 (TORカウントダウン・通信途絶・デッドマン)"""
        # TOR カウントダウン (満了で AUTO_ABORT)
        if self.state["tor_active"] and self.state["tor_remaining_ms"] > 0:
            self.state["tor_remaining_ms"] = max(0, self.state["tor_remaining_ms"] - dt_ms)
            if self.state["tor_remaining_ms"] == 0:
                self.trigger_auto_abort(StopReason.TOR_TIMEOUT)

        # 通信途絶監視 (初回コマンド受信後、1.5秒間コマンド未受信で AUTO_ABORT)
        if self.last_command_time is not None:
            if self.state["mode"] not in (MCUMode.AUTO_ABORT, MCUMode.MANUAL_ABORT):
                if time.monotonic() - self.last_command_time > COMM_TIMEOUT_SEC:
                    self.trigger_auto_abort(StopReason.COMM_TIMEOUT)

            # デッドマン監視 (MANUAL走行中に指示途絶でモーター停止)
            if self.state["mode"] == MCUMode.MANUAL:
                if time.monotonic() - self.last_command_time > DEADMAN_TIMEOUT_SEC:
                    if self.throttle != 0.0 or self.steering != 0.0:
                        self._apply_motor(0.0, 0.0)

    def _apply_motor(self, throttle: float, steering: float):
        self.throttle = throttle
        self.steering = steering
        if self.on_motor_update:
            try:
                self.on_motor_update(throttle, steering)
            except Exception:
                pass
