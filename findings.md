# 調査知見

- 接続済みCodex Microを検証へ流用しない。明示許可時だけ全flash退避→検証→復元→`CODEX_CYD_READY`確認を行い、専用基板の接続可否は毎回実測する。
- `requirements-ci.txt` はPlatformIO Homeを除くCLI依存の完全固定listで、`--no-deps` 導入を前提とする。Release buildでもassertが消えないよう該当host testはNDEBUGを解除する。
- CH340基板は自動resetでboot mode 0x13になる場合がある。手動BOOT保持→RST→BOOT解放でdownload modeへ入り、全4MB退避・partition照合後にapp領域 `0x10000` だけ更新する。起動後の全flash差はNVS自己更新と区別する。
- Wi-Fi STAは2.4GHzで確認済み。`setAutoReconnect(true)` だけではAP再開後に戻らず、切断中は `WiFi.begin` を定期再試行する。秘密入り `device-config.h` とfirmwareを公開しない。
- XPT2046はTFTと別VSPIでPD0=0を維持する。短いnoiseを操作とせず30ms以上の確定押下だけでwake/idle期限を更新し、消灯中の最初の確定touchは復帰専用にする。
- touch calibrationは位置+pressureをversioned単一blobへ保存し、失敗時は旧値を残す。PENIRQが出ないpenはsoftware thresholdでは直らない。
- 画面は320×240 8-bit Spriteの完成frameを16行帯でdiff転送する。rotation/wake/setup/calibration後は全帯。TFT_eSPIの基底 `fillScreen` が幅240しか消さず右側残像を出したため、全画面clearは320×240を明示する。
- 残り時間barの分母は有効期間 `spanMs`。同expiryでは維持し、初回/更新時は `expiry-now`。残り30分以下と期限切れは1分poll、未知期限は通常間隔、renewalで通常へ戻す。
- 認証captureではADB targetを必ず `-s` で固定し、token/Cookie/mail/OTPを保存前に除去する。proxy終了時は `http_proxy :0` と内部host/portを両方消し、CA/JVMTI計測も撤去する。
- 直接認証はlogin/action→otp→public users/auth、更新はGET users/token。`otp_duration=15` を15分と解釈せず、実測の2分期限に従う。期限の正本はQuilt `user-plan-details-v2` の適用中使い放題expiry。
