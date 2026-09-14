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

## ビルド依存の更新

- `requirements-ci.txt` はPlatformIO Homeを除いたCLI用の明示固定リストであり、`--no-deps` で導入する。`test/audit-test.py` はPlatformIOの存在・全項目の完全固定・Homeサーバー依存の除外を検査する。更新を妨げる古いバージョン文字列はテストへ埋め込まない。
- PlatformIOを更新するときは、依存メタデータとCLIビルドの動作を確認し、`scripts/audit-build-tools.py`、ホストテスト、設定画面テストを実行する。2026-09-13時点の更新理由と検証範囲は `verification.md` を参照する。
- ホストテストの一部は標準assertを使う。Releaseで検査が消えないよう該当テスト内でNDEBUGを解除する。MSVCでは `cmake --build build/host --config Release` と `ctest --test-dir build/host -C Release --output-on-failure` も確認する。
- 配布物は `.github/workflows/release.yml` が `git archive` で作るソースZIPとSHA-256一覧であり、個人設定入りファームウェアではない。依存更新も配布ソースの変更になる。版はCHANGELOGの該当節と `vX.Y.Z` タグで管理し、`scripts/release-notes.py` はリポジトリ直下で実行する。

## 実機検証の所見（2026-09-06〜2026-09-14）
- 2026-09-14の現在接続ではCOM3のCodex MicroはUSB位置`1-8.1`、ユーザーがpovo専用基板と説明したCOM4は別位置`1-7`のCH340。COM4へのesptool自動リセット接続は`Wrong boot mode detected (0x13)`、手動モード待ちの`no-reset`接続は`No serial data received`で停止し、まだ読み取り・書き込みをしていない。以後もCOM3へは接続せず、COM4を手動BOOT+RSTでdownload modeに入れ、全flash退避・照合後に進める。ユーザーは現行`include/device-config.h`のSSIDを実運用APとして確認し、ローカルビルドにも同設定が含まれることを値を表示せず確認した。
- STA接続は2.4 GHz帯が必須で、WPA2のPCホットスポットで接続を確認した。5 GHz帯とWPA2/WPA3混在は未検証。
- 切断中は10秒ごとに `WiFi.begin` を再試行する（`src/main.cpp`）。 `setAutoReconnect(true)` だけではホットスポットOFF→ON後に復帰しなかった。
- 設定用APは `povo-setup-` +ランダム4文字・パスワードはランダム16文字で画面表示する。固定名にはできない。
- 現用基板の全flash 4MB退避は `.pio-core/penv` のpythonでesptoolを実行し、 `build/backup-codex-micro/`（Git管理外）へ保存した。CP932環境では進捗表示で例外になるためUTF-8設定が必要。
- 実運用はモバイルルーター等の2.4 GHz帯へ直接接続する。`include/device-config.h`（Git管理外）は2026-09-06には一時PCホットスポットとして記録したが、2026-09-14にユーザーが現行SSIDをCOM4用の実運用APと確認した。パスワードを表示せずに設定済みとローカルビルドへの同梱を確認した。秘密のWi-Fi値や設定入りファームウェアをコミット・公開しない。

## 画面タブ・自動消灯・反転の実装記録（2026-09-07・実機未検証）
- タッチはXPT2046をTFTと別バスのVSPI（CLK 25・MISO 39・MOSI 32・CS 33・IRQ 36）で読み、`lib/sensitive-xpt2046` と近接3回の判定はcodex-notifications式を使う。TFTとタッチのピンが別系統のためTFT_eSPI内蔵タッチは使わない。旧読み取りは最後にPD0=1を残してPENIRQを無効化していたため、ドライバー末尾のPD0=0変換を維持する。消灯中の接触は離すまで復帰専用とする。2026-09-14時点でこの修正と校正はビルド・ホストテスト済み、実機未検証。
- 起動後BOOT長押しで2点の位置・押圧感度を調整する。NVS `povo-display` の `touch_calib` 単一blobへバージョン・検証値付きで保存し、旧版は既定値へ安全にフォールバックする。押下取得・保存に失敗した場合は旧値を維持する。ペン自体が抵抗膜へ接触できずPENIRQが出ない場合はソフトウェア閾値では解決できない（`src/status-display.cpp`、`include/touch-calibration.h`、`verification.md`）。
- BOOTボタンはGPIO0（INPUT_PULLUP）。30msチャタリング除去・50ms以上押して離したら1回押しで上下反転（rotation 1⇔3、タッチ座標も反転）。
- 消灯設定32件（なし・15秒・30秒・1分・2分・5分・10分・30分・1時間・2〜24時間毎時）は `include/display-settings.h` に純粋ロジックとして集約し `test/display-settings-test.cpp` で検証する。表示文言の正本は `include/ui-text.h`、字形は `scripts/generate-font.py` で再生成する。
- 2026-09-14、通常画面は320×240の8-bit Spriteを16行ずつハッシュ比較し、変化した帯だけLCDへ転送する（`include/display-diff.h`、`src/status-display.cpp`）。回転、液晶復帰、ログイン設定画面、タッチ校正の後は全帯を再転送する。メモリ不足時は直接描画へフォールバックし画面へ原因を表示する。ホストテストとビルド済み、実機のちらつき・TLS併用メモリは未確認。
- 設定と画面向きはNVS `povo-display`（sleep_sec・inverted）に保存する。消灯中も取得は継続し描画だけ休止する。設定用ポータル表示中はタブ・消灯を適用しない。
- 2026-09-07はホスト6テスト・PIOビルド・Chrome設定画面テストまで成功。タッチ・消灯・復帰・反転の実機確認は追加基板待ちで未実施（`verification.md` の残り7番）。

## 直接認証の調査資料
- パッチ版アプリ・PC中継への依存を廃止するための認証調査は `docs/auth-capture/` に保存する。静的解析の根拠は `static-analysis.md`、実測と再採取手順は同ディレクトリの `README.md` を参照する。静的解析と実機観測を混同しない。
- 2026-09-05の検証対象はPixel 10a上の `com.kddi.kdla.jp` 1.70.0-JP（857）。ADBは必ず対象端末を `-s` で指定する。同時接続の別AndroidやESP32を操作しない。
- デバッグ・ユーザーCA信頼パッチが適用されていても、キャプチャ用CA自体の端末登録は別途必要。未登録時はアプリの通信エラー2005とプロキシ側の `tls alert certificate unknown` を観測した。
- キャプチャは `scripts/capture-auth.py` で認証値を保存前に除去する。生APK、画面XML、プロキシログはGit管理外の `build/auth-research/` に限定し、トークン、Cookie、メールアドレス、OTPをコミットしない。Androidプロキシは終了時に `http_proxy :0` と内部host/port設定の削除で解除する（`http_proxy`キー削除だけでは内部設定が残る）。
- 同APKのOkHttpピニングは残っていた。再採取時の一時JVMTI計測、NDKビルド、解除手順は `docs/auth-capture/README.md`。2026-09-05は所有者によるCA登録後に採取し、終了後にCAと計測を撤去済み。
- 独立認証はv3 login/action→v4 otp→v5/public users/auth、更新はGET users/token。`next_step=dashboard` が成功。メールコードは実際には2分有効で、`otp_duration=15` を15分と解釈しない。
- 期限の正本は現行Quilt `/api/v1/quilt/page/user-plan-details-v2`。旧account/plan/details/getは今回500だった。日本語・Asia/Tokyoを指定し、適用中/使い放題のexpiry.valueを分精度で読む。構造・選別の変更時は `direct-status.h` とキャプチャ資料を合わせて更新する。
