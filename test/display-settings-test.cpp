#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "../include/display-settings.h"
#include "../include/touch-calibration.h"
#include "../include/display-diff.h"

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace
int main() {
  using namespace povo::display;
  try {
    check(kSleepTimeoutMaxSec == 89940, "slider range max");
    check(sleepTimeoutFromParts(0, 0) == 0, "always on");
    check(sleepTimeoutFromParts(1, 0) == 60, "one minute");
    check(sleepTimeoutFromParts(0, 1) == 3600, "one hour");
    check(sleepTimeoutFromParts(59, 24) == 89940, "full range");
    check(sleepTimeoutFromParts(99, 99) == 89940, "parts clamp");
    check(sleepMinutesPart(7800) == 10, "minutes part");
    check(sleepHoursPart(7800) == 2, "hours part");
    check(sleepMinutesPart(89940) == 59 && sleepHoursPart(89940) == 24, "max parts");
    check(isValidSleepTimeout(0), "always on valid");
    check(isValidSleepTimeout(15) && isValidSleepTimeout(30), "legacy seconds kept");
    check(isValidSleepTimeout(60) && isValidSleepTimeout(89940), "minute steps valid");
    check(!isValidSleepTimeout(45) && !isValidSleepTimeout(89941), "off-step rejected");
    check(isValidPollSlider(60) && isValidPollSlider(600), "poll ends valid");
    check(!isValidPollSlider(59) && !isValidPollSlider(61) && !isValidPollSlider(601),
          "poll off-step rejected");
    check(sliderValueFromX(kSliderX0, 0, 59, 1) == 0, "track left end");
    check(sliderValueFromX(kSliderX1, 0, 59, 1) == 59, "track right end");
    check(sliderValueFromX(kSliderX0, 60, 600, 60) == 60, "poll left end");
    check(sliderValueFromX(kSliderX1, 60, 600, 60) == 600, "poll right end");
    check(sliderValueFromX(-100, 0, 59, 1) == 0, "track clamps left");
    check(sliderValueFromX(999, 60, 600, 60) == 600, "track clamps right");
    check(sliderXFromValue(0, 0, 59) == kSliderX0, "thumb left end");
    check(sliderXFromValue(59, 0, 59) == kSliderX1, "thumb right end");
    Page page = Page::Status;
    check(tabForTouch(0, 10, page) && page == Page::Status, "left tab");
    check(tabForTouch(319, 10, page) && page == Page::Sleep, "right tab");
    check(!tabForTouch(160, 30, page), "below tabs");
    check(!tabForTouch(-1, 10, page), "outside left");
    check(shouldSleep(15, 15000), "sleep at timeout");
    check(!shouldSleep(15, 14999), "awake before timeout");
    check(!shouldSleep(0, 86400000ULL), "none never sleeps");
    check(shouldSleep(86400, 86400000ULL), "24 hours sleeps");
    check(toggledRotation(kRotationNormal) == kRotationInverted, "toggle to inverted");
    check(toggledRotation(kRotationInverted) == kRotationNormal, "toggle to normal");
    check(kBarX == 8 && kBarY == 98 && kBarW == 304 && kBarH == 10, "remaining bar geometry");
    check(barFillWidth(304, 1000) == 304, "full bar");
    check(barFillWidth(304, 500) == 152, "half bar shrinks");
    check(barFillWidth(304, 0) == 0, "expired bar empty");
    check(barFillWidth(304, 1500) == 304, "bar clamps at full");
    check(barFillWidth(0, 500) == 0, "zero width bar");
    check(orientPoint({0, 0}, false).x == 0, "orient normal");
    const Point flipped = orientPoint({0, 0}, true);
    check(flipped.x == 319 && flipped.y == 239, "orient inverted");
    const Point roundTrip = orientPoint(orientPoint({123, 45}, true), true);
    check(roundTrip.x == 123 && roundTrip.y == 45, "orient round trip");
    BootFilter boot;
    bootInit(boot, true, 0);
    check(bootUpdate(boot, false, 10) == BootAction::None, "debounce press");
    check(bootUpdate(boot, false, 45) == BootAction::None, "stable press no release");
    check(bootUpdate(boot, true, 120) == BootAction::None, "debounce release");
    check(bootUpdate(boot, true, 155) == BootAction::Rotate, "single press flips");
    check(bootUpdate(boot, true, 200) == BootAction::None, "no repeat without press");
    bootInit(boot, true, 1000);
    check(bootUpdate(boot, false, 1005) == BootAction::None, "short press change");
    check(bootUpdate(boot, false, 1040) == BootAction::None, "short press stable");
    check(bootUpdate(boot, true, 1045) == BootAction::None, "short release change");
    check(bootUpdate(boot, true, 1080) == BootAction::None, "short release ignored");
    bootInit(boot, true, 2000);
    check(bootUpdate(boot, false, 2010) == BootAction::None, "calibration press bounce");
    check(bootUpdate(boot, false, 2050) == BootAction::None, "calibration press stable");
    check(bootUpdate(boot, true, 3590) == BootAction::None, "calibration release bounce");
    check(bootUpdate(boot, true, 3630) == BootAction::Calibrate, "long press calibrates");
    check(bootUpdate(boot, true, 3660) == BootAction::None, "calibration once");
    PressConfirm press;
    check(!pressConfirmUpdate(press, false, 1000), "idle has no contact");
    check(!pressConfirmUpdate(press, true, 1000), "contact edge unconfirmed");
    check(!pressConfirmUpdate(press, true, 1029), "noise-length contact ignored");
    check(pressConfirmUpdate(press, true, 1030), "sustained contact confirmed");
    check(pressConfirmUpdate(press, true, 5000), "held contact stays confirmed");
    check(!pressConfirmUpdate(press, false, 5010), "release resets confirmation");
    check(!pressConfirmUpdate(press, true, 5020), "repress restarts confirmation");
    check(!pressConfirmUpdate(press, true, 5049), "repress needs full duration");
    check(pressConfirmUpdate(press, true, 5050), "repress confirmed again");
    check(!pressConfirmUpdate(press, false, 5060), "second release resets");
    check(!pressConfirmUpdate(press, true, 5065), "short blip never confirms");
    check(!pressConfirmUpdate(press, false, 5070), "short blip release stays quiet");
    povo::touch::Calibration touch;
    check(povo::touch::valid(touch), "default touch calibration");
    check(povo::touch::thresholdFor(30) == 15, "light stylus threshold");
    check(povo::touch::thresholdFor(10) == 12, "minimum threshold");
    check(povo::touch::thresholdFor(600) == 120, "maximum threshold");
    check(povo::touch::mapAxis(200, 200, 3700, 24, 295, 319) == 24,
          "touch left anchor");
    check(povo::touch::mapAxis(3700, 200, 3700, 24, 295, 319) == 295,
          "touch right anchor");
    check(povo::touch::mapAxis(3700, 3700, 200, 24, 295, 319) == 24,
          "reversed touch axis");
    touch.right = touch.left + 100;
    check(!povo::touch::valid(touch), "reject collapsed touch axis");
    touch = {};
    const auto stored = povo::touch::encode(touch);
    povo::touch::Calibration restored;
    check(povo::touch::decode(stored, restored), "calibration round trip");
    check(restored.left == touch.left && restored.pressure == touch.pressure,
          "calibration values retained");
    auto damaged = stored;
    damaged.values[4] = 0;
    check(!povo::touch::decode(damaged, restored), "reject damaged calibration");
    damaged = stored;
    damaged.version = 2;
    check(!povo::touch::decode(damaged, restored), "reject unknown calibration version");
    povo::touch::SampleFilter filter;
    povo::touch::Sample stable;
    check(!filter.push({100, 100}, stable), "first touch sample pending");
    check(!filter.push({102, 101}, stable), "second touch sample pending");
    check(filter.push({101, 100}, stable) && stable.x == 101,
          "three nearby samples produce contact");
    check(filter.current(stable), "contact held after first delivery");
    filter.reset();
    check(!filter.current(stable), "release clears contact");
    check(!filter.push({10, 10}, stable), "noise first sample");
    check(!filter.push({200, 200}, stable), "jump restarts filter");
    check(!filter.push({202, 200}, stable), "second post-jump sample");
    check(filter.push({201, 201}, stable) && stable.x == 201,
          "stable contact after noise");
    std::array<uint8_t, display_diff::kWidth * display_diff::kHeight> frame{};
    display_diff::Bands diff;
    check(diff.update(frame.data()) == 0x7FFF, "first frame updates all bands");
    check(diff.update(frame.data()) == 0, "identical frame skips transfer");
    frame[17 * display_diff::kWidth + 5] = 1;
    check(diff.update(frame.data()) == (1U << 1), "changed second band only");
    frame[239 * display_diff::kWidth + 319] = 2;
    check(diff.update(frame.data()) == (1U << 14), "changed last band only");
    diff.invalidate();
    check(diff.update(frame.data()) == 0x7FFF, "rotation repaints full frame");
    size_t runs = 0;
    check(display_diff::eachRun(static_cast<uint16_t>((1U << 1) | (1U << 2) |
                                                    (1U << 4)),
        [&](size_t top, size_t height) {
          if (runs == 0) check(top == 16 && height == 32, "merge adjacent bands");
          if (runs == 1) check(top == 64 && height == 16, "separate distant band");
          ++runs; return true;
        }) && runs == 2, "changed runs transferred once");
    std::cout << "display settings timeouts, tabs, power, rotation and boot passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
