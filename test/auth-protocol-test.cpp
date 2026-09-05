#include <cstdlib>
#include <iostream>
#include "auth-protocol.h"
using namespace povo::auth;
void check(bool condition, const char* name) {
  if (!condition) { std::cerr << name << '\n'; std::exit(1); }
}
std::string encode(const std::string& input) {
  const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  unsigned value = 0, bits = 0;
  for (unsigned char c : input) {
    value = (value << 8) | c; bits += 8;
    while (bits >= 6) { bits -= 6; out += table[(value >> bits) & 63]; }
    value &= (1U << bits) - 1;
  }
  if (bits) out += table[(value << (6 - bits)) & 63];
  return out;
}
std::string token(const std::string& claims) {
  return encode(R"({"alg":"HS256"})") + "." + encode(claims) + "." + encode("test-signature");
}
std::string response(const std::string& jwt, const std::string& extra = "") {
  return "{\"success\":true,\"result\":{\"auth_token\":\"" + jwt + "\"" + extra + "}}";
}
int main() {
  std::string decoded = "unchanged";
  check(decodeBase64Url("Zg", decoded) && decoded == "f", "base64 one byte");
  check(decodeBase64Url("Zm9v", decoded) && decoded == "foo", "base64 full group");
  for (const std::string bad : {"", "A", "Zg=", "Zh", "AA+_", "AA/_", "AA\n"})
    check(!decodeBase64Url(bad, decoded), "invalid base64 rejected");
  check(!decodeBase64Url(std::string(kMaxToken + 1, 'A'), decoded), "base64 input bound");
  const auto good = token(R"({"iss":"circles","external_id":"test-user","expiry_time":"2000","exp":3000})");
  Session session;
  check(parseTokenClaims(good, 1000, session), "valid claims");
  check(session.expiresAt == 2000 && session.externalId == "test-user", "minimum expiry");
  check(!parseTokenClaims(good, 2000, session), "expiry boundary");
  check(!parseTokenClaims(good, 0, session), "clock unset");
  for (const char* claims : {
      R"({"iss":"other","external_id":"u","exp":3000})",
      R"({"iss":"circles","external_id":null,"exp":3000})",
      R"({"iss":"circles","external_id":"","exp":3000})",
      R"({"iss":"circles","external_id":"u"})",
      R"({"iss":"circles","external_id":"u","exp":null})",
      R"({"iss":"circles","external_id":"u","exp":true})",
      R"({"iss":"circles","external_id":"u","exp":3000.5})",
      R"({"iss":"circles","external_id":"u","exp":-1})",
      R"({"iss":"circles","external_id":"u","exp":"3e3"})",
      R"({"iss":"circles","external_id":"u","exp":"3000x"})",
      R"({"iss":"circles","external_id":"u","exp":253402300800})",
      R"({"iss":"circles","external_id":"u","exp":3000,"expiry_time":null})",
      R"({"iss":"circles","external_id":"u\u0000x","exp":3000})"}) {
    const auto saved = session.token;
    check(!parseTokenClaims(token(claims), 1000, session), "bad claims");
    check(session.token == saved, "bad claims preserve output");
  }
  check(!parseTokenClaims(good + ".x", 1000, session), "extra segment");
  check(!parseTokenClaims(good.substr(0, good.rfind('.') + 1), 1000, session), "empty signature");
  check(parseLoginActions(R"({"success":true,"result":{"actions":[{"action":"EMAIL_OTP"}]}})").state == State::AwaitingOtp, "email action");
  check(parseLoginActions(R"({"success":true,"result":{"actions":[{"action":"SMS"}]}})").state == State::AdditionalAuth, "unknown action");
  check(parseLoginActions(R"({"success":true,"result":{"actions":[{"action":"EMAIL_OTP"},{"action":"EMAIL_OTP"}]}})").state == State::AdditionalAuth, "multiple actions");
  for (const char* body : {"null", "{}", R"({"success":1,"result":{}})",
      R"({"success":false,"result":{"actions":[{"action":"EMAIL_OTP"}]}})",
      R"({"success":true,"result":{"actions":[]}})",
      R"({"success":true,"result":{"actions":[null]}})",
      R"({"success":true,"result":{"actions":[{"action":null}]}})"})
    check(parseLoginActions(body).state == State::Error, "invalid action response");
  auto otp = parseOtpResponse(R"({"success":true,"result":{"auth_id":"test-challenge"}})");
  check(otp.state == State::AwaitingOtp && otp.authId == "test-challenge", "otp response");
  check(parseOtpResponse(R"({"success":true,"result":{"auth_id":null}})").state == State::Error, "null challenge");
  auto reply = parseAuthResponse(response(good, ",\"first_login\":false"), 1000);
  check(reply.state == State::Authenticated && !reply.session.firstLogin, "auth success");
  check(commitSessionUpdate(reply, session), "commit valid update");
  const auto saved = session.token;
  for (const auto& bad : {parseAuthResponse(response(good), 2000),
       parseAuthResponse(response(good, ",\"pin_token\":\"challenge\""), 1000),
       parseAuthResponse(response(good, ",\"next_step\":\"UNKNOWN\""), 1000),
       parseAuthResponse(response(good, ",\"first_login\":null"), 1000)}) {
    check(!commitSessionUpdate(bad, session), "reject invalid refresh");
    check(session.token == saved, "refresh preserves previous token");
  }
  check(parseAuthResponse(response(good, ",\"next_step\":null"), 1000).state == State::Authenticated, "null next step");
  check(parseAuthResponse(response(good, ",\"next_step\":\"dashboard\""), 1000).state == State::Authenticated, "observed dashboard next step");
  check(parseAuthResponse(response(good, ",\"next_step\":\"dashboard\",\"pin_token\":\"challenge\""), 1000).state == State::AdditionalAuth, "pin still required for dashboard");
  check(parseAuthResponse(response(good, ",\"pin_token\":true"), 1000).state == State::Error, "invalid pin type");
  std::string body;
  check(buildLoginAction("test@example.invalid", "-9223372036854775808", body), "action request");
  DynamicJsonDocument doc(4096);
  check(!deserializeJson(doc, body) && doc.size() == 2, "action keys");
  check(buildOtpRequest("test@example.invalid", "device-test", "123", body), "otp request");
  check(!deserializeJson(doc, body) && doc.size() == 6 && !doc.containsKey("channel"), "otp keys");
  check(buildAuthRequest("challenge-test", "001234", "device-test", "-123", body), "auth request");
  check(!deserializeJson(doc, body) && doc["email_auth"]["otp_code"] == "001234" &&
      doc["device"]["device_type"] == "Mobile" && !doc.containsKey("override"), "auth request shape");
  for (const char* uuid : {"", "-", "9223372036854775808", "-9223372036854775809", "1x", "01"})
    check(!buildLoginAction("test@example.invalid", uuid, body), "bad uuid");
  const auto savedBody = body;
  check(!buildAuthRequest("id", "123x", "device", "1", body) && body == savedBody, "invalid request unchanged");
  check(!buildLoginAction("test\n@example.invalid", "1", body), "control input rejected");
  std::cout << "auth protocol tests passed\n";
}
