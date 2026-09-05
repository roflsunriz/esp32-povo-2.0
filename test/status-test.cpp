#include <cstdlib>
#include <iostream>
#include <string>
#include "status-json.h"

void check(bool ok, const char* label) {
  if (!ok) { std::cerr << label << '\n'; std::exit(1); }
}

std::string sample() {
  return R"({"schema_version":1,"observed_at_ms":1700000000000,"expiry_at_ms":1700310320000,"expiry_source":"server","expiry_observed_at_ms":1700000000000,"code_deadline_at_ms":null,"automatic_renewal":true,"applied_uses":4,"max_uses":24,"renewal_state":"idle","last_applied_at_ms":null,"received_at_ms":1700000000000,"server_time_ms":1700000000000,"stale":false,"remaining_seconds":310320,"renewal_confirmation_pending":false})";
}

std::string replace(std::string text, const std::string& from, const std::string& to) {
  auto pos = text.find(from);
  check(pos != std::string::npos, "test replacement missing");
  text.replace(pos, from.size(), to);
  return text;
}

int main() {
  povo::Status s;
  auto body = sample();
  check(povo::parseStatus(body.c_str(), body.size(), s), "valid relay response");
  auto v = povo::derive(s, 60000);
  check(v.remainingKnown && v.remainingSeconds == 310260 && !v.stale, "countdown");
  check(!povo::derive(s, 899999).stale && povo::derive(s, 900000).stale, "stale boundary");
  check(povo::derive(s, 310320000).confirmationPending, "expired confirmation");
  check(povo::derive(s, 310320001).remainingSeconds == 0, "no underflow");
  check(povo::derive(s, UINT64_MAX).nowMs == povo::kMaxTimestamp, "clock saturation");
  for (const auto& pair : {
      std::pair<std::string, std::string>{"\"schema_version\":1", "\"schema_version\":2"},
      {"\"automatic_renewal\":true", "\"automatic_renewal\":1"},
      {"\"applied_uses\":4", "\"applied_uses\":25"},
      {"\"max_uses\":24", "\"max_uses\":-1"},
      {"\"expiry_source\":\"server\"", "\"expiry_source\":\"bad\""},
      {"\"renewal_state\":\"idle\"", "\"renewal_state\":\"bad\""},
      {"\"remaining_seconds\":310320", "\"remaining_seconds\":1"},
      {"\"expiry_at_ms\":1700310320000", "\"expiry_at_ms\":0"},
      {"\"received_at_ms\":1700000000000", "\"received_at_ms\":null"}}) {
    auto bad = replace(body, pair.first, pair.second);
    povo::Status saved = s;
    check(!povo::parseStatus(bad.c_str(), bad.size(), saved), "reject malformed status");
    check(saved.expiryAtMs == s.expiryAtMs, "failure preserves previous status");
  }
  auto unknown = replace(body, "\"expiry_at_ms\":1700310320000", "\"expiry_at_ms\":null");
  unknown = replace(unknown, "\"remaining_seconds\":310320", "\"remaining_seconds\":null");
  unknown = replace(unknown, "\"expiry_source\":\"server\"", "\"expiry_source\":\"unknown\"");
  check(povo::parseStatus(unknown.c_str(), unknown.size(), s), "null expiry accepted");
  check(!povo::derive(s, 0).remainingKnown, "unknown differs from expired");
  s.expirySource = povo::ExpirySource::Estimated;
  check(povo::derive(s, 0).confirmationPending, "estimated needs confirmation");
  s.expirySource = povo::ExpirySource::Manual;
  check(!povo::derive(s, 0).confirmationPending, "manual remains distinguishable");
  s.stale = true;
  check(povo::derive(s, 0).stale, "relay stale retained");
  std::cout << "status tests passed\n";
}
