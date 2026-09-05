from pathlib import Path
import re
import unittest

class AuditTest(unittest.TestCase):
    def test_cli_lock_is_exact_and_excludes_home_server(self):
        text = (Path(__file__).resolve().parents[1] / "requirements-ci.txt").read_text()
        packages = [line for line in text.splitlines() if line and not line.startswith("#")]
        self.assertIn("platformio==6.1.19", packages)
        for package in packages:
            self.assertRegex(package, r"^[a-z0-9-]+==[^=\s]+$")
        names = {re.split(r"==", package)[0] for package in packages}
        self.assertTrue({"starlette", "uvicorn", "wsproto", "ajsonrpc"}.isdisjoint(names))


if __name__ == "__main__":
    unittest.main()
