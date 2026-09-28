# 変更履歴と現状

新しい記録ほど上の「現状」に追記しています。本文中の古いファイル名（`STATUS_20260927_JA.md` など）は当時の名前です（4m 参照）。

## 2026-09-28 ビルド・検証の手順を2つに整理
- 開発中：e² studio のビルドボタン（`build.py` はビルドだけ。以前は証拠ログがあると `manifest.py` を呼び、ソースを変えるたびに失敗していた）。
- 本番の書き込み前：外部ツール `demo_make_evidence`（`tools/make_evidence.py`）。
- 内容が重複していた `MAKE_EVIDENCE.cmd`・`CHECK_BEFORE_FLASH.cmd` を削除（`manifest.py` の入力一覧からも外した）。外部ツール `demo_manifest_verify` は使わない。

## 2026-09-28 障害物検知をログ通知のみに変更
- 人物・車の検知はWebのバッジとログで手動操作を促す。M33は障害物の検知・検知失敗・検出の揺れでモードもモーター出力も変えない。
- 障害物アラームによる開始拒否も行わない。ToFの100 mm通常停止・50 mm緊急停止、センサー異常時の走行中停止、明示STOP/ESTOP、経路喪失時TORは維持する。
- 変更: `control_motor`, `vehicle_control`, Web操作説明、障害物シーケンス試験。

## 2026-09-28 遅いHTTP応答でAUTO要求を取り消さない
- AUTO要求のUI待ち時間が切れた時、暗黙にMANUAL指令を送る代わりに要求待ちだけを解除し、M33の実状態を表示する。遅れて届いた応答でAUTOに入った場合も、その実状態を表示する。通信切断時も暗黙のMANUAL指令を作らない。
- lwIPを先に起動し、M33のIPC READY待ちはタスクを譲る再試行にする。Gatewayとカメラは共有IPC初期化後に起動し、同時初期化を避ける。
- 変更: `state-machine.js`, `app_main_httpd.c` と遅延応答の状態機械試験。

## 2026-09-28 Web のモードが頻繁に切り替わる件・停止理由の修正（ソース反映済み・未書き込み）
| 内容 | 主なファイル |
|---|---|
| 原因：止まっている間も ToF の無効値（sigma fail、何もない空間など）1回で EMERGENCY/VC_TOF になり、次の有効値で自動解除されていた。Web では MANUAL ⇔ MANUAL_ABORT が測定周期（約60 ms）で入れ替わっていた。停止中は記録だけにし、EMERGENCY は走行中と TOR 中だけにした（開始は従来どおり `tof_ok()` が拒否） | `control/src/vehicle_control.c`, `control/src/control_runtime.c` |
| 停止理由：パッドを離す・モード切替・TOR 引継ぎ・AUTO 拒否のあとが「手動中断」になっていた → `NONE`。ABORT/RESET だけが `MANUAL_ABORT_BUTTON`（表示「ABORT で停止」） | `vehicle_control.c`, `js/constants.js` |
| 停止理由：緊急停止の自動解除で原因が「手動中断」に上書きされていた → 次に走り出すまで元の原因（例：前方50 mm）を残す | `vehicle_control.c` |
| 停止理由：内部異常（`VC_INTERNAL`）が何も表示されなかった → `INTERNAL_FAULT`「内部異常で停止（基板リセットが必要）」を追加 | `web_control_adapter.c`, `protocol/*`, `js/constants.js` |

- e2 studio の `BothCore_Download` でデバッグ中は CPU1 が止まり（I2C 送信の途中で停止）、CPU0 は CPU1 を 10 秒待ってカーネルを終了した。デバッガ接続中の値は通常走行の観察に使えない。

## 2026-09-28 デモ構成変更・蛇行・起動時の微動（ソース反映済み・未書き込み）
段ボール壁をやめ、白いコース上で「壁＝ToFで停止」「人・車＝アラーム、近づいたらTOR」の構成にした。

| 内容 | 主なファイル |
|---|---|
| 人・車（信頼度0.65以上・走行路との重なり0.40以上）は停止ではなくアラーム。M85がWeb応答に `obstacle_alarm` / `obstacle_kind` / `alarm_seq` を載せ、UIはバッジと時刻付きログ（最大5件）を表示 | `CPU0/src/autonomy_controller.c`, `web_control_adapter.c`, `m85_gateway_task.c`, `M85Web/Application/protocol/*`, `mini-4wd-webapp/js/ui.js`, `index.html`, `style.css` |
| AUTO中、その人・車が ToF 250 mm 以内（`VC_OBSTACLE_TOR_MM`）または画像の最下部（0.95、ToFの下の低い物用）に来たら TOR（理由 `VC_TOR_OBSTACLE`、Webは `AI_OBSTACLE`）。以前は画像下55%で即EMERGENCYだった。MANUALでは止めない | `control/src/control_motor.c`, `vehicle_control.c`, `control/include/*` |
| M85の「AI_OBJECT_AHEAD で画像最下部の仮想障害物を足す」処理を削除（実際の検出位置で判定するため） | `CPU0/src/autonomy_controller.c` |
| 判定のしきい値を両コア共通に（`AI_OBSTACLE_CONFIDENCE_MIN_PER_MILLE` 650 / `AI_OBSTACLE_OVERLAP_MIN_PER_MILLE` 400） | `control/include/ai_control_signals.h` |
| 蛇行対策1：左右モーター差の自動補正。AUTOでまっすぐな区間を走る間、生のずれ（不感帯なし）を1フレームごとに15%ずつ積算してトリムを学習（上限±0.10）。電源を切るまで保持し、MANUALの前進にも適用。値はデバッガで `g_vc_steering_trim_permille` | `control_motor.c/.h`, `vehicle_control.c/.h` |
| 蛇行対策2：PWMを 1/256 スロット精度に（周期ごとに端数を持ち越す）。従来は5%刻みで、小さな操舵補正が切り捨てられていた | `control/src/motor_output_drv8833.c/.h` |
| 起動時の微動：CPU0 が起動直後（`R_BSP_WarmStart` POST_C）にモーター4ピンを LOW 出力にする。CPU1 が起動するまでピンが浮き、PMOD1-8（P402、Pmod の RESET 線）などが High 側に振れていたと推定 | `CPU0/src/hal_entry.c`, `control/include/motor_pins_ra8p1.h`, `control/ports/control_hw_ra8p1.c` |

- CPU0・CPU1とも再ビルド・書き込みが必要（IPCの形式は不変）。
- 実機未確認：250 mm でのTOR、トリム学習の収束、起動時の微動が消えるか。

## 2026-09-28 Webアプリ整理（ソース反映済み・未書き込み）
| 内容 | 主なファイル |
|---|---|
| ESTOPボタンを撤去し、停止ボタンを ABORT（中断画面では RESET）の1つに統一。`estop_request` はプロトコルに残し常に `false`。M33のESTOPラッチ処理は不変 | `mini-4wd-webapp/index.html`, `style.css`, `js/ui.js`, `js/app.js` |
| UI状態機械を書き直し。状態は `connected` / `pending`（未確認の操作1つ）/ M33状態 の3つから導出。AUTO中のABORT後に余分なMANUAL要求を送らない（M33の停止がMANUALを選ぶため） | `mini-4wd-webapp/js/state-machine.js`, `js/comm.js`, `tests/*` |
| PC用Pythonモックサーバー（`mock_server.py`, `mock/`, `server/`）を廃止 | `mini-4wd-webapp/` |
| `fsdata.h` 再生成 | `M85Web/Application/web/fsdata.h` |

- CPU0 の再ビルド・書き込みと `MAKE_EVIDENCE.cmd` の再実行が必要。CPU1 は変更なし。

## 現状メモ（2026-09-27）

RA8P1 カメラAI・M33 µT-Kernel 統合版（vehicle-output）。`hardware_verified=false` のまま。

### 1. 基板に入っているファーム
- **9/26 21:15 頃に書き込んだ版**：AIフレーム鮮度 250→600ms の修正のみ入りの版。
- それ以降の変更（下の 2・3）は**まだ書き込んでいない**。
- デバッグ構成 `ra8p1_vision_BothCore_Download` は Reset_Handler ブレークポイントを無効化済み。
  → **書き込み完了と同時にファームが動く。書き込み前に必ず DRV8833 の VM を外す。**

### 2. 9/26 夜〜9/27 のリファクタ（ソース反映済み・未書き込み）
| 内容 | 主なファイル |
|---|---|
| AUTO拒否理由を ToF / 通信 / 走行路 の3つに分けてWeb表示（IPCワイヤ形式は不変） | `control/include/vehicle_control.h`, `control/src/vehicle_control.c`, `CPU0/src/web_control_adapter.c`, `M85Web/Application/protocol/*`, `mini-4wd-webapp/js/constants.js`, `web/fsdata.h` |
| 路面らしさ基準（開始0.65/維持0.50）とAIフレーム鮮度600msを両コア共通の1か所に集約。ずれるとコンパイルエラー | `control/include/ai_control_signals.h`, `CPU0/src/user_config.h`, `control/src/control_motor.c` |
| `VC_WEB_TIMEOUT_MS` 1000→**300ms**（設計値）。HTTP間隔>300msでMANUALはCOMM_TIMEOUT停止 | `control/include/vehicle_control.h` |
| テスト追加：AI周期300ms回帰、拒否理由3種、ヒステリシス、Web表示 | `control/tests/*`, `mini-4wd-webapp/tests/state-machine.test.mjs` |
| 証拠ログ生成 `make_evidence.py`（クリーンビルド→manifest→C/JSテスト→verify）、`fsdata.h` のLinux生成 `M85Web/script/generate_web.sh` | ルート, `M85Web/script/` |
| Node.js を winget で導入（`C:\Program Files\nodejs`） | — |

- 9/27 00:05 のクリーンビルドで `make_evidence.py --resume` と `CHECK_BEFORE_FLASH.cmd` は**全PASS**。
- 詳細は `REFACTOR_NOTES_JA.md` 末尾「実機調整とリファクタ（2026-09-26 夜）」。

### 3. AI高速化・段階1（ソース反映済み・ビルド済み・未書き込み）
- 待ち時間 `PERIODIC_IMAGE_INTERVAL_MS` 100→**10ms**（`CPU0/src/user_config.h`）。1周約90ms短縮の見込み。
- 工程別計測（`CPU0/src/mipi_csi.c`）と arena 使用量（`CPU0/src/ai_model.cc`）を追加。止めずにメモリービューで読める：

| 変数 | アドレス | 内容 |
|---|---|---|
| `g_ai_stage_us[9]` | `0x2204D484` | 直近フレームの各工程 µs：コピー, 回転, 検出全体, NPU推論, 路面解析, M33送信, USB公開, 合計, 周期 |
| `g_ai_stage_max_us[9]` | `0x2204D4A8` | 起動後の最大値（同じ並び） |
| `g_ai_stage_frames` | `0x2204D4CC` | 処理フレーム数 |
| `g_ai_arena_used_bytes` | `0x2204D2C8` | TFLM arena 実使用量（起動時に設定） |
| `g_ai_processing_time_ms` | `0x2204D2DC` | 撮影→M33送信の経過（従来） |

- **注意**：`CPU0/Build/CPU0.elf` は現在この計測版（SHA256 先頭 `CE549D05…`）。証拠ログは 00:05 版（`51E07CAC…`）を指しているので、**今 `CHECK_BEFORE_FLASH.cmd` を実行するとFAIL**。計測版の書き込みは開発用。本番書き込み前に `python -B make_evidence.py`（クリーンビルド）→ `CHECK_BEFORE_FLASH.cmd` をやり直す。

### 4. NPUメモリの計算結果（モデルファイルから算出、実機不要）
`CPU0/model/yolo_fastest_coco80_int8_320_vela_speed.tflite`（Vela 変換済み、Ethos-U55-256、Shared_Sram）

| テンソル | サイズ | Velaのオフライン配置 |
|---|---:|---|
| scratch（作業領域） | 1,229,728 B | offset 0 |
| scratch_fast | 1,229,728 B | offset 0（scratch と同じ領域） |
| 入力 1×320×320×3 int8 | 307,200 B | offset 409,600（scratch 内） |
| 出力 1×20×20×255 | 102,000 B | offset 83,104（scratch 内） |
| 出力 1×10×10×255 | 25,500 B | offset 57,600（scratch 内） |
| 重み（flash）+ コマンド列 | 524,108 B | 定数、MRAM上 |

- **arena 必要量 ≈ 1,229,728 B（約1.17MB）+ TFLM管理領域（数KB〜数十KB）≈ 1.24MB**。現在の確保 0x160000 = 1,441,792 B に収まる。
- arena は現在 **外付けSDRAM**（`.sdram_noinit`、0x68A8C000）に配置。
- CPU0 内蔵SRAM は 1.5MB（0x22000000–0x2217FFFF）。静的使用 約533KB（`.ram.endof`=0x22085400）、残り約1.0MB は µT-Kernel のシステムメモリ。
- **結論：このモデルのままでは arena を内蔵SRAMへ全部移すことはできない**（1.24MB > 空き1.0MB）。
- 入力は 320×320 だがカメラは 320×240 のため、上下 80 行（25%）はレターボックスの詰め物。

### 4b. Vela 再変換の調査結果（2026-09-27 04:30）
- Vela = Arm の Ethos-U 用モデル変換ツール（`pip install ethos-u-vela`、今回 5.2.0）。
- **`--memory-mode Dedicated_Sram` は Ethos-U65 専用。U55（このボード）は Shared_Sram / Sram_Only のみ** → 「一部だけSRAM」案は不可（訂正）。
- 変換前の元モデルは公開あり：OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection の `src/Model/yolo-fastest_int8.tflite`。
  現在使用中のモデルは同リポジトリの `..._opt-speed.tflite` と SHA256 一致（`--accelerator-config ethos-u55-256 --optimise Performance`）。
- 再変換の比較（Vela 5.2.0、ethos-u55-256、Shared_Sram）：

| 変換条件 | 作業領域(scratch) | Vela見積り（500MHz・高速SRAM想定） |
|---|---:|---:|
| 現行（opt-speed、旧Vela） | 1,229,728 B | — |
| Performance（Vela 5.2） | 約1,000 KiB | — |
| **Size** | **435,200 B（425 KiB）** | 約7.0M cycles ≈ 14 ms |
| **Performance + `--arena-cache-size 450000`** | **447,232 B（437 KiB）** | 約7.0M cycles ≈ 14 ms |

- 1推論あたり作業領域との転送量は約24〜25MB。arena が外付けSDRAMにあると、この転送が推論時間を支配している可能性が高い。
- **作業領域が約0.45MBになれば内蔵SRAM（空き約1.0MB）に置ける見込み**。入力・出力の形は現行と同じ（1×320×320×3 → 1×10×10×255, 1×20×20×255）。
- 候補モデルと元モデル、Velaログ：`C:\TRON\demo_vela_candidates\`（パッケージ外。manifest対象外）。
- 未確認：実機のNPU時間（計測版で確認）、RA8P1のNPUクロック、µT-Kernelシステムメモリの必要量（arenaを内蔵SRAMに置いた後に残る約0.5MBで足りるか）、出力テンソル順が後処理と合うか、モデルのライセンス（上流はApache-2.0と記載）。

### 4c. AI処理の最適化（2026-09-27 05:00、ソース反映済み・**ARMビルド未実施・実機未検証**）
方針：結果が変わらない最適化だけ。旧実装との一致はPCテストで確認（実機の速度・動作は未確認）。

| # | 内容 | ファイル | 期待効果（未計測） |
|---|---|---|---|
| 1 | 後処理：`sigmoid(dequantize(q))` を出力テンソルごとに256値の表にして、約120,000回/フレームの `exp` を512回に。objectness がしきい値未満のアンカーは80クラス走査前にスキップ（class≦1.0 なので結果同一） | `CPU0/src/obstacle_kernels.c`（新規）, `ai/obstacle_kernels.h`（新規）, `obstacle_detector.cc` | 大（推定10ms以上） |
| 2 | 前処理：307KB全体の `memset` をやめ余白（上下80行）だけ0x80で埋める。画素ごとの割り算2回→列/行ごとに1回。RGB565→int8を32/64要素の表に | 同上 | 中 |
| 3 | ホットスポットだけ `-O2`（他は `-Os` のまま、fast-mathなし）：`obstacle_kernels.c`, `frame_rotation.c`, `road_navigation.c`。最適化レベルは objのダイジェストと `flags_sha256` に含める | `build.py` | 中 |

- 追加：`CPU0/sources.json` に `src/obstacle_kernels.c`、`test_host.py` に `obstacle_kernels` テスト（非Windowsでは `-lm`）。
- PCテスト `control/tests/test_obstacle_kernels.c`：旧実装を転記した参照と新実装を比較し、**前処理は9種類の画像サイズ（stride付き・縦長・奇数）で全バイト一致、後処理は乱数400グリッド・候補25,583個で float まで完全一致**（飽和で同点になるケース、64枠の入れ替えを含む）。わざと3種類の変更を入れると全部検出されることも確認。`python test_host.py` 全PASS。
- 注意（未確認事項）：
  - ARM LLVM でのビルド（特に `obstacle_detector.cc` の変更）はまだ。**`python -B make_evidence.py` がクリーンビルドを兼ねる**ので、コンパイルエラーがあればそこで出る。
  - `-O2` によるスタック使用量の増加（カメラタスク）は未評価。
  - PCテストは同じコンパイラ同士での一致確認。ARM上の旧版と新版で float が一致するかは、同じclangで同じ式を使っているので一致する見込みだが、実機では未確認。
  - 変更前ファイル：`C:\TRON\demo_evidence_archive\obstacle_detector.cc.before_kernels`, `build.py.before_O2`。
- ビルドが遅い理由：`build.py` はオブジェクトの再利用判定に**全ヘッダーのハッシュ**を含めるため、ヘッダーを1つでも変えると全ファイル再コンパイル（CPU0は約12〜20分）。

### 4d. 証拠タグの一元化（2026-09-27 05:00）
- 日付入りタグ（`demo_autofix_YYYYMMDD` / `BUILD_DATE`）を `evidence_tag.json` に1か所化。`manifest.py` / `verify.py` / `build.py` / `make_evidence.py` はここを読む（import なし＝`__pycache__` を作らない）。
- `make_evidence.py` はクリーンビルドする時、タグをその日の日付に切り替え、旧タグのログを `..\demo_evidence_archive\` へ移す。`--resume`（再ビルドなし）の時はタグを変えない。
- 日付が変わるたびに4ファイルを手で直す必要はなくなった。変更前：`demo_evidence_archive\*.before_tagjson`。

### 4e. 05:00 のクリーンビルド結果
- **最適化1〜3を含めて ARM LLVM ビルドは CPU0/CPU1 とも成功**（CPU0 text 934,092 B、1116 units）。manifest、C/JSテストも PASS。
- verify.py のみ FAIL：`mipi_csi.c` の処理順チェック（回転→検出→路面→送信の呼び出し位置）が、計測用 enum のコメントに書いた関数名 `...()` を先に拾ったため。コメントから関数名を外した（行数は不変、コードは不変）。

### 4f. GitHub 登録の準備（2026-09-27 05:20）
- 登録先：`https://github.com/tut-cc/tron-2026-autonomous-driving`（**公開**・空）。このセッションには push 権限がないため、利用者のPCから push する。
- `.gitignore`：`Build/obj/`・ELF/SREC/MAP・`link.rsp`・`build_profile.json`/`build_metadata.json`・`JLinkLog.log`・`__pycache__` を除外。`Build/*.lld` と `Build/bsp_linker_info.h` はリンク入力なので追跡する。
- `.gitattributes`：`* -text`（改行コード変換を止め、manifest のハッシュを保つ）。
- 対象 4,127 ファイル・約80MB（最大 7MB）。
- 公開前の確認事項：`CPU0/THIRD_PARTY_NOTICES.md` の通り、ST VL53L1X ドライバ・YOLOモデルの再配布条件、`CPU0/src/prebuilt/camera_sensor.o`（ソースなし）の出所。`REFACTOR_NOTES_JA.md` 内のユーザー名入りパスは `<ユーザー名>` に置換済み。

### 4g. チーム資料の非公開化（2026-09-27 05:35、利用者の指示）
- 引継ぎ文書類（担当者名・担当分担を含む）をパッケージ外 `C:\TRON\team_private\control\` へ移動：`PROJECT_CONTEXT.md`, `README_JA.md`, `docs/HANDOFF_AI_JA.md`, `docs/HANDOFF_WEB_JA.md`, `docs/VALIDATION_JA.md`。`control/` はコード（include/ports/src/tests）のみ。
- 公開ファイル中の参照を書き換え：`README_JA.md`, `CPU0/THIRD_PARTY_NOTICES.md`, `docs/BRINGUP_JA.md`, `LICENSE_SURVEY_20260927_JA.md`。担当者名はパッケージ内に残っていない。
- `control/` のツリーハッシュが変わるため、push前に `python -B make_evidence.py --resume` → `CHECK_BEFORE_FLASH.cmd` で manifest を作り直す。

### 4h. README（応募作品向け）と TOR の未実装（2026-09-27 06:00）
- `README.md` を「審査員が作品を理解して動かす入口」として書き直し：作品概要 → Demo → 目的 → システム構成 → 動作モード → 組み立て・接続 → 起動 → デモシナリオ → μT-Kernel の役割 → ソース構成（＋トラブル・開発者向け）。TODO：正式名称、写真、動画・資料リンク、Wi-Fi の SSID。
- **TOR は現ファームで未実装**：`vc_state_t` に `VC_TOR` はあるが CPU1 のコードはこの状態を設定しない。AUTO 継続不可（走行路喪失・AI途絶）は `vc_stop(VC_AI, 0)` で STOPPED になり、Web の停止理由は `OBSTACLE` と表示される。Web UI の TOR 表示・プロトコル（`tor_active`、3秒カウントダウン）は用意済み。応募のデモで TOR を見せるには CPU1 側の実装が必要。
- 通信は Wi-Fi 直結ではなく「スマホ Wi-Fi → ルーター → Ethernet → EK-RA8P1」、HTTP ポーリング（100 ms、WebSocket なし）、固定 IP 192.168.2.200。

### 4i. TOR の実装（2026-09-27 06:30、ソース反映済み・PCテスト PASS・**ARMビルド未実施・実機未検証**）
- CPU1：AUTO 中に `p.motor_enable` が落ちる（走行路喪失・AI途絶）と `enter_tor()` で出力 0・`VC_TOR`・理由 `VC_TOR_REQUEST`。`VC_TOR_TIMEOUT_MS`（3000 ms）経過で STOPPED/MANUAL・理由 `VC_TOR_TIMEOUT`。引継ぎは既存の MODE MANUAL（`VC_OPERATOR` で停止）。自動復帰なし。ToF・ESTOP・AI 緊急停止は TOR 中も優先。
- CPU0：`web_control_adapter` で `VC_TOR_TIMEOUT` → 停止理由 `TOR_TIMEOUT`・mode `AUTO_ABORT`。`web_adapter_tor_remaining_ms()`（M33 の `control_ms` 基準、wrap 安全）を `m85_gateway_task.c` の応答 `tor_remaining_ms` に設定。
- テスト：`test_control.c` の test_tor（7項目、旧「TOR なし」の期待値は TOR に変更）、`test_web_adapter.c` の TOR カウントダウン。`test_host.py` 全 PASS。
- README・protocol.md を更新（4h の「TOR 未実装」は解消）。変更前ファイルの控え：`C:\TRON\demo_evidence_archive\before_tor\`。
- ヘッダ（`vehicle_control.h`、`web_control_adapter.h`）が変わったため次の `make_evidence.py` は全再ビルド（CPU0 は 12〜20 分）。

- 06:20 利用者が `make_evidence.py` → `CHECK_BEFORE_FLASH.cmd` を実行し **all pass**（TOR 入りの ARM ビルド成功）。実機は未確認。

### 4j. 構造体とエラー処理の整理（2026-09-27 06:40、PCテスト PASS・**ARMビルド未実施・実機未検証**）
挙動は変えない方針。IPC の通信形式（`vc_web_t`・`vc_status_t`・`ipc_packet_t`）は変更なし。
- **構造体**：`vc_t` の入力（AI・ToF・Web の最新値と `have_*`、M85 リンク時刻）を `vc_inputs_t in` にまとめた。未使用だった `web_deadman_held`・`web_arm_seq` を削除。各フィールドに役割のコメント。
- **戻り値の型付け**：
  - `vc_ai/vc_tof/vc_web` → `vc_input_result_t`（`ACCEPTED`=0、`INVALID`/`STALE`/`NOT_NEWER` は負。従来の `<0` 判定はそのまま使える）。
  - `vc_start/vc_clear_emergency` → `vc_arm_result_t` を直接返す（`g_vc_start_result`/`g_vc_clear_result` にも従来どおり写す）。`vc_web` はグローバル変数ではなく戻り値から AUTO 拒否理由を決める。
  - `IPC_OK/EMPTY/ERROR/BUSY` を `ipc_result_t`（値は同じ）に。pack/unpack も `IPC_OK`/`IPC_ERROR` を返す。
- **カプセル化**：STOP フェンス（STOP より前に積まれた DRIVE で再始動させない処理）を `control_runtime.c` から `vc_fence_web()` へ移した。
- **M33 ランタイムのエラー処理**（`control_runtime.c`、読みやすい書式に整形）：
  - 失敗処理を `fail_safe(原因)` に一本化。最初の原因を **`g_control_fault`** に記録する（`control_fault_t`：INIT/CLOCK/LOCK/UNLOCK/STOP_FLAG/QUEUE/DELAY/HW/MOTION_STALE）。VC_INTERNAL で止まったときに e2 studio の Expressions で原因がわかる。
  - タスク表（関数と優先度）を1か所に。キュー処理を `drain_ai_locked`/`drain_web_locked` に分離。
  - 唯一の挙動差：メッセージバッファのサイズ不一致で、次周期を待たずに即座に出力停止（安全側）。
- **不具合修正**：
  - `ipc_unpack_status()` が CRC を確認していなかった → 他の型と同じく確認。
  - `ipc_gateway_init()` が失敗時にミューテックスを削除しておらず、1ms ごとの再試行でカーネル資源を使い切るおそれ → 削除して再試行可能に。
- テスト：`test_control.c` に test_results（入力の拒否理由3種、戻り値と診断変数、STOP フェンス、status の CRC）。`control_runtime.c` と `ipc_gateway.c` は µT-Kernel の仮ヘッダで構文チェック（警告なし）。
- 変更前の控え：`C:\TRON\demo_evidence_archive\before_refactor_struct\`。

- 06:45 利用者が `make_evidence.py` → `CHECK_BEFORE_FLASH.cmd` を実行し **all pass**。

### 4k. KISS（使っていないもの・1実装しかない抽象化の削除）（2026-09-27 07:00、PCテスト PASS・**ARMビルド未実施**）
挙動は変えない。消したファイルは `C:\TRON\demo_evidence_archive\before_kiss\removed\` に移動（元ファイルの控えは `before_kiss\`）。
- **モーター出力の間接層を削除**：`motor_output_backend.c/.h`（関数ポインタ経由で DRV8833 を呼ぶだけ、実装は1つ）をやめ、`motor_output_drv8833_apply()` を直接呼ぶ。`CPU1/sources.json`・`test_host.py` から外した。
- **Web ループバック試験タスクを削除**：`control_test_task.c/.h`・`control_test_config.h`・`README_LOOPBACK.md`（`CONTROL_LOOPBACK_TEST_ENABLE=0` で常に無効だった）。`CPU0/sources.json` と `app_main_httpd.c` から外し、`control_api.c` の生ログ用スイッチ `CONTROL_HTTP_RAW_LOG_ENABLE`（既定0）はそのファイル内に移した。`manifest.py`・`verify.py` はファイルの不在を確認するように変更。
- **呼ばれていない関数を削除**：`vc_safe`、`control_motor_state_name/reason_name`、`road_navigation_stop_reason_text`、`frame_stream_send_rgb565/send_navigation/video_lease_busy`（と `frame_stream_copy_source`）。`autonomy_controller_service` は Ethos-U ドライバが呼ぶので残した。

### 4l. demo 直下の整理（2026-09-27 07:15）
- Python スクリプト5本と `evidence_tag.json` を `tools/` へ、`MANIFEST.json`・`README_JA.md`・`START_HERE_JA.md`・本メモ・`REFACTOR_NOTES_JA.md`・`LICENSE_SURVEY_20260927_JA.md` を `docs/` へ移動。直下は `README.md`・`NOTICE.md`・`MAKE_EVIDENCE.cmd`（新規、`tools/make_evidence.py` を呼ぶだけ）・`CHECK_BEFORE_FLASH.cmd` とフォルダのみ。
- 各スクリプトのルートは `tools/` の1つ上。e² studio の `.project`（CPU0/CPU1/Solution）のビルド引数は `../tools/build.py ...` に変更（verify も同じ文字列を確認）。**e² studio で開いている場合はプロジェクトを Refresh（F5）すること**。
- README・NOTICE・docs 内のコマンド表記を更新。`docs/E2STUDIO_BUILD_AUDIT_JA.md` は当時の記録なので変更していない。
- 移動前の控え：`C:\TRON\demo_evidence_archive\before_tidy_root\`。

### 4m. 資料の統合（2026-09-27 07:45）
- 07:34 に 4i〜4l を含めて `MAKE_EVIDENCE.cmd` → `CHECK_BEFORE_FLASH.cmd` が **all pass**。
- docs を 13 本 → 7 本に整理し、`_JA`・日付をファイル名から外した：`DEVELOPMENT.md`（旧 README_JA・START_HERE・RUN_AND_VALIDATION・VERIFICATION）、`HARDWARE_TEST.md`（旧 CONTEST_DEMO_RUNBOOK・AUTO_AI_RETEST・demo_autofix_report の手順部分）、`CHANGELOG.md`（旧 STATUS・REFACTOR_NOTES・demo_autofix_report）、`BRINGUP.md`・`PIN_OWNERSHIP.md`・`LICENSE_SURVEY.md`・`E2STUDIO_BUILD_AUDIT.md`（改名のみ。監査記録は当時の内容のまま）。
- 現行ファームで使わない SW1/SW2 の手順を削除（BRINGUP・PIN_OWNERSHIP の該当行も更新）。
- この文書内の古いファイル名は当時の記録としてそのまま残している。統合前の原本：`C:\TRON\demo_evidence_archive\before_docs_merge\`。

### 4n. 左右タイヤの割り当て（2026-09-27 08:00、PCテスト PASS・**ARMビルド未実施・実機未検証**）
- 07:55 に 4m を含めて all pass。
- 利用者の指摘：旋回で回るタイヤが左右逆（左に曲がるとき右タイヤを前進させたい）。制御の計算（右旋回＝左タイヤが速い）は正しく、DRV8833 の AIN が右モーターにつながっていると判断。
- `MOTOR_SWAP_SIDES`（`motor_build_profile.h`、既定 1）を追加し、DRV8833 ドライバで左指令→BIN、右指令→AIN に振り分ける。MANUAL と AUTO の両方に効く。`MOTOR_LEFT/RIGHT_INVERTED` は車両の左右を指す。
- テスト `test_swap_sides`、`verify.py` に設定の確認を追加。README の配線表・PIN_OWNERSHIP・HARDWARE_TEST を更新（AUTO の曲がる向きも段階 B で別に確認）。
- 変更前の控え：`C:\TRON\demo_evidence_archive\before_swap_sides\`。

### 4o. Web をラジコン式操作に（2026-09-27 08:10、JSテスト PASS・**ARMビルド未実施・実機未検証**）
- 利用者の指示：左手で前進、右手で左右（ボタン式、後退は入れない）。
- `index.html`：十字キー `#dpad` を、前進パッド `#pad-throttle`（▲、左下）と左右パッド `#pad-steer`（◀ ▶、右・フッターの上）に分割。`style.css` のクラスを `.pad` / `.pad-btn` に。
- `js/input.js`：各パッドに同じポインター処理を付け、押した指はそのパッドのボタンだけを操作。送る値（throttle 0/1、steering -1/0/1、deadman）と M33 側は変更なし。`debug-ui.js`・`screen.js` の参照も更新。
- ブラウザで確認（Playwright）：▲＋◀ で throttle=1・steering=-1、指を離すとニュートラル。`fsdata.h` は `M85Web/script/generate_web.sh` で再生成。
- README・webapp README・ui-spec・HARDWARE_TEST を更新。控え：`C:\TRON\demo_evidence_archive\before_rc_layout\`。

### 4p. demo_refine_20260927 の取り込み（2026-09-27 18:00、PC/JSテスト PASS・**ARMビルド未実施・実機未検証**）
別系統で作業した `demo_refine_20260927`（9/26 のリファクタ版から分岐し、映像配信・実走行フィードバックを反映した版）を、この demo を土台に比較して取り込んだ。refine 側の実機記録では、基板には refine 系の r6 または映像候補版が書き込まれている可能性がある（この統合版はまだ書き込んでいない）。

取り込んだもの：
| 内容 | 主なファイル |
|---|---|
| ToF 通常停止 100 mm（`VC_TOF_PRESTOP_MM`、非緊急）・緊急停止 50 mm・解除 101 mm（利用者指定）。100 mm 以内は開始も拒否 | `vehicle_control.h/.c`, `control_runtime.c` |
| 停止理由の分離：`VC_TOF_NEAR` / `VC_TOF_PRESTOP` / `VC_ESTOP` / `VC_AI_OBSTACLE`（値は既存の後ろに追加）→ Web `DISTANCE_EMERGENCY` / `DISTANCE_PRESTOP` / `BUTTON_EMERGENCY` / `AI_OBSTACLE`、AUTO 中の不正・古い AI は `ROAD_UNAVAILABLE` | `vehicle_control.h`, `web_control_adapter.c`, `control_protocol.*`, `js/constants.js` |
| **ESTOP は基板リセットまでラッチ**（Web RESET・センサー回復で解除しない。既存の緊急停止を上書き） | `vehicle_control.c`, `control_runtime.c` |
| M33 status 送信 10→50 ms、M85 gateway 周期 1→5 ms、カメラの推論後待機 10→5 ms | `control_runtime.c`, `m85_gateway_task.c`, `user_config.h` |
| 走行路：実測した行だけの `path_valid_mask`、横ずれは最も近い実測中心、向きは実測中心の傾き（二重計上をやめた）。ゲイン 0.45/0.35→0.30/0.25。人・車の前方検出を走行路喪失より優先。安全コリドーは画像中央基準。障害物は人・車のみ | `road_navigation.c/.h`, `autonomy_controller.c`, `control_motor.c` |
| オンボード映像 `GET /video_feed`：240×180・256 色 BMP にコース境界・走行路・AI 枠を描画。映像タスク（優先度 20）と USB 診断タスク（22）を分離、USB 端末が無くても止まらない書き込み | `frame_stream.c/.h`, `frame_stream_bmp.h`, `frame_stream_overlay.h`, `usb_pcdc_console.c/.h`, `app_main_httpd.c`, `control_api.c` |
| Web：映像表示（`js/video.js`、1 件ずつ取得・100 ms 間隔・3.5 s 中断）、INFO に凡例とカメラ診断。**ラジコン式パッドと縦持ち時の横画面表示（全画面＋向きロック、無理なら 90° 回転）は維持**（refine の「縦横両対応・回転撤去」は実機で縦画面になったため戻した）。左右パッドは操作フッターの上段へ | `index.html`, `style.css`, `js/*.js`, `web/fsdata.h`（`generate_web.sh` で再生成） |
| 起動構成：ELF をワークスペース相対パスに、重複イメージ無効、起動前自動ビルド無効 | `CPU0/ra8p1_vision_BothCore_Download.launch` |
| テスト：ToF 100/50 mm・ESTOP ラッチ・停止理由・操舵符号、`control_motor` / `road_navigation` / BMP / overlay の契約テスト、`video.test.mjs` | `control/tests/*`, `CPU0/tools/*`, `tools/test_host.py`, `make_evidence.py`, `CHECK_BEFORE_FLASH.cmd` |

demo 側を残したもの（refine と食い違った点）：
- AI フレーム鮮度は **600 ms**（refine は 500 ms。利用者の判断で実測根拠のある 600 ms を維持）。
- Reset_Handler で**停止しない**起動構成（利用者の判断）。
- 左右タイヤの入れ替えは `MOTOR_SWAP_SIDES`（ドライバ）で 1 回だけ。refine は同じ現象を `vehicle_control.c` 側で入れ替えていたので、二重に入れ替えないよう取り込んでいない。
- TOR、AUTO 拒否理由 3 種、`VC_WEB_TIMEOUT_MS`=300 ms、`obstacle_kernels.c`（refine の同等の高速化は取り込まず）、ラジコン式パッド、`tools/` 構成、ループバック試験タスク・`motor_output_backend` の削除。
- refine の未使用関数（`frame_stream_send_rgb565` / `send_navigation` / `video_lease_busy`、`road_navigation_stop_reason_text`、`control_motor_*_name`、`vc_safe`）は取り込んでいない。

未確認：ARM LLVM ビルド（`MAKE_EVIDENCE.cmd`）、実機での映像更新・停止理由表示・100/50 mm の実距離・ESTOP ラッチ。50 mm は衝突回避を保証する距離ではない。

### 4q. コース認識をゆるく（2026-09-27 19:40、利用者の指示「がばがばにしたい」、PCテスト PASS・実機未検証）
| 項目 | 前 | 後 |
|---|---|---|
| 白の下限 R/G/B | 115/115/105 | 90/90/80（影・暗い照明の紙も白） |
| 白の色差上限 | 75 | 85 |
| 端の見落とし許容 | 2 サンプル | 3 |
| 1 行の最小幅 | 画面の 6 % | 4 % |
| 手前の最小幅 / 先の最小幅 | 22 % / 8 % | 12 % / 5 % |
| 手前の実測行 | 一番下の行が必須 | 下から 3 行のどれか |
| 目標行 | ちょうど先読み位置が実測 | 近くの実測行で代用可（従来の選び方） |
| 路面らしさ 開始 / 維持 | 0.65 / 0.50 | 0.45 / 0.35（`ai_control_signals.h`） |
| 走行路閉塞とみなす非白の割合 | 30 % | 60 % |
| AUTO 開始・AI 停止解除に必要な連続良好フレーム | 3 | 2 |

合成画像で、暗い紙（RGB 100,100,90）・幅 13 % の細い紙・手前半分だけの紙・一番下が隠れた紙が、前は停止、後は走行可になることを確認。ベージュ系の床を白と誤認しやすくなる。人・車の AI 停止、ToF、STOP/ESTOP は変更なし。

### 4r. 直線をまっすぐ・奥が見えなくても進む（2026-09-27 19:50、利用者の指示、PCテスト PASS・実機未検証）
蛇行対策（M33 `control_motor.c`）：
- ゲイン 横ずれ 0.30→0.22、向き 0.25→0.18。
- 不感帯：横ずれ ±0.06、向き ±0.08 以内は 0 とみなす（まっすぐな紙の上の小さな揺れで舵を切らない）。
- 平滑化：カメラの新しいフレームごとに、舵を新しい値へ半分だけ寄せる（`steering_filter_weight`=0.5）。停止・再開時は直進から始める。カメラ 0.3〜0.5 s の遅れで行き過ぎて戻る振動を抑える。

奥行き（M85 `road_navigation.c`, `user_config.h`）：
- 路面らしさは手前 6 行（12 行中）だけで計算。奥で紙が切れても下がらない。
- 先読み位置 65 %→50 %。
- 走行路閉塞の判定は、紙が実際に見えている範囲（最も奥の実測行まで）だけで行う。紙の終わりの先を「ふさがれている」と扱わない。
- 合成画像：画面下 20〜30 % にしか紙がなくても走行可（前は停止）。紙の上の箱・紙なしは従来どおり停止。

### 4s. デモ想定の反映とAI周期の短縮（2026-09-27 20:00、PCテスト PASS）
想定：ゆるいカーブだけのコースを完全デモとする。止まる条件は ToF（100 mm 停止／50 mm 緊急）と「手前に白いコースが無くなったら TOR（出力 0）」のまま。急カーブ・紙の終わりは TOR の見せ場として使える。
- カーブで減速（M33 `control_motor.c`）：前進分 ×（1 − 0.4 × |舵| / 最大舵）。最大舵で 60 %。カメラの遅れで曲がりきれないのを減らす。
- AI 周期の短縮（M85 `mipi_csi.c`, `user_config.h`）：人・車の NPU 推論を 2 フレームに 1 回（`AI_DETECT_EVERY_N_FRAMES`=2）。間のフレームは新しい画像でコース判定だけを行い、人・車の枠は前回の結果を使う。コース情報が M33 に届く間隔が約半分になる見込み。代わりに、新しく現れた人・車の検出が最大 1 フレーム遅れる（ToF・STOP/ESTOP は影響なし）。1 に戻せば従来どおり。
- 効果は `g_ai_stage_us[8]`（周期）と `[2]`（推論を含む検出）で実機計測する。

### 5. 次にやること
0. `MAKE_EVIDENCE.cmd`（クリーンビルド＋テスト＋verify）でARMビルドが通るか確認 → `CHECK_BEFORE_FLASH.cmd`。
1. VM を外して書き込み（計測変数入り・最適化1〜3入り）、`g_ai_stage_us` / `g_ai_stage_max_us` / `g_ai_arena_used_bytes` を読む（予測 1.24MB と照合）。
2. NPU推論が支配的なら：4b の候補（Size または Performance+cache450k）に差し替え、arena を内蔵SRAMへ移す（`TENSOR_ARENA_SIZE` を約0.5MBに、配置を `.sdram_noinit` から内蔵RAMへ）。`CPU0/model/README.md` と埋め込みC++配列（`tools/embed_tflite.py`）も更新。
3. CPU側（コピー・回転・前処理・路面解析）が大きければ：回転をコピーに統合、色変換のテーブル化、該当ファイルのみ `-O2`。
4. 周期が縮んだら `AI_FRAME_MAX_AGE_MS`（600ms）を縮める（AI停止→車停止の時間短縮）。
5. 最後に `make_evidence.py` → `CHECK_BEFORE_FLASH.cmd` → VM OFF で書き込み → Webで拒否理由3種・AUTO・STOP/ESTOP・TOR（引継ぎ／3秒で時間切れ）を確認。

### 6. 保管場所
- 2026-09-27 05:10、利用者の指示で `C:\TRON\demo_evidence_archive\` と `C:\TRON\demo\_backup_before_reset_only\` を削除した。本メモ内でこれらを「変更前ファイルの保存先」として挙げている箇所は、現在は存在しない（変更内容はこのメモと `REFACTOR_NOTES_JA.md` に記録）。
- `make_evidence.py` は必要になると `..\demo_evidence_archive\` を作り直し、そこへ古いログを移す。
- Vela 候補モデル：`C:\TRON\demo_vela_candidates\`（残してある）。

## リファクタ記録（2026-09-26〜27）

対象：Web ⇄ M85 ⇄ M33 のインターフェースと `control/` の型・定数。
JSON の形式、IPC のワイヤ形式（パケットサイズ・ワード配置・CRC）、安全用の閾値、タスク構成は**変更していません**。
変更前のソースは `_backup_before_refactor_20260926.tar.gz` にあります。

### 全体像（変更後）

```
browser ─JSON─> control_json.c ─> control_request_t ─(controller_if mailbox)─> m85_gateway_task.c
                                                                                  │
                         web_control_adapter.c : web_adapter_request_to_command() │  control_request_t → vc_web_t
                                                                                  ▼
                                               ipc_gateway.c (M33語彙のみ) ─> producer_api.c ─> control_ipc.c ─IPC─> M33
browser <─JSON─ control_response_t <─ web_adapter_status_to_response() <─ vc_status_t <─────────────────────── M33
```

- Web側の型は `control_protocol.h` だけ、M33側の型は `vehicle_control.h` / `control_ipc.h` だけ。
- 2つの語彙の変換は `web_control_adapter.c` の2関数だけ（どちらも純関数でホストテスト対象）。

### 変更点

#### Web ⇄ M85
| 変更前 | 変更後 |
|---|---|
| 同じリクエストが `control_request_t` と `m85_http_control_request_t`（uint32_t版）の2種類。`http_request_from_control()` で1項目ずつコピー | `m85_http_control_request_t` を削除し `control_request_t` に一本化 |
| `web_control_adapter.c` に `CLIENT_MANUAL=0U…` など enum 値を手書きで二重定義 | `VEHICLE_*` / `MODE_REQUEST_*` を直接使用 |
| M33ステータス→Web応答の変換が `m85_gateway_task.c` 内の static 関数（テスト不可） | `web_adapter_status_to_response()` として adapter へ移動し、テスト追加 |
| `ipc_gateway_submit_web()` が HTTP 型を受けて内部で変換 | `vc_web_t` を受け取る（ゲートウェイはM33語彙のみ）。変換は `m85_gateway_runtime_submit_web()` で実施 |
| 名前表のサイズが `[4]` `[6]` や `< 4` などのリテラル | 各 enum に `*_COUNT` を追加し、表のサイズ・範囲チェック・JSONのキー数を派生。`_Static_assert` で表と enum の不一致を検出 |
| `control_json.c` のキーが `case 0..7`、`seen != 255` | `KEY_*` 列挙と `all_keys` に置換 |
| `control_if_get_request()`（未使用） | 削除 |
| `m85_gateway_task.c` のループが1関数に全部入り | `forward_urgent_request()` / `forward_latest_request()` / `publish_status()` に分割（順序・条件は同じ） |

#### control/（M33・IPC）
- `vehicle_control.h`：無名 enum を `vc_state_t` / `vc_mode_t` / `vc_reason_t` / `vc_web_action_t` として型名付きに。構造体フィールドは IPC・メッセージバッファのABIのため `uint32_t` のまま、コメントで対応型を明記。
- `vc_arm_result_t` を追加し、`g_vc_start_result` / `g_vc_clear_result` の 1〜7 のマジックナンバーを置換（値は同じなのでデバッガでの読み方は従来どおり）。extern 宣言もヘッダーへ移動。
- `VC_PERMILLE_MAX`、`vc_permille_ok()`、`vc_web_action_is_stop()` を追加し、各所の `1000` や `action==STOP||action==ESTOP` を置換。
- `control_ipc.h`：パケットごとのワード配置を enum で定義（`IPC_WEB_*`、`IPC_STATUS_*`、`IPC_AI_*`）。`data[3]` のような添字や `words==12`、`24`、`10000.0F` を名前付き定数に。`IPC_OK/IPC_EMPTY/IPC_ERROR/IPC_BUSY` を追加（値は従来の 0/1/-1/-2）。
- `ipc_pack_heartbeat()` / `ipc_unpack_status()` を追加。これまで `producer_api.c` に直書きだったハートビート作成とステータス解読を `control_ipc.c` に集約。
- `ipc_pack_web()` が STOP/ESTOP を直接 `IPC_STOP` スロット向けに作るよう変更（従来は producer 側で type を書き換えて CRC を再計算していた）。
- `control_runtime.c`：ハートビート判定の `300` を `VC_LINK_FRESH_MS`（同値）、`words==0` を `IPC_HEARTBEAT_WORDS`、タスク数 `7` を `TASK_COUNT` に置換。

### 挙動に関わる小さな変更（許可いただいた範囲）
- `producer_get_status()`：従来は範囲外の left/right を検出する前に `status` の前半10項目を書き換えていた（失敗時に中途半端な値が残る）。解読前に検査するよう変更。成功時の結果は同じ。

### テスト
- `python test_host.py`：既存5項目＋新規 `web_protocol`（JSON）すべてPASS（`-Wall -Wextra -Werror -pedantic`）。
- `test_web_adapter.c` を拡充：範囲外・NaN・未知 enum の拒否、ESTOP が後続 STOP で格下げされないこと、送信時の seq/timestamp 付与、ステータス→応答の全分岐。
- 変更した CPU0/CPU1 のファイルは実ヘッダー（µT-Kernel/FSP）を使ってホスト gcc で構文チェック済み。**ARM LLVM でのビルドは Windows 側でしか実行できないため未実施です。**

### 再ビルド手順（Windows）
ソースが変わったので、MANIFEST の再生成が必要です。
```text
python build.py --core CPU0 --clean --profile vehicle-output --allow-physical-output
python build.py --core CPU1 --clean --profile vehicle-output --allow-physical-output
python manifest.py --profile vehicle-output
python verify.py --profile vehicle-output
python test_host.py
```
`CHECK_BEFORE_FLASH.cmd` は manifest を再生成しないため、上記の後に実行してください。

### 追加修正（同日）
- **起動設定ファイルのユーザーパス**：`CPU0/ra8p1_vision_BothCore_Download.launch` から、e² studio の IO レジスタビューが書き込んだ `registerSelection0`（`C:\Users\<ユーザー名>\...\R7KA8P1AD.svd` を含む表示状態のみ）を削除。`verify.py` の起動設定チェックは通過を確認。IO レジスタビューでレジスタを選択すると e² studio が再び書き込むので、配布前に確認してください。
- **起動直後の仮距離 1200mm**：`controller_if.c` の初期値を `-1`（未取得）に変更。UI も初期値を `null` / HTML を `--` にし、負の値は `--` と表示するよう `ui.js` を修正。`fsdata.h` を再生成（生成手順は `generate_web.ps1` と同じで、変更前ソースから再生成するとバイト単位で一致することを確認済み）。`docs/protocol.md` に `-1` の意味を追記。
- **Web指令タイムアウト**：`VC_WEB_TIMEOUT_MS` を設計値 300ms に戻しました。台上試験で 1000ms が必要な場合だけ `-DVC_WEB_TIMEOUT_MS=1000U` で上書きします（床上走行では不可）。注意：スマホ→基板の HTTP 間隔が 300ms を超えると MANUAL 走行は COMM_TIMEOUT で停止します（安全側）。
- **CPU0 のリンクエラー**：9/25 の `autonomy_controller.c` 改修で `autonomy_controller_service()` が消え、NPU ドライバー（`ethosu_driver.c`）の呼び出しが未解決になっていたため、空関数として復活（IPC/ハートビートは gateway タスクが担当）。
- **検証結果（2026-09-26）**：e² studio の Solution ビルドで CPU0/CPU1 とも vehicle-output で成功。`manifest.py` 再生成、`verify.py` 全項目 PASS、`test_host.py` PASS。`hardware_verified=false` のまま（実機未確認）。

### 実機調整とリファクタ（2026-09-26 夜）

#### 実機で見つかった不具合と修正
- **AUTOがどの路面でも開始できない**：M85のAI結果がM33に届く周期は実測300〜500ms、撮影から到着まで約160ms。M33のAI鮮度・経路追従タイムアウトが250msだったため、フレームの合間に毎回 `AI_STALE` となり `good_frame_count` が3に届かず、`g_vc_start_result=4`（PATH_NOT_READY）で拒否され続けていた。
  - 修正：`AI_FRAME_MAX_AGE_MS=600`（`VC_AI_FRESH_MS` / `VC_AUTO_AI_TIMEOUT_MS` はこれを参照）。AI停止からAUTO停止までの最大時間は約0.6秒になる。ToF（100ms）とSTOP/ESTOPは変更なし。
  - 回帰テスト `test_slow_ai_period`：300ms周期・160ms遅延でAUTOが成立し、AI途絶で停止すること。250msに戻すと失敗することを確認済み。
- **起動構成のReset_Handlerブレークポイント**：リセットのたびにCPU0が起動直後で停止し、Webサーバーが起動しなかった。`ra8p1_vision_BothCore_Download` のStartupタブで無効化（自動Resumeは従来どおり無効）。以後、書き込み完了と同時にファームが動くため、書き込み前にVMを外すこと。

#### リファクタ（IPCのワイヤ形式・パケット配置は不変）
- **AUTO拒否理由の区別**：`VC_AUTO_START_REFUSED` を `VC_AUTO_REFUSED_TOF` / `_LINK` / `_PATH`（値8〜10）に分割し、`vc_reason_is_auto_refusal()` を追加。Web応答は `SENSOR_NOT_READY`（ToF）/ `LINK_NOT_READY` / `PATH_NOT_READY`（新規、既存値の後ろに追加）。UI表示は「距離センサー(ToF)が準備できていません」「M85とM33の通信が途切れています」「走行路を認識できていません」。
- **しきい値の一元化**：両コア共通の `control/include/ai_control_signals.h` に `AI_PATH_CONFIDENCE_ENTER_PER_MILLE=650` / `AI_PATH_CONFIDENCE_HOLD_PER_MILLE=500` / `AI_FRAME_MAX_AGE_MS=600` を定義。M85の `NAVIGATION_MIN_ROAD_CONFIDENCE_PER_MILLE` はHOLDと一致しないとコンパイルエラー、`PERIODIC_IMAGE_INTERVAL_MS`＋実測処理時間(400ms)が `AI_FRAME_MAX_AGE_MS` を超えてもコンパイルエラー。値は変更なし（0.5/0.65は意図的なヒステリシス）。テスト `test_path_confidence_band` 追加。
- **Web資産の再生成**：`M85Web/script/generate_web.sh`（generate_web.ps1のLinux版、ファイル順をソートしてWindows版とバイト一致を確認済み）で `fsdata.h` を再生成。
- **証拠ログの自動生成**：`make_evidence.py` を追加。クリーンビルド→manifest→host/JSテスト→verifyを実行し、実出力とELF/stampのハッシュから `docs/demo_autofix_20260926_*.log` を作成。verify.pyが拒否する `JLinkLog.log` と9/22の旧ログは削除せず `..\demo_evidence_archive\` へ移動する。e2 studioの外部ツール `demo_make_evidence` から実行できる。

#### 未解決・注意
- `VC_WEB_TIMEOUT_MS`：既定値が1000ms（9/25台上試験の値）に戻っていたため、利用者の判断で設計値300msに戻した（2026-09-26 23:40）。スマホ→基板のHTTP間隔が300msを超えるとMANUAL走行はCOMM_TIMEOUTで停止する（安全側）。台上で必要な場合のみ `-DVC_WEB_TIMEOUT_MS=1000U`。
- 実機（VM OFF）での確認：600ms化後、M33経路追従が state=AUTO / reason=NONE / good_frame_count=3 に到達（メモリー読み出し）。Web接続（192.168.2.200）後の操作は利用者が良好と確認。VM ONでのモーター回転は未確認。`hardware_verified=false` のまま。

### AI処理の高速化（2026-09-27、ARMビルド・実機とも未検証）
- 待ち時間 `PERIODIC_IMAGE_INTERVAL_MS` 100→10ms、工程別計測 `g_ai_stage_us[]` / `g_ai_stage_max_us[]` / `g_ai_stage_frames`、`g_ai_arena_used_bytes` を追加（`mipi_csi.c`, `ai_model.cc`, `user_config.h`）。
- 結果不変の最適化：後処理のsigmoid表引き・早期スキップ、前処理の余白のみ塗り・割り算削減・色変換表（`obstacle_kernels.c` に分離）、ホットスポット3ファイルのみ `-O2`（`build.py`）。
- PCテスト `test_obstacle_kernels.c` で旧実装と完全一致を確認（前処理9形状、後処理400グリッド）。詳細は `STATUS_20260927_JA.md` 4c。
- 未実施：ARM LLVMビルド、実機での速度計測・動作確認、`-O2` のスタック影響評価、証拠ログ再生成。

- 2026-09-27 05:10：利用者の指示で `demo_evidence_archive` と `_backup_before_reset_only`（変更前ファイルの控え・古いログ）を削除。以前の記述にあるこれらの保存先は現存しない。

- 2026-09-27 06:30：TOR を実装。AUTO 継続不可で出力 0 の `VC_TOR` に入り、3000 ms で `VC_TOR_TIMEOUT` 停止（MANUAL 引継ぎ可、自動復帰なし）。M85 はM33時計基準の `tor_remaining_ms` と `TOR_TIMEOUT`/`AUTO_ABORT` を返す。PCテスト PASS、ARMビルド・実機は未実施。詳細は `STATUS_20260927_JA.md` 4i。
- 2026-09-27 06:40：構造体とエラー処理を整理（挙動・IPC形式は不変）。`vc_inputs_t`、`vc_input_result_t`/`vc_arm_result_t` を戻り値に、`ipc_result_t`、`vc_fence_web()`、M33 ランタイムの `fail_safe()` と故障原因 `g_control_fault`。修正：status の CRC 未確認、`ipc_gateway_init()` のミューテックス漏れ。詳細は `STATUS_20260927_JA.md` 4j。
- 2026-09-27 07:00：KISS。1実装だけの `motor_output_backend` 間接層、常に無効だった Web ループバック試験タスク、呼ばれていない関数8つを削除（挙動不変）。詳細は `STATUS_20260927_JA.md` 4k。
- 2026-09-27 07:15：demo 直下を整理。スクリプトは `tools/`、資料・MANIFEST は `docs/`、実行口は `MAKE_EVIDENCE.cmd` と `CHECK_BEFORE_FLASH.cmd`。`.project` のビルド引数は `../tools/build.py`。

## AUTO/AI 修正記録（2026-09-26）

### 対象と位置づけ

元入力は `demo.zip`（SHA-256 `128D91DFC49D40A599CD730D150E10C25D5A9C87D2B9D19AF7F66DAA9C0E6D55`）で、元ZIPは変更していません。この文書と同じZIPに含まれるCPU0/M85・CPU1/M33・Solutionを一組として使用します。CPU0/CPU1のビルドプロファイルは `vehicle-output` です。物理モーター出力が有効になり得るため、最初の書き込み・試験ではDRV8833のVMを必ず外します。

修正対象は、元版の実機試験で「MANUALとToF Emergencyは動いたが、AUTO切替がタイムアウトし、人体・車両のAI Emergencyとログを確認できなかった」事象です。元版での実機観察は今回の新ELFの実機合格を意味しません。今回の新ELFの基板書き込み・VM通電・実走行は未実施で、Manifestの `hardware_verified` は `false` のままです。

### 設計上の修正

- AUTO開始拒否をM33状態の `VC_AUTO_START_REFUSED` としてWebへ伝え、ToF・通信・AI経路未準備を単なるタイムアウトと区別します。成功はM33がAUTOへ入った状態で判定し、要求番号だけの進行では成功扱いにしません。
- STOP/ESTOPはWeb/AI送信より優先します。M85 IPC gatewayは緊急停止後の古いDRIVE/MODEを破棄し、M33が停止指令番号を状態へ返し、無武装・左右出力ゼロを確認するまで新しい通常指令を通しません。M33は停止前にキューへ入ったDRIVE/MODEも指令番号で拒否します。
- M85の画像認識はCOCO全80クラスの最上位候補を決めてから、走行に使うperson/bicycle/car/motorcycleだけを採用します。その他クラスを無理に人や車として扱いません。
- M33は有効で新しい走行コリドー内の強い障害物を、路面認識欠落より先に評価します。AI障害物による `VC_AI` Emergencyでは出力をゼロにし、Web resetだけで障害物が残るEmergencyを解除しません。障害物除去後も自動再発進はしません。
- AI検知件数と停止理由を低優先のUSB/PCビューア診断行 `AI_DETECTION,...` とデバッガ変数に公開します。Web映像配信は未実装で、今回の制御検証には追加していません。

### オフライン検証と実機再試験

CPU0/CPU1のクリーンビルド、host test、Manifest/hash・ELF/map検査の当日証跡は `demo_autofix_20260926_cpu0.log`、`demo_autofix_20260926_cpu1.log`、`demo_autofix_20260926_host_tests.log`、`demo_autofix_20260926_verify.log` を参照してください。ビルドログには対応するELFとプロファイルスタンプのSHA-256を記録します。古い2026-09-22ログはこの版の受入証跡ではありません。

実機では `AUTO_AI_RETEST_20260926_JA.md` の順に、VM OFFでAUTO成功/拒否、STOP/ESTOP、AIのperson/car件数、M33 `VC_AI` Emergency、ToF Emergency、無出力と再発進禁止を確認します。VM OFFで全て合格するまではVM ONへ進みません。Webのカメラ映像欄は未提供で、AIの稼働確認は診断行またはデバッガ変数で行います。
