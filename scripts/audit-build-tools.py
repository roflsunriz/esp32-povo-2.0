"""Fail on unreviewed advisories; retain the complete unfiltered audit report."""
import json
from pathlib import Path
import subprocess
import sys

# SECURITY.md documents why these HTTP-server-only paths are unreachable in
# the CLI-only build. Do not expand this to other packages or versions.
HOME_ONLY = {
    "GHSA-86qp-5c8j-p5mr", "GHSA-jp82-jpqv-5vv3", "GHSA-82w8-qh3p-5jfq",
    "GHSA-wqp7-x3pw-xc5r", "GHSA-x746-7m8f-x49c",
}


def reviewed(package, vulnerability):
    return (package["name"] == "starlette" and package["version"] == "0.52.1"
            and bool(HOME_ONLY.intersection(
                [vulnerability["id"], *vulnerability.get("aliases", [])])))


def main():
    Path("build").mkdir(exist_ok=True)
    result = subprocess.run([
        sys.executable, "-m", "pip_audit", "-r", "requirements-ci.txt",
        "--cache-dir", "build/pip-audit-cache", "--format", "json",
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
    excluded = 0
    for package in packages:
        for vulnerability in package["vulns"]:
            if reviewed(package, vulnerability):
                excluded += 1
            else:
                unreviewed.append(f'{package["name"]} {package["version"]}: {vulnerability["id"]}')
    print(f"Audited {len(packages)} packages; {excluded} reviewed Home-only findings.")
    if unreviewed:
        print("\n".join(unreviewed), file=sys.stderr)
        return 1
    print("No unreviewed findings. See SECURITY.md for the scoped exclusions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
