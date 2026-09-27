# Mini 4WD WebApp - C言語実装 サンプルサーバー (KISS)

本ディレクトリ（`sample/`）は、ミニ四駆Webアプリ（`mini-4wd-webapp`）と通信を行うサーバー機能を、**KISSの原則（Keep It Simple, Stupid）** に従ってC言語で最小限・軽量に実装したサンプルです。

`server/`（Python版サーバー基盤）および `mock/`（モック機能）の仕様を忠実に反映しつつ、ルネサス EK-RA8P1 などの組み込みマイコン（μT-Kernel 3.0 + lwIP）への移植が容易な構造となっています。

---

## 1. ファイル構成

```text
sample/
├── constants.h      # プロトコル定数 (MCU運転モード、中断要因、タイムアウト値)
├── controller.h     # 車両制御コア インターフェース (状態管理・安全判定)
├── controller.c     # 車両制御コア 実装 (TOR、デッドマン監視、安全マトリクス)
├── http_server.h    # HTTPサーバー インターフェース
├── http_server.c    # 軽量HTTPサーバー 実装 (BSD Socket / lwIP両対応)
├── main.c           # エントリポイント & 端末キーボード入力ループ (1〜5キー切替)
├── Makefile         # ビルド設定
└── README.md        # 本ドキュメント
```

---

## 2. KISSの原則に基づく設計方針

1. **外部依存ゼロ**:
   - 外部HTTPライブラリや重量級JSONライブラリ（cJSON等）を一切使用せず、標準CライブラリとSocket API（POSIX / lwIP）のみで完結。
2. **メモリ動的確保（malloc）の排除**:
   - テレメトリJSONは `snprintf` によるスタックバッファへの直接書き出し。
   - コマンドJSONはシンプルな文字列探索（`strstr` / `strtod`）によりゼロアロケーションで安全・高速にパース。
3. **ステートレスな一問一答（Connection: close）**:
   - リクエストを処理してレスポンスを返した直後にソケットを閉じることで、接続プールやリソースリークの心配を排除。
4. **POSIX / lwIP 共通設計**:
   - ソケットAPI（`socket`, `bind`, `listen`, `accept`, `recv`, `send`, `close`）をそのまま使用しているため、Linux上の開発・検証とマイコン実機（lwIP Socket API）でロジックを共有可能。

---

## 3. Linux上でのビルド & 起動方法

### ビルド
```bash
cd sample
make
```

### 実行
```bash
./mini4wd_server
# またはポート番号を指定 (例: 8765)
./mini4wd_server 8765
```

ブラウザで `http://localhost:8765` を開くと、ミニ四駆操作画面が直接表示され、そのまま操作できます。

### 端末キー操作 (モックシナリオ切替)
サーバー稼働中、ターミナル上で以下のキーを入力して Enter を押すと、実機の状態遷移をシミュレートできます：
- `1` : **MANUAL Ready**（手動走行可能状態）
- `2` : **AUTO Cruising**（自動運転走行中）
- `3` : **TOR Warning**（運転引継ぎ要求警告: 3000msカウントダウン）
- `4` : **Obstacle Stop**（前方障害物検知による自動中断: AUTO_ABORT）
- `5` : **Emergency Stop**（手動中断ボタン押下: MANUAL_ABORT）
- `q` : サーバー終了

---

## 4. マイコン（ルネサス EK-RA8P1 / μT-Kernel 3.0 + lwIP）への移植手順

`~/tcpip` プロジェクトに本コードを組み込む場合は、以下の手順で行います。

### ステップ 1: ソケット切り替えマクロの有効化
コンパイルオプションに `-DLWIP_PLATFORM` を追加するか、`http_server.c` の先頭でマクロを定義します。これにより POSIX ソケットから lwIP のソケット API（`lwip/sockets.h`）に自動で切り替わります。

### ステップ 2: μT-Kernel 3.0 のタスクとして登録
`~/tcpip/Application/app_main_httpd.c` 等の `task_lwip`（DHCP取得完了箇所）から、HTTPサーバースレッドとタイマータスクを起動します。

```c
#include <tk/tkernel.h>
#include "controller.h"
#include "http_server.h"

static VehicleController g_controller;
static HttpServer        g_server;

/* HTTP サーバタスク */
LOCAL void task_http_server(INT stacd, void *exinf)
{
    http_server_run(&g_server);
    tk_ext_tsk();
}
LOCAL ID tskid_http;
LOCAL T_CTSK ctsk_http = {
    .itskpri = 11,
    .stksz   = 4096,
    .task    = task_http_server,
    .tskatr  = TA_HLNG | TA_RNG3,
};

/* 100ms 制御・監視タイマータスク */
LOCAL void task_ctrl_tick(INT stacd, void *exinf)
{
    while (1) {
        tk_dly_tsk(100); /* 100ms */
        controller_tick(&g_controller, 100);
    }
}
LOCAL ID tskid_tick;
LOCAL T_CTSK ctsk_tick = {
    .itskpri = 9,
    .stksz   = 2048,
    .task    = task_ctrl_tick,
    .tskatr  = TA_HLNG | TA_RNG3,
};

/* DHCP 完了後に起動 */
void start_mini4wd_system(void)
{
    controller_init(&g_controller);
    http_server_init(&g_server, &g_controller, 8765, "/romfs"); // またはSDカード/ROM

    tskid_http = tk_cre_tsk(&ctsk_http);
    tk_sta_tsk(tskid_http, 0);

    tskid_tick = tk_cre_tsk(&ctsk_tick);
    tk_sta_tsk(tskid_tick, 0);
}
```

### ステップ 3: センサー値やモーター出力の直結
- 測距センサー（ToF / 超音波等）の割り込み・取得タスクから `controller_update_sensor(&g_controller, distance_mm)` を呼び出します。
- モーター制御（PWM出力等）は、`controller.c` 内の `apply_motor()` 内でマイコンのタイマーPWMレジスタ（GPT等）を操作するように書き換えます。
