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

## プロジェクト固有の知見

- [COMMON-AGENTS.md](COMMON-AGENTS.md) を最優先とし、作業前に [findings.md](findings.md) も先頭から末尾まで全文読む。出力省略や読取り失敗を読了扱いしない。
- 実現手段・環境差・明示的な例外・再利用する調査知見はfindings.mdへ随時記録する。変更履歴はCHANGELOG.md、実施した検証と未検証事項はverification.mdに残す。
- ユーザーの禁止・承認条件、秘密情報と既存データの保護、未コミット変更を戻さない義務を弱めない。

## 実機の変更禁止

PCに接続済みのCodex Micro (esp32-codex-notifications) を無断で検証に使わない。所有者の明示指示がある場合だけ、findings.mdの全flash退避・復元・CODEX_CYD_READY確認条件に従う。専用追加基板の到着・接続状態はその都度確認し、過去の到着予定から使用可能と推測しない。
