#pragma once
#include <ArduinoJson.h>
#include <algorithm>
#include <cstdint>
#include <string>

namespace povo { namespace auth {
constexpr size_t kMaxToken = 8192;
constexpr size_t kMaxResponse = 16384;
constexpr uint64_t kMaxEpoch = 253402300799ULL;
enum class State { Unauthenticated, AwaitingOtp, Authenticated, AdditionalAuth, Error };
struct Session {
  std::string token;
  std::string externalId;
  uint64_t expiresAt = 0;
  bool firstLogin = false;
};
struct Reply {
  State state = State::Error;
  std::string authId;
  Session session;
};

inline bool textValid(const std::string& value, size_t limit) {
  if (value.empty() || value.size() > limit) return false;
  for (unsigned char c : value) if (c < 32 || c == 127) return false;
  return true;
}
inline bool jsonText(JsonVariantConst value, std::string& output, size_t limit) {
  if (!value.is<const char*>()) return false;
  JsonString text = value.as<JsonString>();
  std::string candidate(text.c_str(), text.size());
  if (!textValid(candidate, limit)) return false;
  output = candidate;
  return true;
}
inline bool decimal(const std::string& value, uint64_t& output) {
  if (value.empty() || value.size() > 12) return false;
  uint64_t result = 0;
  for (char c : value) {
    if (c < '0' || c > '9') return false;
    result = result * 10 + static_cast<unsigned>(c - '0');
    if (result > kMaxEpoch) return false;
  }
  if (!result) return false;
  output = result;
  return true;
}
inline bool epoch(JsonVariantConst value, uint64_t& output) {
  if (value.is<const char*>()) {
    std::string text;
    return jsonText(value, text, 12) && decimal(text, output);
  }
  if (!value.is<uint64_t>() || value.is<bool>()) return false;
  output = value.as<uint64_t>();
  return output > 0 && output <= kMaxEpoch;
}
inline int base64Digit(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  return c == '-' ? 62 : c == '_' ? 63 : -1;
}
// JWT uses unpadded, canonical base64url. No signature verification is performed.
inline bool decodeBase64Url(const std::string& input, std::string& output) {
  if (input.empty() || input.size() > kMaxToken || input.size() % 4 == 1) return false;
  std::string decoded;
  decoded.reserve(input.size() * 3 / 4);
  unsigned accumulator = 0, bits = 0;
  for (char c : input) {
    const int digit = base64Digit(c);
    if (digit < 0) return false;
    accumulator = (accumulator << 6) | static_cast<unsigned>(digit);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      decoded.push_back(static_cast<char>((accumulator >> bits) & 255));
    }
    accumulator &= (1U << bits) - 1;
  }
  if (accumulator != 0) return false;
  output = decoded;
  return true;
}
// Call only for a token delivered by a TLS-authenticated server or trusted storage.
// Decoding claims is not JWT signature validation.
inline bool parseTokenClaims(const std::string& token, uint64_t now, Session& output) {
  if (!textValid(token, kMaxToken) || now == 0 || now > kMaxEpoch) return false;
  const size_t first = token.find('.');
  const size_t second = first == std::string::npos ? first : token.find('.', first + 1);
  if (first == std::string::npos || second == std::string::npos ||
      token.find('.', second + 1) != std::string::npos) return false;
  std::string header, claims, signature;
  if (!decodeBase64Url(token.substr(0, first), header) ||
      !decodeBase64Url(token.substr(first + 1, second - first - 1), claims) ||
      !decodeBase64Url(token.substr(second + 1), signature)) return false;
  DynamicJsonDocument headerDoc(1024), doc(12288);
  if (deserializeJson(headerDoc, header, DeserializationOption::NestingLimit(2)) ||
      deserializeJson(doc, claims, DeserializationOption::NestingLimit(4))) return false;
  std::string algorithm, issuer;
  if (!headerDoc.is<JsonObject>() || !doc.is<JsonObject>() ||
      !jsonText(headerDoc["alg"], algorithm, 32) || algorithm == "none" ||
      !jsonText(doc["iss"], issuer, 32) || issuer != "circles") return false;
  Session candidate;
  if (!jsonText(doc["external_id"], candidate.externalId, 256)) return false;
  bool hasTime = false;
  for (const char* key : {"expiry_time", "exp"}) {
    if (!doc.containsKey(key)) continue;
    uint64_t time = 0;
    if (!epoch(doc[key], time)) return false;
    candidate.expiresAt = hasTime ? std::min(candidate.expiresAt, time) : time;
    hasTime = true;
  }
  if (!hasTime || candidate.expiresAt <= now) return false;
  candidate.token = token;
  output = candidate;
  return true;
}
inline bool envelope(const std::string& body, DynamicJsonDocument& doc) {
  return !body.empty() && body.size() <= kMaxResponse &&
      !deserializeJson(doc, body, DeserializationOption::NestingLimit(6)) &&
      doc.is<JsonObject>() && doc["success"].is<bool>() &&
      doc["success"].as<bool>() && doc["result"].is<JsonObject>();
}
inline Reply parseLoginActions(const std::string& body) {
  Reply reply;
  DynamicJsonDocument doc(12288);
  if (!envelope(body, doc)) return reply;
  JsonArrayConst actions = doc["result"]["actions"].as<JsonArrayConst>();
  if (actions.isNull() || actions.size() == 0) return reply;
  for (JsonVariantConst item : actions) {
    std::string action;
    if (!item.is<JsonObjectConst>() || !jsonText(item["action"], action, 128)) return reply;
    if (action != "EMAIL_OTP") { reply.state = State::AdditionalAuth; return reply; }
  }
  reply.state = actions.size() == 1 ? State::AwaitingOtp : State::AdditionalAuth;
  return reply;
}
inline Reply parseOtpResponse(const std::string& body) {
  Reply reply;
  DynamicJsonDocument doc(4096);
  if (envelope(body, doc) && jsonText(doc["result"]["auth_id"], reply.authId, 1024))
    reply.state = State::AwaitingOtp;
  return reply;
}
inline Reply parseAuthResponse(const std::string& body, uint64_t now) {
  Reply reply;
  DynamicJsonDocument doc(24576);
  if (!envelope(body, doc)) return reply;
  JsonObjectConst result = doc["result"];
  for (const char* key : {"pin_token", "next_step"}) {
    JsonVariantConst value = result[key];
    if (value.isNull()) continue;
    if (!value.is<const char*>()) return reply;
    const JsonString step = value.as<JsonString>();
    const bool dashboard = std::string(key) == "next_step" &&
        std::string(step.c_str(), step.size()) == "dashboard";
    if (step.size() > 0 && !dashboard) {
      reply.state = State::AdditionalAuth;
      return reply;
    }
  }
  std::string token;
  if (!jsonText(result["auth_token"], token, kMaxToken) ||
      !parseTokenClaims(token, now, reply.session)) return reply;
  if (result.containsKey("first_login")) {
    if (!result["first_login"].is<bool>()) return Reply{};
    reply.session.firstLogin = result["first_login"];
  }
  reply.state = State::Authenticated;
  return reply;
}
inline bool commitSessionUpdate(const Reply& reply, Session& current) {
  if (reply.state != State::Authenticated || reply.session.token.empty() ||
      reply.session.externalId.empty() || reply.session.expiresAt == 0) return false;
  current = reply.session;
  return true;
}
inline bool signedUuid(const std::string& value) {
  size_t start = !value.empty() && value[0] == '-' ? 1 : 0;
  if (value.size() <= start || value.size() - start > 19) return false;
  for (size_t i = start; i < value.size(); ++i) if (value[i] < '0' || value[i] > '9') return false;
  const std::string magnitude = value.substr(start);
  if (magnitude.size() > 1 && magnitude[0] == '0') return false;
  return magnitude.size() < 19 || magnitude <= (start ? "9223372036854775808" : "9223372036854775807");
}
inline bool emit(JsonDocument& doc, std::string& output) {
  if (doc.overflowed()) return false;
  std::string candidate;
  serializeJson(doc, candidate);
  output = candidate;
  return true;
}
inline bool buildLoginAction(const std::string& email, const std::string& uuid, std::string& out) {
  if (!textValid(email, 254) || !signedUuid(uuid)) return false;
  DynamicJsonDocument doc(1024);
  doc["email"] = email; doc["uuid"] = uuid;
  return emit(doc, out);
}
inline bool buildOtpRequest(const std::string& email, const std::string& deviceId,
                            const std::string& uuid, std::string& out) {
  if (!textValid(email, 254) || !textValid(deviceId, 256) || !signedUuid(uuid)) return false;
  DynamicJsonDocument doc(2048);
  doc["device_id"] = deviceId; doc["email"] = email;
  doc["otp_duration"] = 15; doc["auth_mode"] = "ENHANCED_EMAIL_OTP";
  doc["request_type"] = "LOGIN_EMAIL_OTP"; doc["uuid"] = uuid;
  return emit(doc, out);
}
inline bool buildAuthRequest(const std::string& authId, const std::string& otp,
                             const std::string& deviceId, const std::string& uuid,
                             std::string& out) {
  if (!textValid(authId, 1024) || !textValid(deviceId, 256) || !signedUuid(uuid) ||
      otp.empty() || otp.size() > 16) return false;
  for (char c : otp) if (c < '0' || c > '9') return false;
  DynamicJsonDocument doc(4096);
  JsonObject auth = doc.createNestedObject("email_auth");
  auth["auth_id"] = authId; auth["otp_code"] = otp; auth["device_id"] = deviceId;
  JsonObject device = doc.createNestedObject("device");
  device["device_id"] = deviceId; device["device_type"] = "Mobile";
  device["app_type"] = "ecosystem"; doc["uuid"] = uuid;
  return emit(doc, out);
}
}}  // namespace povo::auth
