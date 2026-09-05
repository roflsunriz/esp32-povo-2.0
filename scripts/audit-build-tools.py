"""Fail on unreviewed advisories; retain the complete unfiltered audit report."""
import json
from pathlib import Path
import subprocess
import sys

def main():
    Path("build").mkdir(exist_ok=True)
    result = subprocess.run([
        sys.executable, "-m", "pip_audit", "-r", "requirements-ci.txt",
        "--no-deps", "--disable-pip", "--cache-dir", "build/pip-audit-cache",
        "--format", "json",
    ], capture_output=True, text=True, encoding="utf-8")
    if result.returncode not in (0, 1):
        print(result.stderr, file=sys.stderr)
        return 1
    try:
        report = json.loads(result.stdout)
        packages = report["dependencies"]
        if not packages or any("skip_reason" in item for item in packages):
            raise ValueError("incomplete dependency audit")
    except (ValueError, KeyError):
        print("Audit did not return a complete report", file=sys.stderr)
        return 1
    Path("build/build-tools-audit.json").write_text(result.stdout, encoding="utf-8")
    unreviewed = []
    for package in packages:
        for vulnerability in package["vulns"]:
            unreviewed.append(f'{package["name"]} {package["version"]}: {vulnerability["id"]}')
    print(f"Audited {len(packages)} explicitly pinned build packages.")
    if unreviewed:
        print("\n".join(unreviewed), file=sys.stderr)
        return 1
    print("No known vulnerabilities found in the installed CLI build set.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
