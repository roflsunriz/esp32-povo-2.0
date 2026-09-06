# AGENTS.md

## 作業開始前の必須手順（最優先・例外なし）

1. エージェントは、調査、計画、コマンド実行、スキル利用、ファイル編集、コミット、プッシュを始める前に、必ずリポジトリ直下の `.\COMMON-AGENTS.md` を開き、先頭から末尾まで全文を読む。
2. `COMMON-AGENTS.md` はGit管理外のシンボリックリンクである。`git`や既定のignore設定が有効な`rg --files`の検索結果だけで、ファイルが存在しないと判断してはならない。PowerShellでは最初に次を実行する。

```powershell
Get-Content -Raw -LiteralPath .\COMMON-AGENTS.md
```

3. 読み取りに失敗した場合、出力が省略された場合、または末尾まで読めたことを確認できない場合は、一切の作業を開始せず、パスとシンボリックリンク先を確認して全文を再取得する。必要なら分割して末尾まで読む。
4. 全文を読了するまで、ローカル `AGENTS.md` だけを根拠に作業を続けてはならない。読了後は `COMMON-AGENTS.md` を最優先の指針とし、読了直後の最初の進捗報告で全文を読了したことを明示する。
   このファイルでは `esp32-povo2.0` 固有の補足だけを記載する。

## 目的
- ESP32-2432S028Rがpovoへ直接メール認証し、認証を保持して適用中データ使い放題の期限を表示する。旧パッチ版アプリの表示API・PC中継は現行ファームウェアで使用しない。
- API の詳細は roflsunriz/povo-2.0-revanced のコード、または同名プロジェクトのCodex履歴、または "C:\Users\UserName\Documents\povo-2.0-revanced" を見る
- 関連プロジェクト: roflsunriz/esp32-codex-notifications のコード、または同名プロジェクトのCodex履歴、または "C:\Users\UserName\Documents\esp32-codex-notifications"
- 原型は ./original-idea.md にある

## 制約
- PCに接続済みのESP32はCodex Micro (esp32-codex-notifications) として使用中なので検証に使うのは禁止する。所有者の明示指示がある場合だけ、全flash退避→検証→書き戻し→ `CODEX_CYD_READY` 確認の手順で例外的に使う。
- アリエクスプレスで追加のESP32を購入済みで数日すると着く予定なのでそれを使う

## 実機検証の所見（2026-09-06）
- STA接続は2.4 GHz帯が必須で、5 GHz帯とWPA2/WPA3混在は不可。WPA2のPCホットスポットで接続を確認した。
- 切断中は10秒ごとに `WiFi.begin` を再試行する（`src/main.cpp`）。 `setAutoReconnect(true)` だけではホットスポットOFF→ON後に復帰しなかった。
- 設定用APは `povo-setup-` +ランダム4文字・パスワードはランダム16文字で画面表示する。固定名にはできない。
- 現用基板の全flash 4MB退避は `.pio-core/penv` のpythonでesptoolを実行し、 `build/backup-codex-micro/`（Git管理外）へ保存した。CP932環境では進捗表示で例外になるためUTF-8設定が必要。
- 実運用はモバイルルーター等の2.4 GHz帯へ直接接続する。 `include/device-config.h`（Git管理外）には2026-09-06検証時の一時的なPCホットスポット設定が残っているため、次回書き込み時は実運用APに設定し直す。

## 直接認証の調査資料
- パッチ版アプリ・PC中継への依存を廃止するための認証調査は `docs/auth-capture/` に保存する。静的解析の根拠は `static-analysis.md`、実測と再採取手順は同ディレクトリの `README.md` を参照する。静的解析と実機観測を混同しない。
- 2026-09-05の検証対象はPixel 10a上の `com.kddi.kdla.jp` 1.70.0-JP（857）。ADBは必ず対象端末を `-s` で指定する。同時接続の別AndroidやESP32を操作しない。
- デバッグ・ユーザーCA信頼パッチが適用されていても、キャプチャ用CA自体の端末登録は別途必要。未登録時はアプリの通信エラー2005とプロキシ側の `tls alert certificate unknown` を観測した。
- キャプチャは `scripts/capture-auth.py` で認証値を保存前に除去する。生APK、画面XML、プロキシログはGit管理外の `build/auth-research/` に限定し、トークン、Cookie、メールアドレス、OTPをコミットしない。Androidプロキシは終了時に `http_proxy :0` と内部host/port設定の削除で解除する（`http_proxy`キー削除だけでは内部設定が残る）。
- 同APKのOkHttpピニングは残っていた。再採取時の一時JVMTI計測、NDKビルド、解除手順は `docs/auth-capture/README.md`。2026-09-05は所有者によるCA登録後に採取し、終了後にCAと計測を撤去済み。
- 独立認証はv3 login/action→v4 otp→v5/public users/auth、更新はGET users/token。`next_step=dashboard` が成功。メールコードは実際には2分有効で、`otp_duration=15` を15分と解釈しない。
- 期限の正本は現行Quilt `/api/v1/quilt/page/user-plan-details-v2`。旧account/plan/details/getは今回500だった。日本語・Asia/Tokyoを指定し、適用中/使い放題のexpiry.valueを分精度で読む。構造・選別の変更時は `direct-status.h` とキャプチャ資料を合わせて更新する。
