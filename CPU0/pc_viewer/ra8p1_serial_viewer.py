"""Display RA8P1 pseudo-segmentation and road-navigation frames over USB PCDC."""

from __future__ import annotations

import argparse
import queue
import struct
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    print("pyserial is required. Install with: python -m pip install -r requirements.txt")
    raise SystemExit(1) from exc

try:
    from PIL import Image, ImageDraw, ImageTk
except ImportError as exc:
    print("Pillow is required. Install with: python -m pip install -r requirements.txt")
    raise SystemExit(1) from exc


FRAME_MAGIC = b"RA8P"
AI_MAGIC = b"RAAI"
NAV_MAGIC = b"RANV"
FRAME_HEADER_REST_SIZE = 14
AI_HEADER_REST_SIZE = 12
AI_RECORD_SIZE = 12
NAV_HEADER_REST_SIZE = 24
NAV_RECORD_SIZE = 8
FORMAT_RGB565_LE = 1
MAX_PAYLOAD_BYTES = 2_000_000
MAX_AI_RECORD_BYTES = 512
MAX_NAV_RECORD_BYTES = 256
STOP_REASON_NAMES = {
    0: "DRIVE",
    1: "ROAD NOT FOUND",
    2: "ROAD TOO NARROW",
    3: "AI OBJECT AHEAD",
    4: "PATH BLOCKED",
    5: "INVALID INPUT",
}
STREAM_COMMANDS = {
    "1024x600": "f",
    "VGA": "v",
    "QVGA": "q",
}
DETECTION_CLASS_NAMES = {
    0: "person",
    1: "bicycle",
    2: "car",
    3: "motorcycle",
}
DETECTION_CLASS_COLORS = {
    0: "#008cff",
    1: "#008cff",
    2: "#008cff",
    3: "#008cff",
}


def drain_commands(port: serial.Serial, command_queue: queue.Queue) -> None:
    while True:
        try:
            command = command_queue.get_nowait()
        except queue.Empty:
            return
        port.write(command)
        port.flush()


def read_exact(
    port: serial.Serial,
    size: int,
    stop_event: threading.Event,
    command_queue: queue.Queue,
) -> bytes | None:
    data = bytearray()
    while len(data) < size and not stop_event.is_set():
        drain_commands(port, command_queue)
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
    if len(data) != size:
        return None
    return bytes(data)


def wait_for_packet_magic(
    port: serial.Serial,
    stop_event: threading.Event,
    command_queue: queue.Queue,
    status_queue: queue.Queue,
    motor_queue: queue.Queue,
) -> bytes | None:
    window = bytearray()
    line_buffer = bytearray()
    bytes_seen = 0
    last_report = time.monotonic()
    while not stop_event.is_set():
        drain_commands(port, command_queue)
        byte = port.read(1)
        if not byte:
            if time.monotonic() - last_report >= 5.0:
                port.write(b"q\r")
                port.flush()
                status_queue.put(f"Connected; retrying QVGA image request ({bytes_seen} bytes received)")
                last_report = time.monotonic()
            continue
        bytes_seen += len(byte)
        if byte == b"\n":
            line = bytes(line_buffer).strip()
            line_buffer.clear()
            motor = parse_motor_line(line)
            if motor is not None:
                put_latest_value(motor_queue, motor)
        elif byte != b"\r":
            line_buffer += byte
            if len(line_buffer) > 512:
                line_buffer.clear()
        window += byte
        if len(window) > len(FRAME_MAGIC):
            del window[0]
        magic = bytes(window)
        if magic in (FRAME_MAGIC, AI_MAGIC, NAV_MAGIC):
            return magic
    return None


def parse_motor_line(line: bytes) -> dict | None:
    if not line.startswith(b"SIGNAL,"):
        return None

    try:
        fields = line.decode("ascii").split(",")[1:]
        values = dict(field.split("=", 1) for field in fields)
        return {
            "sequence": int(values["seq"]),
            "state": values["state"],
            "reason": values["reason"],
            "enabled": bool(int(values["enable"])),
            "left": int(values["left_pwm"]) / 1000.0,
            "right": int(values["right_pwm"]) / 1000.0,
            "steering": int(values["steer"]) / 1000.0,
            "speed": int(values["speed"]) / 1000.0,
            "risk": int(values["risk"]) / 1000.0,
            "stop_action": values["stop"],
            "distance_mm": int(values["tof_mm"]),
            "tof_valid": bool(int(values["tof_valid"])),
            "yolo_count": int(values["yolo_count"]),
            "person_count": int(values.get("person", "0")),
            "bicycle_count": int(values.get("bicycle", "0")),
            "car_count": int(values.get("car", "0")),
            "motorcycle_count": int(values.get("motorcycle", "0")),
            "white_ratio": int(values["white"]) / 1000.0,
            "brown_ratio": int(values["brown"]) / 1000.0,
            "ai_sequence": int(values["ai_seq"]),
            "tof_sequence": int(values["tof_seq"]),
            "physical": bool(int(values["physical"])),
        }
    except (UnicodeDecodeError, ValueError, KeyError):
        return None


def rgb565le_to_rgb(data: bytes) -> bytes:
    out = bytearray((len(data) // 2) * 3)
    out_index = 0

    for index in range(0, len(data), 2):
        value = data[index] | (data[index + 1] << 8)
        red = (value >> 11) & 0x1F
        green = (value >> 5) & 0x3F
        blue = value & 0x1F

        out[out_index] = (red * 255) // 31
        out[out_index + 1] = (green * 255) // 63
        out[out_index + 2] = (blue * 255) // 31
        out_index += 3

    return bytes(out)


def annotate_detections(image: Image.Image, ai_result: dict | None) -> Image.Image:
    annotated = image.copy()
    if ai_result is None:
        return annotated

    draw = ImageDraw.Draw(annotated)
    for detection in ai_result["detections"]:
        class_id = detection["class_id"]
        class_name = DETECTION_CLASS_NAMES.get(class_id, f"class {class_id}")
        color = DETECTION_CLASS_COLORS.get(class_id, "#ffffff")
        left = detection["x"]
        top = detection["y"]
        right = left + detection["width"] - 1
        bottom = top + detection["height"] - 1
        draw.rectangle((left, top, right, bottom), outline=color, width=3)

        label = f"{class_name} {detection['score']:.0%}"
        text_box = draw.textbbox((left, top), label)
        text_height = text_box[3] - text_box[1]
        label_top = max(0, top - text_height - 3)
        label_box = draw.textbbox((left + 2, label_top + 1), label)
        draw.rectangle(
            (left, label_top, label_box[2] + 2, label_box[3] + 1),
            fill="#111111",
        )
        draw.text((left + 2, label_top + 1), label, fill=color)

    return annotated


def put_latest(
    frame_queue: queue.Queue,
    item: tuple[int, Image.Image, dict | None, dict | None],
) -> None:
    try:
        frame_queue.put_nowait(item)
    except queue.Full:
        try:
            frame_queue.get_nowait()
        except queue.Empty:
            pass
        frame_queue.put_nowait(item)


def put_latest_value(value_queue: queue.Queue, item: object) -> None:
    try:
        value_queue.put_nowait(item)
    except queue.Full:
        try:
            value_queue.get_nowait()
        except queue.Empty:
            pass
        value_queue.put_nowait(item)


def serial_worker(
    port_name: str,
    baud_rate: int,
    frame_queue: queue.Queue,
    status_queue: queue.Queue,
    motor_queue: queue.Queue,
    command_queue: queue.Queue,
    stop_event: threading.Event,
) -> None:
    try:
        with serial.Serial(port_name, baud_rate, timeout=0.2) as port:
            port.dtr = True
            port.rts = False
            time.sleep(0.25)
            port.write(b"q\r")
            port.flush()
            status_queue.put(f"Connected to {port_name}; requesting periodic QVGA images")
            pending_ai: dict[int, dict] = {}
            pending_navigation: dict[int, dict] = {}

            while not stop_event.is_set():
                magic = wait_for_packet_magic(
                    port,
                    stop_event,
                    command_queue,
                    status_queue,
                    motor_queue,
                )
                if magic is None:
                    break

                if magic == AI_MAGIC:
                    rest = read_exact(port, AI_HEADER_REST_SIZE, stop_event, command_queue)
                    if rest is None:
                        break

                    version, count, record_bytes, inference_us, frame_id = struct.unpack("<BBHII", rest)
                    if record_bytes > MAX_AI_RECORD_BYTES:
                        status_queue.put("Skipping invalid AI metadata size")
                        continue

                    records = read_exact(port, record_bytes, stop_event, command_queue)
                    if records is None:
                        break

                    if version != 1 or record_bytes != count * AI_RECORD_SIZE:
                        status_queue.put("Skipping invalid AI metadata")
                        continue

                    detections = []
                    for offset in range(0, record_bytes, AI_RECORD_SIZE):
                        x, y, width, height, score_milli, class_id = struct.unpack_from(
                            "<HHHHHH", records, offset
                        )
                        detections.append(
                            {
                                "x": x,
                                "y": y,
                                "width": width,
                                "height": height,
                                "score": score_milli / 1000.0,
                                "class_id": class_id,
                            }
                        )

                    pending_ai[frame_id] = {
                        "inference_us": inference_us,
                        "detections": detections,
                    }
                    if len(pending_ai) > 16:
                        del pending_ai[min(pending_ai)]
                    continue

                if magic == NAV_MAGIC:
                    rest = read_exact(port, NAV_HEADER_REST_SIZE, stop_event, command_queue)
                    if rest is None:
                        break

                    (
                        version,
                        drive_allowed,
                        stop_reason,
                        path_count,
                        steering_cdeg,
                        throttle_milli,
                        road_confidence_milli,
                        obstacle_risk_milli,
                        target_x,
                        target_y,
                        frame_id,
                        record_bytes,
                        _reserved,
                    ) = struct.unpack("<BBBBhHHHHHIHH", rest)

                    if record_bytes > MAX_NAV_RECORD_BYTES:
                        status_queue.put("Skipping invalid navigation metadata size")
                        continue

                    records = read_exact(port, record_bytes, stop_event, command_queue)
                    if records is None:
                        break

                    if version != 1 or record_bytes != path_count * NAV_RECORD_SIZE:
                        status_queue.put("Skipping invalid navigation metadata")
                        continue

                    path = []
                    for offset in range(0, record_bytes, NAV_RECORD_SIZE):
                        y, left, center, right = struct.unpack_from("<HHHH", records, offset)
                        path.append(
                            {
                                "y": y,
                                "left": left,
                                "center": center,
                                "right": right,
                            }
                        )

                    pending_navigation[frame_id] = {
                        "drive_allowed": bool(drive_allowed),
                        "stop_reason": stop_reason,
                        "steering_deg": steering_cdeg / 100.0,
                        "throttle": throttle_milli / 1000.0,
                        "road_confidence": road_confidence_milli / 1000.0,
                        "obstacle_risk": obstacle_risk_milli / 1000.0,
                        "target_x": target_x,
                        "target_y": target_y,
                        "path": path,
                    }
                    if len(pending_navigation) > 16:
                        del pending_navigation[min(pending_navigation)]
                    continue

                rest = read_exact(port, FRAME_HEADER_REST_SIZE, stop_event, command_queue)
                if rest is None:
                    break

                version, frame_format, width, height, payload_length, frame_id = struct.unpack("<BBHHII", rest)
                expected_length = int(width) * int(height) * 2

                if version != 1 or frame_format != FORMAT_RGB565_LE:
                    status_queue.put("Skipping unsupported frame format")
                    continue

                if payload_length != expected_length or payload_length > MAX_PAYLOAD_BYTES:
                    status_queue.put("Skipping invalid frame size")
                    continue

                payload = read_exact(port, payload_length, stop_event, command_queue)
                if payload is None:
                    break

                rgb = rgb565le_to_rgb(payload)
                image = Image.frombytes("RGB", (width, height), rgb)
                put_latest(
                    frame_queue,
                    (
                        frame_id,
                        image,
                        pending_ai.pop(frame_id, None),
                        pending_navigation.pop(frame_id, None),
                    ),
                )
    except serial.SerialException as exc:
        status_queue.put(f"Serial error: {exc}")
    except Exception as exc:  # Keep the UI open so the message is visible.
        status_queue.put(f"Viewer error: {exc}")


class Viewer:
    def __init__(self, port_name: str, baud_rate: int, scale: int) -> None:
        self.root = tk.Tk()
        self.root.title("RA8P1 Autonomous Motor Controller")
        self.root.protocol("WM_DELETE_WINDOW", self.close)

        self.scale = scale
        self.frame_queue: queue.Queue[
            tuple[int, Image.Image, dict | None, dict | None]
        ] = queue.Queue(maxsize=1)
        self.status_queue: queue.Queue[str] = queue.Queue()
        self.motor_queue: queue.Queue[dict] = queue.Queue(maxsize=1)
        self.command_queue: queue.Queue[bytes] = queue.Queue()
        self.stop_event = threading.Event()
        self.photo: ImageTk.PhotoImage | None = None
        self.last_time = time.monotonic()
        self.frame_count = 0
        self.stream_command = STREAM_COMMANDS["QVGA"]

        self.image_label = ttk.Label(self.root, anchor="center")
        self.image_label.pack(fill="both", expand=True, padx=8, pady=8)

        controls = ttk.Frame(self.root)
        controls.pack(fill="x", padx=8, pady=(0, 8))

        ttk.Button(controls, text="1024x600", command=lambda: self.select_resolution("1024x600")).pack(side="left")
        ttk.Button(controls, text="VGA", command=lambda: self.select_resolution("VGA")).pack(side="left", padx=(6, 0))
        ttk.Button(controls, text="QVGA", command=lambda: self.select_resolution("QVGA")).pack(side="left", padx=(6, 0))
        ttk.Button(controls, text="Start Images", command=self.start_stream).pack(side="left", padx=(18, 0))
        ttk.Button(controls, text="Stop/Back", command=lambda: self.send_command("0")).pack(side="left", padx=(6, 0))

        self.status_var = tk.StringVar(value="Opening serial port...")
        self.status_label = ttk.Label(self.root, textvariable=self.status_var, anchor="w")
        self.status_label.pack(fill="x", padx=8, pady=(0, 8))

        self.motor_var = tk.StringVar(value="Motor: waiting for controller diagnostics")
        self.motor_label = ttk.Label(
            self.root, textvariable=self.motor_var, anchor="w", justify="left"
        )
        self.motor_label.pack(fill="x", padx=8, pady=(0, 8))

        self.worker = threading.Thread(
            target=serial_worker,
            args=(
                port_name,
                baud_rate,
                self.frame_queue,
                self.status_queue,
                self.motor_queue,
                self.command_queue,
                self.stop_event,
            ),
            daemon=True,
        )
        self.worker.start()

        self.root.after(30, self.poll)

    def poll(self) -> None:
        while True:
            try:
                self.status_var.set(self.status_queue.get_nowait())
            except queue.Empty:
                break

        latest_motor = None
        while True:
            try:
                latest_motor = self.motor_queue.get_nowait()
            except queue.Empty:
                break

        if latest_motor is not None:
            output_mode = "PWM ACTIVE" if latest_motor["physical"] else "DRY RUN"
            enable = "ENABLE" if latest_motor["enabled"] else "STOP"
            distance = (
                f"{latest_motor['distance_mm']} mm"
                if latest_motor["tof_valid"]
                else "invalid"
            )
            self.motor_var.set(
                f"Motor {output_mode}  {enable}  {latest_motor['state']} / "
                f"{latest_motor['reason']}  {latest_motor['stop_action']}  "
                f"L {latest_motor['left']:+.3f}  "
                f"R {latest_motor['right']:+.3f}  "
                f"Steer {latest_motor['steering']:+.3f}  "
                f"Speed {latest_motor['speed']:.3f}  Risk {latest_motor['risk']:.3f}  "
                f"ToF {distance}\n"
                f"YOLO {latest_motor['yolo_count']}  "
                f"P {latest_motor['person_count']}  B {latest_motor['bicycle_count']}  "
                f"C {latest_motor['car_count']}  M {latest_motor['motorcycle_count']}  "
                f"White road {latest_motor['white_ratio'] * 100:.1f}%  "
                f"Brown wall {latest_motor['brown_ratio'] * 100:.1f}%"
            )

        latest: tuple[int, Image.Image, dict | None, dict | None] | None = None
        while True:
            try:
                latest = self.frame_queue.get_nowait()
            except queue.Empty:
                break

        if latest is not None:
            frame_id, image, ai_result, navigation = latest
            display_image = annotate_detections(image, ai_result)
            if self.scale > 1:
                resampling = getattr(Image, "Resampling", Image).NEAREST
                display_image = image.resize((image.width * self.scale, image.height * self.scale), resampling)

            self.photo = ImageTk.PhotoImage(display_image)
            self.image_label.configure(image=self.photo)

            self.frame_count += 1
            now = time.monotonic()
            elapsed = max(now - self.last_time, 0.001)
            images_per_second = self.frame_count / elapsed
            if ai_result is None:
                ai_status = "AI metadata unavailable"
            else:
                counts = {name: 0 for name in DETECTION_CLASS_NAMES.values()}
                for detection in ai_result["detections"]:
                    name = DETECTION_CLASS_NAMES.get(detection["class_id"])
                    if name is not None:
                        counts[name] += 1
                inference_ms = ai_result["inference_us"] / 1000.0
                ai_status = (
                    f"person {counts['person']}  bicycle {counts['bicycle']}  "
                    f"car {counts['car']}  motorcycle {counts['motorcycle']}  "
                    f"AI: {inference_ms:.2f} ms"
                )

            if navigation is None:
                navigation_status = "Navigation metadata unavailable"
            else:
                reason = STOP_REASON_NAMES.get(navigation["stop_reason"], "UNKNOWN")
                state = "DRIVE" if navigation["drive_allowed"] else f"STOP ({reason})"
                navigation_status = (
                    f"{state}  Steering: {navigation['steering_deg']:+.1f} deg  "
                    f"Throttle: {navigation['throttle'] * 100:.0f}%  "
                    f"Road: {navigation['road_confidence'] * 100:.0f}%  "
                    f"Risk: {navigation['obstacle_risk'] * 100:.0f}%"
                )
            self.status_var.set(
                f"Image {frame_id}  {image.width}x{image.height}  {ai_status}  "
                f"{navigation_status}  {images_per_second:.2f} images/s"
            )

            if elapsed >= 5.0:
                self.last_time = now
                self.frame_count = 0

        if not self.stop_event.is_set():
            self.root.after(30, self.poll)

    def close(self) -> None:
        self.stop_event.set()
        self.root.after(100, self.root.destroy)

    def send_command(self, value: str) -> None:
        self.command_queue.put(value.encode("ascii") + b"\r")
        self.status_var.set(f"Sent command {value}")

    def select_resolution(self, name: str) -> None:
        self.stream_command = STREAM_COMMANDS[name]
        self.send_command(self.stream_command)
        self.status_var.set(f"Requesting periodic {name} images")

    def start_stream(self) -> None:
        self.send_command(self.stream_command)
        self.status_var.set("Requesting periodic images")

    def run(self) -> None:
        self.root.mainloop()


def list_available_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    for item in ports:
        print(f"{item.device}\t{item.description}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="RA8P1 OV5640 road-navigation USB PCDC viewer")
    parser.add_argument("--port", help="PCDC COM port, for example COM5")
    parser.add_argument("--baud", type=int, default=921600, help="CDC line coding value; transfer uses USB, not this rate")
    parser.add_argument("--scale", type=int, default=2, help="Integer display scale")
    parser.add_argument("--list-ports", action="store_true", help="List available serial ports and exit")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.list_ports:
        list_available_ports()
        return 0

    if not args.port:
        print("Specify --port, or run with --list-ports first.")
        return 2

    viewer = Viewer(args.port, args.baud, max(args.scale, 1))
    viewer.run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
