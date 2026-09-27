#!/usr/bin/env python3
"""
Mini 4WD WebApp - Mock Server
共通サーバー基盤 (server/) とモック専用コンポーネント (mock/) を組み合わせて動作するエントリポイント
"""
import asyncio
import os

from server.controller  import VehicleController
from server.http_server import HttpServer
from mock.camera        import MockCameraProvider
from mock.scenario      import ScenarioManager

BASE_DIR       = os.path.dirname(os.path.abspath(__file__))
SCENARIOS_FILE = os.path.join(BASE_DIR, "mock", "scenarios.json")

async def main():
    # 1. 共通制御コア (Source of Truth & 安全判定)
    controller = VehicleController()

    # 2. モック用カメラ (疑似白線コース生成)
    camera = MockCameraProvider(controller)

    # 3. 共通HTTPサーバー (REST API / MJPEG / 静的アセット配信)
    server = HttpServer(
        controller=controller,
        camera_provider=camera,
        static_dir=BASE_DIR,
        host="0.0.0.0",
        port=8765
    )

    # 4. モック用シナリオ管理 & キー入力ハンドラ
    scenario_mgr = ScenarioManager(
        controller     = controller,
        scenarios_file = SCENARIOS_FILE
    )

    print("==================================================")
    print("  Mini 4WD WebApp Mock Server Started")
    print(f"  Web UI : http://localhost:8765")
    print(f"  Stream : http://localhost:8765/video_feed")
    print("==================================================")

    # サーバーとキーボード入力ループを並行実行
    server_task = asyncio.create_task(server.serve_forever())
    input_task  = asyncio.create_task(scenario_mgr.terminal_input_loop())
    try:
        done, pending = await asyncio.wait(
            [server_task, input_task],
            return_when=asyncio.FIRST_EXCEPTION
        )
        for task in done:
            if not task.cancelled() and task.exception():
                raise task.exception()
    except (asyncio.CancelledError, KeyboardInterrupt):
        pass
    finally:
        server_task.cancel()
        input_task.cancel()
        await asyncio.gather(server_task, input_task, return_exceptions=True)
        await server.close()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
    finally:
        print("\nServer stopped.")
