# セットアップと更新

## ビルド

PlatformIO CoreとPythonを用意します。検証と同じCLI専用ツールを使う場合は、Python 3.12以降で `python -m pip install --no-deps -r requirements-ci.txt` を実行します。`--no-deps` を外すと未使用のPlatformIO Homeサーバー依存も導入されるため、省略しないでください。

```powershell
Copy-Item include/device-config.example.h include/device-config.h
```

`include/device-config.h` のWi-Fi SSID・パスワードを設定します。実運用はモバイルルーター等の2.4 GHz帯へ直接接続してください。PCホットスポット経由は一時的な検証用です。公式povoホストを検証する公開ルートCAは同梱しています。NTPサーバーはLANから到達できるものを選びます。明るさは `POVO_BRIGHTNESS`（0〜255）です。

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

### 自動で書き込みモードに入れない基板

2026-09-14の専用CYDでは自動リセットで`Wrong boot mode detected (0x13)`になった。**対象ポートを確認し、BOOTを押したままRSTを短く押して離し、最後にBOOTを離す。** 4MBフラッシュの基板に限り、次のように全領域を退避・照合する。パスワードや認証情報を含む退避ファイルはGit管理外の`build/`だけに置く。

```powershell
$py = '.\.pio-core\penv\Scripts\python.exe'
$tool = '.\.pio-core\packages\tool-esptoolpy\esptool.py'
$port = 'COM番号'
$backup = 'build\backup-cyd\full-flash.bin'
$env:PYTHONUTF8 = '1'
New-Item -ItemType Directory -Path (Split-Path $backup) -ErrorAction Stop
& $py $tool --chip esp32 --port $port --baud 460800 --before no-reset --after no-reset read-flash 0x0 0x400000 $backup
if ($LASTEXITCODE -ne 0) { throw '全フラッシュの退避に失敗しました。' }
```

読み取り後にもう一度BOOT→RST→BOOT解放を行い、`verify-flash`が成功するまで書き込まない。既存パーティション表が`.pio/build/cyd/partitions.bin`と一致する場合だけ、NVSを保つアプリ領域の更新を使う。異なる場合は書き込みを止め、移行手順を確認する。

```powershell
& $py $tool --chip esp32 --port $port --baud 460800 --before no-reset --after no-reset-stub verify-flash 0x0 $backup
if ($LASTEXITCODE -ne 0) { throw '全フラッシュの照合に失敗しました。書き込みを中止してください。' }
$old = [IO.File]::ReadAllBytes((Resolve-Path $backup).Path)
$new = [IO.File]::ReadAllBytes((Resolve-Path '.\.pio\build\cyd\partitions.bin').Path)
$segment = New-Object byte[] $new.Length
[Array]::Copy($old, 0x8000, $segment, 0, $new.Length)
if (Compare-Object $segment $new) { throw 'パーティション表が異なります。書き込みを中止してください。' }
& $py $tool --chip esp32 --port $port --baud 460800 --before no-reset-no-sync --after no-reset-stub write-flash 0x10000 .\.pio\build\cyd\firmware.bin
if ($LASTEXITCODE -ne 0) { throw 'アプリ領域の書き込みに失敗しました。' }
& $py $tool --chip esp32 --port $port --baud 460800 --before no-reset-no-sync --after hard-reset verify-flash 0x10000 .\.pio\build\cyd\firmware.bin
if ($LASTEXITCODE -ne 0) { throw 'アプリ領域の照合に失敗しました。' }
```

起動しない場合は、元の4MBバックアップを保ったまま再度手動で書き込みモードへ入り、`write-flash 0x0 $backup`で復元する。復元後は`verify-flash 0x0 $backup`で照合する。具体的な実機結果は[検証](verification.md)を参照。

起動後は[READMEのメール認証手順](README.md#使い始める)へ進みます。設定用Wi-Fiのパスワードは起動ごとに生成し、画面へ表示します。メールOTPの有効期限は2分として扱い、保存しません。認証トークンと端末IDは本体NVSの `povo-auth` 名前空間へ保存します。

## 旧中継方式から移行

`POVO_STATUS_URL` と `POVO_READ_TOKEN` は廃止しました。設定ファイルを現行の例から作り直し、Wi-Fiと明るさの設定だけを移します。`POVO_ROOT_CA` が旧中継用証明書なら、例の `povo::rootCa` へ変更してください。旧中継サーバー・パッチ版アプリの停止は、直接取得を専用基板で確認した後に行ってください。

旧版のコード適用回数や推定期限を認証データへ移行する処理はありません。直接ログインを新規に行います。認証保存形式が不正・破損・未対応なら、正常な認証として採用せずメールログインへ戻ります。

## 更新・復旧

個人設定を安全にバックアップしてソースを更新し、再ビルドします。通常のファームウェア更新ではNVSの認証を再利用します。認証が拒否される場合は設定用Wi-Fiから再ログインします。自動消灯、画面向き、タッチ位置・押圧感度はNVSの `povo-display` に保存され、通常更新では引き継がれます。日本語表示の文言変更時は `python scripts/generate-font.py` で字形を再生成します。

全消去が必要な場合は対象ポートを厳重に確認し、その専用基板だけを消去して書き直します。NVS消去で認証も消えるため再ログインが必要です。自動消灯、画面向き、タッチ調整も初期値に戻ります。ロールバックしてv0.1.0へ戻す場合は、その版に対応する旧中継設定も必要になります。

ルートCA更新は保守作業です。Python 3.13以降で `python scripts/update-povo-ca.py` を実行すると、OSで検証済みの `app.povo.jp` の証明書チェーンから公開ルートを再生成します。証明書の発行者・期限・差分を確認してから採用し、証明書検証自体を無効化しないでください。

## 開発チェック

Dependabotの更新を取り込む場合は、`requirements-ci.txt` の版固定と更新対象の依存メタデータを確認し、下記をすべて実行します。監査テストはPlatformIOの存在、全項目の完全固定、Homeサーバー依存の除外を保証し、特定の旧バージョンへの一致は要求しません。更新の取り消しは該当コミットをrevertし、同じ依存構成を入れ直して再検証します。

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
