#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "status-display.h"
#include "ui-text.h"
#include "japanese-font.h"
#if __has_include("device-config.h")
#include "device-config.h"
#else
#include "device-config.example.h"
#endif

namespace {
TFT_eSPI tft;
constexpr uint16_t bg = 0x0841, fg = 0xFFFF, accent = 0xFFE0;
void line(int y, const char* value, uint16_t color = fg) {
  tft.setTextColor(color, bg);
  int x = 8;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(value);
  while (*p && x < 312) {
    uint16_t c = *p++;
    if (c < 128) {
      if (c < 32 || c > 126 || x + 8 > 312) break;
      tft.drawBitmap(x, y, povo::kAsciiGlyphs[c - 32], 8, 16, color);
      x += 8; continue;
    }
    if ((c & 0xE0) == 0xC0 && *p) { c = ((c & 31) << 6) | (*p++ & 63); }
    else if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
      c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2;
    } else break;
    if (x + 16 > 312) break;
    bool found = false;
    for (const auto& glyph : povo::kJapaneseGlyphs) if (glyph.codepoint == c) {
      tft.drawBitmap(x, y, glyph.bitmap, 16, 16, color); found = true; break;
    }
    if (!found) tft.drawRect(x, y, 14, 14, color);
    x += 16;
  }
}
String date(uint64_t epoch) {
  if (!epoch) return povo::text::unknown;
  // Gregorian civil date, independent of ESP32's 32-bit time_t (2038).
  const uint64_t seconds = epoch / 1000 + 9 * 3600;
  const uint64_t z = seconds / 86400 + 719468;
  const uint64_t era = z / 146097, doe = z - era * 146097;
  const uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint64_t mp = (5 * doy + 2) / 153;
  const unsigned day = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
  const unsigned month = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%02u/%02u %02u:%02u JST", month, day,
      (unsigned)(seconds / 3600 % 24), (unsigned)(seconds / 60 % 60));
  return buffer;
}
}
void beginDisplay() {
  tft.init(); tft.setRotation(1); tft.setTextColor(fg, bg);
  ledcSetup(0, 5000, 8); ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, constrain(POVO_BRIGHTNESS, 0, 255));
}
void drawDisplay(const povo::Status* status, uint64_t elapsedMs, const char* error) {
  using namespace povo;
  tft.fillScreen(bg); line(6, text::title, accent);
  if (!status) { line(50, text::noStatus); if (error) line(88, error, accent); return; }
  const View v = derive(*status, elapsedMs);
  char buffer[96];
  if (!v.remainingKnown) line(34, text::unknown);
  else {
    snprintf(buffer, sizeof(buffer), text::remaining,
      (unsigned long long)(v.remainingSeconds / 86400), (unsigned long long)(v.remainingSeconds / 3600 % 24)); line(34, buffer, accent);
    snprintf(buffer, sizeof(buffer), text::minutes,
      (unsigned long long)(v.remainingSeconds / 3600), (unsigned long long)(v.remainingSeconds / 60 % 60)); line(54, buffer);
  }
  line(76, (String(text::expiry) + date(status->expiryAtMs) + " [" + text::sources[(int)status->expirySource] + "]").c_str());
  snprintf(buffer, sizeof(buffer), text::renewal, status->automaticRenewal ? "ON" : "OFF", (unsigned long)status->appliedUses, (unsigned long)status->maxUses); line(98, buffer);
  String state = text::states[(int)status->renewalState];
  if (v.confirmationPending) state += String(" / ") + text::pending;
  line(120, state.c_str(), accent);
  line(142, (String(text::codeDeadline) + date(status->codeDeadlineAtMs)).c_str());
  snprintf(buffer, sizeof(buffer), text::sync, (unsigned long long)(v.syncAgeMs / 60000)); line(164, buffer);
  if (v.stale) line(186, text::stale, accent);
  if (error) line(208, error, accent);
}
