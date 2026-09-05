#pragma once
#include "status-model.h"
void beginDisplay();
void drawDisplay(const povo::Status* status, uint64_t elapsedMs, const char* error);
