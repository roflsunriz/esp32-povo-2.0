#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_timer.h>
#include <time.h>
#include "status-display.h"
#include "status-json.h"
#include "ui-text.h"
#if __has_include("device-config.h")
#include "device-config.h"
#else
#include "device-config.example.h"
#endif
namespace {
constexpr uint64_t pollMs = 300000;
povo::Status lastStatus;
bool haveStatus = false, configured = false;
uint64_t lastReceived = 0, nextPoll = 0, nextDraw = 0;
const char* failure = nullptr;
uint64_t monotonicMs() { return esp_timer_get_time() / 1000; }
bool validConfig() {
  String url(POVO_STATUS_URL), token(POVO_READ_TOKEN);
  if (!strlen(POVO_WIFI_SSID) || !strlen(POVO_ROOT_CA) ||
      !url.startsWith("https://") || !url.endsWith("/api/v1/status") ||
      url.indexOf('@') >= 0 || url.indexOf('?') >= 0 || url.indexOf('#') >= 0 ||
      token.length() < 32 || token.length() > 256) return false;
  for (unsigned i = 0; i < token.length(); ++i)
    if (!isalnum((unsigned char)token[i]) && token[i] != '_' && token[i] != '-') return false;
  return true;
}
bool fetch() {
  WiFiClientSecure client;
  client.setCACert(POVO_ROOT_CA); client.setHandshakeTimeout(10);
  HTTPClient http;
  http.setConnectTimeout(10000); http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  if (!http.begin(client, POVO_STATUS_URL)) { failure = povo::text::connection; return false; }
  http.addHeader("Authorization", String("Bearer ") + POVO_READ_TOKEN);
  const int code = http.GET();
  failure = code == 401 ? povo::text::unauthorized : code == 503 ? povo::text::unavailable : povo::text::connection;
  if (code != 200) { http.end(); return false; }
  const int size = http.getSize();
  // The relay supplies Content-Length; reject unbounded/chunked responses.
  if (size <= 0 || size > 4096) { failure = povo::text::invalid; http.end(); return false; }
  static char body[4097];
  size_t read = 0;
  const uint64_t deadline = monotonicMs() + 10000;
  auto& stream = http.getStream();
  while (read < (size_t)size && monotonicMs() < deadline) {
    if (stream.available()) {
      const int byte = stream.read();
      if (byte >= 0) body[read++] = static_cast<char>(byte);
    } else if (!stream.connected()) break;
    else delay(1);
  }
  http.end();
  povo::Status candidate;
  if (read != (size_t)size || !povo::parseStatus(body, read, candidate)) { failure = povo::text::invalid; return false; }
  lastStatus = candidate; haveStatus = true; lastReceived = monotonicMs(); failure = nullptr;
  return true;
}
}
void setup() {
  beginDisplay();
  configured = validConfig();
  if (!configured) { failure = povo::text::configuring; drawDisplay(nullptr, 0, failure); return; }
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(POVO_WIFI_SSID, POVO_WIFI_PASSWORD);
  configTime(0, 0, POVO_NTP_SERVER);
}
void loop() {
  if (!configured) { delay(1000); return; }
  const uint64_t now = monotonicMs();
  if (WiFi.status() != WL_CONNECTED) failure = povo::text::wifi;
  else if (time(nullptr) < 1700000000) failure = povo::text::clock;
  else if (now >= nextPoll) { fetch(); nextPoll = monotonicMs() + pollMs; nextDraw = 0; }
  if (now >= nextDraw) {
    drawDisplay(haveStatus ? &lastStatus : nullptr, haveStatus ? monotonicMs() - lastReceived : 0, failure);
    nextDraw = monotonicMs() + 60000;
  }
  delay(100);
}
