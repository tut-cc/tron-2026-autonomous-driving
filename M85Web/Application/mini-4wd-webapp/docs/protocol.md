# 通信プロトコル・インターフェース仕様書

本ドキュメントでは、WebApp と 車載マイコン間で送受信される通信データのフォーマット、Heartbeat、各種タイムアウト閾値、および状態不一致時の安全判定ルールについて定義します。

## 1. 通信方式の概要
- **WebApp画面配信**: HTTP GET（`index.html`, `js/*.js`, `style.css` 等をマイコンから直接配信）
- **制御・状態通信**: HTTP POST `/api/control`。現行ファームウェアに単独の`GET /api/telemetry` endpointはない。
  - WebAppから操作コマンドJSONを `POST /api/control` で送信し、サーバーは応答URIから最新の状態JSONを返す。
- **カメラ映像配信**: HTTP GET `/video_feed`。オンボードM85が生成した240×180・256色（RGB332）BMPスナップショットをUIが繰り返し取得する。コース境界、生成走行路、AI検出枠（人・車）を画像に描き込む。MJPEGではなく、2.5秒より古いフレームしかない場合は503とする。映像処理・送信は制御より低優先。
- **映像取得間隔**: UIは前回の `/video_feed` 応答完了から100ms後に次のGETを開始する（同時要求は1件、3.5秒で中断）。10 Hzはベストエフォートのポーリング目標で、実際の更新率はカメラ/AIの生成周期とHTTP取得・画像デコード時間で下がり得る。
- **制御更新周期**: 10 Hz (100ms周期)
- **ポート統合**: 同一オンボードHTTPポート（既定80）で静的UI、制御API、BMP映像を提供する。既定の基板IPは `192.168.2.200`。

## 2. データフォーマット仕様

### 2.1 WebApp → マイコン（操作コマンド: POST /api/control）

WebAppは、自身が現在認識しているモード `client_mode` を付与してコマンドを `POST /api/control` で送信します。マイコン側はこの値と実際の内部状態を照合して実行可否を判定します。

```json
{
  "client_mode": "MANUAL",
  "throttle": 1.0,
  "steering": -0.5,
  "mode_request": "NONE",
  "deadman": true,
  "estop_request": false,
  "manual_abort_request": false,
  "reset_abort_request": false
}
```

| フィールド名 | 型 / 取り得る値 | 必須 | 内容・説明 |
|---|---|---|---|
| `client_mode` | `MANUAL` / `AUTO` / `AUTO_ABORT` / `MANUAL_ABORT` | ○ | WebAppが認識している現在モード |
| `throttle` | 数値 `-1.0 〜 1.0` | ○ | 入力 wire range。現行車両UI/M33は負値を走行指令に使わず0へ抑制し、後退は未提供（後方監視未確認） |
| `steering` | 数値 `-1.0 〜 1.0` | ○ | 左右ステアリング指示（負: 左、正: 右、0: 直進） |
| `mode_request` | `NONE` / `MANUAL` / `AUTO` | ○ | 運転モード切替要求。応答状態が確定するまでHTTP要求ごとに再送し、1つのPOSTに含める切替要求は1件 |
| `deadman` | `true` / `false` | ○ | 明示的な手動保持入力。`true` のときだけM33が手動走行を許可し、モード選択では代用しない |
| `estop_request` | `true` / `false` | ○ | 明示的な緊急停止要求（現在のWeb画面にはボタンが無く、常に `false` を送る）。M33のEmergencyラッチを立てるが、Web resetでは解除しない |
| `manual_abort_request` | `true` / `false` | ○ | 手動中断要求（最優先処理）※旧 `emergency_stop_request` 互換 |
| `reset_abort_request` | `true` / `false` | ○ | AUTO_ABORT / MANUAL_ABORT の解除・リセット要求 ※旧 `reset_stop_request` 互換 |

### 2.2 マイコン → WebApp（テレメトリ: レスポンスボディ）

マイコンは `POST /api/control` の応答URIから最新状態をJSON形式で返します。100ms周期のポーリングで最新要求と状態を受け渡します。独立した`GET /api/telemetry` endpointは実装していません。

```json
{
  "mode": "AUTO",
  "front_distance_mm": 450,
  "armed": true,
  "web_seq": 42,
  "tor_active": true,
  "tor_remaining_ms": 3000,
  "stop_reason": "NONE",
  "request_reject_reason": "NONE",
  "obstacle_alarm": true,
  "obstacle_kind": "PERSON",
  "alarm_seq": 3
}
```

| フィールド名 | 型 / 取り得る値 | 内容 |
|---|---|---|
| `mode` | `MANUAL` / `AUTO` / `AUTO_ABORT` / `MANUAL_ABORT` | マイコンの確定現在モード |
| `front_distance_mm` | 整数 (mm) | 前方測距センサーの値。未取得・センサー無効時は `-1`（UIは `--` 表示） |
| `armed` | `true` / `false` | M33の走行許可状態。手動復帰完了の確認に使用 |
| `web_seq` | uint32 | M33が最後に受理したWeb要求（STOP/ESTOPを含む）の連番。処理進行確認用であり、AUTO成功ACKそのものではない |
| `tor_active` | `true` / `false` | TOR（運転引継ぎ要求）発生中フラグ |
| `tor_remaining_ms` | 整数 (ms) | TOR残り猶予時間（通常時は `0`） |
| `stop_reason` | 列挙型（後述） | 中断（自動中断・手動中断）の要因 |
| `request_reject_reason` | 列挙型（後述） | モード切替や復帰要求が拒否された理由 |
| `obstacle_alarm` | `true` / `false` | M85のカメラが走行路上の人物・車を今見ているか（信頼度0.65以上・走行路との重なり0.40以上）。停止の指示ではない |
| `obstacle_kind` | `NONE` / `PERSON` / `CAR` / `PERSON_AND_CAR` | 検知中の種類（`obstacle_alarm=false` なら `NONE`） |
| `alarm_seq` | uint32 | 新しいアラームのたびに1増える番号。WebAppはこの値が変わるたびにログを1行残す（3フレーム見えなくなると次の検知は新しいアラーム） |

HTTP応答は、POSTした要求より前のM33状態スナップショットを返す場合があります。WebAppは新しい `web_seq` だけで切替成功とはせず、AUTOは `mode=AUTO` または明示拒否、MANUAL復帰は `mode=MANUAL` かつ `armed=false` を確認します。AUTO要求がタイムアウトした場合はAUTO再送を止め、MANUAL要求を再送してM33のMANUAL・非arm状態が確認できるまで操作入力を解放しません。Web resetはM33のEmergencyラッチを解除しません。

#### `stop_reason`（中断要因）の定義
- `NONE`: 中断なし（正常）
- `OBSTACLE`: 旧版との互換用。現行のM33によるAI人物・車停止では `AI_OBSTACLE` を使う
- `TOR_TIMEOUT`: TOR猶予時間切れ（AUTO_ABORT）
- `MANUAL_ABORT_BUTTON`: WebAppの ABORT / RESET 押下による停止（パッドを離しただけ・モード切替・AUTO拒否のあとは `NONE`）
- `COMM_TIMEOUT`: 通信途絶による自律中断（AUTO_ABORT）
- `SENSOR_ERROR`: 走行中（またはTOR中）に測距センサ（ToF）の値が無効・鮮度切れになったための緊急停止。停止中の無効値では停止理由を変えない
- `DISTANCE_PRESTOP`: ToFが100 mm以下の通常停止（緊急停止ではない）
- `DISTANCE_EMERGENCY`: ToFが50 mm以下の緊急停止（101 mm以上に離れると自動解除、再発進には操作が必要）
- `BUTTON_EMERGENCY`: ESTOP要求による緊急停止（Web画面からは発生しない）（基板リセットまで解除されない）
- `AI_OBSTACLE`: AUTO中、進路上の人物・車が前方250 mm以内（ToF）に近づいたための運転引継ぎ要求（`tor_active=true`）。人物・車が遠い間は停止せず `obstacle_alarm` で知らせるだけ（2026-09-28）
- `ROAD_UNAVAILABLE`: AUTO中に受け取ったAI情報が不正・鮮度切れのための停止（走行路の喪失はTORになる）
- `INTERNAL_FAULT`: M33のモーター出力・カーネルの異常による停止（基板リセットまで解除されない）

センサー・通信による緊急停止は原因が消えると自動で解除されますが、`stop_reason` は次に走り出すまで元の原因のまま残ります（2026-09-28）。

#### `request_reject_reason`（要求拒否理由）の定義
- `NONE`: 拒否なし（正常受諾）
- `SENSOR_NOT_READY`: センサが初期化中または値が不安定
- `OBSTACLE_NEAR`: 前方に障害物があるためAUTO切替不可
- `IN_TOR`: TOR中のためAUTO切替等の要求を拒否
- `IN_MANUAL_ABORT`: 手動中断中のため走行・切替要求を拒否
- `MODE_MISMATCH`: WebAppの認識モードと実状態が不一致のため拒否
- `LINK_NOT_READY`: M85→M33のハートビートが途絶しているためAUTO切替不可
- `PATH_NOT_READY`: AIの走行路認識（路面・フレームの鮮度・連続フレーム数）が未成立のためAUTO切替不可

AUTO切替拒否ではM33の拒否理由をそのまま区別して返します：ToF未準備は`SENSOR_NOT_READY`、M85通信途絶は`LINK_NOT_READY`、AI経路未準備は`PATH_NOT_READY`。

### 2.3 TOR（Take Over Request）の表現ルール

```text
通常AUTO時:
  mode = "AUTO"
  tor_active = false
  tor_remaining_ms = 0

TOR発生時 (AUTO_TOR):
  mode = "AUTO"
  tor_active = true
  tor_remaining_ms = 3000
```

TOR は M33（CPU1）が決定します：AUTO中に走行路喪失・AI途絶が起きるとモーター出力を0にして `VC_TOR` に入り、`VC_TOR_TIMEOUT_MS`（3000 ms）応答がなければ STOPPED（停止理由 `TOR_TIMEOUT`、mode は `AUTO_ABORT`、ABORT/RESETで解除）になります。`tor_remaining_ms` はM85がM33の `control_ms` を基準に算出する残り時間（wrap安全、0で下限）です。TOR中に MODE=MANUAL を受けると MANUAL（停止）へ引き継ぎ、条件が戻っても自動でAUTOへは復帰しません。

WebAppは `tor_active: true` を受信した際に、UI内部で `AUTO_TOR` 状態として扱い、警告オーバーレイを表示します。

## 3. タイムアウト & 定常通信パラメータ一覧

| パラメータ名 | 閾値 / 周期 | 監視主体 | 説明・タイムアウト時の動作 |
|---|---|---|---|
| **HTTPポーリング周期** | `100 ms` | WebApp | コマンド送信 (`POST /api/control`) の周期 |
| **Heartbeat 監視** | `100 ms` | マイコン | 内部状態の定周期更新とデッドマン/タイムアウト評価 |
| **デッドマンタイマー** | `300 ms` | マイコン | 操作コマンドが途絶えた場合にモーターを自動停止 |
| **Pending タイムアウト** | `1,000 ms` | WebApp | モード切替要求後、マイコンの状態が変わらない場合に要求失敗と判定 |
| **通信切断判定** | `1,500 ms` | WebApp / マイコン | テレメトリ途絶で `DISCONNECTED` 遷移、マイコンは `AUTO_ABORT` |
| **TOR 猶予時間** | `3,000 ms` (3秒) | マイコン | カウントダウンが0に達した場合、マイコンが `AUTO_ABORT` へ自律遷移 |

## 4. モード不一致時の安全判定マトリクス

WebAppの認識しているモード（`client_mode`）とマイコンの実際のモード（`mode`）が不一致である場合、マイコンは以下のルールで操作の受容または破棄を判定します。

- **安全側操作（手動中断・スロットル0・手動介入）は不一致でも即時実行する**
- **危険な操作（中断中・自律走行中の走行指示）は破棄して誤発進・衝突を防ぐ**

| マイコンの現在状態 | WebApp認識 (`client_mode`) | 送信された操作・要求 | 判定 | 動作内容 |
|---|---|---|---|---|
| **任意** | 任意 | `manual_abort_request: true` | **即時実行** | 最優先で全出力を遮断し `MANUAL_ABORT` |
| **任意** | 任意 | `throttle: 0.0` (停止指示) | **実行** | 安全側操作のため受容し停止 |
| **AUTO** (TOR中含む) | 任意 | `mode_request: MANUAL` | **実行** | 手動介入として即座に `MANUAL` へ遷移 |
| **AUTO_ABORT** | MANUAL | `throttle != 0.0` (走行指示) | **破棄** | 誤発進防止のため無視し、最新状態を返信 |
| **MANUAL_ABORT** | MANUAL | `throttle != 0.0` (走行指示) | **破棄** | 中断中のため無視し、最新状態を返信 |
| **AUTO** (走行中) | MANUAL | `throttle != 0.0` (走行指示) | **破棄** | 自律制御優先のため手動操作を破棄 |
| **AUTO_ABORT** | MANUAL | `mode_request: AUTO` | **拒否** | 中断状態からのAUTO直行は拒否（要リセット） |
| **AUTO_ABORT** / **MANUAL_ABORT** | AUTO_ABORT / MANUAL_ABORT | `reset_abort_request: true` | **安全確認後実行** | 障害物がクリアであれば `MANUAL` へ復帰 |
