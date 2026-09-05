#pragma once
// Copy to device-config.h (Git ignored). No credentials are logged.
#define POVO_WIFI_SSID ""
#define POVO_WIFI_PASSWORD ""
// Full HTTPS URL ending in /api/v1/status; no query or user information.
#define POVO_STATUS_URL ""
// Only the relay read token, never the Android write token.
#define POVO_READ_TOKEN ""
// PEM root CA that validates the relay HTTPS certificate and hostname.
#define POVO_ROOT_CA ""
#define POVO_NTP_SERVER "pool.ntp.org"
// 0..255, always-on display; no BLE-dependent sleep.
#define POVO_BRIGHTNESS 128
