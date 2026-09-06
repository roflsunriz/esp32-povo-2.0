#include <cstring>
#include <iostream>
#include <stdexcept>
#include "../include/display-settings.h"

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void checkFormat(uint32_t seconds, const char* expected) {
  char buffer[32];
  check(povo::display::formatTimeout(seconds, buffer, sizeof(buffer)), "format failed");
  check(std::strcmp(buffer, expected) == 0, buffer);
}
}  // namespace
int main() {
  using namespace povo::display;
  try {
    check(kSleepTimeoutCount == 32, "timeout option count");
    check(kSleepTimeoutOptions[0] == 0, "first option none");
    check(kSleepTimeoutOptions[8] == 3600, "1 hour option");
    check(kSleepTimeoutOptions[kSleepTimeoutCount - 1] == 86400, "last option 24 hours");
    for (size_t i = 9; i < kSleepTimeoutCount; ++i)
      check(kSleepTimeoutOptions[i] - kSleepTimeoutOptions[i - 1] == 3600, "hourly steps");
    check(indexForTimeout(0) == 0, "index none");
    check(indexForTimeout(15) == 1, "index 15s");
    check(indexForTimeout(1800) == 7, "index 30m");
    check(indexForTimeout(86400) == kSleepTimeoutCount - 1, "index 24h");
    check(timeoutForIndex(0) == 0, "timeout none");
    check(timeoutForIndex(100000) == 86400, "timeout clamp");
    checkFormat(0, "なし");
    checkFormat(15, "15秒");
    checkFormat(30, "30秒");
    checkFormat(60, "1分");
    checkFormat(120, "2分");
    checkFormat(300, "5分");
    checkFormat(600, "10分");
    checkFormat(1800, "30分");
    checkFormat(3600, "1時間");
    checkFormat(7200, "2時間");
    checkFormat(86400, "24時間");
    check(sleepPageCount() == 3, "page count");
    check(sleepPageForIndex(0) == 0, "page 0");
    check(sleepPageForIndex(12) == 1, "page 1");
    check(sleepPageForIndex(31) == 2, "page 2");
    Page page = Page::Status;
    check(tabForTouch(0, 216, page) && page == Page::Status, "left tab");
    check(tabForTouch(319, 239, page) && page == Page::Sleep, "right tab");
    check(!tabForTouch(160, 215, page), "above tabs");
    check(!tabForTouch(-1, 220, page), "outside left");
    size_t index = 0;
    check(sleepCellForTouch(4, 66, 0, index) && index == 0, "first cell");
    check(sleepCellForTouch(216 + 99, 66 + 3 * 30 + 23, 0, index) && index == 11,
          "last cell page 0");
    check(!sleepCellForTouch(4, 66 + 4 * 30, 0, index), "below grid");
    check(sleepCellForTouch(4, 66, 2, index) && index == 24, "first cell page 2");
    check(sleepCellForTouch(110, 66, 2, index) && index == 25, "second cell page 2");
    check(!sleepCellForTouch(216 + 50, 66 + 2 * 30, 2, index), "empty cell page 2");
    check(!sleepCellForTouch(4, 66, 9, index), "invalid page");
    bool prev = false;
    check(sleepNavForTouch(0, 188, prev) && prev, "prev button");
    check(sleepNavForTouch(319, 207, prev) && !prev, "next button");
    check(!sleepNavForTouch(160, 187, prev), "above nav");
    check(shouldSleep(15, 15000), "sleep at timeout");
    check(!shouldSleep(15, 14999), "awake before timeout");
    check(!shouldSleep(0, 86400000ULL), "none never sleeps");
    check(shouldSleep(86400, 86400000ULL), "24 hours sleeps");
    check(toggledRotation(kRotationNormal) == kRotationInverted, "toggle to inverted");
    check(toggledRotation(kRotationInverted) == kRotationNormal, "toggle to normal");
    check(orientPoint({0, 0}, false).x == 0, "orient normal");
    const Point flipped = orientPoint({0, 0}, true);
    check(flipped.x == 319 && flipped.y == 239, "orient inverted");
    const Point roundTrip = orientPoint(orientPoint({123, 45}, true), true);
    check(roundTrip.x == 123 && roundTrip.y == 45, "orient round trip");
    BootFilter boot;
    bootInit(boot, true, 0);
    check(!bootUpdate(boot, false, 10), "debounce press");
    check(!bootUpdate(boot, false, 45), "stable press no release");
    check(!bootUpdate(boot, true, 120), "debounce release");
    check(bootUpdate(boot, true, 155), "single press flips");
    check(!bootUpdate(boot, true, 200), "no repeat without press");
    bootInit(boot, true, 1000);
    check(!bootUpdate(boot, false, 1005), "short press change");
    check(!bootUpdate(boot, false, 1040), "short press stable");
    check(!bootUpdate(boot, true, 1045), "short release change");
    check(!bootUpdate(boot, true, 1080), "short release ignored");
    std::cout << "display settings timeouts, tabs, power, rotation and boot passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
