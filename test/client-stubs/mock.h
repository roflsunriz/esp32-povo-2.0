#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& value) : std::string(value) {}
  void toLowerCase() {
    for (char& c : *this) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
};
namespace mock {
struct Response {
  int code = 200;
  std::string body;
  int length = -2;
  std::string transfer, encoding;
  bool holdOpen = false;
};
struct Request {
  std::string url, body;
  std::map<std::string, std::string> headers;
  bool tls = false, noRedirect = false;
};
inline uint64_t millis = 0;
inline bool openFails = false, saveFails = false, wifi = true;
inline std::map<std::string, std::string> storage;
inline std::deque<Response> responses;
inline std::vector<Request> requests;
inline void reset() {
  millis = 0; openFails = saveFails = false; wifi = true;
  storage.clear(); responses.clear(); requests.clear();
}
inline void push(const std::string& body, int code = 200) {
  Response response; response.body = body; response.code = code;
  responses.push_back(response);
}
}
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(mock::millis * 1000); }
inline void delay(unsigned milliseconds) { mock::millis += milliseconds; }
inline void esp_fill_random(void* data, size_t size) {
  static unsigned counter = 1;
  auto* bytes = static_cast<uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(counter++);
}
constexpr int WL_CONNECTED = 3;
struct MockWifi { int status() const { return mock::wifi ? WL_CONNECTED : 0; } };
inline MockWifi WiFi;
class Preferences {
 public:
  bool begin(const char*, bool) { return !mock::openFails; }
  size_t putString(const char* key, const char* value) {
    if (mock::saveFails) return 0;
    mock::storage[key] = value;
    return std::strlen(value);
  }
  size_t getString(const char* key, char* value, size_t size) {
    const auto it = mock::storage.find(key);
    if (it == mock::storage.end() || it->second.size() + 1 > size) return 0;
    std::memcpy(value, it->second.c_str(), it->second.size() + 1);
    return it->second.size() + 1;
  }
};
class WiFiClientSecure {
 public:
  bool tls = false;
  mock::Response response;
  size_t position = 0;
  void setCACert(const char* value) { tls = value && *value; }
  void setHandshakeTimeout(unsigned) {}
  int available() { return static_cast<int>(response.body.size() - position); }
  bool connected() { return available() != 0 || response.holdOpen; }
  int read() { return available() ? static_cast<unsigned char>(response.body[position++]) : -1; }
};
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS = 0;
class HTTPClient {
 public:
  void setConnectTimeout(unsigned) {}
  void setTimeout(unsigned) {}
  void setReuse(bool) {}
  void setFollowRedirects(int mode) { request_.noRedirect = mode == 0; }
  bool begin(WiFiClientSecure& transport, const String& url) {
    transport_ = &transport; request_.url = url; request_.tls = transport.tls;
    return true;
  }
  void addHeader(const String& key, const String& value) { request_.headers[key] = value; }
  void collectHeaders(const char**, size_t) {}
  int POST(const String& body) { request_.body = body; return GET(); }
  int GET() {
    if (mock::responses.empty()) throw std::runtime_error("unexpected HTTP request");
    mock::requests.push_back(request_);
    transport_->response = mock::responses.front(); mock::responses.pop_front();
    return transport_->response.code;
  }
  int getSize() const {
    const auto& response = transport_->response;
    return response.length == -2 ? static_cast<int>(response.body.size()) : response.length;
  }
  String header(const char* name) const {
    return std::strcmp(name, "Transfer-Encoding") == 0 ? transport_->response.transfer : transport_->response.encoding;
  }
  void end() {}
 private:
  WiFiClientSecure* transport_ = nullptr;
  mock::Request request_;
};
