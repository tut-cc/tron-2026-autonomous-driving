# mini-4wd-webapp
車載HTTP（CPU0 / M85）が配信するブラウザUIです。`index.html`、`style.css`、`js/` から `../web/fsdata.h` を生成してファームに埋め込みます。

## 使い方
スマホやPCのブラウザで `http://192.168.2.200/` を開きます。UI・`/api/control`・映像 `/video_feed`（BMPスナップショット 240×180、256色）はすべて車載M85から配信されます。詳細は `../README_HTTP.md` を参照してください。

- ラジコン式：左手で前進ボタン（画面左下）、右手で ◀ / ▶（画面右）。同時に押すと曲がりながら進みます。キーボードは `↑` / `W` で前進、`←` / `→` または `A` / `D` で左右。後退入力は提供しません。
- 停止ボタンは `ABORT` の1つだけです。中断画面では `RESET` になります。
- 右上の「INFO」ボタンからいつでも操作説明を確認できます。

## 構成

```text
mini-4wd-webapp/
├── index.html / style.css
├── js/
│   ├── app.js            # 各部品の組み立て
│   ├── state-machine.js  # UI状態機械（仕組みはファイル先頭のコメントを参照）
│   ├── comm.js           # POST /api/control の100ms周期ポーリング
│   ├── input.js          # パッド・キーボード入力
│   ├── ui.js             # 画面描画
│   ├── video.js          # /video_feed の表示
│   ├── screen.js         # スマホ向け画面制御（選択抑止・横画面）
│   ├── debug-ui.js       # UI調整用デバッグパネル
│   └── constants.js      # 状態名・理由文言・タイミング定数
├── tests/                # node --test tests/*.test.mjs
├── docs/                 # protocol.md / sequences.md / ui-spec.md
└── sample/               # C言語実装サンプル（参考用。ファームには含まれない）
```

## UIを変更したら
`M85Web/script/generate_web.sh`（Windowsは `generate_web.ps1`）で `../web/fsdata.h` を再生成し、CPU0をビルドし直します。
