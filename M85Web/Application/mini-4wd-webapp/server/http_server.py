"""
HTTP 統合非同期サーバー (標準ライブラリのみ)
REST API (/api/command, /api/telemetry) + MJPEGストリーミング + 静的アセット配信
HTTP/1.1 Keep-Alive (永続接続) による高頻度ポーリング対応
"""
import asyncio
import json
import os
from typing import Optional, Protocol

from .constants  import DEFAULT_HOST, DEFAULT_PORT, HEARTBEAT_INTERVAL_SEC
from .controller import VehicleController

MIME_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js":   "application/javascript; charset=utf-8",
    ".css":  "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".ico":  "image/x-icon",
}

class CameraProvider(Protocol):
    def get_frame(self) -> bytes: ...

class HttpServer:
    def __init__(
        self                                                                 ,
        controller:         VehicleController                                ,
        camera_provider:    Optional[CameraProvider] = None                  ,
        static_dir:         Optional[str]            = None                  ,
        host:               str                      = DEFAULT_HOST          ,
        port:               int                      = DEFAULT_PORT          ,
        heartbeat_interval: float                    = HEARTBEAT_INTERVAL_SEC,
        keepalive_timeout:  float                    = 10.0                  ,
    ):
        self.controller         = controller
        self.camera_provider    = camera_provider
        self.static_dir         = static_dir or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.host               = host
        self.port               = port
        self.heartbeat_interval = heartbeat_interval
        self.keepalive_timeout  = keepalive_timeout
        self._server            = None
        self._active_writers    = set()

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """HTTPリクエスト受付 (API / MJPEG / 静的配信 / Keep-Alive対応)"""
        self._active_writers.add(writer)
        buffer = bytearray()
        try:
            while True:
                # 1. ヘッダー終端 (b"\r\n\r\n") まで受信
                while b"\r\n\r\n" not in buffer:
                    if len(buffer) > 65536:
                        return
                    try:
                        chunk = await asyncio.wait_for(reader.read(4096), timeout=self.keepalive_timeout)
                    except asyncio.TimeoutError:
                        return
                    if not chunk:
                        return
                    buffer.extend(chunk)

                header_part, _, rest = buffer.partition(b"\r\n\r\n")
                lines                = header_part.decode("utf-8", errors="ignore").split("\r\n")
                parts                = lines[0].split(" ") if lines else []
                method               = parts[0].upper()    if parts else "GET"
                path                 = (parts[1] if len(parts) > 1 else "/").split("?")[0]
                version              = parts[2].upper()    if len(parts) > 2 else "HTTP/1.1"

                # 必要なヘッダーのみ抽出 (KISS: 全辞書化を廃止しメモリ・処理を節約)
                content_length = 0
                keep_alive = (version != "HTTP/1.0")
                for line in lines[1:]:
                    low = line.lower()
                    if low.startswith("content-length:"):
                        try:
                            content_length = max(0, int(line.split(":", 1)[1].strip()))
                        except (ValueError, IndexError):
                            content_length = 0
                    elif low.startswith("connection:"):
                        val = line.split(":", 1)[1].strip().lower()
                        if val == "close":
                            keep_alive = False
                        elif val == "keep-alive":
                            keep_alive = True

                # 2. ボディの読み込み (Content-Length に応じて)
                while len(rest) < content_length:
                    try:
                        chunk = await asyncio.wait_for(reader.read(min(4096, content_length - len(rest))), timeout=self.keepalive_timeout)
                    except asyncio.TimeoutError:
                        return
                    if not chunk:
                        return
                    rest.extend(chunk)

                body   = bytes(rest[:content_length])
                buffer = bytearray(rest[content_length:])

                conn_header_str = "Connection: keep-alive\r\nKeep-Alive: timeout=10\r\n" if keep_alive else "Connection: close\r\n"

                # CORS プリフライト対応 (デバッグ・別オリジン開発用)
                if method == "OPTIONS":
                    writer.write(
                        b"HTTP/1.1 204 No Content\r\n"
                        b"Access-Control-Allow-Origin: *\r\n"
                        b"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                        b"Access-Control-Allow-Headers: Content-Type\r\n"
                        + conn_header_str.encode("utf-8")
                        + b"\r\n"
                    )
                    await writer.drain()
                    if not keep_alive:
                        return
                    continue

                # 1. 制御 & テレメトリ API (/api/command, /api/telemetry)
                if path in ("/api/command", "/api/telemetry"):
                    if method == "POST" and body:
                        try:
                            cmd = json.loads(body.decode("utf-8", errors="ignore"))
                            self.controller.process_command(cmd)
                        except Exception:
                            self.controller.process_command({})

                    telemetry  = self.controller.get_telemetry()
                    resp_bytes = json.dumps(telemetry).encode("utf-8")
                    header     = (
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json; charset=utf-8\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                        f"{conn_header_str}"
                        f"Content-Length: {len(resp_bytes)}\r\n\r\n"
                    ).encode("utf-8")
                    writer.write(header + resp_bytes)
                    await writer.drain()
                    if not keep_alive:
                        return
                    continue

                # 3. カメラ MJPEG ストリーミング (/video_feed)
                if path == "/video_feed":
                    await self._handle_mjpeg_stream(writer)
                    return

                # 4. 静的Webアセット配信 (index.html, JS, CSS)
                await self._handle_static_file(writer, path, conn_header_str)
                if not keep_alive:
                    return
                continue

        except Exception:
            pass
        finally:
            self._active_writers.discard(writer)
            try:
                writer.close()
                await asyncio.wait_for(writer.wait_closed(), timeout=2.0)
            except Exception:
                pass

    async def _handle_mjpeg_stream(self, writer: asyncio.StreamWriter):
        """MJPEG カメラストリーム配信 (/video_feed)"""
        if not self.camera_provider:
            body   = b"Camera not configured"
            header = (
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Connection: close\r\n"
                f"Content-Length: {len(body)}\r\n\r\n"
            ).encode("utf-8")
            writer.write(header + body)
            await writer.drain()
            return

        writer.write(
            b"HTTP/1.1 200 OK\r\n"
            b"Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            b"Cache-Control: no-cache, no-store, must-revalidate\r\n"
            b"Pragma: no-cache\r\n"
            b"Connection: close\r\n"
            b"Access-Control-Allow-Origin: *\r\n\r\n"
        )
        await writer.drain()

        while True:
            frame = self.camera_provider.get_frame()
            if frame:
                content_type = "image/png" if frame.startswith(b"\x89PNG") else "image/jpeg"
                writer.write(
                    f"--frame\r\n"
                    f"Content-Type: {content_type}\r\n"
                    f"Content-Length: {len(frame)}\r\n\r\n".encode("utf-8")
                    + frame
                    + b"\r\n"
                )
                await writer.drain()
            await asyncio.sleep(0.04)  # ~25 FPS

    async def _handle_static_file(self, writer: asyncio.StreamWriter, path: str, conn_header_str: str = "Connection: keep-alive\r\n"):
        """静的Webアセット配信"""
        rel_path  = "index.html" if path in ("", "/") else path.lstrip("/")
        file_path = os.path.normpath(os.path.join(self.static_dir, rel_path))

        if not file_path.startswith(self.static_dir) or not os.path.isfile(file_path):
            body   = b"404 Not Found"
            header = (
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                f"{conn_header_str}"
                f"Content-Length: {len(body)}\r\n\r\n"
            ).encode("utf-8")
            writer.write(header + body)
        else:
            with open(file_path, "rb") as f:
                content = f.read()
            ext          = os.path.splitext(file_path)[1].lower()
            content_type = MIME_TYPES.get(ext, "application/octet-stream")
            # HTMLは更新反映のため no-cache、CSS/JS等の静的アセットは1日キャッシュしてマイコン負荷をゼロに
            cache_ctrl   = "no-cache" if ext == ".html" else "public, max-age=86400"
            header       = (
                "HTTP/1.1 200 OK\r\n"
                f"Content-Type: {content_type}\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                f"Cache-Control: {cache_ctrl}\r\n"
                f"{conn_header_str}"
                f"Content-Length: {len(content)}\r\n\r\n"
            ).encode("utf-8")
            writer.write(header + content)
        await writer.drain()

    async def tick_loop(self):
        """定期周期更新 (TORカウントダウン・通信途絶監視・デッドマンタイマー / 100ms周期)"""
        interval_ms = int(self.heartbeat_interval * 1000)
        while True:
            self.controller.tick(interval_ms)
            await asyncio.sleep(self.heartbeat_interval)

    async def close(self):
        """サーバーの停止"""
        if self._server:
            self._server.close()
            for w in list(self._active_writers):
                try:
                    w.close()
                except Exception:
                    pass
            await self._server.wait_closed()

    async def serve_forever(self):
        """サーバーの起動と並行実行"""
        bind_host = None if self.host in ("0.0.0.0", "", None) else self.host
        try:
            self._server = await asyncio.start_server(self.handle_client, host=bind_host, port=self.port)
        except Exception:
            self._server = await asyncio.start_server(self.handle_client, host=self.host, port=self.port)

        display_host = "localhost" if self.host in ("0.0.0.0", "", None) else self.host
        print(f"Mini 4WD Server running at http://{display_host}:{self.port}")
        try:
            await asyncio.gather(self._server.serve_forever(), self.tick_loop())
        finally:
            await self.close()
