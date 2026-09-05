# セットアップと更新

## ビルド

PlatformIO CoreとPythonを用意します。検証と同じCLI専用ツールを使う場合は、Python 3.12以降で `python -m pip install --no-deps -r requirements-ci.txt` を実行します。`--no-deps` を外すと未使用のPlatformIO Homeサーバー依存も導入されるため、省略しないでください。

```powershell
Copy-Item include/device-config.example.h include/device-config.h
```

`include/device-config.h` のWi-Fi SSID・パスワードを設定します。公式povoホストを検証する公開ルートCAは同梱しています。NTPサーバーはLANから到達できるものを選びます。明るさは `POVO_BRIGHTNESS`（0〜255）です。

```powershell
New-Item -ItemType Directory -Force .pio-core | Out-Null
$env:PLATFORMIO_CORE_DIR = (Resolve-Path .pio-core).Path
pio run -e cyd
```

このコマンドはビルドだけを行います。Wi-Fi設定入りのファームウェアを公開しないでください。

## 専用基板への書き込み

接続前後のポートを確認して、対象COM番号を明示します。別用途で使用中の基板には書き込まないでください。

```powershell
pio run -e cyd -t upload --upload-port COM番号
```

起動後は[READMEのメール認証手順](README.md#使い始める)へ進みます。設定用Wi-Fiのパスワードは起動ごとに生成し、画面へ表示します。メールOTPの有効期限は2分として扱い、保存しません。認証トークンと端末IDは本体NVSの `povo-auth` 名前空間へ保存します。

## 旧中継方式から移行

`POVO_STATUS_URL` と `POVO_READ_TOKEN` は廃止しました。設定ファイルを現行の例から作り直し、Wi-Fiと明るさの設定だけを移します。`POVO_ROOT_CA` が旧中継用証明書なら、例の `povo::rootCa` へ変更してください。旧中継サーバー・パッチ版アプリの停止は、直接取得を専用基板で確認した後に行ってください。

旧版のコード適用回数や推定期限を認証データへ移行する処理はありません。直接ログインを新規に行います。認証保存形式が不正・破損・未対応なら、正常な認証として採用せずメールログインへ戻ります。

## 更新・復旧

個人設定を安全にバックアップしてソースを更新し、再ビルドします。通常のファームウェア更新ではNVSの認証を再利用します。認証が拒否される場合は設定用Wi-Fiから再ログインします。日本語表示の文言変更時は `python scripts/generate-font.py` で字形を再生成します。

全消去が必要な場合は対象ポートを厳重に確認し、その専用基板だけを消去して書き直します。NVS消去で認証も消えるため再ログインが必要です。ロールバックしてv0.1.0へ戻す場合は、その版に対応する旧中継設定も必要になります。

ルートCA更新は保守作業です。Python 3.13以降で `python scripts/update-povo-ca.py` を実行すると、OSで検証済みの `app.povo.jp` の証明書チェーンから公開ルートを再生成します。証明書の発行者・期限・差分を確認してから採用し、証明書検証自体を無効化しないでください。

## 開発チェック

```powershell
python test/capture-auth-test.py
python test/audit-test.py
python scripts/audit-build-tools.py
pio run -e cyd
cmake -S . -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
node test/portal-test.mjs
python scripts/generate-font.py
git diff --exit-code -- include/japanese-font.h
```

ホストテストにはC++17コンパイラが必要です。WindowsではVisual Studio Developer Command Promptで、`cmake -S . -B build/host -G "NMake Makefiles"` を使用できます。ブラウザテストにはNode 22以降とChromeが必要です。既定位置にない場合は `CHROME_PATH` 環境変数でChrome実行ファイルを指定します。
