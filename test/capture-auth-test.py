"""Verify that reusable capture records cannot retain credential values."""

import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "capture_auth", Path(__file__).parents[1] / "scripts" / "capture-auth.py"
)
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    def test_nested_secrets_and_dynamic_keys(self):
        payload = {
            "auth_token": "secret-jwt", "otp_code": "123456",
            "email": "owner@example.com", "account": 123456789,
            "nested": [{"owner@example.com": "secret"}],
            "auth_mode": "ENHANCED_EMAIL_OTP", "empty": None,
        }
        result = capture.shape(payload)
        encoded = json.dumps(result)
        for secret in ("secret", "123456", "owner@example.com"):
            self.assertNotIn(secret, encoded)
        self.assertEqual(result["auth_mode"], "ENHANCED_EMAIL_OTP")
        self.assertIsNone(result["empty"])

    def test_arrays_are_bounded(self):
        self.assertEqual(capture.shape(list(range(1000))), ["<number>"] * 3)


if __name__ == "__main__":
    unittest.main()
