# Third-Party Notices

This project contains Renesas FSP, Arm CMSIS, TensorFlow Lite Micro,
FlatBuffers, Ethos-U software, model files, and code derived from sample
projects. Refer to the license headers and license files shipped with each
component before redistribution.

## YOLO-Fastest COCO80 model

- Original architecture: YOLO-Fastest v1.1, https://github.com/dog-qiuqiu/Yolo-Fastest (YOLO LICENSE)
- NPU-optimized model source: https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection
- Model lineage stated by that source: OpenNuvoton/ML_YOLO, https://github.com/OpenNuvoton/ML_YOLO/tree/master/yolo_fastest_v1.1. The application code, not the model, is described as originating from Arm's ML embedded evaluation kit.
- Binary identity: the embedded TFLite file matches the source's `src/Model/yolo-fastest_int8_ethos-u55-256_opt-speed.tflite` by SHA-256; see [model/README.md](model/README.md).
- Upstream model licensing evidence: ML_YOLO contains an Apache-2.0 license and an Apache-2.0-marked C++ file containing model data. That file's model bytes do not exactly match the optimized binary used here.
- Upstream model wrapper and postprocessing license: Apache-2.0
- Modification: C++ embedding and selection of four COCO road-user classes
- Model redistribution terms: applicability to the exact optimized binary and its trained weights remains unconfirmed. Do not infer that this model is relicensed under the project's MIT License or that the wrapper's Apache-2.0 notice alone licenses the model. Confirm the applicable terms with the distributor before redistribution.

## VL53L1X Ultra Lite Driver

- Source family: STMicroelectronics VL53L1X Ultra Lite Driver
- Use: Unit ToF4M ranging API and platform adapter
- The API source offers a BSD-3-Clause option, which this project uses.
  Retain the upstream license headers when redistributing it.

## Motor-control reference

The control interface and state-machine structure were adapted from
non-public team reference material.
