# セキュリティ

Wi-Fiパスワード・読み取りトークンを含む `include/device-config.h` と個人用ビルド成果物を公開しないでください。CYDに書き込みトークン、povo認証情報、プロモコードを設定する必要はありません。

HTTPSの証明書検証を無効化しないでください。漏えいしたトークンは中継側で交換し、各端末を再設定します。脆弱性は再現条件のみを管理者へ非公開で連絡し、秘密の値や個人データをIssueに投稿しないでください。

## ビルドツールの監査

`requirements-ci.txt` に監査した依存グラフを固定し、`python scripts/audit-build-tools.py` でPyPIの既知脆弱性を照会します。未評価の検出や照会失敗はCIを失敗させます。全検出は `build/build-tools-audit.json` に残します。

2026-09-05時点のPlatformIO 6.1.19はStarletteを `<0.53` に制限しており、修正版1.xと互換性を保証できません。以下5件（DB上の重複を含め7検出）はStarlette 0.52.1に限り、CLIビルドの評価済み対象外として記録します。

- [Hostヘッダー検証](https://github.com/Kludex/starlette/security/advisories/GHSA-86qp-5c8j-p5mr)
- [リクエストパス検証](https://github.com/Kludex/starlette/security/advisories/GHSA-jp82-jpqv-5vv3)
- [フォーム処理のDoS](https://github.com/Kludex/starlette/security/advisories/GHSA-82w8-qh3p-5jfq)
- [Windows静的ファイル処理](https://github.com/Kludex/starlette/security/advisories/GHSA-wqp7-x3pw-xc5r)
- [HTTPメソッド振り分け](https://github.com/Kludex/starlette/security/advisories/GHSA-x746-7m8f-x49c)

これらはHTTPサーバーが外部入力を処理する際の問題です。本プロジェクトは `pio run` のCLIビルドのみを使用し、`platformio/home/run.py` のStarlette/Uvicornサーバーを起動しません。StarletteはファームウェアにもPC中継にも含まれません。**このビルド環境で `pio home` を起動しないでください。** Homeを利用する場合、またはPlatformIOの対応範囲が変わった場合は例外を再評価し、互換性のある修正版へ更新してください。脆弱性が存在しないという判定ではありません。

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
| CVE-2026-50581 | 標準PC中継はPython SSLを使用。両端Mbed TLSの条件に該当しない |

NetworkClientSecureが `mbedtls_ssl_set_hostname` を呼ぶことも確認しています。[ArduinoJson](https://github.com/bblanchon/ArduinoJson/security/advisories)・[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI/security/advisories)には確認時点で公開アドバイザリーがありませんでした。ESP-IDFのDHCPサーバー・WPS等のアドバイザリーは修正済み版に更新したうえで、本アプリがWi-Fi STAのみを使用し、WPS・Bluetooth・ESP-NOW・HTTPサーバーを起動しないことを確認しています。

これは使用経路を含む公開情報の確認です。全バイナリのSBOM照合、ファジング、未公開脆弱性や物理攻撃への保証ではありません。TLSサーバー・証明書登録・秘密鍵・通信方式を追加する場合は上記の対象外判定を再評価してください。
