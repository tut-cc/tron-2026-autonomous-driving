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

Source integration and precompiled model:
`https://github.com/OpenNuvoton/NuMaker-Zephyr-TFLM-ObjectDetection`

The source repository states that the model came from Arm's ML embedded
evaluation kit and marks its model wrapper and postprocessing as Apache-2.0.
Review upstream licensing before redistribution.

Regenerate the embedded C++ array with:

```powershell
python tools/embed_tflite.py `
  model/yolo_fastest_coco80_int8_320_vela_speed.tflite `
  src/yolo_fastest_streets_vela.cc
```
