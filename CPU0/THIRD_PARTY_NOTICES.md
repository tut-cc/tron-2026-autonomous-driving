# Third-Party Notices

This project contains Renesas FSP, Arm CMSIS, TensorFlow Lite Micro,
FlatBuffers, Ethos-U software, model files, and code derived from sample
projects. Refer to the license headers and license files shipped with each
component before redistribution.

## YOLO-Fastest COCO80 model

- Original architecture: YOLO-Fastest v1.1, https://github.com/dog-qiuqiu/Yolo-Fastest (YOLO LICENSE)
- NPU-optimized model source: https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection
- Model lineage stated by that source: Arm ML embedded evaluation kit / ML Zoo
- Upstream model wrapper and postprocessing license: Apache-2.0
- Modification: C++ embedding and selection of four COCO road-user classes
- Model redistribution terms: not confirmed from the OpenNuvoton source. The original YOLO LICENSE and the wrapper's Apache-2.0 notice do not by themselves establish the terms for the optimized model.

## VL53L1X Ultra Lite Driver

- Source family: STMicroelectronics VL53L1X Ultra Lite Driver
- Use: Unit ToF4M ranging API and platform adapter
- The API source offers a BSD-3-Clause option, which this project uses.
  Retain the upstream license headers when redistributing it.

## Motor-control reference

The control interface and state-machine structure were adapted from
non-public team reference material.
