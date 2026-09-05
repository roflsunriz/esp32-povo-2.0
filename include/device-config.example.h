#pragma once
#include "povo-root-ca.h"
// Copy to device-config.h (Git ignored). No credentials are logged.
#define POVO_WIFI_SSID ""
#define POVO_WIFI_PASSWORD ""
// PEM root CA that validates app.povo.jp; hostname verification remains enabled.
#define POVO_ROOT_CA povo::rootCa
#define POVO_NTP_SERVER "pool.ntp.org"
// 0..255, always-on display; no BLE-dependent sleep.
#define POVO_BRIGHTNESS 128
