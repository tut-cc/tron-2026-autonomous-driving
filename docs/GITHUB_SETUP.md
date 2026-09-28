# GitHubから取得してe² studioで開く

このリポジトリはソースと、取得直後のデバッグ用に検証済みの `CPU0/Build/CPU0.elf` / `CPU1/Build/CPU1.elf` を配布します。ELFは **vehicle-output（モーター物理出力有効）版**です。Web画面用の `fsdata.h` とカメラ設定用の `camera_sensor.o` も同梱されています。ソースを変更したら、古いELFを使わず両コアを再ビルドしてください。

## 1. PCの準備

- Windowsの短い英数字パスを使う（以下は `C:\TRON`）。日本語を含む親フォルダへ展開しない。
- e² studio 2026-04.2 / FSP 6.5.0、Arm Toolchain for Embedded LLVM 21.1.1、Python 3を用意する。e² studioから `python` が見えるよう、PATHを設定してからIDEを起動する。
- 書き込みにはEK-RA8P1のJ-Link接続とドライバが必要。PCテストも実施する場合はNode.jsとホスト用GCCを用意する。

## 2. ソースを取得する

Gitが使える場合は次を実行する。GitHubの **Code → Download ZIP** から取得して `C:\TRON\tron-2026-autonomous-driving` に展開してもよい。

```powershell
git clone https://github.com/tut-cc/tron-2026-autonomous-driving.git C:\TRON\tron-2026-autonomous-driving
```

元ZIPや取得済みのコピーを上書きせず、新しいフォルダに展開する。別のコミットに更新した場合は両コアを再ビルドする。

## 3. e² studioに取り込む

1. e² studioを起動し、別の英数字ワークスペース（例 `C:\TRON\ws_ra8p1`）を選ぶ。
2. **File → Import → General → Existing Projects into Workspace**（またはWelcomeの **Import existing projects**）を選ぶ。
3. ルート・ディレクトリに `C:\TRON\tron-2026-autonomous-driving` を指定し、`ra8p1_vision_CPU0`、`ra8p1_vision_CPU1`、`ra8p1_vision_Solution` の3件をすべて選ぶ。**Copy projects into workspaceはオフ**にする。
4. Project Explorerで `ra8p1_vision_Solution` を右クリックし、**Build Project** を選ぶと両コアをビルドできる。`ra8p1_vision_CPU0` の **Build Project** でも両コアをビルドする設定。コンソールに **`CPU0 OK` と `CPU1 OK`** が表示され、`terminated with exit code` がないことを確認する。e² studioの最終行が `0 errors` でも、外部ビルドコマンドが失敗している場合がある。初回からELFは入っているが、これは受取人の環境でビルドできることを確認する手順でもある。

デバッグ起動設定は「起動前にCPU0プロジェクトをビルド」を有効化してあり、そのビルドは両コアを生成する。したがって **Debug As** を選ぶだけでもビルド後に書き込みへ進む。初回ビルドは数分かかる。`python` や `clang.exe` が見つからない場合は、PATHまたは `LLVM_ARM_BIN` を設定してIDEを再起動する。FSP Configuratorの **Generate Project Contentは実行しない**。設定とビルド対象は同梱済み。

## 4. 書き込み前の検証と書き込み

車輪を浮かせ、**DRV8833のVM／モーター電池を物理的に外す**。リポジトリ直下で次を実行すると、両コアのクリーンビルド、Manifest、ホストテスト、静的検証をまとめて行う（初回は時間がかかる）。`PASS` を確認する。

```powershell
cd C:\TRON\tron-2026-autonomous-driving
python -B tools/make_evidence.py
```

e² studioのProject Explorerで `CPU0/ra8p1_vision_BothCore_Download.launch` を右クリックし、**Debug As → ra8p1_vision_BothCore_Download** を選ぶ。これはビルドしてから2つのELFを同時に書き込む設定であり、CPU1単独の起動設定は使わない。コンソールにターゲット接続 `OK` と「ダウンロード終了」が出たら、CPU0のReset_Handler停止から **F8（再開）**、その後 **Run → Disconnect** でデバッガを外す。通常の緑色の **Run** ボタンはWindows上でARMのELFを動かすボタンではない。e² studioではこの **Debug As → F8** が基板上の実行手順。デバッガ接続中はCPU1の動作観察が不安定になる場合がある。

まずVMを外したまま、LANを起動前から接続し、スマホを同じ `192.168.2.x` のWi-Fiに接続して `http://192.168.2.200/` を開く。実機確認は [HARDWARE_TEST.md](HARDWARE_TEST.md) に従う。モーターを接続しての走行はVM OFF段階を通過してから。

前方距離が `--` と表示される場合は、遠方だけでなく測距エラーや通信異常の可能性もある。安全のため現行ファームウェアはToF値が無効な間、AUTOだけでなくMANUALのモーター起動も拒否する。原因が分かるまでVMを接続して走行しない。

## 更新するとき

Git版なら取得したフォルダで `git pull` し、e² studioで3プロジェクトをRefresh（F5）してから両コアを再ビルドする。ZIP版なら新しい英数字フォルダへ展開し、別のワークスペースにインポートする。**古いELFを使い回さない**。
