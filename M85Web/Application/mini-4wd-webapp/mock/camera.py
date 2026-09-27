"""
モック用擬似カメラプロバイダ (MockCameraProvider) 数式ベースでコース白線や障害物を描画したPNG画像を動的に生成
"""
import struct
import zlib
from server.camera_base import BaseCameraProvider
from server.controller  import VehicleController

class MockCameraProvider(BaseCameraProvider):
    def __init__(self, controller: VehicleController, width: int = 320, height: int = 180):
        self.controller     = controller
        self.w              = width
        self.h              = height
        self.road_offset    = 0.0
        self.lateral_offset = 0.0

    def get_frame(self) -> bytes:
        w, h               = self.w, self.h
        state              = self.controller.get_telemetry()
        throttle, steering = self.controller.get_motor_output()

        mode  = state.get("mode", "MANUAL")
        dist  = state.get("front_distance_mm", 1200)
        speed = 0.8 if mode == "AUTO" else (throttle if mode == "MANUAL" else 0.0)
        steer = 0.0 if mode != "MANUAL" else steering

        self.road_offset     = (self.road_offset + speed * 6.0) % 40.0
        self.lateral_offset += (steer * 20.0 - self.lateral_offset) * 0.15

        horizon_y = int(h * 0.48)
        vanish_x  = int(w / 2 + self.lateral_offset)
        raw       = bytearray((w + 1) * h)

        for y in range(h):
            row = y * (w + 1)
            raw[row] = 0  # PNG filter type 0 (None)
            if y < horizon_y:
                for x in range(w):
                    raw[row + 1 + x] = 245
            elif y == horizon_y:
                for x in range(w):
                    raw[row + 1 + x] = 190
            else:
                ratio           = (y - horizon_y) / (h - horizon_y)
                track_w         = int(24 + ratio * (int(w * 0.78) - 24))
                cur_vanish      = int(vanish_x * (1.0 - ratio) + (w / 2) * ratio)
                left_x, right_x = cur_vanish - track_w // 2, cur_vanish + track_w // 2

                for x in range(w):
                    val = 210 if left_x <= x <= right_x else 230
                    if abs(x - left_x) <= 2 or abs(x - right_x) <= 2:
                        val = 40 if (((int(y + self.road_offset)) // 6) % 2 == 0) else 255
                    elif abs(x - cur_vanish) <= 1 and ((int(y + self.road_offset)) // 10) % 2 == 0:
                        val = 60
                    raw[row + 1 + x] = val

        def chunk(tag: bytes, data: bytes) -> bytes:
            return struct.pack("!I4s", len(data), tag) + data + struct.pack("!I", zlib.crc32(tag + data))

        ihdr = struct.pack("!IIBBBBB", w, h, 8, 0, 0, 0, 0)
        idat = zlib.compress(bytes(raw), 1)
        return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")
