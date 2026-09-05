"""Extract the exact release section; refuse an unrecorded version."""
import re
import sys
from pathlib import Path

version = sys.argv[1].removeprefix("v")
text = Path("CHANGELOG.md").read_text(encoding="utf-8")
match = re.search(r"^## \[" + re.escape(version) + r"\][^\n]*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
if not match or not match[1].strip():
    sys.exit("CHANGELOG.md に該当版の変更履歴がありません")
print(match[1].strip())
