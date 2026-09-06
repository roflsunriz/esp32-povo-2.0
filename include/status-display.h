#pragma once
#include <stdint.h>
#include "display-settings.h"
#include "status-model.h"
void beginDisplay();
void drawDisplay(const povo::Status* status, uint64_t elapsedMs, const char* error);
void drawSetup(const char* ssid, const char* password);
// タブ・タッチ・BOOT・自動消灯。設定画面表示中はtrueを返す。
void pollDisplayInput(uint64_t nowMs);
void updateDisplayPower(uint64_t nowMs);
bool displayAwake();
povo::display::Page displayPage();
void requestRedraw();
