#pragma once
#include <stdint.h>

namespace povo {
constexpr uint64_t kStaleMs = 900000;
constexpr uint64_t kMaxTimestamp = 253402300799999ULL;
enum class ExpirySource { Unknown, Server };
struct Status {
  uint64_t expiryAtMs = 0;
  uint64_t receivedAtMs = 0, serverTimeMs = 0;
  ExpirySource expirySource = ExpirySource::Unknown;
};
struct View {
  uint64_t nowMs, remainingSeconds, syncAgeMs;
  bool remainingKnown, stale, confirmationPending;
};
inline View derive(const Status& s, uint64_t elapsedMs) {
  const uint64_t now = elapsedMs > kMaxTimestamp - s.serverTimeMs
      ? kMaxTimestamp : s.serverTimeMs + elapsedMs;
  const uint64_t age = now > s.receivedAtMs ? now - s.receivedAtMs : 0;
  return {now, s.expiryAtMs > now ? (s.expiryAtMs - now) / 1000 : 0,
          age, s.expiryAtMs != 0, age >= kStaleMs || elapsedMs >= kStaleMs,
          s.expiryAtMs != 0 && s.expiryAtMs <= now};
}
}  // namespace povo
