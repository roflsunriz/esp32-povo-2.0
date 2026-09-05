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
}
