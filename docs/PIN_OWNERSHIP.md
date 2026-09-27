# EK-RA8P1 ピン・資源所有表

| 資源 | CPU0/M85 | CPU1/M33 | 備考 |
|---|---|---|---|
| P006 / PMOD1-7 | 未設定・変更しない | DRV8833 AIN1（右モーター） | M85 FSP pin tableから除外 |
| P402 / PMOD1-8 | 未設定・変更しない | DRV8833 AIN2 | SSIと共有しない |
| P412 / PMOD1-9 | 未設定・変更しない | DRV8833 BIN1（左モーター） | M85から触らない |
| P413 / PMOD1-10 | 未設定・変更しない | DRV8833 BIN2 | M85から触らない |
| P511/P512 / IIC1 | RPC要求を仲介するだけ | ToFの実IICアクセス | M33専有。カメラ設定もM33仲介 |
| P009 / SW1、P008 / SW2 | 所有しない | 使用しない | RC ビルドは設定も読み取りもしない。走行開始は Web の操作だけ。電源投入だけでは発進しない |
| GPT0 | 所有しない | 100 µsモーターティック | PWM ISRはM33 |
| HSEM/shared RAM | IPC送信 | IPC受信・安全判定 | 共有RAM 0x221D3000–0x221D3FFF、NOLOAD |
| OV5640/MIPI/NPU/SDRAM | 所有 | 所有しない | AI結果だけをM33へ送る |
| Ethernet/lwIP/HTTPD | 所有 | 所有しない | Webは意図を送るだけ |

AIN=右・BIN=左は `MOTOR_SWAP_SIDES=1`（`control/include/motor_build_profile.h`）による。M33だけが最終左右モーター指令を決める。AI、Web、Ethernet、カメラの停止や遅延で安全条件を迂回できない。
