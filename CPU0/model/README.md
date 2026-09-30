# Object Detection Model

This project embeds `yolo_fastest_coco80_int8_320_vela_speed.tflite`.

- Architecture: YOLO-Fastest v1.1
- Dataset/classes: COCO, 80 classes
- Input: `1 x 320 x 320 x 3`, signed INT8 RGB
- Outputs: `1 x 20 x 20 x 255` and `1 x 10 x 10 x 255`
- Quantisation: full INT8
- NPU target: Ethos-U55-256
- Vela optimisation: Performance
- Embedded model size: 525,408 bytes
- SHA-256: `E7E0782B8180A0EBC52B9714423E991D40896A1778627D1C3C78C121272F27B0`

The model contains 80 COCO classes. Firmware postprocessing retains only:

| COCO ID | Label |
| ---: | --- |
| 0 | person |
| 1 | bicycle |
| 2 | car |
| 3 | motorcycle |

## Provenance and redistribution status

Public-source review and binary comparison: 2026-09-30. No permission request has been sent to the distributor.

- Original model family: [YOLO-Fastest v1.1](https://github.com/dog-qiuqiu/Yolo-Fastest), whose original repository contains [YOLO LICENSE](https://github.com/dog-qiuqiu/Yolo-Fastest/blob/master/LICENSE).
- Immediate source: [OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection](https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection/tree/d59b7a06e62c3aeb9819e1f037ca937092753497).
- The [source README](https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection/blob/d59b7a06e62c3aeb9819e1f037ca937092753497/README.rst) identifies [OpenNuvoton/ML_YOLO](https://github.com/OpenNuvoton/ML_YOLO/tree/6fc427af20fc97669ef4443d45183c1db87eddaa/yolo_fastest_v1.1) as the model's upstream repository. Its link named `ML_ZOO` points to ML_YOLO, not Arm ML Zoo. The application code is described as originating from Arm's ML embedded evaluation kit.

The local TFLite file is byte-identical to the immediate source's [src/Model/yolo-fastest_int8_ethos-u55-256_opt-speed.tflite](https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection/blob/d59b7a06e62c3aeb9819e1f037ca937092753497/src/Model/yolo-fastest_int8_ethos-u55-256_opt-speed.tflite): 525,408 bytes, SHA-256 `E7E0782B8180A0EBC52B9714423E991D40896A1778627D1C3C78C121272F27B0`. The source README describes generating that file using Vela's Performance optimisation.

ML_YOLO contains an [Apache-2.0 license](https://github.com/OpenNuvoton/ML_YOLO/blob/6fc427af20fc97669ef4443d45183c1db87eddaa/LICENSE.txt). Its [generated C++ model file](https://github.com/OpenNuvoton/ML_YOLO/blob/6fc427af20fc97669ef4443d45183c1db87eddaa/yolo_fastest_v1.1/vela/generated/yolo-fastest-1.1-int8_vela.tflite.cc) carries an Apache-2.0 notice and contains actual model bytes. The extracted array is 516,784 bytes with SHA-256 `790318CDD891DFD4476ECDCE9475B2700BE3314B7FA148B94DBBF6B6A0292EAB`; it does not exactly match the local optimized binary. The immediate source's wrapper and postprocessing code also carry Apache-2.0 notices.

These are licensing clues, not confirmation of the terms applicable to the exact optimized binary and its trained weights. No explicit statement resolving that scope was found in the reviewed sources. Before redistribution, ask the distributor to confirm the applicable license and required notices for use on Renesas EK-RA8P1 and publication of the TFLite file, embedded C++ array, and model-containing ELF files. The model is not relicensed under this project's MIT License.

If Apache-2.0 is confirmed to apply, redistribution must comply with its Section 4, including supplying the license, marking modified files, preserving applicable copyright and attribution notices, and carrying forward any applicable upstream NOTICE text.

## Regenerating the embedded array

Regenerate the embedded C++ array with:

```powershell
python tools/embed_tflite.py `
  model/yolo_fastest_coco80_int8_320_vela_speed.tflite `
  src/yolo_fastest_streets_vela.cc
```
