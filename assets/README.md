# 日本語字形の生成元

`unifont-jp.hex.gz` は公式の Unifont JP 17.0.05 です。

- 取得元: https://unifoundry.com/pub/unifont/unifont-17.0.05/font-builds/unifont_jp-17.0.05.hex.gz
- SHA-256: `ecf9455c8a44b09e3f645e6b1bab4ac22a57399bf19b1f8e950e11af9523df8c`
- ライセンス: SIL Open Font License 1.1（提供されるデュアルライセンスのうちOFLを選択）。`licenses/unifont-OFL-1.1.txt` を参照。

`python scripts/generate-font.py` は `include/ui-text.h` の日本語文字だけを抽出し、`include/japanese-font.h` を生成します。元ファイルのハッシュと不足字形を検査します。生成ヘッダーは手編集しません。追加の描画ライブラリとその保守負担を避け、小さな固定字形をTFTへ描画するために採用しました。
