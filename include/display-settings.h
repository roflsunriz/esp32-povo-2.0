#pragma once
// 画面タブ・自動消灯・反転の純粋ロジック。Arduino依存なしでホストテストする。
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "ui-text.h"

namespace povo {
namespace display {

enum class Page : uint8_t { Status = 0, Sleep = 1 };

// なし、15秒、30秒、1分、2分、5分、10分、30分、1時間、2時間〜24時間（1時間刻み）。
constexpr uint32_t kSleepTimeoutOptions[] = {
    0,    15,   30,   60,   120,  300,  600,  1800, 3600, 7200,
    10800, 14400, 18000, 21600, 25200, 28800, 32400, 36000, 39600, 43200,
    46800, 50400, 54000, 57600, 61200, 64800, 68400, 72000, 75600, 79200,
    82800, 86400,
};
constexpr size_t kSleepTimeoutCount =
    sizeof(kSleepTimeoutOptions) / sizeof(kSleepTimeoutOptions[0]);

constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr int kTabH = 24;
constexpr int kTabY = kScreenH - kTabH;
constexpr int kContentBottom = kTabY;

// 設定グリッド: 3列×4行=12件/ページ。32件で3ページ。
constexpr int kSleepCols = 3;
constexpr int kSleepRows = 4;
constexpr size_t kSleepPerPage =
    static_cast<size_t>(kSleepCols * kSleepRows);
constexpr int kGridTop = 66;
constexpr int kGridRowH = 30;
constexpr int kGridCellH = 24;
constexpr int kGridColX[kSleepCols] = {4, 110, 216};
constexpr int kGridCellW = 100;
constexpr int kNavY = 188;
constexpr int kNavH = 20;

constexpr uint8_t kRotationNormal = 1;
constexpr uint8_t kRotationInverted = 3;

constexpr uint32_t kBootDebounceMs = 30;
constexpr uint32_t kBootPressMinMs = 50;

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

inline size_t indexForTimeout(uint32_t seconds) {
  for (size_t i = 0; i < kSleepTimeoutCount; ++i)
    if (kSleepTimeoutOptions[i] == seconds) return i;
  return 0;
}

inline uint32_t timeoutForIndex(size_t index) {
  if (index >= kSleepTimeoutCount) index = kSleepTimeoutCount - 1;
  return kSleepTimeoutOptions[index];
}

inline size_t sleepPageCount() {
  return (kSleepTimeoutCount + kSleepPerPage - 1) / kSleepPerPage;
}

inline size_t sleepPageForIndex(size_t index) { return index / kSleepPerPage; }

// 日本語表記。「なし」、15秒、30秒、1分、2分、5分、10分、30分、1時間〜24時間。
// 単位の正本はui-text.h。字形生成も同ファイル基準。
inline bool formatTimeout(uint32_t seconds, char* out, size_t size) {
  if (!out || !size) return false;
  if (seconds == 0) {
    snprintf(out, size, "%s", povo::text::sleepNone);
    return true;
  }
  if (seconds < 60) {
    snprintf(out, size, "%u%s", static_cast<unsigned>(seconds), povo::text::sleepSecUnit);
    return true;
  }
  if (seconds < 3600) {
    snprintf(out, size, "%u%s", static_cast<unsigned>(seconds / 60),
             povo::text::sleepMinUnit);
    return true;
  }
  snprintf(out, size, "%u%s", static_cast<unsigned>(seconds / 3600),
           povo::text::sleepHourUnit);
  return true;
}

inline bool tabForTouch(int x, int y, Page& out) {
  if (x < 0 || x >= kScreenW || y < kTabY || y >= kScreenH) return false;
  out = x < kScreenW / 2 ? Page::Status : Page::Sleep;
  return true;
}

inline bool sleepCellForTouch(int x, int y, size_t page, size_t& outIndex) {
  if (page >= sleepPageCount()) return false;
  for (int row = 0; row < kSleepRows; ++row) {
    const int top = kGridTop + row * kGridRowH;
    if (y < top || y >= top + kGridCellH) continue;
    for (int col = 0; col < kSleepCols; ++col) {
      const int left = kGridColX[col];
      if (x < left || x >= left + kGridCellW) continue;
      const size_t index = page * kSleepPerPage +
                           static_cast<size_t>(row * kSleepCols + col);
      if (index >= kSleepTimeoutCount) return false;
      outIndex = index;
      return true;
    }
  }
  return false;
}

// 設定画面の前へ/次へボタン。left=trueで前へ領域、falseで次へ領域。
inline bool sleepNavForTouch(int x, int y, bool& outPrev) {
  if (x < 0 || x >= kScreenW || y < kNavY || y >= kNavY + kNavH) return false;
  outPrev = x < kScreenW / 2;
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
inline bool bootUpdate(BootFilter& filter, bool rawHigh, uint64_t nowMs) {
  if (rawHigh != filter.rawHigh) {
    filter.rawHigh = rawHigh;
    filter.changedAt = nowMs;
  }
  if (rawHigh == filter.stableHigh || nowMs - filter.changedAt < kBootDebounceMs)
    return false;
  filter.stableHigh = rawHigh;
  if (!filter.stableHigh) {
    if (filter.armed) filter.pressedAt = nowMs;
    return false;
  }
  const bool pressed = filter.armed && filter.pressedAt != 0 &&
                       nowMs - filter.pressedAt >= kBootPressMinMs;
  filter.pressedAt = 0;
  filter.armed = true;
  return pressed;
}

}  // namespace display
}  // namespace povo
