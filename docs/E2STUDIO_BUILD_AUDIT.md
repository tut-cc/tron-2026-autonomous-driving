# e² studio インポート／ビルド監査（旧版・2026-09-22）

注意：この記録のELF/hashは2026-09-22版に限定されます。2026-09-26 AUTO/AI修正版のIDE検証または実機動作の証拠ではありません。今回のオフライン検証は `VERIFICATION_JA.md` を参照してください。

## 結論

e² studio 2026-04.2 / FSP 6.5.0 の実ランタイムで、`CPU0`、`CPU1`、`Solution` をリンク形式で新規ワークスペースへ取り込み、3プロジェクトがCDTの `Source Build` 構成として認識されることを確認した。続いてGUIの `Project > Build Project` と同じEclipse project builderを `ra8p1_vision_Solution` に対して実行し、外部コマンド `build.py` がCPU0/CPU1のELF、map、profile stampを更新した。

この監査はオフラインビルドだけである。J-Link接続、基板書き込み、DRV8833 VM接続、モーター通電は実行していない。

## 確認環境

- e² studio: `2026-04.2`（build id `v26.4.2`）
- FSP: `6.5.0`
- Arm Toolchain for Embedded LLVM: `21.1.1`
- Workspace: 新規の監査用ワークスペース
- Project import: 元フォルダをコピーしないリンク形式

## CDT認識結果

```text
E2_CDT_CONFIG project=ra8p1_vision_CPU0 id=vehicle.cpu0.source name=Source Build
E2_CDT_CONFIG project=ra8p1_vision_CPU1 id=vehicle.cpu1.source name=Source Build
E2_CDT_CONFIG project=ra8p1_vision_Solution id=vehicle.solution.source name=Source Build
```

Solutionの登録builderと引数は次のとおり。

```text
builder=org.eclipse.cdt.make.core.makeBuilder
command=python
arguments=../build.py --core all --profile vehicle-output --allow-physical-output
kind=FULL_BUILD
```

## 最終e² studio builder実行結果

```text
E2_BUILD_BEGIN project=ra8p1_vision_Solution
E2_BUILD_END project=ra8p1_vision_Solution
exit=0
```

builder実行後に更新された主要成果物は次のとおり。

| 成果物 | 更新時刻 (JST) | SHA-256 |
|---|---:|---|
| `CPU0/Build/CPU0.elf` | 2026-09-22 23:50:08 | `DA38291A84EA904EEB4D705E8B6908BC130BB3D4CCD453893657080690934BAC` |
| `CPU1/Build/CPU1.elf` | 2026-09-22 23:50:10 | `F89A5C0AC9C9DC3120A0D75789013BD13519FC6790553E39EB7B3C08DA4D3D57` |
| `CPU0/Build/CPU0.map` | 2026-09-22 23:50:08 | `D662716B9370E53586828AF5F7ECAA6CC856553AA1AE718DAC178F256248A83C` |
| `CPU1/Build/CPU1.map` | 2026-09-22 23:50:10 | `EEC3288CA70BED879179ABB3EB2D9B20727CE832F133A533CD97E182209EF51E` |

監査用のEASE headless実行ではワークベンチUIが存在しないため、ビルド完了後のCDT console/indexer終了処理にUI/preference関連の診断が記録された。これはELF生成・builder終了後のheadless固有診断であり、3プロジェクトのCDT認識、外部builder起動、成果物更新、終了コードには影響しなかった。通常のe² studio GUIにはワークベンチとBuild Consoleが存在する。

## GUIでの最小操作

1. `File > Import > General > Existing Projects into Workspace` で完成物ルートを選ぶ。
2. `ra8p1_vision_CPU0`、`ra8p1_vision_CPU1`、`ra8p1_vision_Solution` を選び、`Copy projects into workspace` はオフにする。
3. FSPの `Generate Project Content` は実行しない。
4. `ra8p1_vision_Solution` を選択し、`Project > Build Project` を実行する。
5. Consoleの終了だけでなく、両ELF、map、`Build/build_profile.json` の更新を確認する。
6. `manifest.py`、`verify.py --profile vehicle-output`、`test_host.py` を実行し、全ゲート合格後にだけDownload設定の確認へ進む。

実機書き込み手順は `BRINGUP_JA.md`、モーターVM OFF/ONを分離した受入手順は `CONTEST_DEMO_RUNBOOK_JA.md` を使用する。
