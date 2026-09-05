#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
using String = std::string;
inline int constrain(int value, int low, int high) { return std::clamp(value, low, high); }
inline bool ledcAttach(int, int, int) { return true; }
inline void ledcWrite(int, int) {}
#define TFT_BL 21
#define PROGMEM
