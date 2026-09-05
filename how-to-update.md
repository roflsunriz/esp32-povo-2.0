# セットアップと更新

## 初回設定・ビルド

PlatformIO CoreとPython 3.10以降を用意し、リポジトリ直下で実行します。

リリース検証と同じビルドツールを用意する場合はPython 3.12以降で `python -m pip install -r requirements-ci.txt` を実行します。この環境ではCLIビルドを使用し、PlatformIO Homeは起動しません（理由は `SECURITY.md`）。

```powershell
Copy-Item include/device-config.example.h include/device-config.h
```

`include/device-config.h` に2.4 GHz Wi-FiのSSID・パスワード、中継のHTTPS URL、読み取りトークン、PEM形式のルートCAを設定します。PEMを複数行で設定する場合はC++のraw文字列 `R"PEM(証明書本文)PEM"` を使います。NTPサーバーはLANから到達できるものを選びます。明るさは `POVO_BRIGHTNESS`（0〜255）です。未設定のビルドは設定不足画面になります。

```powershell
New-Item -ItemType Directory -Force .pio-core | Out-Null
$env:PLATFORMIO_CORE_DIR = (Resolve-Path .pio-core).Path
pio run -e cyd
```

このコマンドはコンパイルだけを実行します。個人設定入りの `.pio/build/cyd/firmware.bin` を公開・添付しないでください。

## 追加基板が届いた後の書き込み

使用中のCodex Microは対象にしません。新しい基板の接続前後でWindowsデバイスマネージャーのポートを確認し、その基板と確認できたCOM番号を明示します。ポート自動選択は使いません。

```powershell
pio run -e cyd -t upload --upload-port COM番号
```

`COM番号` は確認した実際のポート名に置き換えます。今回の作業ではこのコマンドを実行していません。書き込み後は `verification.md` の実機手順を実施します。

## 更新・復旧

個人設定を安全にバックアップし、変更内容を確認してソースを更新します。`device-config.example.h` の変更を個人設定へ反映して再ビルドします。日本語文言変更時は `python scripts/generate-font.py` で字形を再生成します。

v0.1.0ではTLS修正を取り込むため、ビルド基盤をpioarduino 55.03.311（Arduino 3.3.11 / ESP-IDF 5.5.5）へ移行しました。初回ビルドでは新しいSDKをダウンロードするため時間がかかります。旧SDKのキャッシュを手動で編集する必要はありません。

不具合時は以前のソースと設定で再ビルドし、専用基板へ書き戻します。PC中継の更新・DBバックアップ・復旧は関連プロジェクトの `relay/README.md` に従います。このファームウェアはpovoのコードや中継DBを変更しません。

## 開発チェック

```powershell
python test/audit-test.py
python scripts/audit-build-tools.py
pio run -e cyd
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python scripts/generate-font.py
git diff --exit-code -- include/japanese-font.h
```

CMakeにはホストC++コンパイラが必要です。Visual StudioではDeveloper Command Promptを利用できます。CMakeが利用できない場合も `test/status-test.cpp` をC++17、`include` と `.pio/libdeps/cyd/ArduinoJson/src` のインクルードパスでコンパイルして実行できます。
