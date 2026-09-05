#include "povo-client.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <cstring>
#include <memory>
#if __has_include("device-config.h")
#include "device-config.h"
#else
#include "device-config.example.h"
#endif

namespace povo {
namespace {
constexpr size_t maxBody = 65536;
constexpr const char* actionPath = "/api/v3/user-service/v3/jp/ja/mobile/users/login/action";
constexpr const char* otpPath = "/api/v3/user-service/v4/jp/ja/mobile/otp";
constexpr const char* authPath = "/user-service/v5/public/jp/ja/mobile/users/auth";
constexpr const char* refreshPath = "/api/v3/user-service/v4/jp/ja/mobile/users/token";
constexpr const char* planPath = "/api/v1/quilt/page/user-plan-details-v2";
uint64_t monotonicMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
std::string requestUuid() {
  int64_t value;
  esp_fill_random(&value, sizeof(value));
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
  return buffer;
}
std::string newDevice() {
  uint8_t bytes[16];
  esp_fill_random(bytes, sizeof(bytes));
  bytes[6] = (bytes[6] & 15) | 64;
  bytes[8] = (bytes[8] & 63) | 128;
  char result[64];
  snprintf(result, sizeof(result),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02xcom.kddi.kdla.jp",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
      bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return result;
}
// Bound both decoded bytes and chunk framing, including slow streamed responses.
class BoundedBody {
 public:
  explicit BoundedBody(WiFiClientSecure& input) : input_(input) {}
  std::string body;
  const uint64_t started = monotonicMs();
  bool read(int length, bool chunked) {
    if (chunked) {
      for (;;) {
        std::string header;
        if (!line(header, 128)) return false;
        const size_t end = header.find(';');
        const std::string digits = header.substr(0, end);
        if (digits.empty() || digits.size() > 8) return false;
        size_t size = 0;
        for (char c : digits) {
          int digit = c >= '0' && c <= '9' ? c - '0' :
              c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
          if (digit < 0) return false;
          size = size * 16 + static_cast<unsigned>(digit);
          if (size > maxBody - body.size()) return false;
        }
        if (size == 0) {
          do { if (!line(header, 512)) return false; } while (!header.empty());
          return !body.empty();
        }
        if (!data(size)) return false;
        if (byte() != '\r' || byte() != '\n') return false;
      }
    }
    if (length >= 0) return data(static_cast<size_t>(length)) && !body.empty();
    for (;;) {
      int c = byte();
      if (c == -2) return !body.empty();
      if (c < 0 || body.size() >= maxBody) return false;
      body.push_back(static_cast<char>(c));
    }
  }
 private:
  WiFiClientSecure& input_;
  size_t rawBytes_ = 0;
  int byte() {
    for (;;) {
      if (monotonicMs() - started >= 20000 || rawBytes_ >= maxBody + 8192) return -1;
      if (input_.available()) { ++rawBytes_; return input_.read(); }
      if (!input_.connected()) return -2;
      delay(1);
    }
  }
  bool line(std::string& value, size_t limit) {
    value.clear();
    for (;;) {
      int c = byte();
      if (c < 0) return false;
      if (c == '\r') return byte() == '\n';
      if (c == '\n' || value.size() >= limit) return false;
      value.push_back(static_cast<char>(c));
    }
  }
  bool data(size_t size) {
    if (size > maxBody - body.size()) return false;
    for (size_t i = 0; i < size; ++i) {
      int c = byte();
      if (c < 0) return false;
      body.push_back(static_cast<char>(c));
    }
    return true;
  }
};
}

auth::State Client::fail(const char* message) {
  error_ = message;
  state_ = auth::State::Error;
  return state_;
}
void Client::clearChallenge() {
  email_.clear(); authId_.clear(); otpStartedMs_ = 0;
}
bool Client::save(const auth::Session& candidate) {
  DynamicJsonDocument doc(16384);
  doc["schema"] = 1;
  doc["device"] = device_;
  doc["token"] = candidate.token;
  std::string blob;
  if (!auth::emit(doc, blob) ||
      preferences_.putString("session", blob.c_str()) != blob.size()) {
    error_ = "session_save_failed";
    return false;
  }
  return true;
}
void Client::clearSession() {
  session_ = auth::Session{};
  state_ = auth::State::Unauthenticated;
  if (ready_ && !save(session_)) state_ = auth::State::Error;
}
bool Client::begin(uint64_t now) {
  if (ready_) return true;
  if (!preferences_.begin("povo-auth", false)) { fail("storage_unavailable"); return false; }
  ready_ = true;
  std::unique_ptr<char[]> blob(new char[16385]);
  blob[0] = '\0';
  const size_t length = preferences_.getString("session", blob.get(), 16385);
  bool valid = false;
  if (length > 0 && length <= 16384) {
    DynamicJsonDocument doc(24576);
    if (!deserializeJson(doc, blob.get(), DeserializationOption::NestingLimit(2)) &&
        doc.is<JsonObject>() && doc["schema"].is<unsigned>() && doc["schema"].as<unsigned>() == 1 &&
        auth::jsonText(doc["device"], device_, 256) &&
        doc["token"].is<const char*>()) {
      JsonString token = doc["token"].as<JsonString>();
      if (token.size() == 0) valid = true;
      else valid = auth::parseTokenClaims(std::string(token.c_str(), token.size()), 1, session_);
    }
  }
  if (!valid) {
    // Preserve only a structurally valid device ID; never carry forward malformed auth.
    if (!auth::textValid(device_, 256)) device_ = newDevice();
    session_ = auth::Session{};
    if (!save(session_)) { state_ = auth::State::Error; return false; }
  }
  if (session_.token.empty()) return true;
  if (now == 0 || now > auth::kMaxEpoch) { fail("clock_unavailable"); return false; }
  if (session_.expiresAt <= now || session_.expiresAt - now <= 60) return refresh(now);
  state_ = auth::State::Authenticated;
  return true;
}
bool Client::authenticated() const {
  return state_ == auth::State::Authenticated && !session_.token.empty();
}
uint32_t Client::otpSecondsRemaining() const {
  if (authId_.empty()) return 0;
  const uint64_t elapsed = monotonicMs() - otpStartedMs_;
  return elapsed >= 120000 ? 0 : static_cast<uint32_t>((120000 - elapsed + 999) / 1000);
}
int Client::request(const char* path, const std::string* post, bool authorized, std::string& response) {
  if (!ready_ || WiFi.status() != WL_CONNECTED) { error_ = "network_unavailable"; return -1; }
  if (strlen(POVO_ROOT_CA) == 0) { error_ = "certificate_missing"; return -1; }
  WiFiClientSecure transport;
  transport.setCACert(POVO_ROOT_CA);
  transport.setHandshakeTimeout(10);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(transport, String("https://app.povo.jp") + path)) {
    error_ = "connection_failed"; return -1;
  }
  http.addHeader("X-App-Version", "1.70.0");
  http.addHeader("X-App-Platform", "Android");
  http.addHeader("X-Deviceid", device_.c_str());
  http.addHeader("X-Timezone", "Asia/Tokyo");
  http.addHeader("Accept-Language", "ja-JP");
  http.addHeader("Accept-Encoding", "identity");
  const char* responseHeaders[] = {"Transfer-Encoding", "Content-Encoding"};
  http.collectHeaders(responseHeaders, 2);
  if (authorized) {
    http.addHeader("X-AUTH", session_.token.c_str());
    http.addHeader("X-USER-ID", session_.externalId.c_str());
  }
  int code;
  if (post) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(String(post->c_str()));
  } else code = http.GET();
  if (code != 200) {
    error_ = code == 401 || code == 403 ? "authentication_rejected" : "http_failed";
    http.end(); return code;
  }
  if (http.getSize() > static_cast<int>(maxBody)) {
    error_ = "response_too_large"; http.end(); return -1;
  }
  String transfer = http.header("Transfer-Encoding"), encoding = http.header("Content-Encoding");
  transfer.toLowerCase(); encoding.toLowerCase();
  if ((transfer.length() && transfer != "chunked" && transfer != "identity") ||
      (encoding.length() && encoding != "identity")) {
    error_ = "unsupported_encoding"; http.end(); return -1;
  }
  BoundedBody sink(transport);
  const bool complete = sink.read(http.getSize(), transfer == "chunked");
  http.end();
  if (!complete) {
    error_ = "response_incomplete";
    return -1;
  }
  response = std::move(sink.body);
  return code;
}
bool Client::accept(const auth::Reply& reply) {
  if (reply.state != auth::State::Authenticated) {
    state_ = reply.state;
    error_ = state_ == auth::State::AdditionalAuth ? "additional_auth_required" : "invalid_auth_response";
    return false;
  }
  if (!save(reply.session)) { state_ = auth::State::Error; return false; }
  auth::commitSessionUpdate(reply, session_);
  state_ = auth::State::Authenticated;
  error_ = "";
  clearChallenge();
  return true;
}
bool Client::refresh(uint64_t now) {
  if (session_.token.empty()) { fail("login_required"); return false; }
  std::string response;
  const int code = request(refreshPath, nullptr, true, response);
  if (code == 401 || code == 403) {
    clearSession();
    if (state_ != auth::State::Error) error_ = "login_required";
    return false;
  }
  if (code != 200) { state_ = auth::State::Error; return false; }
  const auto reply = auth::parseAuthResponse(response, now);
  if (reply.state == auth::State::Authenticated && reply.session.externalId != session_.externalId) {
    fail("refresh_identity_changed");
    return false;
  }
  return accept(reply);
}
auth::State Client::startLogin(const std::string& email, uint64_t now) {
  if (!ready_) return fail("storage_unavailable");
  if (now == 0 || now > auth::kMaxEpoch) return fail("clock_unavailable");
  const uint64_t started = monotonicMs();
  if (sentBefore_ && started - lastSendMs_ < 30000) return fail("resend_too_soon");
  std::string body, response;
  if (!auth::buildLoginAction(email, requestUuid(), body)) return fail("invalid_email");
  clearChallenge();
  sentBefore_ = true; lastSendMs_ = started;
  if (request(actionPath, &body, false, response) != 200) { state_ = auth::State::Error; return state_; }
  auto reply = auth::parseLoginActions(response);
  if (reply.state != auth::State::AwaitingOtp) {
    state_ = reply.state;
    error_ = state_ == auth::State::AdditionalAuth ? "additional_auth_required" : "invalid_auth_response";
    return state_;
  }
  if (!auth::buildOtpRequest(email, device_, requestUuid(), body)) return fail("invalid_email");
  const uint64_t sentAt = monotonicMs();
  if (request(otpPath, &body, false, response) != 200) { state_ = auth::State::Error; return state_; }
  reply = auth::parseOtpResponse(response);
  if (reply.state != auth::State::AwaitingOtp) return fail("invalid_auth_response");
  authId_ = reply.authId; email_ = email; otpStartedMs_ = sentAt;
  state_ = auth::State::AwaitingOtp; error_ = "";
  return state_;
}
auth::State Client::submitOtp(const std::string& otp, uint64_t now) {
  if (now == 0 || now > auth::kMaxEpoch) return fail("clock_unavailable");
  if (authId_.empty()) return fail("otp_required");
  if (monotonicMs() - otpStartedMs_ >= 120000) { clearChallenge(); return fail("otp_expired"); }
  std::string body, response;
  if (!auth::buildAuthRequest(authId_, otp, device_, requestUuid(), body)) return fail("invalid_otp");
  const int code = request(authPath, &body, false, response);
  if (code != 200) { state_ = auth::State::Error; return state_; }
  accept(auth::parseAuthResponse(response, now));
  return state_;
}
bool Client::fetchPlan(std::string& body, uint64_t now) {
  if (session_.token.empty()) { error_ = "login_required"; return false; }
  if (now == 0 || now > auth::kMaxEpoch) { error_ = "clock_unavailable"; return false; }
  bool refreshed = false;
  if (session_.expiresAt <= now || session_.expiresAt - now <= 60) {
    if (!refresh(now)) return false;
    refreshed = true;
  }
  std::string response;
  int code = request(planPath, nullptr, true, response);
  if (code == 401 && !refreshed) {
    if (!refresh(now)) return false;
    code = request(planPath, nullptr, true, response);
  }
  if (code == 401 || code == 403) {
    clearSession();
    if (state_ != auth::State::Error) error_ = "login_required";
    return false;
  }
  if (code != 200) return false;
  body = std::move(response); error_ = "";
  return true;
}
void Client::logout() {
  clearChallenge();
  error_ = "";
  clearSession();
}
}
