# NOTICE / 第三者ソフトウェアの表示

This repository contains third-party software. Each component keeps its own
copyright notice and license in its source files. / 各コンポーネントの著作権表示とライセンスは各ソースに記載のとおりです。

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
- **YOLO-Fastest v1.1 COCO80 model** (`CPU0/model/`, `CPU0/src/yolo_fastest_streets_vela.cc`) — obtained from
  https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection ; original model: https://github.com/dog-qiuqiu/Yolo-Fastest
  (YOLO LICENSE); related wrapper/postprocessing Apache-2.0. Trained on MS COCO.

See `CPU0/THIRD_PARTY_NOTICES.md` and `docs/LICENSE_SURVEY.md` for details.
