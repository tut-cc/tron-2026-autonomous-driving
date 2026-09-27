# mini-4wd-webapp
車載HTTP用の静的ブラウザUIと、開発時のPC mock/sampleを含むWeb資産です。PC mockのREST/MJPEG機能はオンボードCPU0のHTTPD機能を意味しません。

## 起動・利用手順

### 1. PC mockを起動（開発用のみ）
```bash
python3 mock_server.py
```
- ポート `8765` でPC用mockサーバー（REST API / MJPEG / 静的配信）が起動します。このmockのMJPEGはオンボード`/video_feed`実装ではありません。
- ターミナル上で `1` 〜 `5` のキーを押すことで、動作シナリオ（手動走行、自動運転、TOR警告、自動中断、手動中断）をリアルタイムに切り替えられます。

### 2. ブラウザ（スマホまたはPC）でアクセス
スマホやPCのブラウザで以下のURLを開きます：

**http://<サーバーのIPアドレスまたはlocalhost>:8765**

- ラジコン式：左手で前進ボタン（画面左下）、右手で ◀ / ▶（画面右）。同時に押すと曲がりながら進みます。キーボードは `↑` / `W` で前進、`←` / `→` または `A` / `D` で左右。後退入力は提供しません。
- 右上の「INFO」ボタンからいつでも操作説明を確認できます。

車載用に埋め込むUIはこのディレクトリの`index.html`、`style.css`、`js/`から生成します。オンボードM85は既定で `http://192.168.2.200/` にUIと `/api/control`、コース・経路・AI枠を描いたBMPスナップショット `/video_feed`（240×180、256色）を配信します。PC mockのMJPEGとは別実装です。映像およびモーター動作は実機未確認です。詳細は`../README_HTTP.md`を参照してください。

## サーバーアーキテクチャ

```text
mini-4wd-webapp/
├── server/                     # 車載マイコン・実機でもそのまま使える共通パッケージ
│   ├── constants.py            # プロトコル定数 (モード・停止要因・拒否理由)
│   ├── controller.py           # 制御コア (Source of Truth・安全マトリクス・TOR・デッドマン監視)
│   ├── http_server.py          # 軽量非同期HTTP/REST/MJPEGサーバー (標準ライブラリのみ、Keep-Alive対応)
│   └── camera_base.py          # カメラ映像プロバイダの基底インターフェース
│
├── mock/                       # モック開発専用パッケージ
│   ├── camera.py               # 疑似コース・疑似カメラ
│   ├── scenario.py             # テストシナリオ管理
│   └── scenarios.json          # シナリオ定義データ
│
├── mock_server.py              # モック起動エントリポイント (Python版)
└── sample/                     # C言語実装サンプル (KISS原則・マイコン移植向け)
    ├── main.c                  # C版エントリポイント & 端末キー入力
    ├── http_server.c/.h        # 軽量HTTPサーバー (BSD Socket / lwIP両対応)
    ├── controller.c/.h         # 車両制御コア (安全判定・タイマー)
    ├── constants.h             # 定数定義
    └── Makefile                # ビルド設定
```
