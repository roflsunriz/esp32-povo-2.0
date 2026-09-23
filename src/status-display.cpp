#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "status-display.h"
#include "display-settings.h"
#include "display-diff.h"
#include "touch-calibration.h"
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
#include <esp_timer.h>
#include "sensitive-xpt2046.h"
#endif

namespace {
using povo::display::Page;
TFT_eSPI tft;
TFT_eSprite canvas(&tft);
TFT_eSPI* drawing = &tft;
display_diff::Bands bandDiff;
bool canvasReady = false;
constexpr uint16_t bg = 0x0841, fg = 0xFFFF, accent = 0xFFE0, panel = 0x18E3;
// XPT2046配線とBOOTボタンはESP32-2432S028Rの代表値。TFTとは別バス(VSPI)。
constexpr int kTouchClockPin = 25, kTouchMisoPin = 39, kTouchMosiPin = 32,
              kTouchChipSelectPin = 33, kTouchIrqPin = 36;
constexpr int kBootButtonPin = 0;
// ILI9341命令。TFT_eSPIの内部定義に依存しない。
constexpr uint8_t kDispoff = 0x28, kDispon = 0x29, kSlpin = 0x10, kSlpout = 0x11;
constexpr uint32_t kTapMinIntervalMs = 350;
constexpr char kSettingsStore[] = "povo-display";

struct State {
  Page page = Page::Status;
  uint32_t sleepSec = 0;
  uint32_t pollSec = 300;
  int sleepScroll = 0;
  // Whole-content drag gesture fixed at contact start.
  int dragMode = 0;  // 0 none, 1 minutes, 2 hours, 3 poll, 4 scroll
  int dragStartX = 0;
  int dragStartY = 0;
  int dragStartScroll = 0;
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
  uint64_t cachedNextPollInMs = 0;
  bool cachedFetching = false;
  char cachedError[128] = {};
  bool haveError = false;
  povo::touch::Calibration calibration;
  povo::touch::SampleFilter touchFilter;
};
State state;
#ifdef ARDUINO
SPIClass touchBus(VSPI);
SensitiveXpt2046 touch(kTouchChipSelectPin, kTouchIrqPin,
                       povo::touch::kDefaultPressure);
#endif

void lineAt(int x, int y, const char* value, uint16_t color = fg) {
  drawing->setTextColor(color, bg);
  const uint8_t* p = reinterpret_cast<const uint8_t*>(value);
  while (*p && x < 312) {
    uint16_t c = *p++;
    if (c < 128) {
      if (c < 32 || c > 126 || x + 8 > 312) break;
      drawing->drawBitmap(x, y, povo::kAsciiGlyphs[c - 32], 8, 16, color);
      x += 8; continue;
    }
    if ((c & 0xE0) == 0xC0 && *p) { c = ((c & 31) << 6) | (*p++ & 63); }
    else if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
      c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2;
    } else break;
    if (x + 16 > 312) break;
    bool found = false;
    for (const auto& glyph : povo::kJapaneseGlyphs) if (glyph.codepoint == c) {
      drawing->drawBitmap(x, y, glyph.bitmap, 16, 16, color); found = true; break;
    }
    if (!found) drawing->drawRect(x, y, 14, 14, color);
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
  bandDiff.invalidate();
  applyBacklight();
}
void loadSettings() {
  state.sleepSec = 0;
  state.pollSec = 300;
  state.sleepScroll = 0;
  state.dragMode = 0;
  state.inverted = false;
  state.calibration = {};
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin(kSettingsStore, true)) return;
  const uint32_t seconds = prefs.getUInt("sleep_sec", 0);
  const uint32_t poll = prefs.getUInt("poll_sec", 300);
  state.inverted = prefs.getBool("inverted", false);
  povo::touch::StoredCalibration stored = {};
  if (prefs.getBytesLength("touch_calib") == sizeof(stored) &&
      prefs.getBytes("touch_calib", &stored, sizeof(stored)) == sizeof(stored)) {
    povo::touch::decode(stored, state.calibration);
  }
  prefs.end();
  if (povo::display::isValidSleepTimeout(seconds)) state.sleepSec = seconds;
  if (povo::display::isValidPollSlider(poll)) state.pollSec = poll;
#endif
}
void saveSettings() {
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin(kSettingsStore, false)) return;
  prefs.putUInt("version", 2);
  prefs.putUInt("sleep_sec", state.sleepSec);
  prefs.putUInt("poll_sec", state.pollSec);
  prefs.putBool("inverted", state.inverted);
  prefs.end();
#endif
}
#ifdef ARDUINO
bool saveTouchCalibration() {
  Preferences prefs;
  if (!prefs.begin(kSettingsStore, false)) return false;
  const auto stored = povo::touch::encode(state.calibration);
  const bool saved = prefs.putBytes("touch_calib", &stored, sizeof(stored)) ==
                     sizeof(stored);
  prefs.end();
  return saved;
}
#endif
void applyRotation() {
  tft.setRotation(state.inverted ? povo::display::kRotationInverted
                                 : povo::display::kRotationNormal);
  bandDiff.invalidate();
}
void drawTabs() {
  using namespace povo::display;
  const bool statusSelected = state.page == Page::Status;
  drawing->fillRect(0, kTabY, kScreenW / 2, kTabH, statusSelected ? accent : panel);
  drawing->fillRect(kScreenW / 2, kTabY, kScreenW - kScreenW / 2, kTabH,
               statusSelected ? panel : accent);
  lineAt(64, kTabY + 4, povo::text::tabStatus, statusSelected ? bg : fg);
  lineAt(208, kTabY + 4, povo::text::tabSleep, statusSelected ? fg : bg);
}
void drawStatusPage(const povo::Status* status, uint64_t elapsedMs, const char* error,
                      uint64_t nextPollInMs, bool fetching) {
  using namespace povo;
  line(6, text::title, accent);
  if (!status) {
    line(50, text::noStatus);
    if (error) line(88, error, accent);
    if (!canvasReady) line(186, text::displayMemoryError, accent);
    return;
  }
  const View v = derive(*status, elapsedMs);
  char buffer[96];
  if (!v.remainingKnown) line(32, text::unknown);
  else {
    snprintf(buffer, sizeof(buffer), text::remaining,
      (unsigned long long)(v.remainingSeconds / 86400), (unsigned long long)(v.remainingSeconds / 3600 % 24)); line(32, buffer, accent);
    snprintf(buffer, sizeof(buffer), text::minutes,
      (unsigned long long)(v.remainingSeconds / 3600), (unsigned long long)(v.remainingSeconds / 60 % 60)); line(52, buffer);
  }
  // 減るタイプの残り時間バー。数値表示は維持し、視覚的に残量を示す。
  const uint64_t barPermille = v.remainingKnown
      ? progressPermille(status->expiryAtMs, v.nowMs, status->spanMs) : 0;
  const int fill = povo::display::barFillWidth(povo::display::kBarW, barPermille);
  drawing->fillRect(povo::display::kBarX, povo::display::kBarY,
                    povo::display::kBarW, povo::display::kBarH, panel);
  if (fill > 0)
    drawing->fillRect(povo::display::kBarX, povo::display::kBarY,
                      fill, povo::display::kBarH, accent);
  line(90, (String(text::expiry) + date(status->expiryAtMs) + " [" + text::sources[(int)status->expirySource] + "]").c_str());
  line(112, text::directMode);
  if (v.confirmationPending) line(132, text::pending, accent);
  line(150, text::precision);
  if (error) line(168, error, accent);
  else if (fetching) line(168, text::fetchingText, accent);
  else if (nextPollInMs > 0) {
    String combined;
    {
      char syncPart[64];
      snprintf(syncPart, sizeof(syncPart), text::sync,
               (unsigned long long)(v.syncAgeMs / 60000));
      combined = syncPart;
    }
    char nextPart[64];
    snprintf(nextPart, sizeof(nextPart), text::nextPoll,
             (unsigned long long)(nextPollInMs / 60000),
             (unsigned long long)(nextPollInMs / 1000 % 60));
    combined += " ";
    combined += nextPart;
    line(168, combined.c_str());
  }
  else {
    snprintf(buffer, sizeof(buffer), text::sync, (unsigned long long)(v.syncAgeMs / 60000)); line(168, buffer);
  }
  if (error) line(188, error, accent);
  else if (!canvasReady) line(188, text::displayMemoryError, accent);
  else if (v.stale) line(188, text::stale, accent);
  else line(188, text::rotateHint);
}
void drawSleepPage() {
  using namespace povo::display;
  const int scroll = clampSleepScroll(state.sleepScroll);
  auto visibleY = [scroll](int y) { return y - scroll; };
  auto drawRow = [&](int y, const char* text, uint16_t color = fg) {
    const int visible = visibleY(y);
    if (visible < kSleepVisibleTop || visible > kSleepVisibleBottom - 16)
      return;
    line(visible, text, color);
  };
  auto drawTrack = [&](int centerY, uint32_t value, uint32_t minV,
                       uint32_t maxV) {
    const int y = visibleY(centerY);
    // タブと見出しの領域へはみ出さない。
    if (y < kSleepVisibleTop + 8 || y > kSleepVisibleBottom - 8) return;
    drawing->drawRect(kSliderX0, y - 2, kSliderX1 - kSliderX0, 5, fg);
    const int thumbX = sliderXFromValue(value, minV, maxV);
    if (thumbX > kSliderX0)
      drawing->fillRect(kSliderX0, y - 2, thumbX - kSliderX0, 5, accent);
    drawing->fillRect(thumbX - 6, y - 6, 12, 13, fg);
    drawing->fillRect(thumbX - 4, y - 4, 8, 9, panel);
  };
  char combined[48] = {};
  const uint32_t minutes = sleepMinutesPart(state.sleepSec);
  const uint32_t hours = sleepHoursPart(state.sleepSec);
  if (state.sleepSec == 0)
    snprintf(combined, sizeof(combined), "%s", povo::text::sleepAlwaysOn);
  else
    snprintf(combined, sizeof(combined), povo::text::sleepCombined,
             static_cast<unsigned>(hours), static_cast<unsigned>(minutes));
  line(6, povo::text::sleepTitle, accent);
  char title[64];
  snprintf(title, sizeof(title), "%s", combined);
  drawRow(32, title, fg);
  char label[48];
  snprintf(label, sizeof(label), povo::text::sleepMinutesLabel,
           static_cast<unsigned>(minutes));
  drawRow(52, label, fg);
  drawTrack(kSliderMinutesY, minutes, 0, kSleepMinutesMax);
  snprintf(label, sizeof(label), povo::text::sleepHoursLabel,
           static_cast<unsigned>(hours));
  drawRow(100, label, fg);
  drawTrack(kSliderHoursY, hours, 0, kSleepHoursMax);
  snprintf(label, sizeof(label), povo::text::sleepPollLabel,
           static_cast<unsigned>(state.pollSec));
  drawRow(148, label, fg);
  drawTrack(kSliderPollY, state.pollSec, kPollSliderMinSec, kPollSliderMaxSec);
  drawRow(194, povo::text::sleepAlwaysNote, fg);
  // 右端のスクロールバーは固定表示。
  const int trackH = kSleepScrollBarY1 - kSleepScrollBarY0;
  const int thumbH = (kSleepVisibleBottom - kSleepVisibleTop) * trackH /
                     kSleepContentH;
  const int travel = trackH - thumbH;
  const int thumbY = travel <= 0 || kSleepScrollMax <= 0
                         ? kSleepScrollBarY0
                         : kSleepScrollBarY0 + scroll * travel / kSleepScrollMax;
  drawing->drawRect(kSleepScrollBarX0, kSleepScrollBarY0, 12, trackH, panel);
  drawing->fillRect(kSleepScrollBarX0 + 2, thumbY, 8, thumbH, fg);
}
void redrawFromCache() {
  if (!state.awake) return;
  drawing = canvasReady ? static_cast<TFT_eSPI*>(&canvas) : &tft;
  display_diff::clearFrame(*drawing, bg);
  if (state.page == Page::Sleep) drawSleepPage();
  else drawStatusPage(state.haveCached ? &state.cachedStatus : nullptr,
                      state.cachedElapsedMs, state.haveError ? state.cachedError : nullptr,
                      state.cachedNextPollInMs, state.cachedFetching);
  drawTabs();
  if (canvasReady) {
    const uint16_t changed = bandDiff.update(
        static_cast<const uint8_t*>(canvas.getPointer()));
    if (!display_diff::eachRun(changed, [](size_t top, size_t height) {
          return canvas.pushSprite(0, static_cast<int32_t>(top), 0,
                                   static_cast<int32_t>(top),
                                   static_cast<int32_t>(display_diff::kWidth),
                                   static_cast<int32_t>(height));
        })) {
      canvas.pushSprite(0, 0);
      bandDiff.invalidate();
    }
  }
  drawing = &tft;
}
#ifdef ARDUINO
bool readTouchHardware(povo::display::Point& out) {
  if (!touch.tirqTouched()) { state.touchFilter.reset(); return false; }
  const SensitiveTouchPoint point = touch.getPoint();
  povo::touch::Sample stable;
  if (point.z < state.calibration.pressure) {
    if (!state.touchFilter.current(stable)) return false;
    out = {stable.x, stable.y};
    return true;
  }
  povo::display::Point mapped{
      povo::touch::mapAxis(point.x, state.calibration.left, state.calibration.right,
                           povo::touch::kTargetLeft, povo::touch::kTargetRight,
                           povo::display::kScreenW - 1),
      povo::touch::mapAxis(point.y, state.calibration.top, state.calibration.bottom,
                           povo::touch::kTargetTop, povo::touch::kTargetBottom,
                           povo::display::kScreenH - 1)};
  mapped = povo::display::orientPoint(mapped, state.inverted);
  if (!state.touchFilter.push({static_cast<int16_t>(mapped.x),
                               static_cast<int16_t>(mapped.y)}, stable)) return false;
  out = {stable.x, stable.y};
  return true;
}

bool captureCalibrationPoint(int16_t& rawX, int16_t& rawY, int16_t& pressure) {
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) < 15000) {
    if (!touch.tirqTouched()) { delay(10); continue; }
    int32_t sumX = 0, sumY = 0;
    int16_t count = 0, minimum = 32767;
    while (touch.tirqTouched() && count < 12 &&
           static_cast<uint32_t>(millis() - start) < 15000) {
      const SensitiveTouchPoint point = touch.getPoint();
      if (point.z >= povo::touch::kCapturePressure &&
          point.x >= 0 && point.x <= 4095 &&
          point.y >= 0 && point.y <= 4095) {
        sumX += point.x; sumY += point.y;
        if (point.z < minimum) minimum = point.z;
        ++count;
      }
      delay(12);
    }
    while (touch.tirqTouched() &&
           static_cast<uint32_t>(millis() - start) < 15000) {
      touch.getPoint(); delay(10);
    }
    if (count >= 4) {
      rawX = static_cast<int16_t>(sumX / count);
      rawY = static_cast<int16_t>(sumY / count);
      pressure = minimum;
      return true;
    }
  }
  return false;
}
#endif

void drawCalibrationStep(const char* title, int x, int y) {
  drawing = &tft;
  bandDiff.invalidate();
  tft.fillScreen(bg);
  line(8, title, accent);
  line(40, povo::text::touchInstruction);
  tft.fillRect(x - 10, y, 21, 1, accent);
  tft.fillRect(x, y - 10, 1, 21, accent);
}

#ifdef ARDUINO
void calibrateTouch() {
  setAwakeLocked(true);
  touch.setPressureThreshold(povo::touch::kCapturePressure);
  tft.setRotation(povo::display::kRotationNormal);
  int16_t left = 0, top = 0, right = 0, bottom = 0, firstZ = 0, secondZ = 0;
  drawCalibrationStep(povo::text::touchStep1, povo::touch::kTargetLeft,
                      povo::touch::kTargetTop);
  const bool first = captureCalibrationPoint(left, top, firstZ);
  if (first) {
    drawCalibrationStep(povo::text::touchStep2, povo::touch::kTargetRight,
                        povo::touch::kTargetBottom);
  }
  const bool second = first && captureCalibrationPoint(right, bottom, secondZ);
  if (second) {
    povo::touch::Calibration candidate{left, right, top, bottom,
        povo::touch::thresholdFor(firstZ < secondZ ? firstZ : secondZ)};
    if (povo::touch::valid(candidate)) {
      const auto previous = state.calibration;
      state.calibration = candidate;
      if (!saveTouchCalibration()) {
        state.calibration = previous;
        tft.fillScreen(bg); line(60, povo::text::touchSaveError, accent); delay(1800);
      }
    } else {
      tft.fillScreen(bg); line(60, povo::text::touchInvalid, accent); delay(1800);
    }
  } else {
    tft.fillScreen(bg); line(60, povo::text::touchTimeout, accent); delay(1800);
  }
  applyRotation();
  touch.setPressureThreshold(state.calibration.pressure);
  state.touchFilter.reset();
  state.wasTouched = false;
  state.lastActivityMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  redrawFromCache();
}
#endif
}
void beginDisplay() {
  loadSettings();
  tft.init();
  applyRotation();
  canvas.setColorDepth(8);
  canvasReady = canvas.createSprite(povo::display::kScreenW,
                                     povo::display::kScreenH) != nullptr;
  tft.setTextColor(fg, bg);
  ledcAttach(TFT_BL, 5000, 8);
  applyBacklight();
#ifdef ARDUINO
  pinMode(kBootButtonPin, INPUT_PULLUP);
  touchBus.begin(kTouchClockPin, kTouchMisoPin, kTouchMosiPin, kTouchChipSelectPin);
  touch.begin(touchBus);
  touch.setRotation(1);
  touch.setPressureThreshold(state.calibration.pressure);
#endif
}
void drawDisplay(const povo::Status* status, uint64_t elapsedMs, const char* error,
                   uint64_t nextPollInMs, bool fetching) {
  if (status) { state.cachedStatus = *status; state.haveCached = true; }
  else state.haveCached = false;
  state.cachedElapsedMs = elapsedMs;
  state.cachedNextPollInMs = nextPollInMs;
  state.cachedFetching = fetching;
  if (error) {
    snprintf(state.cachedError, sizeof(state.cachedError), "%s", error);
    state.haveError = true;
  } else state.haveError = false;
  state.dirty = false;
  redrawFromCache();
}
void drawSetup(const char* ssid, const char* password) {
  setAwakeLocked(true);
  drawing = &tft;
  bandDiff.invalidate();
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
  } else {
    const auto action = povo::display::bootUpdate(state.boot, rawHigh, nowMs);
    if (action != povo::display::BootAction::None) {
      state.lastActivityMs = nowMs;
      if (action == povo::display::BootAction::Calibrate) calibrateTouch();
      else {
        state.inverted = !state.inverted;
        saveSettings();
        applyRotation();
        if (!state.awake) setAwakeLocked(true);
        redrawFromCache();
      }
      return;
    }
  }
  povo::display::Point point;
  const bool touched = readTouchHardware(point);
  const bool contactStart = touched && !state.wasTouched;
  const bool tap = touched && !state.wasTouched &&
                   (nowMs - state.lastTapMs >= kTapMinIntervalMs || state.lastTapMs == 0);
  state.wasTouched = touched;
  if (!touched) {
    state.dragMode = 0;
    if (!tap) return;
  }
  if (!tap && !touched) return;
  if (contactStart) {
    // 接触開始点で操作種別を固定する。タブ上は切替専用で変化させない。
    // タップ間隔でタップ自体が抑止されてもドラッグは追従する。
    state.dragMode = 0;
    state.dragStartX = point.x;
    state.dragStartY = point.y;
    state.dragStartScroll = povo::display::clampSleepScroll(state.sleepScroll);
    Page startTab;
    if (!povo::display::tabForTouch(point.x, point.y, startTab) &&
        state.awake && state.page == Page::Sleep) {
      const int startContentY =
          point.y + povo::display::clampSleepScroll(state.sleepScroll);
      state.dragMode = 4;
      if (point.x >= 16 && point.x <= 283) {
        if (startContentY >= povo::display::kSliderMinutesY - povo::display::kSliderHalfH &&
            startContentY < povo::display::kSliderMinutesY + povo::display::kSliderHalfH)
          state.dragMode = 1;
        else if (startContentY >= povo::display::kSliderHoursY - povo::display::kSliderHalfH &&
                 startContentY < povo::display::kSliderHoursY + povo::display::kSliderHalfH)
          state.dragMode = 2;
        else if (startContentY >= povo::display::kSliderPollY - povo::display::kSliderHalfH &&
                 startContentY < povo::display::kSliderPollY + povo::display::kSliderHalfH)
          state.dragMode = 3;
      }
    }
  }
  if (!tap && !contactStart) {
    // 接触継続中のドラッグ。Sleepタブでは同種別の操作だけを追従する。
    if (state.awake && state.page == Page::Sleep && state.dragMode != 0) {
      state.lastActivityMs = nowMs;
      if (state.dragMode == 4) {
        const int delta = state.dragStartY - point.y;
        if (delta < 6 && delta > -6) return;
        const int target = povo::display::clampSleepScroll(state.dragStartScroll + delta);
        if (target != povo::display::clampSleepScroll(state.sleepScroll)) {
          state.sleepScroll = target;
          redrawFromCache();
        }
        return;
      }
      if (point.x < 16 || point.x > 283) return;
      if (state.dragMode == 1) {
        const uint32_t minutes = povo::display::sliderValueFromX(
            point.x, 0, povo::display::kSleepMinutesMax, 1);
        if (minutes != povo::display::sleepMinutesPart(state.sleepSec)) {
          state.sleepSec = povo::display::sleepTimeoutFromParts(
              minutes, povo::display::sleepHoursPart(state.sleepSec));
          saveSettings();
          redrawFromCache();
        }
      } else if (state.dragMode == 2) {
        const uint32_t hours = povo::display::sliderValueFromX(
            point.x, 0, povo::display::kSleepHoursMax, 1);
        if (hours != povo::display::sleepHoursPart(state.sleepSec)) {
          state.sleepSec = povo::display::sleepTimeoutFromParts(
              povo::display::sleepMinutesPart(state.sleepSec), hours);
          saveSettings();
          redrawFromCache();
        }
      } else if (state.dragMode == 3) {
        const uint32_t poll = povo::display::sliderValueFromX(
            point.x, povo::display::kPollSliderMinSec,
            povo::display::kPollSliderMaxSec,
            povo::display::kPollSliderStepSec);
        if (poll != state.pollSec) {
          state.pollSec = poll;
          saveSettings();
          redrawFromCache();
        }
      }
    }
    return;
  }
  if (!tap) return;
  state.lastTapMs = nowMs;
  state.lastActivityMs = nowMs;
  if (!state.awake) {
    setAwakeLocked(true);
    state.wasTouched = true; // 復帰に使った接触は離すまで操作へ流さない。
    state.dragMode = 0;
    redrawFromCache(); return;
  }
  Page tab;
  if (povo::display::tabForTouch(point.x, point.y, tab)) {
    if (tab != state.page) {
      state.page = tab;
      state.dragMode = 0;
      redrawFromCache();
    }
    return;
  }
  if (state.page != Page::Sleep) return;
  const int scroll = povo::display::clampSleepScroll(state.sleepScroll);
  const int contentY = point.y + scroll;
  // スクロールバーは指位置へ比例移動する。
  if (point.x >= 281 && point.y >= povo::display::kSleepScrollBarY0 &&
      point.y < povo::display::kSleepScrollBarY1) {
    state.sleepScroll = povo::display::sleepScrollFromTrackY(point.y);
    state.dragMode = 4;
    redrawFromCache();
    return;
  }
  state.dragMode = 4;
  uint32_t minutes = povo::display::sleepMinutesPart(state.sleepSec);
  uint32_t hours = povo::display::sleepHoursPart(state.sleepSec);
  bool timeoutTouched = false;
  if (point.x >= 16 && point.x <= 283) {
    if (contentY >= povo::display::kSliderMinutesY - povo::display::kSliderHalfH &&
        contentY < povo::display::kSliderMinutesY + povo::display::kSliderHalfH) {
      minutes = povo::display::sliderValueFromX(point.x, 0,
                                                povo::display::kSleepMinutesMax, 1);
      state.dragMode = 1;
      timeoutTouched = true;
    } else if (contentY >= povo::display::kSliderHoursY - povo::display::kSliderHalfH &&
               contentY < povo::display::kSliderHoursY + povo::display::kSliderHalfH) {
      hours = povo::display::sliderValueFromX(point.x, 0,
                                              povo::display::kSleepHoursMax, 1);
      state.dragMode = 2;
      timeoutTouched = true;
    } else if (contentY >= povo::display::kSliderPollY - povo::display::kSliderHalfH &&
               contentY < povo::display::kSliderPollY + povo::display::kSliderHalfH) {
      const uint32_t poll = povo::display::sliderValueFromX(
          point.x, povo::display::kPollSliderMinSec,
          povo::display::kPollSliderMaxSec,
          povo::display::kPollSliderStepSec);
      if (poll != state.pollSec) {
        state.pollSec = poll;
        saveSettings();
      }
      state.dragMode = 3;
      redrawFromCache();
      return;
    }
  }
  if (timeoutTouched) {
    const uint32_t total =
        povo::display::sleepTimeoutFromParts(minutes, hours);
    if (total != state.sleepSec) {
      state.sleepSec = total;
      saveSettings();
    }
    redrawFromCache();
    return;
  }
  // 空白タップはスクロール開始点だけを確定する。
  redrawFromCache();
#else
  (void)nowMs;
#endif
}
void updateDisplayPower(uint64_t nowMs) {
  if (!state.awake) return;
  if (povo::display::shouldSleep(state.sleepSec, nowMs - state.lastActivityMs))
    setAwakeLocked(false);
}
bool displayAwake() { return state.awake; }
povo::display::Page displayPage() { return state.page; }
void setDisplayPage(povo::display::Page page) {
  if (state.page == page) return;
  state.page = page;
  state.dragMode = 0;
  state.dirty = true;
  redrawFromCache();
}
bool setSleepTimeout(uint32_t seconds) {
  if (!povo::display::isValidSleepTimeout(seconds)) return false;
  if (seconds == state.sleepSec) {
    state.dirty = true;
    redrawFromCache();
    return true;
  }
  state.sleepSec = seconds;
  saveSettings();
  state.dirty = true;
  redrawFromCache();
  return true;
}
uint32_t sleepTimeout() { return state.sleepSec; }
uint32_t pollIntervalSec() { return state.pollSec; }
bool setPollIntervalSec(uint32_t seconds) {
  if (!povo::display::isValidPollSlider(seconds)) return false;
  if (seconds == state.pollSec) {
    state.dirty = true;
    redrawFromCache();
    return true;
  }
  state.pollSec = seconds;
  saveSettings();
  state.dirty = true;
  redrawFromCache();
  return true;
}
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
