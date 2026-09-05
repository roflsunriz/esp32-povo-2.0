#pragma once
#include <stdint.h>

namespace povo {
constexpr uint64_t kStaleMs = 900000;
constexpr uint64_t kMaxTimestamp = 253402300799999ULL;
enum class ExpirySource { Unknown, Manual, Estimated, Server };
enum class RenewalState { Unknown, Idle, Applying, Retrying, AuthRequired, NeedsReview, Completed, CodeExpired };
struct Status {
  uint64_t observedAtMs = 0, expiryAtMs = 0, expiryObservedAtMs = 0;
  uint64_t codeDeadlineAtMs = 0, lastAppliedAtMs = 0;
  uint64_t receivedAtMs = 0, serverTimeMs = 0;
  uint32_t appliedUses = 0, maxUses = 0;
  bool automaticRenewal = false, stale = false, confirmationPending = false;
  ExpirySource expirySource = ExpirySource::Unknown;
  RenewalState renewalState = RenewalState::Unknown;
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
          age, s.expiryAtMs != 0, s.stale || age >= kStaleMs || elapsedMs >= kStaleMs,
          s.confirmationPending || s.expirySource == ExpirySource::Estimated ||
              (s.expiryAtMs != 0 && s.expiryAtMs <= now)};
}
}  // namespace povo
