"""mitmproxy addon: save only redacted povo API structures, never raw flows."""
from __future__ import annotations

import json
import os
import re
from pathlib import Path
from urllib.parse import urlsplit

from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from mitmproxy import http

SAFE_VALUES = {
    "ENHANCED_EMAIL_OTP", "LOGIN_EMAIL_OTP", "EMAIL_OTP", "EMAIL",
    "mobile", "Mobile", "dashboard", "ecosystem", "LOGIN", "v2", "SUCCESS", "FAILURE",
}


def shape(value):
    if isinstance(value, dict):
        return {
            key if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]{0,63}", key)
            else "<dynamic-key>": shape(item) for key, item in value.items()
        }
    if isinstance(value, list):
        return [shape(item) for item in value[:3]]
    if value is None:
        return None
    if isinstance(value, bool):
        return "<boolean>"
    if isinstance(value, (int, float)):
        return "<number>"
    return value if value in SAFE_VALUES else "<string>"


def body_shape(message):
    try:
        return shape(json.loads(message.get_text(strict=False)))
    except (ValueError, TypeError):
        return "<non-json>"


def response(flow: http.HTTPFlow):
    if flow.request.host != "app.povo.jp" or flow.response is None:
        return
    path = urlsplit(flow.request.url).path
    # Only fixed, account-independent authentication paths are recorded.
    suffixes = ("/users/login/action", "/users/login", "/users/auth",
                "/users/token", "/otp", "/users/logout", "/users/auth/otp/send",
                "/account/plan/details/get", "/account/usage/plan/get")
    if not path.endswith(suffixes):
        return
    record = {
        "method": flow.request.method,
        "host": flow.request.host,
        "path": path,
        "request_header_names": sorted(set(flow.request.headers.keys())),
        "request": body_shape(flow.request),
        "status": flow.response.status_code,
        "response_header_names": sorted(set(flow.response.headers.keys())),
        "response": body_shape(flow.response),
    }
    destination = Path(os.environ["POVO_CAPTURE_OUTPUT"])
    with destination.open("a", encoding="utf-8") as output:
        output.write(json.dumps(record, ensure_ascii=False) + "\n")
