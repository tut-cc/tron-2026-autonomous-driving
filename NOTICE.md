# NOTICE / 第三者ソフトウェアの表示

Original project code is licensed under the MIT License; see [LICENSE](LICENSE). Third-party software and the AI model retain their own terms and are not relicensed under this project's MIT License.
本プロジェクトのオリジナルプログラムは[MITライセンス](LICENSE)で公開しています。第三者のソフトウェアとAIモデルには、それぞれの権利条件が適用されます。

- **μT-Kernel 3.0 / μT-Kernel 3.0 BSP2** (TRON Forum) — `CPU0/mtk3_bsp2`, `CPU1/mtk3_bsp2`
  本ソフトウェアはトロンフォーラムの μT-Kernel 3.0 のソースコードを T-License 2.2 に基づいて利用しています。
  This software uses the source code of μT-Kernel 3.0 by TRON Forum under T-License 2.2.
  T-License 2.2: https://www.tron.org/download/index.php?route=information/information&information_id=79
- **Renesas Flexible Software Package (FSP)** — BSD-3-Clause, © Renesas Electronics Corporation.
- **Arm CMSIS 6, CMSIS-NN, CMSIS-DSP, CMSIS-View, TensorFlow Lite Micro, FlatBuffers, Arm Ethos-U driver** — Apache-2.0.
- **lwIP** — BSD-3-Clause, © Swedish Institute of Computer Science.
- **SEGGER RTT** — BSD-3-Clause, © SEGGER Microcontroller GmbH.
- **STMicroelectronics VL53L1X Ultra Lite Driver** (`CPU1/src/tof/`) — dual licensed; used here under the
  **BSD-3-Clause** option stated in the file headers. © STMicroelectronics.
- **`CPU0/src/prebuilt/camera_sensor.o`** (OV5640 configuration, object code only) — built from `camera_sensor.c`
  of a Renesas EK-RA8P1 example project (BSD-3-Clause, © Renesas Electronics Corporation). Redistribution confirmed on 2026-09-28; included in the repository.
- **YOLO-Fastest v1.1 COCO80 model** (`CPU0/model/`, `CPU0/src/yolo_fastest_streets_vela.cc`) — original model family:
  https://github.com/dog-qiuqiu/Yolo-Fastest (YOLO LICENSE). The NPU-optimized model used here was obtained through
  https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection . The original license does not establish the
  redistribution terms of this optimized model; those terms remain unconfirmed. Related wrapper/postprocessing code
  carries its own Apache-2.0 notices.

See [CPU0/THIRD_PARTY_NOTICES.md](CPU0/THIRD_PARTY_NOTICES.md) and [CPU0/model/README.md](CPU0/model/README.md) for details. Retain the license files and copyright notices shipped with each component.

## オリジナルプログラムとAIモデル

オリジナルプログラムの著作権者は `tron-2026-autonomous-driving contributors` です。改造・再配布の際は、著作権表示とMITライセンス本文を残してください。

NPU向け最適化モデルの再配布条件は未確認です。再配布前に取得元へ確認し、条件を確認できない場合は、再配布条件が明確なモデルへの差し替えを検討してください。第三者ソフトウェアに同梱されたライセンスと著作権表示は残してください。
