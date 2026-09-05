#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include "status-model.h"

namespace povo {
inline void spaces(const char*& p) { while (*p == ' ') ++p; }
inline bool literal(const char*& p, const char* text) {
  spaces(p);
  const size_t n = std::strlen(text);
  if (std::strncmp(p, text, n)) return false;
  p += n;
  return true;
}
inline bool number(const char*& p, unsigned& n) {
  spaces(p); n = 0;
  unsigned count = 0;
  while (*p >= '0' && *p <= '9') {
    if (++count > 4) return false;
    n = n * 10 + static_cast<unsigned>(*p++ - '0');
  }
  return count != 0;
}
// Server's ja-JP/Asia-Tokyo display value has minute precision.
inline bool parseJapaneseExpiry(const char* value, uint64_t& epochMs) {
  if (!value || std::strlen(value) > 256) return false;
  const char* p = value;
  unsigned y, m, d, hour, minute;
  if (!number(p, y) || !literal(p, "年") || !number(p, m) || !literal(p, "月") ||
      !number(p, d) || !literal(p, "日")) return false;
  bool pm = false;
  if (literal(p, "午後")) pm = true;
  else if (!literal(p, "午前")) return false;
  if (!number(p, hour) || !literal(p, ":") || !number(p, minute)) return false;
  spaces(p);
  if (*p && *p != '\n') return false;
  if (y < 1970 || y > 9999 || m < 1 || m > 12 || hour < 1 || hour > 12 || minute > 59)
    return false;
  const bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
  const unsigned days[] = {31, static_cast<unsigned>(leap ? 29 : 28), 31, 30, 31, 30,
                           31, 31, 30, 31, 30, 31};
  if (d < 1 || d > days[m - 1]) return false;
  const int year = static_cast<int>(y) - (m <= 2);
  const int era = year / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned mp = m > 2 ? m - 3 : m + 9;
  const unsigned doy = (153 * mp + 2) / 5 + d - 1;
  const int64_t day = static_cast<int64_t>(era) * 146097 +
      yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  const int64_t seconds = day * 86400 + (hour % 12 + (pm ? 12 : 0)) * 3600 + minute * 60 - 9 * 3600;
  if (seconds <= 0) return false;
  epochMs = static_cast<uint64_t>(seconds) * 1000;
  return true;
}

inline bool parseDirectStatus(const std::string& body, uint64_t nowMs, Status& output) {
  if (body.empty() || body.size() > 65536 || nowMs == 0 || nowMs > kMaxTimestamp) return false;
  StaticJsonDocument<512> filter;
  filter["code"] = true; filter["error"] = true;
  JsonObject component = filter["widgets"][0]["components"][0].to<JsonObject>();
  component["type"] = true;
  component["data"]["name"]["value"] = true;
  component["data"]["remaining"]["value"] = true;
  component["data"]["expiry"]["value"] = true;
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(20))) return false;
  if (!doc["widgets"].is<JsonArray>() || !doc["error"].isNull()) return false;
  if (doc.containsKey("code") && (!doc["code"].is<int>() || doc["code"].as<int>() != 0)) return false;
  uint64_t earliest = 0;
  for (JsonObjectConst widget : doc["widgets"].as<JsonArrayConst>()) {
    JsonVariantConst components = widget["components"];
    if (components.isNull()) continue;
    if (!components.is<JsonArrayConst>()) return false;
    for (JsonObjectConst item : components.as<JsonArrayConst>()) {
      const char* type = item["type"] | "";
      if (std::strcmp(type, "povo-tile-plan-detail")) continue;
      const char* name = item["data"]["name"]["value"] | "";
      const char* remaining = item["data"]["remaining"]["value"] | "";
      if (std::strcmp(name, "適用中") || std::strcmp(remaining, "使い放題")) continue;
      if (!item["data"]["expiry"]["value"].is<const char*>()) return false;
      uint64_t expiry = 0;
      if (!parseJapaneseExpiry(item["data"]["expiry"]["value"], expiry)) return false;
      if (!earliest || expiry < earliest) earliest = expiry;
    }
  }
  Status candidate;
  candidate.receivedAtMs = candidate.serverTimeMs = nowMs;
  candidate.expiryAtMs = earliest;
  candidate.expirySource = earliest ? ExpirySource::Server : ExpirySource::Unknown;
  output = candidate;
  return true;
}
}  // namespace povo
