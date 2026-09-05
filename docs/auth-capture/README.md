# povo直接認証の調査・キャプチャ

パッチ版povoアプリとPC中継を常時稼働させず、CYD自身がログインと期限取得を行うための調査資料です。確認済みの静的解析は [static-analysis.md](static-analysis.md) にまとめています。静的解析だけで直接認証の動作確認済みとは扱いません。

## 対象と実測

- 調査日: 2026-09-05
- 実機: Pixel 10a、パッケージ `com.kddi.kdla.jp`、1.70.0-JP、versionCode 857
- 実機から取得したパッチ版base.apk SHA-256: `56582bdbe95ff341c5fc909d734f6d734bcf3934de0a0aefbef1d22ccd0743bb`
- jadx 1.5.6でパッチ版リソースを展開し、Manifestのdebuggable=trueとnetwork_security_configのsystem/user CA、overridePins=trueを確認。
- ユーザーCA一覧にキャプチャ用mitmproxy CAはなく、プロキシ経由のログインはアプリ側2005、プロキシ側 `tls alert certificate unknown` で失敗。これは認証APIからの拒否応答ではなく、HTTP到達前のTLS失敗。
- CA登録後もアプリのOkHttp `CertificatePinner.check$okhttp` は有効で、`SSLPeerUnverifiedException: Certificate pinning failure` が発生。インストール済みAPKの単一クラス解析でも処理の存在を確認した。
- デバッグ可能なプロセスへ `scripts/capture-pinning-agent.cpp` をJVMTI接続し、このチェックだけを一時的にスキップ。CA/ホスト名検証は維持した。アプリ再インストールは不要だった。
- [android-flows.jsonl](android-flows.jsonl): v4 login/action → v4 otp → v5/public users/auth の3応答が200。
- [independent-flows.jsonl](independent-flows.jsonl): アプリの認証なしでv3 login/action、独自端末IDでv4 otp、v5/public users/authに成功。最初の入力は401、2分のメール期限内に再送分を入力すると200。`otp_duration=15` を15分の保証と解釈しない。
- 独立認証をPCのDPAPI暗号化ファイルへ保存・再読込し、アプリ停止・プロキシ無効の状態でusers/tokenと現行期限APIの200を確認した。秘密の保存ファイルは本資料に含めない。ESP32の保存はNVSであり、DPAPIとは別実装。
- 直接認証の成功値は `next_step=dashboard`、JWT issuerは `circles`、device_typeは `Mobile`。JWTには `exp` と `expiry_time` がある。PIN・未知next_stepを成功にしない。
- 旧account/plan/details/getは500 / ERROR_MYSQL。現行は `/api/v1/quilt/page/user-plan-details-v2`。`povo-tile-plan-detail` のdata.name.value=適用中、remaining.value=使い放題、expiry.valueが「年 月 日 午前/午後h:mm\n残り…」形式。期限は分精度。配列の固定インデックスには依存しない。
- 作業後はプロセスを終了して計測を解除し、プロキシ・CA・転送・実機の中間ファイルを撤去した。従前のAdGuard CAは変更していない。

CAPTCHA付きのv4 login/actionもアプリで観測したが、独立クライアントにはアプリ内に実装されているv3経路を採用し、実際に成功を確認した。将来のサービス変更でv3が拒否される場合は追加認証/通信失敗として扱い、成功と偽装しない。

## 再採取

1. `adb devices -l` でPixelのserialを確認。以降は必ず `adb -s <serial>` を使う。
2. `settings get global http_proxy` と `global_http_proxy_host` / `global_http_proxy_port` の開始値を記録する。既存プロキシがある場合は復元値を保持する。
3. mitmproxyを用意し、そのCA証明書をPixelのCA証明書として登録する。証明書登録と端末ロック解除は所有者が行う。秘密鍵は端末へ送らない。
4. Git管理外の `build/auth-research/` を作成し、PowerShellで起動する。

```powershell
$env:POVO_CAPTURE_OUTPUT = Join-Path (Get-Location) 'build/auth-research/capture.jsonl'
mitmdump --listen-host 127.0.0.1 --listen-port 18082 --set flow_detail=0 --set allow_hosts=app.povo.jp -s scripts/capture-auth.py
adb -s <serial> reverse tcp:18082 tcp:18082
adb -s <serial> shell settings put global http_proxy 127.0.0.1:18082
```

5. ピニングエラーが残る場合は下記の一時計測を接続してからpovoのメールログインを行う。所有者がメールとOTPをPixel上で入力する。プロキシはpovoホストの指定したパスだけを構造として記録する。生flowを保存する `-w` は使用しない。
6. エラー時に再送を繰り返さず、TLSとHTTP応答を分けて調査する。`capture.jsonl` が存在しない場合は採取成功としない。
7. 終了時、元がプロキシなしなら下記で解除する。元の設定があった場合はその値に戻す。プロキシプロセスを停止し、今回追加したCAだけをユーザーCA設定で削除、Downloadの今回のCAファイルも削除する。

```powershell
adb -s <serial> shell settings put global http_proxy :0
adb -s <serial> shell settings delete global global_http_proxy_host
adb -s <serial> shell settings delete global global_http_proxy_port
adb -s <serial> reverse --remove tcp:18082
```

8. 構造記録をレビューし、メール、OTP、認証値、アカウント識別子がないことを確認したものだけ本ディレクトリへコピーする。APKや逆コンパイル全文はコミットしない。

## Pixel用の一時計測

今回使ったのはAndroid NDK 29.0.14206865、JDK 25のjvmti.h、arm64/Android API28ターゲット。`$ndkRoot` はNDKの実際の場所、`$serial` はPixelの確認済みserialに設定する。アプリを開いてCertificatePinnerがロードされた状態で実行する。

```powershell
New-Item -ItemType Directory -Force build/auth-research/headers | Out-Null
Copy-Item "$env:JAVA_HOME/include/jvmti.h" build/auth-research/headers/jvmti.h
& "$ndkRoot/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android28-clang++.cmd" -shared -static-libstdc++ -fPIC -Wall -Wextra -Werror -Ibuild/auth-research/headers scripts/capture-pinning-agent.cpp -llog -o build/auth-research/libpovo-capture.so
adb -s $serial push build/auth-research/libpovo-capture.so /data/local/tmp/libpovo-capture.so
adb -s $serial shell run-as com.kddi.kdla.jp cp /data/local/tmp/libpovo-capture.so files/libpovo-capture.so
adb -s $serial shell am attach-agent com.kddi.kdla.jp /data/data/com.kddi.kdla.jp/files/libpovo-capture.so
adb -s $serial logcat -d -s PovoCapture:I
```

`attached error=0` を確認。最初のshared STLビルドはlibc++_shared.so不足でロードできなかったため、上記はstatic STLにしている。任意のアプリや非デバッグアプリで使える方式とは限らない。

```powershell
adb -s $serial shell am force-stop com.kddi.kdla.jp
adb -s $serial shell run-as com.kddi.kdla.jp rm files/libpovo-capture.so
adb -s $serial shell rm /data/local/tmp/libpovo-capture.so
```

プロセス終了でチェック処理は元に戻る。APKや端末全体の証明書検証を恒久的に無効化しない。

## キャプチャ検査

`python test/capture-auth-test.py` は認証値と動的な個人情報キーの除去、配列記録数の制限を検査します。文字列・数値・真偽値は型のプレースホルダーへ置換し、実装判断に必要な既知の列挙値だけ保持します。ヘッダー値は保存しません。実際のトークン値で再生する資料ではなく、HTTP契約の再実装に使う構造資料です。
