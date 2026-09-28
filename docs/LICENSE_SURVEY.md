# 第三者コンポーネントのライセンス調査（2026-09-27）

対象：GitHub 公開リポジトリ `tut-cc/tron-2026-autonomous-driving` へ本パッケージ（demo）を登録する前の確認。
法的助言ではない。同梱ファイルのライセンス表記と公開情報を確認した結果のまとめ。

| コンポーネント | 場所 | ライセンス | 公開リポジトリでの再配布 | 必要な対応 |
|---|---|---|---|---|
| Renesas FSP（BSP・ドライバ・生成コード） | `CPU*/ra/fsp`, `ra_gen`, `ra_cfg` | BSD-3-Clause（各ファイルに SPDX 表記） | 可 | 著作権表示を残す（そのまま） |
| Arm CMSIS 6 / CMSIS-NN / CMSIS-DSP / CMSIS-View | `CPU*/ra/arm` | Apache-2.0（`CMSIS_6/LICENSE` 同梱） | 可 | LICENSE・表示を残す |
| TensorFlow Lite Micro, FlatBuffers, Ethos-U ドライバ, ML eval kit 由来のラッパ | `CPU0/ra/...`, `CPU0/src/ai/common` | Apache-2.0 | 可 | 表示を残す。変更したファイルは変更した旨が分かるように |
| μT-Kernel 3.0 / BSP2（トロンフォーラム） | `CPU*/mtk3_bsp2` | T-License 2.2 | 可。GitHub での公開は T-Kernel トレーサビリティサービスへの登録（配布ucode）不要（T-License 2.x FAQ Q14） | **T-License 2.2 本文（またはリンク）を同梱**、**利用表示**（ドキュメント等に μT-Kernel 利用の表示）。本文は同梱されておらず README にリンクのみ → `NOTICE.md` に表示とリンクを追加 |
| lwIP | `CPU0/mtk3_bsp2/uct/lwip` | BSD-3-Clause（SICS） | 可 | 著作権表示を残す（そのまま） |
| SEGGER RTT | `CPU0/src/SEGGER_RTT` | BSD-3-Clause 系（SEGGER） | 可 | 表示を残す（そのまま） |
| ST VL53L1X ULD（ToF） | `CPU1/src/tof/VL53L1X_api.*` | デュアル：ST Proprietary（SLA0081）**または BSD-3-Clause を選択可** | BSD-3-Clause を選べば可 | ヘッダーの BSD-3 条項を残し、BSD-3 で利用する旨を `NOTICE.md` に明記 |
| `camera_sensor.o`（OV5640 設定、ソースなし） | `CPU0/src/prebuilt` | オブジェクト内のパス `../src\camera_sensor.c` と README の記述から、Renesas の EK-RA8P1 サンプル（ra-fsp-examples、BSD-3-Clause）由来と推定 | BSD-3 ならバイナリ再配布可 | **2026-09-28 に配布可と確認、リポジトリに同梱**。BSD-3 のバイナリ再配布条件として著作権表示・条項をドキュメントに記載 |
| YOLO-Fastest v1.1 COCO80 モデル | `CPU0/model/*.tflite`, `CPU0/src/yolo_fastest_streets_vela.cc` | 取得元 OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection はリポジトリ全体のライセンス表記を確認できず。系統元の OpenNuvoton/ML_YOLO は Apache-2.0、原典 dog-qiuqiu/Yolo-Fastest は YOLO LICENSE（Darknet、public domain 相当） | 原典は非常に緩いが、**取得元リポジトリのライセンスが未確認** | 取得元・原典を `NOTICE.md` に明記。取得元の LICENSE ファイルを直接確認するか、原典（dog-qiuqiu）から自前で変換したモデルに置き換えると確実。学習データは COCO |
| Vela 候補モデル | `C:\TRON\demo_vela_candidates`（リポジトリ外） | 同上 | 登録対象外 | — |
| 制御インタフェース（チーム内参考資料由来のコード） | `control/` | チーム内 | コードのみ公開。チーム資料（引継ぎ文書等）は**非公開**とし、パッケージ外 `C:\\TRON\\team_private\\` へ移動（2026-09-27） | — |
| 本チームの自作コード | `control/`, `CPU*/src` の一部, `M85Web`, スクリプト | **未設定**（ライセンス無し＝既定では全権利留保） | — | リポジトリのライセンス（例：MIT / BSD-3 / Apache-2.0）をチームで決める |

## 結論
- 再配布を妨げるもの（再配布禁止のライセンス）は見つからなかった。ST ドライバは BSD-3 を選べば公開可能。
- 公開前にやること：
  1. `NOTICE.md`（本調査で追加）をリポジトリに含める。μT-Kernel の利用表示と T-License 2.2 へのリンク、ST ドライバを BSD-3 で利用する旨、モデル・camera_sensor.o の出所を記載。
  2. （対応済み 2026-09-28）`camera_sensor.o` は配布可と確認し、リポジトリに同梱した。
  3. モデル取得元リポジトリの LICENSE を直接確認する（不明なら原典から自前変換）。
  4. （対応済み）チーム資料は非公開。`control/` の引継ぎ文書類はパッケージ外へ移動した。
  5. 自作部分のライセンスを決めて `LICENSE` を置く。

## 参照
- T-License 2.x FAQ（GitHub 公開時の扱い）：https://www.tron.org/download/index.php?route=information%2Finformation&information_id=57
- T-License 2.2 本文：https://www.tron.org/download/index.php?route=information/information&information_id=79
- tron-forum/mtk3_bsp2：https://github.com/tron-forum/mtk3_bsp2
- renesas/ra-fsp-examples LICENSE（BSD-3-Clause）：https://github.com/renesas/ra-fsp-examples
- Arm-Examples/ML-zoo（Apache-2.0、明示が無い限り）：https://github.com/Arm-Examples/ML-zoo
- OpenNuvoton/ML_YOLO（Apache-2.0）：https://github.com/OpenNuvoton/ML_YOLO
- OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection：https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection
- dog-qiuqiu/Yolo-Fastest（YOLO LICENSE）：https://github.com/dog-qiuqiu/Yolo-Fastest
