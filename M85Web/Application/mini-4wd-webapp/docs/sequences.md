# 状態遷移・シーケンス図集

本ドキュメントでは、WebApp と 車載マイコン間の対話フローを、正常系・異常系・フェイルセーフの各シナリオにおけるシーケンス図として示します。

> [!NOTE]
> WebApp と 車載マイコン間の制御・状態同期は、100ms周期の **HTTP Keep-Alive 永続接続ポーリング (`POST /api/command`)** により行われます。
> WebAppが送信するリクエストボディに操作コマンドが含まれ、マイコンからのレスポンスボディ (200 OK) に最新テレメトリ (Heartbeat相当) が含まれます。
> 以下のシーケンス図では、理解を容易にするため「WebAppからのコマンド送信（リクエスト）」と「マイコンからの状態返信（レスポンス）」として対話を描いています。

## 1. モード切替: MANUAL → AUTO

WebAppからAUTO要求を出した際、マイコンの状態判定により分岐します。

```mermaid
sequenceDiagram
    participant U as User
    participant W as WebApp
    participant M as マイコン

    Note over W, M: 初期状態: MANUALモードで同期中

    U->>W: AUTOボタン押下
    W->>W: Pendingモードに移行
    W->>M: client_mode=MANUAL <br> mode_request=AUTO

    alt 正常系: 条件を満たしている場合
        M->>M: センサーおよび安全確認完了<br/>mode = AUTO へ遷移
        M->>W: [Heartbeat] <br> mode=AUTO <br> request_reject_reason=NONE
        W->>W: UIを AUTOモード に確定更新

    else 異常系 1: 要求拒否 (前方障害物あり / センサー未準備)
        M->>M: 条件未達<br/>mode = MANUAL を維持
        M->>W: [Heartbeat] <br> mode=MANUAL <br> request_reject_reason=OBSTACLE_NEAR
        W->>W: エラー通知表示: 前方に障害物があります

    else 異常系 2: 状態不一致による拒否 (マイコンがAUTO_ABORT中など)
        Note over M: マイコンはAUTO_ABORT状態
        M->>M: 不一致かつ安全未確認のため拒否<br/>mode = AUTO_ABORT を維持
        M->>W: [Heartbeat] <br> mode=AUTO_ABORT <br> request_reject_reason=MODE_MISMATCH
        W->>W: UIを AUTO_ABORT に強制同期<br/>エラー通知表示: 中断中のためAUTOに切替できません

    else 異常系 3: タイムアウト (パケットロス / 無応答)
        Note over W: 要求送信後 1000ms 経過しても<br/>mode=AUTO のHeartbeatを受信しない
        W->>W: MANUALモード に戻す<br/>エラー通知表示: モード切替がタイムアウトしました
    end
```

## 2. モード切替: AUTO → MANUAL

手動運転への復帰は安全確保のため、マイコン側で最優先で即時受諾されます。

```mermaid
sequenceDiagram
    participant U as User
    participant W as WebApp
    participant M as マイコン

    Note over W, M: 初期状態: AUTOモードで同期中

    U->>W: MANUALボタン押下
    W->>W: UIを MANUAL切替要求中 (Pending) に変更
    W->>M: client_mode=AUTO, mode_request=MANUAL

    alt 正常系: マイコンが即座に受諾
        M->>M: 自動運転制御を停止<br/>mode = MANUAL へ遷移
        M->>W: Heartbeat (mode=MANUAL)
        W->>W: UIを MANUALモード に確定更新

    else 異常系: 要求パケットロス時
        Note over W: 1000ms経過しても mode=MANUAL が返らない
        W->>M: client_mode=AUTO, mode_request=MANUAL を再送
        M->>M: mode = MANUAL へ遷移
        M->>W: Heartbeat (mode=MANUAL)
        W->>W: UIを MANUALモード に確定更新
    end
```

## 3. MANUALモードの方向操作と通信途絶（2軸仮想ジョイスティック・斜め走行）

```mermaid
sequenceDiagram
    participant U as User
    participant W as WebApp
    participant M as マイコン

    Note over W, M: 初期状態: MANUALモード (停止中)

    U->>W: 左上方向へドラッグ (斜め前左へ走行)
    W->>M: client_mode=MANUAL, throttle=1.0, steering=-1.0
    M->>M: 前進左旋回駆動開始<br/>デッドマンタイマー開始

    opt ドラッグ継続中
        W->>M: client_mode=MANUAL, throttle=1.0, steering=-1.0 (100ms周期)
        M->>M: デッドマンタイマーリセット
    end

    alt 正常系 1: 真上ドラッグへ戻す (直進前進へ移行)
        U->>W: 真上方向へドラッグ位置を戻す
        W->>M: client_mode=MANUAL, throttle=1.0, steering=0.0
        M->>M: ステアリングを中央に戻し直進前進を維持

    else 正常系 2: 指を離す (停止)
        U->>W: タッチ終了 (指を離す)
        W->>M: client_mode=MANUAL, throttle=0.0, steering=0.0
        M->>M: モーター停止

    else 異常系 1: 状態不一致時の走行指示 (マイコンがAUTO_ABORT中の場合)
        Note over M: マイコンは障害物検知でAUTO_ABORT中
        W->>M: client_mode=MANUAL, throttle=1.0, steering=0.0
        M->>M: 状態不一致かつ危険操作のためスロットル破棄<br/>モーターは停止状態を維持
        M->>W: Heartbeat (mode=AUTO_ABORT, stop_reason=OBSTACLE)
        W->>W: UIを AUTO_ABORT 画面へ強制同期

    else 異常系 2: 停止信号パケットロス または 通信途絶
        U->>W: 指を離すが停止信号が欠落
        Note over M: デッドマンタイマー満了 (300ms間受信なし)
        M->>M: モーターを自動停止 (throttle=0, steering=0)
    end
```

## 4. TOR（Take Over Request）のライフサイクル

AUTO走行中、マイコンが自律走行困難を検知した際のシナリオ（障害物接近、白線ロスト、センサー信頼性低下など）。

```mermaid
sequenceDiagram
    participant U as User
    participant W as WebApp
    participant M as マイコン

    Note over W, M: 初期状態: AUTOモードで走行中

    M->>M: 走行困難または危険接近を検知<br/>tor_active = true, 残り時間カウント開始
    M->>W: Heartbeat (mode=AUTO, tor_active=true, tor_remaining_ms=3000)

    W->>W: AUTO_TOR 画面へ遷移<br/>警告アラート表示、手動切替ボタン強調

    alt 分岐 A: ユーザーが手動引継ぎ (WebAppがAUTO認識のままでも即時受容)
        U->>W: 「手動操作へ切替」ボタン押下 (または手動操作)
        W->>M: client_mode=AUTO, mode_request=MANUAL
        M->>M: tor_active = false<br/>mode = MANUAL へ移行
        M->>W: Heartbeat (mode=MANUAL, tor_active=false)
        W->>W: TOR画面解除、MANUAL操作画面へ復帰

    else 分岐 B: ユーザー無応答によるタイムアウト
        Note over M: tor_remaining_ms が 0 に到達
        M->>M: 減速および安全停止処理を実行<br/>mode = AUTO_ABORT, stop_reason = TOR_TIMEOUT
        M->>W: Heartbeat (mode=AUTO_ABORT, tor_active=false, stop_reason=TOR_TIMEOUT)
        W->>W: TOR画面解除、AUTO_ABORT 画面へ遷移

    else 分岐 C: 危険要因の自律解消
        Note over M: 一時的な障害物が通過し自律走行可能に回復
        M->>M: tor_active = false にリセット
        M->>W: Heartbeat (mode=AUTO, tor_active=false)
        W->>W: TOR画面を自動解除、通常のAUTO画面に戻る

    else 分岐 D: TOR中にMANUAL_ABORT (手動中断)
        U->>W: ABORT ボタン押下
        W->>M: client_mode=AUTO, manual_abort_request=true
        M->>M: 即座に出力遮断<br/>mode = MANUAL_ABORT, stop_reason = MANUAL_ABORT_BUTTON
        M->>W: Heartbeat (mode=MANUAL_ABORT, stop_reason=MANUAL_ABORT_BUTTON)
        W->>W: MANUAL_ABORT 画面へ遷移
    end
```

## 5. MANUAL_ABORT (手動中断) の発報と復帰手順

```mermaid
sequenceDiagram
    participant U as User
    participant W as WebApp
    participant M as マイコン

    Note over W, M: 任意の状態 (認識モード不問)

    U->>W: ABORT ボタン押下
    W->>M: client_mode=任意, manual_abort_request=true
    M->>M: 最優先で即座にモーター遮断<br/>mode = MANUAL_ABORT<br/>stop_reason = MANUAL_ABORT_BUTTON

    M->>W: Heartbeat (mode=MANUAL_ABORT, stop_reason=MANUAL_ABORT_BUTTON)
    W->>W: 全操作ロック、MANUAL_ABORT 画面表示

    Note over U, W: 安全確認および復帰操作

    U->>W: 「RESET」ボタン押下
    W->>M: client_mode=MANUAL_ABORT, reset_abort_request=true

    alt 復帰成功: マイコンが安全を確認
        M->>M: センサーおよびハードウェアの安全確認完了<br/>mode = MANUAL, stop_reason = NONE
        M->>W: Heartbeat (mode=MANUAL, stop_reason=NONE)
        W->>W: ロック解除、MANUAL操作画面に復帰

    else 復帰拒否: 危険が継続中
        M->>M: 安全未確認のため MANUAL_ABORT を維持
        M->>W: Heartbeat (mode=MANUAL_ABORT, request_reject_reason=OBSTACLE_NEAR)
        W->>W: エラー表示: 障害物を取り除いてください
    end
```

## 6. AUTO_ABORT (自動中断) からの復帰シーケンス

```mermaid
sequenceDiagram
    participant U as User
    participant W as WebApp
    participant M as マイコン

    Note over W, M: 状態: AUTO_ABORT

    U->>W: 「RESET」ボタン押下
    W->>M: client_mode=AUTO_ABORT, reset_abort_request=true

    alt 復帰成功
        M->>M: 停止要因クリアを確認<br/>mode = MANUAL, stop_reason = NONE
        M->>W: Heartbeat (mode=MANUAL, stop_reason=NONE)
        W->>W: MANUAL操作画面へ移行

    else 復帰拒否: 停止要因が未解消
        M->>M: 停止要因が残っているため AUTO_ABORT 維持
        M->>W: Heartbeat (mode=AUTO_ABORT, request_reject_reason=OBSTACLE_NEAR)
        W->>W: エラー表示: 障害物を検知しています
    end
```

## 7. 通信切断と再接続

```mermaid
sequenceDiagram
    participant W as WebApp
    participant M as マイコン

    Note over W, M: 正常通信中

    Note over W, M: 【通信断絶】

    par WebApp側の切断処理
        Note over W: Heartbeat未受信が1.5秒を超過
        W->>W: CONNECTED から DISCONNECTED へ遷移<br/>操作UIを無効化、切断アラート表示
    and マイコン側のフェイルセーフ処理
        Note over M: WebAppからのパケット受信途絶
        M->>M: モーター停止<br/>mode = AUTO_ABORT, stop_reason = COMM_TIMEOUT
    end

    Note over W, M: 【通信回復・再接続】
    Note over W: WebAppは定周期で POST /api/command を試行継続

    W->>M: POST /api/command (ポーリング試行)
    M-->>W: 200 OK (mode=AUTO_ABORT, stop_reason=COMM_TIMEOUT)
    W->>W: DISCONNECTED から CONNECTED へ復帰<br/>マイコンの最新状態 AUTO_ABORT を同期反映<br/>通知表示: 通信切断により自律中断しました
```
