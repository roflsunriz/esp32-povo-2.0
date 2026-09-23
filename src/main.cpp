#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <time.h>
#ifdef ARDUINO
#include <Preferences.h>
#endif
#include "povo-client.h"
#include "direct-status.h"
#include "setup-portal.h"
#include "status-display.h"
#include "ui-text.h"
#if __has_include("device-config.h")
#include "device-config.h"
#else
#include "device-config.example.h"
#endif
namespace {
povo::Client client;
povo::Status status;
bool configured = false, started = false, haveStatus = false;
uint64_t nextPoll = 0, nextDraw = 0, receivedAt = 0, nextInit = 0, nextWifiRetry = 0;
uint64_t savedSpanMs = 0, savedExpiryMs = 0;
const char* failure = nullptr;
uint64_t ms() { return esp_timer_get_time() / 1000; }
#ifdef ARDUINO
void loadSpan() {
  Preferences prefs;
  if (!prefs.begin("povo-display", true)) return;
  savedSpanMs = static_cast<uint64_t>(prefs.getULong64("span_ms", 0));
  savedExpiryMs = static_cast<uint64_t>(prefs.getULong64("expiry_ms", 0));
  prefs.end();
}
void storeSpan() {
  Preferences prefs;
  if (!prefs.begin("povo-display", false)) return;
  prefs.putULong64("span_ms", savedSpanMs);
  prefs.putULong64("expiry_ms", savedExpiryMs);
  prefs.end();
}
#else
void loadSpan() {}
void storeSpan() {}
#endif
}
void setup() {
  beginDisplay();
  loadSpan();
  configured = strlen(POVO_WIFI_SSID) && strlen(POVO_ROOT_CA);
  if (!configured) { drawDisplay(nullptr, 0, povo::text::configuring, 0, false); return; }
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  WiFi.begin(POVO_WIFI_SSID, POVO_WIFI_PASSWORD);
  configTime(0, 0, POVO_NTP_SERVER);
}
void loop() {
  if (!configured) { delay(1000); return; }
  servicePortal();
  const uint64_t now = ms();
  if (portalActive()) { delay(5); return; }
  pollDisplayInput(now);
  updateDisplayPower(now);
  if (WiFi.status() != WL_CONNECTED) {
    failure = povo::text::wifi;
    if (now >= nextWifiRetry) {
      WiFi.begin(POVO_WIFI_SSID, POVO_WIFI_PASSWORD);
      nextWifiRetry = now + 10000;
    }
  }
  else if (time(nullptr) < 1700000000) failure = povo::text::clock;
  else {
    if (!started && now >= nextInit) {
      started = client.begin(time(nullptr)) || client.hasSession();
      nextInit = ms() + 30000;
      if (!started) failure = povo::text::storageError;
    }
    if (started && !client.hasSession()) {
      if (!portalActive()) beginPortal(client);
      nextPoll = 0; failure = povo::text::unauthorized;
    } else if (started && !portalActive() && now >= nextPoll) {
      const uint64_t clockMs = static_cast<uint64_t>(time(nullptr)) * 1000;
      const uint64_t knownExpiry = haveStatus ? status.expiryAtMs : savedExpiryMs;
      if (displayAwake())
        drawDisplay(haveStatus ? &status : nullptr, haveStatus ? ms() - receivedAt : 0,
                    failure, 0, true);
      client.setCritical(povo::isCritical(knownExpiry, clockMs));
      std::string body;
      povo::Status candidate;
      if (!client.fetchPlan(body, time(nullptr))) failure = povo::text::connection;
      else if (!povo::parseDirectStatus(body, static_cast<uint64_t>(time(nullptr)) * 1000, candidate))
        failure = povo::text::invalid;
      else {
        const uint64_t baseSpan = haveStatus ? status.spanMs : savedSpanMs;
        const uint64_t baseExpiry = haveStatus ? status.expiryAtMs : savedExpiryMs;
        candidate.spanMs = povo::updateSpan(baseSpan, baseExpiry, candidate.expiryAtMs,
                                            candidate.serverTimeMs);
        status = candidate; haveStatus = true; receivedAt = ms(); failure = nullptr;
        savedSpanMs = status.spanMs; savedExpiryMs = status.expiryAtMs;
        storeSpan();
      }
      // リニュー後は期限が遠のき通常間隔へ自動復帰する。取得失敗時も
      // 重点期間中は短間隔で再試行し、低速回線での取りこぼしを補う。
      const uint64_t afterExpiry = haveStatus ? status.expiryAtMs : 0;
      nextPoll = ms() + povo::pollIntervalMs(afterExpiry, clockMs,
                                             pollIntervalSec() * 1000);
      nextDraw = 0;
    }
  }
  if (!portalActive() && displayAwake() && now >= nextDraw) {
    drawDisplay(haveStatus ? &status : nullptr, haveStatus ? ms() - receivedAt : 0, failure,
                nextPoll > now ? nextPoll - now : 0, false);
    nextDraw = ms() + 1000;
  }
  delay(5);
}
