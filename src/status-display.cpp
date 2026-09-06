#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "status-display.h"
#include "display-settings.h"
#include "ui-text.h"
#include "japanese-font.h"
#if __has_include("device-config.h")
#include "device-config.h"
#else
#include "device-config.example.h"
#endif
#ifdef ARDUINO
#include <Preferences.h>
#include <SPI.h>
#endif

namespace {
using povo::display::Page;
TFT_eSPI tft;
constexpr uint16_t bg = 0x0841, fg = 0xFFFF, accent = 0xFFE0, panel = 0x18E3;
// XPT2046配線とBOOTボタンはESP32-2432S028Rの代表値。TFTとは別バス(VSPI)。
constexpr int kTouchClockPin = 25, kTouchMisoPin = 39, kTouchMosiPin = 32,
              kTouchChipSelectPin = 33, kTouchIrqPin = 36;
constexpr int kBootButtonPin = 0;
constexpr int kTouchPressureMinimum = 120;
constexpr int kTouchRawLeft = 200, kTouchRawRight = 3700, kTouchRawTop = 240,
              kTouchRawBottom = 3800;
// ILI9341命令。TFT_eSPIの内部定義に依存しない。
constexpr uint8_t kDispoff = 0x28, kDispon = 0x29, kSlpin = 0x10, kSlpout = 0x11;
constexpr uint32_t kTapMinIntervalMs = 350;
constexpr char kSettingsStore[] = "povo-display";

struct State {
  Page page = Page::Status;
  size_t timeoutIndex = 0;
  size_t sleepPage = 0;
  bool inverted = false;
  bool awake = true;
  bool dirty = false;
  bool wasTouched = false;
  bool bootReady = false;
  uint64_t lastActivityMs = 0;
  uint64_t lastTapMs = 0;
  povo::display::BootFilter boot;
  povo::Status cachedStatus;
  bool haveCached = false;
  uint64_t cachedElapsedMs = 0;
  char cachedError[128] = {};
  bool haveError = false;
};
State state;
#ifdef ARDUINO
SPIClass touchBus(VSPI);
#endif

void lineAt(int x, int y, const char* value, uint16_t color = fg) {
  tft.setTextColor(color, bg);
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
void line(int y, const char* value, uint16_t color = fg) { lineAt(8, y, value, color); }
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
void applyBacklight() {
#ifdef ARDUINO
  ledcWrite(TFT_BL, state.awake ? constrain(POVO_BRIGHTNESS, 0, 255) : 0);
#else
  ledcWrite(TFT_BL, state.awake ? constrain(POVO_BRIGHTNESS, 0, 255) : 0);
#endif
}
void setAwakeLocked(bool awake) {
  if (state.awake == awake) { applyBacklight(); return; }
  state.awake = awake;
  state.wasTouched = false;
  if (!awake) {
    applyBacklight();
    tft.writecommand(kDispoff);
    tft.writecommand(kSlpin);
    return;
  }
  tft.writecommand(kSlpout);
#ifdef ARDUINO
  delay(120);
#endif
  tft.writecommand(kDispon);
  applyBacklight();
}
void loadSettings() {
  state.timeoutIndex = 0;
  state.inverted = false;
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin(kSettingsStore, true)) return;
  const uint32_t seconds = prefs.getUInt("sleep_sec", 0);
  state.inverted = prefs.getBool("inverted", false);
  prefs.end();
  for (size_t i = 0; i < povo::display::kSleepTimeoutCount; ++i)
    if (povo::display::kSleepTimeoutOptions[i] == seconds) { state.timeoutIndex = i; break; }
#endif
}
void saveSettings() {
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin(kSettingsStore, false)) return;
  prefs.putUInt("version", 1);
  prefs.putUInt("sleep_sec", povo::display::timeoutForIndex(state.timeoutIndex));
  prefs.putBool("inverted", state.inverted);
  prefs.end();
#endif
}
void applyRotation() {
  tft.setRotation(state.inverted ? povo::display::kRotationInverted
                                 : povo::display::kRotationNormal);
}
void drawTabs() {
  using namespace povo::display;
  const bool statusSelected = state.page == Page::Status;
  tft.fillRect(0, kTabY, kScreenW / 2, kTabH, statusSelected ? accent : panel);
  tft.fillRect(kScreenW / 2, kTabY, kScreenW - kScreenW / 2, kTabH,
               statusSelected ? panel : accent);
  lineAt(64, kTabY + 4, povo::text::tabStatus, statusSelected ? bg : fg);
  lineAt(208, kTabY + 4, povo::text::tabSleep, statusSelected ? fg : bg);
}
void drawStatusPage(const povo::Status* status, uint64_t elapsedMs, const char* error) {
  using namespace povo;
  line(6, text::title, accent);
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
  line(98, text::directMode);
  if (v.confirmationPending) line(120, text::pending, accent);
  line(142, text::precision);
  snprintf(buffer, sizeof(buffer), text::sync, (unsigned long long)(v.syncAgeMs / 60000)); line(164, buffer);
  if (error) line(186, error, accent);
  else if (v.stale) line(186, text::stale, accent);
  else line(186, text::rotateHint);
}
void drawSleepPage() {
  using namespace povo::display;
  char cell[32], current[32], title[96], made[96];
  formatTimeout(timeoutForIndex(state.timeoutIndex), current, sizeof(current));
  snprintf(title, sizeof(title), "%s %u/%u", povo::text::sleepTitle,
           static_cast<unsigned>(state.sleepPage + 1),
           static_cast<unsigned>(sleepPageCount()));
  line(6, title, accent);
  snprintf(made, sizeof(made), "%s %s", povo::text::sleepNow, current);
  line(30, made, accent);
  line(48, povo::text::sleepSelect);
  for (int row = 0; row < kSleepRows; ++row) {
    for (int col = 0; col < kSleepCols; ++col) {
      const size_t index =
          state.sleepPage * kSleepPerPage + static_cast<size_t>(row * kSleepCols + col);
      if (index >= kSleepTimeoutCount) continue;
      const int left = kGridColX[col], top = kGridTop + row * kGridRowH;
      const bool selected = index == state.timeoutIndex;
      tft.fillRect(left, top, kGridCellW, kGridCellH, selected ? accent : panel);
      formatTimeout(timeoutForIndex(index), cell, sizeof(cell));
      lineAt(left + 6, top + 4, cell, selected ? bg : fg);
    }
  }
  tft.fillRect(4, kNavY, 152, kNavH, panel);
  tft.fillRect(164, kNavY, 152, kNavH, panel);
  lineAt(64, kNavY + 2, povo::text::sleepPrev, fg);
  lineAt(224, kNavY + 2, povo::text::sleepNext, fg);
}
void redrawFromCache() {
  if (!state.awake) return;
  tft.fillScreen(bg);
  if (state.page == Page::Sleep) drawSleepPage();
  else drawStatusPage(state.haveCached ? &state.cachedStatus : nullptr,
                      state.cachedElapsedMs, state.haveError ? state.cachedError : nullptr);
  drawTabs();
}
#ifdef ARDUINO
int16_t clampAxis(int16_t value, int16_t maximum) {
  if (value < 0) return 0;
  if (value > maximum) return maximum;
  return value;
}
int16_t mapRaw(int16_t raw, int16_t rawStart, int16_t rawEnd, int16_t screenMaximum) {
  const long denominator = static_cast<long>(rawEnd) - rawStart;
  if (denominator > -100 && denominator < 100) return 0;
  const long value =
      (static_cast<long>(raw) - rawStart) * screenMaximum / denominator;
  return clampAxis(static_cast<int16_t>(value), screenMaximum);
}
int16_t bestTwoAverage(int16_t first, int16_t second, int16_t third) {
  const int16_t firstSecond = abs(first - second);
  const int16_t firstThird = abs(first - third);
  const int16_t thirdSecond = abs(third - second);
  if (firstSecond <= firstThird && firstSecond <= thirdSecond)
    return static_cast<int16_t>((first + second) / 2);
  if (firstThird <= firstSecond && firstThird <= thirdSecond)
    return static_cast<int16_t>((first + third) / 2);
  return static_cast<int16_t>((second + third) / 2);
}
// XPT2046の読み取りはコントローラーのデータシートに従う。複数サンプルの
// 中央寄せは抵抗膜タッチの定番手法。
bool readRawTouch(int16_t& rawX, int16_t& rawY, int16_t& pressure) {
  static const SPISettings settings(2000000, MSBFIRST, SPI_MODE0);
  touchBus.beginTransaction(settings);
  digitalWrite(kTouchChipSelectPin, LOW);
  touchBus.transfer(0xB1);
  const int16_t z1 = static_cast<int16_t>(touchBus.transfer16(0xC1) >> 3);
  const int16_t z2 = static_cast<int16_t>(touchBus.transfer16(0x91) >> 3);
  pressure = static_cast<int16_t>(z1 + 4095 - z2);
  int16_t x[3] = {}, y[3] = {};
  if (pressure >= kTouchPressureMinimum) {
    touchBus.transfer16(0x91);
    for (int i = 0; i < 3; ++i) {
      x[i] = static_cast<int16_t>(touchBus.transfer16(0xD1) >> 3);
      y[i] = static_cast<int16_t>(touchBus.transfer16(0x91) >> 3);
    }
  }
  digitalWrite(kTouchChipSelectPin, HIGH);
  touchBus.endTransaction();
  if (pressure < kTouchPressureMinimum) return false;
  rawX = bestTwoAverage(x[0], x[1], x[2]);
  rawY = bestTwoAverage(y[0], y[1], y[2]);
  return true;
}
bool readTouchHardware(povo::display::Point& out) {
  if (digitalRead(kTouchIrqPin) != LOW) return false;
  int16_t rawX = 0, rawY = 0, pressure = 0;
  if (!readRawTouch(rawX, rawY, pressure)) return false;
  povo::display::Point mapped{
      mapRaw(rawX, kTouchRawLeft, kTouchRawRight, povo::display::kScreenW - 1),
      mapRaw(rawY, kTouchRawTop, kTouchRawBottom, povo::display::kScreenH - 1)};
  out = povo::display::orientPoint(mapped, state.inverted);
  return true;
}
#endif
}
void beginDisplay() {
  loadSettings();
  state.sleepPage = povo::display::sleepPageForIndex(state.timeoutIndex);
  tft.init();
  applyRotation();
  tft.setTextColor(fg, bg);
  ledcAttach(TFT_BL, 5000, 8);
  applyBacklight();
#ifdef ARDUINO
  pinMode(kBootButtonPin, INPUT_PULLUP);
  pinMode(kTouchChipSelectPin, OUTPUT);
  digitalWrite(kTouchChipSelectPin, HIGH);
  pinMode(kTouchIrqPin, INPUT_PULLUP);
  touchBus.begin(kTouchClockPin, kTouchMisoPin, kTouchMosiPin, kTouchChipSelectPin);
#endif
}
void drawDisplay(const povo::Status* status, uint64_t elapsedMs, const char* error) {
  if (status) { state.cachedStatus = *status; state.haveCached = true; }
  else state.haveCached = false;
  state.cachedElapsedMs = elapsedMs;
  if (error) {
    snprintf(state.cachedError, sizeof(state.cachedError), "%s", error);
    state.haveError = true;
  } else state.haveError = false;
  state.dirty = false;
  redrawFromCache();
}
void drawSetup(const char* ssid, const char* password) {
  setAwakeLocked(true);
  applyBacklight();
  tft.fillScreen(bg);
  line(6, povo::text::setupTitle, accent);
  line(38, povo::text::setupWifi);
  line(62, ssid, accent);
  line(90, "Password:"); line(112, password, accent);
  line(152, povo::text::setupOpen);
  line(178, "http://192.168.4.1", accent);
}
void pollDisplayInput(uint64_t nowMs) {
#ifdef ARDUINO
  const bool rawHigh = digitalRead(kBootButtonPin) != LOW;
  if (!state.bootReady) {
    povo::display::bootInit(state.boot, rawHigh, nowMs);
    state.bootReady = true;
  } else if (povo::display::bootUpdate(state.boot, rawHigh, nowMs)) {
    state.inverted = !state.inverted;
    saveSettings();
    applyRotation();
    state.lastActivityMs = nowMs;
    if (!state.awake) setAwakeLocked(true);
    redrawFromCache();
    return;
  }
  povo::display::Point point;
  const bool touched = readTouchHardware(point);
  const bool tap = touched && !state.wasTouched &&
                   (nowMs - state.lastTapMs >= kTapMinIntervalMs || state.lastTapMs == 0);
  state.wasTouched = touched;
  if (!tap) return;
  state.lastTapMs = nowMs;
  state.lastActivityMs = nowMs;
  if (!state.awake) { setAwakeLocked(true); redrawFromCache(); return; }
  Page tab;
  if (povo::display::tabForTouch(point.x, point.y, tab)) {
    if (tab != state.page) {
      state.page = tab;
      if (tab == Page::Sleep)
        state.sleepPage = povo::display::sleepPageForIndex(state.timeoutIndex);
      redrawFromCache();
    }
    return;
  }
  if (state.page != Page::Sleep) return;
  size_t index = 0;
  if (povo::display::sleepCellForTouch(point.x, point.y, state.sleepPage, index)) {
    if (index != state.timeoutIndex) {
      state.timeoutIndex = index;
      saveSettings();
    }
    redrawFromCache();
    return;
  }
  bool prev = false;
  if (povo::display::sleepNavForTouch(point.x, point.y, prev)) {
    const size_t count = povo::display::sleepPageCount();
    state.sleepPage = prev ? (state.sleepPage + count - 1) % count
                           : (state.sleepPage + 1) % count;
    redrawFromCache();
  }
#else
  (void)nowMs;
#endif
}
void updateDisplayPower(uint64_t nowMs) {
  if (!state.awake) return;
  const uint32_t timeout = povo::display::timeoutForIndex(state.timeoutIndex);
  if (povo::display::shouldSleep(timeout, nowMs - state.lastActivityMs))
    setAwakeLocked(false);
}
bool displayAwake() { return state.awake; }
povo::display::Page displayPage() { return state.page; }
void setDisplayPage(povo::display::Page page) {
  if (state.page == page) return;
  state.page = page;
  if (page == Page::Sleep)
    state.sleepPage = povo::display::sleepPageForIndex(state.timeoutIndex);
  state.dirty = true;
  redrawFromCache();
}
bool setSleepTimeout(uint32_t seconds) {
  for (size_t i = 0; i < povo::display::kSleepTimeoutCount; ++i) {
    if (povo::display::kSleepTimeoutOptions[i] != seconds) continue;
    state.timeoutIndex = i;
    state.sleepPage = povo::display::sleepPageForIndex(i);
    saveSettings();
    state.dirty = true;
    redrawFromCache();
    return true;
  }
  return false;
}
uint32_t sleepTimeout() { return povo::display::timeoutForIndex(state.timeoutIndex); }
void toggleDisplayRotation() {
  state.inverted = !state.inverted;
  saveSettings();
  applyRotation();
  redrawFromCache();
}
void requestRedraw() { state.dirty = true; redrawFromCache(); }
bool consumeRedraw() {
  const bool redraw = state.dirty;
  state.dirty = false;
  return redraw;
}
