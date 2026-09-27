# 両コア起動と受入確認

## 配置・所有

| 項目 | M85 / CPU0 | M33 / CPU1 |
|---|---|---|
| 内蔵コード領域 | 0x02000000–0x020EFFFF（960 KiB） | 0x020F0000–0x020FFFFF（64 KiB） |
| 内蔵RAM | 0x22000000–0x2217FFFF | 0x22180000–0x221D2FFF |
| 共有RAM | 両コアとも0x221D3000–0x221D3FFF、NOLOAD、MPU region 7/attribute 7で非キャッシュ |
| 共有同期 | 制御IPC HSEM3、カメラ要求 HSEM4。HSEM0は使わない |
| 外部SDRAM | 0x68000000から128 MiB、画像・AI用 | 使用しない |
| クロック | CPUCLK 1 GHz、共通クロック設定を所有 | CPUCLK1 250 MHz、共通クロック再初期化を省略 |
| カメラ・NPU・USB | VIN、MIPI CSI、NPU、USB、GPT12カメラクロック | 所有しない |
| IIC1 | カメラ0x3C/ボードスイッチ0x43の要求を送信 | P511/P512、ToF0x29とカメラ要求の実アクセス |
| モーター | GPIOを変更しない | PMOD1 P006/P402/P412/P413、GPT0 100 µs周期 |
| 操作スイッチ | 読み取らない | 使用しない（SW1/SW2 は設定しない。開始は Web 操作） |
| 割り込み | NPU/USB/VIN/MIPI/表示/UART | GPT0 IRQ0、IIC1 IRQ6–9、kernel SysTick/PendSV/SVC |

共有ヘッダー128バイトの後ろに添付の `ipc_shared_t` を配置します。制御IPCの仕様は変更せず、カメラ仲介メールボックスを別に追加しています。

## 起動手順

1. M85のFSP起動処理がクロック・M85所有ピン・SDRAMを設定する。
2. M85が共有RAMの古いreadyとIPC magicを無効化する。保持された起動カウンターと補数が一致する場合は加算し、不一致時は1から開始する。0は使用しない。
3. M85が `R_BSP_SecondaryCoreStart()` でM33のベクターアドレス0x020F0000を設定して起動する。CPU0/CPU1のFlash分割はSolution、Build/memory_regions.lld、bsp_linker_info.hで同じ値に固定している。
4. M33は自領域の初期化後、共有RAMを非キャッシュ設定にする。µT-KernelがROMベクターを512バイト境界のRAMテーブルへ移し、SysTick/PendSV/SVCを設定する。
5. FPU属性付きの2タスクを開始する。異なる周期で20回ずつ待ち、s16の値が保持されるか確認する。失敗すれば `fault` を立て、制御は開始しない。
6. 制御タスクを開始し、M33が制御IPCを初期化する。IIC1専用タスクがToFの初期設定を試みた後、readyを公開する。ToF失敗時もカメラ通信は可能だが、距離が無効なので走行は許可されない。
7. M85が同じ起動セッションでattachし、カメラ設定・AI処理に進む。

保持カウンターは両コア同時のウォームリセットで旧データを区別するためのものです。電源断時の永続カウンターではありません。片側だけのリセット、デバッガによる片側だけの再ダウンロードは対応外です。共有RAMを保持するリセット条件・冷間起動時のRAMアクセスは基板で確認してください。

## µT-Kernelの移植箇所

`_RAFSP_EK_RA8P1_M33_` を追加し、ARMv8-M/Cortex-M33、250 MHz、M33専用RAM範囲にしています。元のM85定義は保存しています。ARMv8-Mの実dispatcherを使用し、s16–s31、例外復帰情報、MSPLIMを保存・復元します。FPUを全タスクに有効化しています。

システムメモリは128 KiBの静的領域です。例外テーブルの要素数を112へ修正し、VTOR変更後にDSB/ISBを追加しています。IIC割り込みはµT-Kernelの高級言語割り込みラッパーを通して完了フラグを通知します。GPT割り込みはPWM更新のみを実行します。

## 書き込み・デバッグの順序

本作業では書き込み・保護設定変更を実行していません。既存の基板用ELF/SRECとデバッグ設定を保存してから進めてください。

1. まずモーター電源を切り、dry-runプロファイル（`--profile dry-run`）で両ELFをビルドして静的検証する。Solutionのvehicle-output既定ビルドも基板へ書き込まず通電しない。実機確認は、(A) VM OFFでGPIO/PWMと状態遷移だけを確認し物理的に動かないことを確認し、(B) A合格後だけ電流制限・車輪浮上・物理キル準備のうえVM ON・短時間低dutyで左右極性を確認する、の順に分ける。
2. e2 studioに3プロジェクトをインポートする。Renesas例由来のlaunchはひな形として添付しているため、使用するJ-Link、CPU0/CPU1デバイス名、ELFパスを確認する。IDEからの起動は未検証。
   手動確認項目（まだ実行しない）：Debug ConfigurationsでCPU0の
   `CPU0/ra8p1_vision_BothCore_Download.launch`を開き、Download Imagesに
   `${workspace_loc:/ra8p1_vision_CPU0}/Build/CPU0.elf`と
   `${workspace_loc:/ra8p1_vision_CPU1}/Build/CPU1.elf`（CPU1はリンク先0x020f0000、offset 0）が2件あること、TargetがCPU0であること、run commandsが空であることを確認する。
   CPU1の`ra8p1_vision_CPU1 Debug_Attach.launch`はhot attach/symbol専用として確認し、CPU1のrelease/startコマンドがないことを確認する。CPU1単独のDownload/RunやDebug_MulticoreのRunは選ばず、CPU0製品コードがM33を起動する。確認中はモーターVMを切り、車輪を浮かせ、確認後もIDEのDownload/Runボタンを押さない。
3. 両コアを停止した状態で、M85に `CPU0/Build/CPU0.elf`、M33に `CPU1/Build/CPU1.elf` をロードする。M33を独立に走らせず、両コアをリセットしてM85から開始する。OTP/セキュリティ/保護設定をこの統合のために変更しない。
4. M85の `dc_primary_start` とM33の `hal_entry` にブレークポイントを置き、CPU1INITVTOR、初期SP、VTORが上表の範囲にあるか確認する。
5. M33の `supervisor` で、共有RAM `smoke[0]` と `smoke[1]` が20になり `fault=0` であることを確認する。タスク切り替えとFPU保持は、この実行結果で初めて確認済みとする。
6. `ready=0x4D333352`、制御IPCの同一session、10 ms刻みのcontrol_ms、ToF測定更新、AI結果とstatus返信を確認する。
7. デバッガを外して電源を再投入し、同じ順で両コアが起動することを確認する。

## 制御の受入項目（全て実機未確認）

- 起動時停止。MANUAL は Web の方向ボタンを押している間だけ、AUTO は Web の AUTO ボタンで開始。STOP/ESTOP で停止。
- ToF 100 mm以下で通常停止、50 mm以下で緊急停止、101 mm以上への復帰条件、100 msを超えるデータ途絶、センサー抜去・NACK・SDA固定。
- ESTOPは基板リセットまでラッチされ、Web RESETやセンサー回復では解除されないこと。
- AIの無効値、NaN、範囲外、古いseq、撮影から `AI_FRAME_MAX_AGE_MS`（600 ms）を超えた結果、AI停止。
- Web手動指令・M85 heartbeatの300 ms途絶、IPC混雑・破損、正常値復帰後に自動で再発進しないこと。
- カメラ設定連続要求中もToF測定が優先されること。IIC1異常時にカメラ待ちがタイムアウトし、モーターは停止を維持すること。
- AI負荷最大時の制御更新間隔、障害発生から実GPIO停止までの最大時間、各タスクのスタック残量。

しきい値は添付制御コードの値です。測定していない停止時間・スタック余裕の数値は報告していません。段階AのVM OFF確認と、段階BのVM ON・車輪浮上・低duty確認を分けて記録し、両方を確認した後にだけ制御担当と床上出力を判断してください。

## M85の送信とWeb担当への接続

M85のメイン実行コンテキストだけが同じproducerを操作します。NPU待機とUSBポーリング中もサービス処理を呼び、50 msを目安にheartbeatを送ります。1 msのM85 SysTickは待機解除だけを行い、ISR内から送信しません。ソフトウェア処理区間を含む100 ms以内の実測保証は未確認です。途絶した場合はM33の既存300 ms制限で停止します。

統合CPU0にはlwIP/HTTPDとWeb制御APIが含まれています。Web要求は `controller_if` → `m85_gateway_task.c`（唯一のIPC送信タスク）→ M33 の順に流れます。明示的なSTOP/ESTOP/リセット操作だけがurgent STOPになり、UIのABORT表示中の定期送信は中立DRIVE（deadman=0）として送られます。カメラ/AIの失敗は `autonomy_controller_report_ai_unavailable()` で「経路無効」の認識結果として送られ、AUTOはTOR、MANUALはToFとdeadmanで継続します。HTTPDの制御APIと静的UIはビルド対象ですが、`/video_feed` の実機映像配信、DHCP環境、実デバイス応答、ハードウェア走行は未確認です。PCテストで確認した通信APIとオンボードHTTP statusを混同しないでください。

## 参照

- [Renesas: Developing RA8 Dual Core MCU](https://www.renesas.com/en/document/apn/developing-ra8-dual-core-mcu?language=en)
- [Renesas: Getting Started with IPC for Dual Core MCU](https://www.renesas.com/en/document/apn/getting-started-ipc-dual-core-mcu)
- 公式 `renesas/ra-fsp-examples`、参照commit `828503ebfc63bfde53284f71e41c3cc7c8363dc1`。
- 制御部品の元資料（チーム内・非公開）。ソース内の著作権・ライセンス表記を保持しています。
