#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <time.h>
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
const char* failure = nullptr;
uint64_t ms() { return esp_timer_get_time() / 1000; }
}
void setup() {
  beginDisplay();
  configured = strlen(POVO_WIFI_SSID) && strlen(POVO_ROOT_CA);
  if (!configured) { drawDisplay(nullptr, 0, povo::text::configuring); return; }
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  WiFi.begin(POVO_WIFI_SSID, POVO_WIFI_PASSWORD);
  configTime(0, 0, POVO_NTP_SERVER);
}
void loop() {
  if (!configured) { delay(1000); return; }
  servicePortal();
  const uint64_t now = ms();
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
      std::string body;
      povo::Status candidate;
      if (!client.fetchPlan(body, time(nullptr))) failure = povo::text::connection;
      else if (!povo::parseDirectStatus(body, static_cast<uint64_t>(time(nullptr)) * 1000, candidate))
        failure = povo::text::invalid;
      else {
        status = candidate; haveStatus = true; receivedAt = ms(); failure = nullptr;
      }
      nextPoll = ms() + 300000; nextDraw = 0;
    }
  }
  if (!portalActive() && now >= nextDraw) {
    drawDisplay(haveStatus ? &status : nullptr, haveStatus ? ms() - receivedAt : 0, failure);
    nextDraw = ms() + 60000;
  }
  delay(5);
}
