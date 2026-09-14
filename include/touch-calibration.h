#pragma once
#include <stdint.h>
#include <stdlib.h>

namespace povo { namespace touch {

constexpr int16_t kDefaultPressure = 120;
constexpr int16_t kCapturePressure = 12;
constexpr int16_t kTargetLeft = 24, kTargetRight = 295;
constexpr int16_t kTargetTop = 24, kTargetBottom = 215;

struct Calibration {
  int16_t left = 200, right = 3700, top = 240, bottom = 3800;
  int16_t pressure = kDefaultPressure;
};

struct Sample { int16_t x = 0, y = 0; };
class SampleFilter {
 public:
  void reset() { sumX_ = sumY_ = 0; count_ = 0; delivered_ = false; }
  bool current(Sample& out) const {
    if (!delivered_) return false;
    out = stable_;
    return true;
  }
  bool push(Sample value, Sample& out) {
    if (delivered_) return current(out);
    if (count_ && (abs(value.x - sumX_ / count_) > 18 ||
                   abs(value.y - sumY_ / count_) > 18)) reset();
    sumX_ += value.x;
    sumY_ += value.y;
    if (++count_ < 3) return false;
    stable_ = {static_cast<int16_t>(sumX_ / count_),
               static_cast<int16_t>(sumY_ / count_)};
    delivered_ = true;
    out = stable_;
    return true;
  }
 private:
  int32_t sumX_ = 0, sumY_ = 0;
  uint8_t count_ = 0;
  bool delivered_ = false;
  Sample stable_;
};

struct StoredCalibration {
  uint32_t version;
  int16_t values[5];
  uint16_t check;
};
static_assert(sizeof(StoredCalibration) == 16, "touch calibration record size");

inline bool valid(const Calibration& value) {
  return abs(value.right - value.left) > 1000 &&
         abs(value.bottom - value.top) > 1000 &&
         value.pressure >= kCapturePressure && value.pressure <= 120;
}

inline uint16_t check(const StoredCalibration& value) {
  uint16_t result = 0xA53C;
  for (const int16_t coordinate : value.values)
    result ^= static_cast<uint16_t>(coordinate);
  return result;
}

inline StoredCalibration encode(const Calibration& value) {
  StoredCalibration stored{1,
      {value.left, value.right, value.top, value.bottom, value.pressure}, 0};
  stored.check = check(stored);
  return stored;
}

inline bool decode(const StoredCalibration& stored, Calibration& result) {
  if (stored.version != 1 || stored.check != check(stored)) return false;
  const Calibration candidate{stored.values[0], stored.values[1],
      stored.values[2], stored.values[3], stored.values[4]};
  if (!valid(candidate)) return false;
  result = candidate;
  return true;
}

inline int16_t thresholdFor(int16_t weakestPressure) {
  // 校正時の最も弱い有効な押下の半分。誤検知防止の下限を残す。
  int16_t value = weakestPressure / 2;
  if (value < kCapturePressure) value = kCapturePressure;
  if (value > kDefaultPressure) value = kDefaultPressure;
  return value;
}

inline int16_t mapAxis(int16_t raw, int16_t start, int16_t end,
                       int16_t targetStart, int16_t targetEnd, int16_t maximum) {
  const long denominator = static_cast<long>(end) - start;
  if (denominator > -100 && denominator < 100) return targetStart;
  long value = targetStart + (static_cast<long>(raw) - start) *
                                 (targetEnd - targetStart) / denominator;
  if (value < 0) value = 0;
  if (value > maximum) value = maximum;
  return static_cast<int16_t>(value);
}

} }
