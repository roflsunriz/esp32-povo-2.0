import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "audit", Path(__file__).resolve().parents[1] / "scripts/audit-build-tools.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditTest(unittest.TestCase):
    def test_exception_is_scoped_to_reviewed_package_version_and_advisory(self):
        package = {"name": "starlette", "version": "0.52.1"}
        advisory = {"id": "PYSEC-2026-161", "aliases": ["GHSA-86qp-5c8j-p5mr"]}
        self.assertTrue(audit.reviewed(package, advisory))
        self.assertFalse(audit.reviewed({**package, "version": "0.52.0"}, advisory))
        self.assertFalse(audit.reviewed({**package, "name": "other"}, advisory))
        self.assertFalse(audit.reviewed(package, {"id": "new-advisory"}))


if __name__ == "__main__":
    unittest.main()
