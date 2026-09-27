"""Probe the RA8P1 PCDC port and summarize raw traffic."""

from __future__ import annotations

import argparse
import struct
import time
from pathlib import Path

import serial

MAGIC = b"RA8P"
HEADER_SIZE = 18
FORMAT_RGB565_LE = 1
MAX_PAYLOAD_BYTES = 2_000_000


def find_complete_frame(data: bytearray) -> tuple[int, int, int, int, bytes] | None:
    offset = data.find(MAGIC)
    if offset < 0 or len(data) < offset + HEADER_SIZE:
        return None

    version, frame_format, width, height, payload_length, frame_id = struct.unpack_from(
        "<BBHHII", data, offset + len(MAGIC)
    )
    expected_length = width * height * 2
    if (
        version != 1
        or frame_format != FORMAT_RGB565_LE
        or payload_length != expected_length
        or payload_length > MAX_PAYLOAD_BYTES
    ):
        del data[: offset + 1]
        return None

    frame_end = offset + HEADER_SIZE + payload_length
    if len(data) < frame_end:
        return None

    return width, height, frame_id, offset, bytes(data[offset + HEADER_SIZE : frame_end])


def save_rgb565_png(path: Path, width: int, height: int, payload: bytes) -> None:
    from PIL import Image

    rgb = bytearray(width * height * 3)
    output_index = 0
    for input_index in range(0, len(payload), 2):
        value = payload[input_index] | (payload[input_index + 1] << 8)
        rgb[output_index] = (((value >> 11) & 0x1F) * 255) // 31
        rgb[output_index + 1] = (((value >> 5) & 0x3F) * 255) // 63
        rgb[output_index + 2] = ((value & 0x1F) * 255) // 31
        output_index += 3

    Image.frombytes("RGB", (width, height), bytes(rgb)).save(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--frame-number", type=int, default=1, help="Capture this frame number after opening the port")
    parser.add_argument("--output", type=Path, help="Save the selected complete frame as a PNG")
    args = parser.parse_args()

    deadline: float | None = None
    received = bytearray()
    total_received = 0
    port: serial.Serial | None = None
    next_command_time = 0.0
    next_progress_size = 65536
    frames_seen = 0
    frame: tuple[int, int, int, int, bytes] | None = None

    print(f"Monitoring {args.port} for {args.seconds:.0f} seconds. Press RESET now.", flush=True)

    while deadline is None or time.monotonic() < deadline:
        if port is None:
            try:
                port = serial.Serial(args.port, 921600, timeout=0.2)
                port.dtr = True
                port.rts = False
                time.sleep(0.25)
                deadline = time.monotonic() + max(args.seconds, 1.0)
                next_command_time = time.monotonic()
                print("PCDC port opened", flush=True)
            except serial.SerialException:
                time.sleep(0.25)
                continue

        try:
            if time.monotonic() >= next_command_time:
                port.write(b"q\r")
                port.flush()
                next_command_time = float("inf")

            chunk = port.read(4096)
            if chunk:
                received.extend(chunk)
                total_received += len(chunk)
                if total_received >= next_progress_size:
                    print(f"received {total_received} bytes", flush=True)
                    next_progress_size += 65536
                frame = find_complete_frame(received)
                if frame is not None:
                    frames_seen += 1
                    if frames_seen >= max(args.frame_number, 1):
                        break

                    _, _, _, offset, payload = frame
                    del received[: offset + HEADER_SIZE + len(payload)]
                    frame = None
        except serial.SerialException as exc:
            print(f"PCDC disconnected: {exc}", flush=True)
            try:
                port.close()
            except serial.SerialException:
                pass
            port = None
            time.sleep(0.25)

    if port is not None:
        try:
            port.cancel_read()
            port.cancel_write()
        except (AttributeError, serial.SerialException):
            pass
        port.close()

    print(f"total_bytes={total_received}")
    print(f"frame_magic_offset={received.find(MAGIC)}")
    print(f"first_128_hex={bytes(received[:128]).hex(' ')}")
    print(f"first_256_raw={bytes(received[:256])!r}")

    if frame is None:
        print("complete_frame=not_found")
        return 1

    width, height, frame_id, offset, payload = frame
    print(
        f"complete_frame=number:{frames_seen} id:{frame_id} "
        f"size:{width}x{height} payload:{len(payload)} offset:{offset}"
    )
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        save_rgb565_png(args.output, width, height, payload)
        print(f"saved_png={args.output.resolve()}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
