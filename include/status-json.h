#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "status-model.h"

namespace povo {
inline bool timestamp(JsonVariantConst v, uint64_t& out, bool nullable) {
  if (nullable && v.isNull()) { out = 0; return true; }
  if (!v.is<uint64_t>()) return false;
  out = v.as<uint64_t>();
  return out > 0 && out <= kMaxTimestamp;
}
inline int enumIndex(JsonVariantConst v, const char* const* values, unsigned count) {
  if (!v.is<const char*>()) return -1;
  for (unsigned i = 0; i < count; ++i) if (!strcmp(v.as<const char*>(), values[i])) return i;
  return -1;
}
inline bool parseStatus(const char* body, size_t length, Status& output) {
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, body, length, DeserializationOption::NestingLimit(2))) return false;
  const JsonObjectConst o = doc.as<JsonObjectConst>();
  const char* keys[] = {"schema_version", "observed_at_ms", "expiry_at_ms", "expiry_source", "expiry_observed_at_ms", "code_deadline_at_ms", "automatic_renewal", "applied_uses", "max_uses", "renewal_state", "last_applied_at_ms", "received_at_ms", "server_time_ms", "stale", "remaining_seconds", "renewal_confirmation_pending"};
  if (o.isNull() || o.size() != 16) return false;
  for (const char* key : keys) if (!o.containsKey(key)) return false;
  if (!o["schema_version"].is<int>() || o["schema_version"].as<int>() != 1) return false;
  Status s;
  if (!timestamp(o["observed_at_ms"], s.observedAtMs, false) ||
      !timestamp(o["expiry_at_ms"], s.expiryAtMs, true) ||
      !timestamp(o["expiry_observed_at_ms"], s.expiryObservedAtMs, true) ||
      !timestamp(o["code_deadline_at_ms"], s.codeDeadlineAtMs, true) ||
      !timestamp(o["last_applied_at_ms"], s.lastAppliedAtMs, true) ||
      !timestamp(o["received_at_ms"], s.receivedAtMs, false) ||
      !timestamp(o["server_time_ms"], s.serverTimeMs, false)) return false;
  if (!o["automatic_renewal"].is<bool>() || !o["stale"].is<bool>() ||
      !o["renewal_confirmation_pending"].is<bool>() ||
      !o["applied_uses"].is<uint32_t>() || !o["max_uses"].is<uint32_t>()) return false;
  s.appliedUses = o["applied_uses"]; s.maxUses = o["max_uses"];
  if (s.appliedUses > s.maxUses || s.maxUses > 2147483647U) return false;
  const char* sources[] = {"unknown", "manual", "estimated", "server"};
  const char* states[] = {"unknown", "idle", "applying", "retrying", "auth_required", "needs_review", "completed", "code_expired"};
  int source = enumIndex(o["expiry_source"], sources, 4), state = enumIndex(o["renewal_state"], states, 8);
  if (source < 0 || state < 0) return false;
  s.expirySource = static_cast<ExpirySource>(source); s.renewalState = static_cast<RenewalState>(state);
  s.automaticRenewal = o["automatic_renewal"]; s.stale = o["stale"];
  s.confirmationPending = o["renewal_confirmation_pending"];
  if (s.expiryAtMs == 0) { if (!o["remaining_seconds"].isNull()) return false; }
  else if (!o["remaining_seconds"].is<uint64_t>() || o["remaining_seconds"].as<uint64_t>() != derive(s, 0).remainingSeconds) return false;
  output = s;
  return true;
}
}  // namespace povo
