# セキュリティ

Wi-Fiパスワードを含む `include/device-config.h`、個人用ファームウェア、NVSのダンプを公開しないでください。直接ログイン後のpovo認証トークンと端末IDは本体のNVSへ保存します。メールOTPはメモリだけで保持し、120秒で期限切れと扱います。

HTTPSの証明書検証を無効化しないでください。同梱CAは2026-09-05にOSで検証したGlobalSign Root CA - R3（SHA-256 `cbb522d7b7f127ad6a0113865bdf1cd4102e7d0759af635a7cf4720dc963c53b`）です。任意ホストへのリダイレクトを許可しません。脆弱性は再現条件のみを管理者へ非公開で連絡し、秘密の値や個人データをIssueに投稿しないでください。

NVSは単一のスキーマ付きレコードとして保存し、保存に失敗した新トークンを成功扱いしません。ただしこの既定ビルドはフラッシュ暗号化を有効にしていません。本体やフラッシュ読出し口への物理アクセスを管理してください。端末を譲渡・処分するときは専用基板のNVSを消去します。トークン漏えい時はpovo側で認証を失効させ、再ログインしてください。

設定画面はログインが必要な間だけ開く、ランダム16文字のWPA2パスワード付きSoftAPです。HTTPサーバーはAP側の192.168.4.1へバインドし、入場情報を本体画面に表示します。設定用パスワードを共有せず、認証成功後はAPを終了します。POSTには起動ごとのトークンを要求します。Webページ自体はHTTPであり、通信保護は設定用Wi-FiのWPA2に依存します。

キャプチャ資料は保存前に認証値を除去します。公開資料へ生トークン・OTP・Cookie・メール・逆コンパイル全文を含めません。調査に使用した一時計測は製品ファームウェアに含めず、使用後はアプリ停止・CA削除・プロキシ解除を行います。

## ビルドツールの監査

`requirements-ci.txt` に監査した依存グラフを固定し、`python scripts/audit-build-tools.py` でPyPIの既知脆弱性を照会します。未評価の検出や照会失敗はCIを失敗させます。全検出は `build/build-tools-audit.json` に残します。

2026-09-05時点のPlatformIO 6.1.19はStarletteを `<0.53` に制限しており、修正版1.xと互換性を保証できません。以下5件（DB上の重複を含め7検出）はStarlette 0.52.1に限り、CLIビルドの評価済み対象外として記録します。

- [Hostヘッダー検証](https://github.com/Kludex/starlette/security/advisories/GHSA-86qp-5c8j-p5mr)
- [リクエストパス検証](https://github.com/Kludex/starlette/security/advisories/GHSA-jp82-jpqv-5vv3)
- [フォーム処理のDoS](https://github.com/Kludex/starlette/security/advisories/GHSA-82w8-qh3p-5jfq)
- [Windows静的ファイル処理](https://github.com/Kludex/starlette/security/advisories/GHSA-wqp7-x3pw-xc5r)
- [HTTPメソッド振り分け](https://github.com/Kludex/starlette/security/advisories/GHSA-x746-7m8f-x49c)

これらはHTTPサーバーが外部入力を処理する際の問題です。本プロジェクトは `pio run` のCLIビルドのみを使用し、`platformio/home/run.py` のStarlette/Uvicornサーバーを起動しません。Starletteはファームウェアに含まれません。**このビルド環境で `pio home` を起動しないでください。** Homeを利用する場合、またはPlatformIOの対応範囲が変わった場合は例外を再評価し、互換性のある修正版へ更新してください。脆弱性が存在しないという判定ではありません。

## ファームウェア依存の確認（2026-09-05）

旧SDKのMbed TLS 2.28.7にはHTTPSクライアントでも影響し得る [CVE-2025-27810](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2025-03-2/) がありました。修正済みMbed TLS 3.6.6を含む [Arduino 3.3.11](https://github.com/espressif/arduino-esp32/releases/tag/3.3.11) / ESP-IDF 5.5.5へ更新しています。PlatformIO公式6.13.0のArduino 2.x系列では取り込めないため、[pioarduino 55.03.311](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.311)を版固定で採用しました。変更はSDKとバックライトAPIに限定し、表示・API契約は維持しています。

[Mbed TLSの公式アドバイザリー](https://mbed-tls.readthedocs.io/en/latest/security-advisories/)を新SDKにも照合しました。3.6.6に対する2026年7月の検出は、本アプリの構成では次のとおり評価しています。

| CVE | このアプリでの評価根拠 |
| --- | --- |
| CVE-2026-35336 / CVE-2026-50584 | 公式資料がTLS経路を非影響と明記 |
| CVE-2026-50587 / CVE-2026-54435 | 本アプリはRSA秘密鍵復号・長期ECC秘密鍵を使用しない |
| CVE-2026-49300 | CAは所有者がビルド時設定。非信頼ユーザーの証明書登録機能なし |
| CVE-2026-73064 | TLS1.3サーバーが対象。本アプリはクライアント |
| CVE-2026-25832 | ESP32のDIO/QIO双方のsdkconfigでTLS1.3無効を確認 |
| CVE-2026-50583 | 同sdkconfigでbuilt-in ECP有効を確認。driver-only ECCではない |
| CVE-2026-50581 | 旧版のPC中継経路の評価。現行の接続先はpovoの公開HTTPSサーバー。SDK自体は修正済み版 |

NetworkClientSecureが `mbedtls_ssl_set_hostname` を呼ぶことも確認しています。[ArduinoJson](https://github.com/bblanchon/ArduinoJson/security/advisories)・[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI/security/advisories)には確認時点で公開アドバイザリーがありませんでした。v0.1.0時点はWi-Fi STAのみでした。現行版は設定時にSoftAP/DHCPとArduino WebServerを使用するため、旧版の「HTTPサーバー未使用」という対象外判定を現行版へ適用しません。WPS・Bluetooth・ESP-NOWは使用しません。

これは使用経路を含む公開情報の確認です。全バイナリのSBOM照合、ファジング、未公開脆弱性や物理攻撃への保証ではありません。TLSサーバー・証明書登録・秘密鍵・通信方式を追加する場合は上記の対象外判定を再評価してください。

直接認証版のSoftAP追加時に再確認した[DHCPサーバーの境界外読み取り](https://github.com/espressif/esp-idf/security/advisories/GHSA-g764-gwc3-75m5)は、採用済みESP-IDF 5.5.5が修正版です。[WebServer multipart解析](https://github.com/espressif/arduino-esp32/security/advisories/GHSA-8cmm-3887-r32j)と[Digest認証](https://github.com/espressif/arduino-esp32/security/advisories/GHSA-28hv-fwm3-rpcq)はArduino 3.3.8以降で修正されており、採用版は3.3.11です。本実装にファイルアップロード・Digest認証・OTA更新エンドポイントはありません。
