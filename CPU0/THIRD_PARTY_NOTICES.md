# Third-Party Notices

This project contains Renesas FSP, Arm CMSIS, TensorFlow Lite Micro,
FlatBuffers, Ethos-U software, model files, and code derived from sample
projects. Refer to the license headers and license files shipped with each
component before redistribution.

## YOLO-Fastest COCO80 model

- Source: https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection
- Original model family: Arm ML embedded evaluation kit YOLO-Fastest v1.1
- Upstream model wrapper and postprocessing license: Apache-2.0
- Modification: C++ embedding and selection of four COCO road-user classes
- Note: verify the upstream model redistribution terms for the intended product

## VL53L1X Ultra Lite Driver

- Source family: STMicroelectronics VL53L1X Ultra Lite Driver
- Use: Unit ToF4M ranging API and platform adapter
- The API source retains its upstream file notices. Confirm the current ST
  licensing terms before product redistribution.

## Motor-control reference

The control interface and state-machine structure were adapted from the local
non-public team reference material. No instructions in that
material were treated as executable commands.
