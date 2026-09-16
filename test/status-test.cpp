// Assertions are the test checks and must also run in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "status-model.h"
int main() {
  povo::Status s;
  s.serverTimeMs = s.receivedAtMs = 1700000000000;
  s.expiryAtMs = 1700310320000;
  s.expirySource = povo::ExpirySource::Server;
  auto v = povo::derive(s, 60000);
  assert(v.remainingKnown && v.remainingSeconds == 310260 && !v.stale);
  assert(!povo::derive(s, 899999).stale && povo::derive(s, 900000).stale);
  assert(povo::derive(s, 310320000).confirmationPending);
  assert(povo::derive(s, 310320001).remainingSeconds == 0);
  assert(povo::derive(s, UINT64_MAX).nowMs == povo::kMaxTimestamp);
  s.expiryAtMs = 0;
  assert(!povo::derive(s, 0).remainingKnown);
  assert(!povo::derive(s, 0).confirmationPending);
  // 期限間近・期限切れ後は短間隔、リニュー後は通常間隔へ戻る。
  const uint64_t now = 1700000000000;
  assert(!povo::isCritical(0, now));
  assert(povo::pollIntervalMs(0, now) == povo::kNormalPollMs);
  assert(!povo::isCritical(now + 3600000, now));
  assert(povo::pollIntervalMs(now + 3600000, now) == povo::kNormalPollMs);
  assert(povo::isCritical(now + 1800000, now));
  assert(povo::isCritical(now + 60000, now));
  assert(povo::isCritical(now, now));
  assert(povo::isCritical(now - 1, now));
  assert(povo::pollIntervalMs(now + 1800000, now) == povo::kCriticalPollMs);
  assert(povo::pollIntervalMs(now - 1, now) == povo::kCriticalPollMs);
  assert(!povo::isRenewed(0, now + 1000));
  assert(!povo::isRenewed(now, now));
  assert(povo::isRenewed(now, now + 60000));
  // バー分母は同じ期限で維持し、初回・変更時は取り直す。
  assert(povo::updateSpan(0, 0, 0, now) == 0);
  assert(povo::updateSpan(0, 0, now + 3600000, now) == 3600000);
  assert(povo::updateSpan(3600000, now + 3600000, now + 3600000, now + 60000) == 3600000);
  assert(povo::updateSpan(0, now + 3600000, now + 3600000, now + 60000) == 3540000);
  assert(povo::updateSpan(3600000, now + 3600000, now + 7200000, now + 60000) == 7140000);
  assert(povo::updateSpan(3600000, now + 3600000, 0, now + 60000) == 3600000);
  assert(povo::updateSpan(3600000, now + 3600000, now - 1, now) == 3600000);
  // バーは減る方向に1000分率で縮む。
  assert(povo::progressPermille(0, now, 3600000) == 0);
  assert(povo::progressPermille(now + 1000, now, 0) == 0);
  assert(povo::progressPermille(now, now, 3600000) == 0);
  assert(povo::progressPermille(now - 1, now, 3600000) == 0);
  assert(povo::progressPermille(now + 3600000, now, 3600000) == 1000);
  assert(povo::progressPermille(now + 1800000, now, 3600000) == 500);
  assert(povo::progressPermille(now + 7200000, now, 3600000) == 1000);
}
