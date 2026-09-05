#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <time.h>
#include "setup-portal.h"
#include "setup-page.h"
#include "status-display.h"
#include "ui-text.h"

namespace {
WebServer server(IPAddress(192, 168, 4, 1), 80);
povo::Client* api = nullptr;
bool active = false;
uint64_t stopAt = 0;
String token;
String randomText(unsigned count) {
  static const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  String value; value.reserve(count);
  while (count--) value += chars[esp_random() % (sizeof(chars) - 1)];
  return value;
}
void response(int code, const char* state, const char* message) {
  StaticJsonDocument<512> doc;
  doc["state"] = state; doc["message"] = message;
  if (api) doc["ttl"] = api->otpSecondsRemaining();
  String body; serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}
void submit(bool otp) {
  if (server.hostHeader() != "192.168.4.1" || server.header("X-Setup-Token") != token) {
    response(403, "error", "設定画面を開き直してください。"); return;
  }
  if (server.arg("plain").length() > 1024) {
    response(413, "error", "入力が長すぎます。"); return;
  }
  if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) {
    response(503, "error", "本体のWi-Fi接続と時刻同期を待ってから、もう一度お試しください。"); return;
  }
  StaticJsonDocument<1536> doc;
  if (deserializeJson(doc, server.arg("plain")) || !doc[otp ? "otp" : "email"].is<const char*>()) {
    response(400, "error", "入力内容を確認してください。"); return;
  }
  const std::string value = doc[otp ? "otp" : "email"].as<const char*>();
  if (otp && (value.size() != 6 || value.find_first_not_of("0123456789") != std::string::npos)) {
    response(400, "error", "6桁のコードを入力してください。"); return;
  }
  const auto state = otp ? api->submitOtp(value, time(nullptr)) : api->startLogin(value, time(nullptr));
  if (state == povo::auth::State::Authenticated) {
    response(200, "done", "ログインしました。認証を保存しました。通常のWi-Fiへ戻ってください。");
    stopAt = esp_timer_get_time() / 1000 + 2000;
  } else if (state == povo::auth::State::AwaitingOtp && !otp) {
    response(200, "otp", "コードを送信しました。今回届いたコードを2分以内に入力してください。");
  } else if (state == povo::auth::State::AdditionalAuth) {
    response(409, "error", "追加の認証が必要です。この認証方式には現在対応していません。");
  } else {
    const char* reason = api->error();
    const char* message = "認証できませんでした。コードと接続を確認し、もう一度お試しください。";
    if (!strcmp(reason, "resend_too_soon")) message = "再送は前回の送信から30秒後にできます。直前に届いたコードは引き続き使えます。";
    else if (!strcmp(reason, "otp_expired")) message = "コードの期限が切れました。「コードを送る」で新しいコードを取得してください。";
    else if (!strcmp(reason, "session_save_failed") || !strcmp(reason, "storage_unavailable")) message = "本体に認証を保存できません。本体を再起動してからお試しください。";
    else if (!strcmp(reason, "additional_auth_required")) message = "追加の認証が必要です。この認証方式には現在対応していません。";
    response(400, "error", message);
  }
}
}
void beginPortal(povo::Client& client) {
  if (active) return;
  api = &client; token = randomText(32); stopAt = 0;
  const String password = randomText(16);
  const String ssid = String("povo-setup-") + randomText(4);
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(ssid.c_str(), password.c_str(), 1, false, 1)) {
    drawDisplay(nullptr, 0, povo::text::portalError); return;
  }
  const char* headers[] = {"X-Setup-Token"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, [] {
    String page(povo::setupPage); page.replace("{{TOKEN}}", token);
    page.replace("{{INITIAL_MESSAGE}}", !strcmp(api->error(), "session_save_failed")
        ? "本体の認証保存に失敗しました。再起動後、もう一度ログインしてください。" : "");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8", page);
  });
  server.on("/api/login", HTTP_POST, [] { submit(false); });
  server.on("/api/otp", HTTP_POST, [] { submit(true); });
  server.onNotFound([] { response(404, "error", "設定ページが見つかりません。"); });
  server.begin(); active = true;
  drawSetup(ssid.c_str(), password.c_str());
}
void servicePortal() {
  if (!active) return;
  server.handleClient();
  if (stopAt && static_cast<uint64_t>(esp_timer_get_time() / 1000) >= stopAt) {
    server.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
    active = false; token = "";
  }
}
bool portalActive() { return active; }
