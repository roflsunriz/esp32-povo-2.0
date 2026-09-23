#pragma once
#include <stdint.h>

namespace povo {
constexpr uint64_t kStaleMs = 900000;
constexpr uint64_t kMaxTimestamp = 253402300799999ULL;
// 通常の期限取得間隔はNVSの取得間隔設定（秒）に従い、期限間近・期限切れ
// 後は短い取得間隔（1分）で追う。旧記録に設定がない場合は5分を使う。
constexpr uint64_t kDefaultPollSec = 300;
constexpr uint64_t kCriticalPollMs = 60000;
// 期限間近とみなす残り時間。使い放題の最終盤と切替直後を短間隔で追う。
constexpr uint64_t kNearExpiryMs = 1800000;
enum class ExpirySource { Unknown, Server };
struct Status {
  uint64_t expiryAtMs = 0;
  uint64_t receivedAtMs = 0, serverTimeMs = 0;
  ExpirySource expirySource = ExpirySource::Unknown;
  // プログレスバーの分母（トッピングの有効期間）。同じ期限の再取得では維持し、
  // 期限が変わった観測・初回観測では expiry-now で取り直す。NVSへ保存する。
  uint64_t spanMs = 0;
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
// 期限切れ後（更新確認待ち）または残り30分以下を重点取得対象とする。
// 期限不明は対象外で通常間隔のままにする。
inline bool isCritical(uint64_t expiryAtMs, uint64_t nowMs) {
  if (expiryAtMs == 0) return false;
  if (expiryAtMs <= nowMs) return true;
  return expiryAtMs - nowMs <= kNearExpiryMs;
}
inline uint64_t pollIntervalMs(uint64_t expiryAtMs, uint64_t nowMs,
                               uint64_t normalMs = kDefaultPollSec * 1000) {
  return isCritical(expiryAtMs, nowMs) ? kCriticalPollMs : normalMs;
}
// カバレッジのリニュー（期限の後ろ倒し）を検出する。通常間隔への復帰条件。
inline bool isRenewed(uint64_t oldExpiryAtMs, uint64_t newExpiryAtMs) {
  return oldExpiryAtMs != 0 && newExpiryAtMs > oldExpiryAtMs;
}
// バー分母の更新。同じ期限なら維持し、初回・変更時は今回の残りで取り直す。
// 期限不明・期限切れの観測では旧分母を保つ（バーは0表示になる）。
inline uint64_t updateSpan(uint64_t oldSpanMs, uint64_t oldExpiryAtMs,
                           uint64_t newExpiryAtMs, uint64_t nowMs) {
  if (newExpiryAtMs == 0 || newExpiryAtMs <= nowMs) return oldSpanMs;
  if (oldExpiryAtMs != 0 && newExpiryAtMs == oldExpiryAtMs) {
    return oldSpanMs == 0 ? newExpiryAtMs - nowMs : oldSpanMs;
  }
  return newExpiryAtMs - nowMs;
}
// 残り割合を1000分率で返す。減るタイプのバー numerator。分母不明・期限切れは0。
inline uint64_t progressPermille(uint64_t expiryAtMs, uint64_t nowMs,
                                 uint64_t spanMs) {
  if (expiryAtMs == 0 || spanMs == 0 || expiryAtMs <= nowMs) return 0;
  const uint64_t remaining = expiryAtMs - nowMs;
  if (remaining >= spanMs) return 1000;
  return remaining * 1000 / spanMs;
}
}  // namespace povo
