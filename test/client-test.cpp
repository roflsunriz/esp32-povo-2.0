#include <cstdlib>
#include <iostream>
#include "client-stubs/mock.h"
#include "../src/povo-client.cpp"

void check(bool condition, const char* label) {
  if (!condition) { std::cerr << label << '\n'; std::exit(1); }
}
std::string encode(const std::string& input) {
  const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string result;
  unsigned bits = 0, accumulator = 0;
  for (unsigned char c : input) {
    accumulator = (accumulator << 8) | c; bits += 8;
    while (bits >= 6) { bits -= 6; result += alphabet[(accumulator >> bits) & 63]; }
    accumulator &= (1U << bits) - 1;
  }
  if (bits) result += alphabet[(accumulator << (6 - bits)) & 63];
  return result;
}
std::string jwt(unsigned expires) {
  return encode(R"({"alg":"HS256"})") + "." +
      encode("{\"iss\":\"circles\",\"external_id\":\"test-user\",\"exp\":" +
             std::to_string(expires) + "}") + "." + encode("fake-signature");
}
std::string authResponse(unsigned expires) {
  return "{\"success\":true,\"result\":{\"auth_token\":\"" + jwt(expires) +
      "\",\"first_login\":false,\"next_step\":\"dashboard\"}}";
}
void seed(unsigned expires) {
  mock::storage["session"] = "{\"schema\":1,\"device\":\"test-device\",\"token\":\"" + jwt(expires) + "\"}";
}
void challenge(povo::Client& client) {
  mock::push(R"({"success":true,"result":{"actions":[{"action":"EMAIL_OTP"}]}})");
  mock::push(R"({"success":true,"result":{"auth_id":"test-challenge"}})");
  check(client.startLogin("test@example.invalid", 1000) == povo::auth::State::AwaitingOtp, "challenge starts");
}
void requestTransport() {
  for (const auto& request : mock::requests) {
    check(request.tls && request.noRedirect, "TLS verification and redirect policy");
    check(request.headers.at("Accept-Language") == "ja-JP", "Japanese locale");
    check(request.headers.at("X-Timezone") == "Asia/Tokyo", "Tokyo timezone");
    check(request.url.rfind("https://app.povo.jp/", 0) == 0, "fixed HTTPS authority");
  }
}
int main() {
  mock::reset();
  {
    povo::Client client; check(client.begin(1000), "empty init");
    challenge(client);
    check(client.otpSecondsRemaining() == 120, "initial OTP ttl");
    mock::millis = 29000;
    check(client.startLogin("test@example.invalid", 1000) == povo::auth::State::Error, "cooldown error");
    check(mock::requests.size() == 2 && client.otpSecondsRemaining() == 91, "cooldown does not resend or reset");
    mock::push(authResponse(4000));
    check(client.submitOtp("001234", 1000) == povo::auth::State::Authenticated, "original OTP usable after cooldown");
    check(client.hasSession() && client.authenticated() && client.otpSecondsRemaining() == 0, "committed login clears challenge");
    requestTransport();
    check(mock::requests.back().headers.count("X-AUTH") == 0, "independent login no auth header");
  }
  mock::reset();
  {
    povo::Client client; check(client.begin(1000), "ttl init"); challenge(client);
    mock::millis = 119001; check(client.otpSecondsRemaining() == 1, "ceil ttl");
    mock::millis = 120000; check(client.otpSecondsRemaining() == 0, "deadline ttl");
    check(client.submitOtp("123456", 1000) == povo::auth::State::Error &&
        std::string(client.error()) == "otp_expired" && mock::requests.size() == 2, "expired OTP not transmitted");
  }
  mock::reset(); seed(900);
  {
    const auto previous = mock::storage["session"];
    mock::push("", -1);
    povo::Client client; check(!client.begin(1000), "expired token refresh fails");
    check(client.hasSession() && mock::storage["session"] == previous, "transient failure retains saved token");
    mock::push(authResponse(4000)); mock::push("{\"widgets\":[]}");
    std::string body;
    check(client.fetchPlan(body, 1000) && client.authenticated(), "retry recovers without OTP");
    check(mock::requests.back().headers.at("X-AUTH") == jwt(4000), "new token sent after saved refresh");
  }
  mock::reset(); seed(4000);
  {
    povo::Client client; check(client.begin(1000), "logout failure init");
    mock::saveFails = true; mock::push("", 403);
    std::string body = "previous";
    check(!client.fetchPlan(body, 1000) && !client.hasSession(), "403 clears RAM session");
    check(std::string(client.error()) == "session_save_failed" && body == "previous", "clear save failure visible");
  }
  mock::reset(); seed(2000);
  {
    povo::Client client; check(client.begin(1000), "save failure init");
    const auto previous = mock::storage["session"];
    mock::saveFails = true; mock::push(authResponse(4000));
    std::string body;
    check(!client.fetchPlan(body, 1950) && client.hasSession() && !client.authenticated(), "refresh save failure not success");
    check(mock::storage["session"] == previous, "failed candidate never persisted");
    mock::saveFails = false; mock::push(authResponse(4000)); mock::push("{}");
    check(client.fetchPlan(body, 1950), "refresh retry succeeds");
    check(mock::requests[1].headers.at("X-AUTH") == jwt(2000), "failed candidate never published");
  }
  mock::reset(); seed(4000);
  {
    povo::Client client; check(client.begin(1000), "chunk init");
    mock::Response response;
    response.transfer = "chunked"; response.length = -1;
    response.body = "2\r\n{}\r\n0\r\nX-Test: yes\r\n\r\n";
    mock::responses.push_back(response);
    std::string body;
    check(client.fetchPlan(body, 1000) && body == "{}", "valid chunk with trailer");
    for (const char* encoded : {"10001\r\n", "xx\r\n", "2\r\n{", "2\r\n{}xx0\r\n\r\n", "0\r\n\r\n"}) {
      response.body = encoded; mock::responses.push_back(response); body = "previous";
      check(!client.fetchPlan(body, 1000) && body == "previous", "bad chunk rejected without output mutation");
    }
    response.body = std::string(129, '0') + "\r\n";
    mock::responses.push_back(response); check(!client.fetchPlan(body, 1000), "chunk header bounded");
    response.body = "10000\r\n" + std::string(65536, 'x') + "\r\n1\r\nx\r\n0\r\n\r\n";
    mock::responses.push_back(response); check(!client.fetchPlan(body, 1000), "cumulative chunks bounded");
    response.body = "1\r\nx\r\n";
    for (unsigned i = 0; i < 14000; ++i) response.body += "1\r\nx\r\n";
    response.body += "0\r\n\r\n";
    mock::responses.push_back(response); check(!client.fetchPlan(body, 1000), "chunk framing total bounded");
    response.transfer = ""; response.body = "{"; response.length = 2;
    mock::responses.push_back(response); check(!client.fetchPlan(body, 1000), "truncated content length");
    response.body = "{}"; response.length = 65537;
    mock::responses.push_back(response); check(!client.fetchPlan(body, 1000), "excessive content length");
    response.body = std::string(65536, 'x'); response.length = 65536;
    mock::responses.push_back(response); check(client.fetchPlan(body, 1000) && body.size() == 65536, "exact body limit accepted");
    response.body = "{}"; response.length = 2; response.encoding = "gzip";
    mock::responses.push_back(response); check(!client.fetchPlan(body, 1000), "compressed body refused");
    response.encoding.clear();
    response.body = "{}"; response.length = -1; response.holdOpen = true;
    mock::responses.push_back(response);
    const auto start = mock::millis;
    check(!client.fetchPlan(body, 1000) && mock::millis - start == 20000, "hard body deadline");
  }
  mock::reset(); seed(4000);
  {
    povo::Client client; check(client.begin(1000), "401 init");
    mock::push("", 401); mock::push(authResponse(5000)); mock::push("", 401);
    std::string body;
    check(!client.fetchPlan(body, 1000) && !client.hasSession() && mock::requests.size() == 3, "401 retries exactly once");
  }
  mock::reset(); mock::openFails = true;
  {
    povo::Client client; check(!client.begin(1000), "storage init fails");
    mock::openFails = false; check(client.begin(1000), "storage init retry");
  }
  std::cout << "client tests passed\n";
}
