#pragma once
// 画面タブ・自動消灯・反転の純粋ロジック。Arduino依存なしでホストテストする。
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "ui-text.h"

namespace povo {
namespace display {

enum class Page : uint8_t { Status = 0, Sleep = 1 };

// 自動消灯スライダー: 0〜59分＋0〜24時間。合計は 時*3600＋分*60（秒）で、
// 0分0時間は自動消灯オフ（常時点灯）と同じ。取得間隔スライダーは
// 60〜600秒の60秒刻み。旧固定値の15秒・30秒は保存互換のため有効のまま。
constexpr uint32_t kSleepMinutesMax = 59;
constexpr uint32_t kSleepHoursMax = 24;
constexpr uint32_t kSleepTimeoutMaxSec = 24 * 3600 + 59 * 60;  // 89940
constexpr uint32_t kPollSliderMinSec = 60;
constexpr uint32_t kPollSliderMaxSec = 600;
constexpr uint32_t kPollSliderStepSec = 60;

inline uint32_t sleepMinutesPart(uint32_t timeoutSec) {
  return (timeoutSec % 3600) / 60;
}

inline uint32_t sleepHoursPart(uint32_t timeoutSec) {
  return timeoutSec / 3600;
}

inline uint32_t sleepTimeoutFromParts(uint32_t minutes, uint32_t hours) {
  if (minutes > kSleepMinutesMax) minutes = kSleepMinutesMax;
  if (hours > kSleepHoursMax) hours = kSleepHoursMax;
  return hours * 3600 + minutes * 60;
}

inline bool isValidSleepTimeout(uint32_t seconds) {
  if (seconds == 15 || seconds == 30) return true;
  return seconds <= kSleepTimeoutMaxSec && seconds % 60 == 0;
}

inline bool isValidPollSlider(uint32_t seconds) {
  return seconds >= kPollSliderMinSec && seconds <= kPollSliderMaxSec &&
         seconds % kPollSliderStepSec == 0;
}

constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr int kTabH = 24;
constexpr int kTabY = 0;
constexpr int kContentTop = kTabY + kTabH;
constexpr int kContentBottom = kScreenH;

// 状態ページの残り時間バー。残り割合に応じて左から縮む減るタイプ。
constexpr int kBarX = 8;
constexpr int kBarY = 98;
constexpr int kBarW = 304;
constexpr int kBarH = 10;

// 1000分率の残り割合からバーの塗り幅（ピクセル）を求める。
inline int barFillWidth(int totalWidth, uint64_t permille) {
  if (totalWidth <= 0) return 0;
  if (permille >= 1000) return totalWidth;
  return static_cast<int>(static_cast<uint64_t>(totalWidth) * permille / 1000);
}

// 設定タブのスライダー配置。タッチ面は x 24..295・y 24..215 のため、
// 操作子はその範囲へ収める。内容は表示域に収まるためスクロールは不要。
constexpr int kSliderX0 = 24;
constexpr int kSliderX1 = 275;
constexpr int kSliderMinutesY = 102;
constexpr int kSliderHoursY = 150;
constexpr int kSliderPollY = 198;
constexpr int kSliderHalfH = 14;

constexpr uint8_t kRotationNormal = 1;
constexpr uint8_t kRotationInverted = 3;

constexpr uint32_t kBootDebounceMs = 30;
constexpr uint32_t kBootPressMinMs = 50;
constexpr uint32_t kBootCalibrationHoldMs = 1500;
enum class BootAction : uint8_t { None, Rotate, Calibrate };

struct Point {
  int x = 0;
  int y = 0;
};

struct BootFilter {
  bool stableHigh = true;
  bool rawHigh = true;
  uint64_t changedAt = 0;
  uint64_t pressedAt = 0;
  bool armed = true;
};

inline int sliderXFromValue(uint32_t value, uint32_t minV, uint32_t maxV) {
  if (maxV <= minV) return kSliderX0;
  if (value < minV) value = minV;
  if (value > maxV) value = maxV;
  const uint32_t trackW = static_cast<uint32_t>(kSliderX1 - kSliderX0);
  const uint32_t range = maxV - minV;
  return kSliderX0 +
         static_cast<int>((static_cast<uint64_t>(value - minV) * trackW +
                           range / 2) /
                          range);
}

// タップ位置からスライダー値を求める。端は丸めて段階値へ寄せる。
inline uint32_t sliderValueFromX(int x, uint32_t minV, uint32_t maxV,
                                 uint32_t step) {
  if (maxV <= minV || step == 0) return minV;
  if (x < kSliderX0) x = kSliderX0;
  if (x > kSliderX1) x = kSliderX1;
  const uint32_t trackW = static_cast<uint32_t>(kSliderX1 - kSliderX0);
  const uint32_t offset = static_cast<uint32_t>(x - kSliderX0);
  const uint32_t range = maxV - minV;
  const uint32_t steps = range / step;
  uint32_t index = (offset * steps + trackW / 2) / trackW;
  if (index > steps) index = steps;
  return minV + index * step;
}

inline bool tabForTouch(int x, int y, Page& out) {
  if (x < 0 || x >= kScreenW || y < kTabY || y >= kTabY + kTabH) return false;
  out = x < kScreenW / 2 ? Page::Status : Page::Sleep;
  return true;
}

inline bool shouldSleep(uint32_t timeoutSec, uint64_t idleMs) {
  if (timeoutSec == 0) return false;
  return idleMs >= static_cast<uint64_t>(timeoutSec) * 1000ULL;
}

inline uint8_t toggledRotation(uint8_t current) {
  return current == kRotationNormal ? kRotationInverted : kRotationNormal;
}

inline Point orientPoint(Point point, bool inverted) {
  if (!inverted) return point;
  return {kScreenW - 1 - point.x, kScreenH - 1 - point.y};
}

inline void bootInit(BootFilter& filter, bool rawHigh, uint64_t nowMs) {
  filter.rawHigh = rawHigh;
  filter.stableHigh = rawHigh;
  filter.changedAt = nowMs;
  filter.pressedAt = 0;
  filter.armed = rawHigh;
}

// 離した瞬間に1回押し成立でtrue。チャタリング30ms、50ms未満の短絡は無視。
inline BootAction bootUpdate(BootFilter& filter, bool rawHigh, uint64_t nowMs) {
  if (rawHigh != filter.rawHigh) {
    filter.rawHigh = rawHigh;
    filter.changedAt = nowMs;
  }
  if (rawHigh == filter.stableHigh || nowMs - filter.changedAt < kBootDebounceMs)
    return BootAction::None;
  filter.stableHigh = rawHigh;
  if (!filter.stableHigh) {
    if (filter.armed) filter.pressedAt = nowMs;
    return BootAction::None;
  }
  const uint64_t duration = nowMs - filter.pressedAt;
  const bool pressed = filter.armed && filter.pressedAt != 0 &&
                       duration >= kBootPressMinMs;
  filter.pressedAt = 0;
  filter.armed = true;
  if (!pressed) return BootAction::None;
  return duration >= kBootCalibrationHoldMs ? BootAction::Calibrate : BootAction::Rotate;
}

}  // namespace display
}  // namespace povo
