# 開発者向け：ビルド・書き込み・検証

この文書はファームを作り直す人向けです。

## 環境

- e² studio 2026-04.2、FSP 6.5.0、Arm Toolchain for Embedded LLVM 21.1.1（e² studio 同梱）
- Python 3、Node.js（Web の状態遷移テスト）、ホスト用 GCC（PC テスト）
- 展開先は英数字だけの短いパス（例 `C:\TRON\demo`）。
- 見つからない場合は環境変数で場所を指定します：`LLVM_ARM_BIN`（`clang.exe` のフォルダ）、`HOST_CC`（`gcc.exe`）、`NODE`（`node.exe`）。

## ビルドと書き込み前チェック

手順は2つだけです（2026-09-28 に整理。旧 `MAKE_EVIDENCE.cmd` / `CHECK_BEFORE_FLASH.cmd` は削除）。どちらも基板には書き込みません。

| いつ | e² studio での操作 | 中身 |
|---|---|---|
| 開発中 | ビルドボタン | `tools/build.py --core all`（ビルドだけ） |
| 本番の書き込み前 | リポジトリ直下で `python -B tools/make_evidence.py` | 両コアのクリーンビルド → MANIFEST → PC テスト（C / JS）→ 検証。ログと MANIFEST.json は `docs\`（`--resume` でビルドを再利用） |

既存の開発PCには `demo_make_evidence` 外部ツール設定がありますが、その設定はGitHubに同梱されません。受取人は上記のコマンドを使ってください。`demo_manifest_verify` 単独ではクリーンビルドを検証できません。

中身は `tools/` の Python スクリプトです。

| スクリプト | 役割 |
|---|---|
| `tools/build.py` | 片側（`--core CPU0` / `CPU1`）または両方（`all`）をビルド。`--clean` で全再コンパイル |
| `tools/test_host.py` | 制御・IPC・モータードライバ・Web アダプタ・AI 前後処理の C テスト（PC 上） |
| `tools/manifest.py` | 成果物・入力のハッシュを `docs/MANIFEST.json` に記録 |
| `tools/verify.py` | ELF/map の配置、Flash 分割、共有 RAM、ピン所有、両コアのプロファイル一致、Web UI の埋め込み内容などを検査 |
| `tools/make_evidence.py` | 上の4つを順に実行し、当日のログを `docs/` に残す（外部ツール `demo_make_evidence` の中身） |
| `tools/evidence_tag.json` | ログ名に使う証拠タグと日付（`make_evidence.py` が自動更新） |

ヘッダを変えると両コアとも全再ビルドになります（CPU0 は 12〜20 分）。

### プロファイル

- `vehicle-output`（既定・e² studio のビルド引数）：モーター出力あり。`--profile vehicle-output --allow-physical-output` の両方がないとビルドできません。
- `dry-run`：モーター出力なし。静的確認用。両コアを必ず同じプロファイルで `--clean` ビルドします（混在は `verify.py` が拒否）。

```text
python -B tools/build.py --core CPU0 --clean --profile dry-run
python -B tools/build.py --core CPU1 --clean --profile dry-run
python -B tools/manifest.py --profile dry-run
python -B tools/verify.py --profile dry-run
```

## e² studio

1. 新しい workspace（例 `C:\e2ws\demo`）で File → Import → Existing Projects into Workspace。`Solution`・`CPU0`・`CPU1` の3つだけを選び、「Copy projects into workspace」はオフ。
2. ビルドは外部ビルダー（`python ../tools/build.py ...`）です。Python が IDE の PATH 上に必要です。
3. **FSP Configurator の Generate Project Content は実行しない**。両コアの実際の設定は `ra_gen` / `ra_cfg` / `Build/*.lld`、ビルド対象ファイルは各コアの `sources.json` にあります（`configuration_reference` の XML は移植前の参照用）。

## 書き込み

1. **必ずモーター電源（DRV8833 VM）を外し、車輪を浮かせる。**
2. 本番の書き込みなら、外部ツール `demo_make_evidence` が PASS していることを確認する（開発中の試し書きでは省略可）。
3. Project Explorerの `CPU0/ra8p1_vision_BothCore_Download.launch` を右クリック → Debug As → `ra8p1_vision_BothCore_Download` を使う（CPU0/CPU1 の ELF を1回で書き込む）。CPU1 単独の Download/Run は使わない（M33 は M85 が起動する）。
4. 書き込みが終わると CPU0 が Reset_Handler で止まるので F8 で再開し、「実行 → 切断」でデバッガを外す（接続したままだと CPU1 が止まることがある。原因は調査中）。起動構成は ELF をワークスペース相対（`${workspace_loc:/ra8p1_vision_CPU0}` / `..._CPU1`）で指定し、起動前の自動ビルドは無効（検証済みの ELF をそのまま書く）。
5. 書き込み後はブラウザのタブを開き直す（古い UI を使わない）。

## デバッガで見る変数

| 変数 | 意味 |
|---|---|
| `g_vc_start_result` | 最後の開始要求の結果（0 OK、1 緊急停止中、2 ToF 未準備、3 M85 通信途絶、4 AI 経路未準備、5/6 MANUAL 操作） |
| `g_vc_clear_result` | 最後の緊急停止解除の結果（0 OK、1 ESTOP または内部故障のため解除不可＝基板リセットが必要、2 ToF が 100 mm 以内・無効、3 M85 通信途絶） |
| `g_control_fault` | M33 内部故障の最初の原因（0 なし、1 初期化、2 時計、3 ロック、4 アンロック、5 停止フラグ、6 キュー、7 遅延、8 モータードライバ、9 走行指令の遅れ） |
| `g_start_count` | 走行開始の回数 |
| `g_hb_max_gap_ms` / `g_hb_gap_over300` | M85 heartbeat の最大間隔と 300 ms 超えの回数 |
| `g_ai_detector_status_code` | AI の状態（100 READY、1〜5 初期化失敗、101/102 実行失敗） |
| `g_ai_person_count` / `g_ai_car_count` | 検出数 |
| `g_ai_stage_us` / `g_ai_stage_max_us` | AI 処理の段階ごとの時間（µs、最新と最大） |
| `g_ai_arena_used_bytes` | AI（TFLite Micro）が実際に使ったメモリ |

## 注意

- M85 のカメラセンサー設定は元プロジェクト由来の `camera_sensor.o`（ソースなし）を使っています。2026-09-28 に配布してよいことを確認し、`CPU0/src/prebuilt/camera_sensor.o` としてリポジトリに含めています（[NOTICE.md](../NOTICE.md) 参照）。
- PC 上の検査は基板動作の証明ではありません。`MANIFEST.json` の `hardware_verified` は常に `false` です。実機確認は `HARDWARE_TEST.md` の手順で行います。
- Web 構成の詳細は `M85Web/Application/README_HTTP.md`、通信仕様は `M85Web/Application/mini-4wd-webapp/docs/protocol.md`。
