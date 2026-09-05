#pragma once
#include <Preferences.h>
#include <cstdint>
#include <string>
#include "auth-protocol.h"

namespace povo {
class Client {
 public:
  bool begin(uint64_t now);
  auth::State startLogin(const std::string& email, uint64_t now);
  auth::State submitOtp(const std::string& otp, uint64_t now);
  bool authenticated() const;
  bool hasSession() const { return !session_.token.empty(); }
  uint32_t otpSecondsRemaining() const;
  bool fetchPlan(std::string& body, uint64_t now);
  void logout();
  const char* error() const { return error_; }
 private:
  Preferences preferences_;
  bool ready_ = false;
  auth::State state_ = auth::State::Unauthenticated;
  auth::Session session_;
  std::string device_, email_, authId_;
  uint64_t otpStartedMs_ = 0, lastSendMs_ = 0;
  bool sentBefore_ = false;
  const char* error_ = "";
  int request(const char* path, const std::string* post, bool authorized, std::string& response);
  bool save(const auth::Session& candidate);
  bool refresh(uint64_t now);
  bool accept(const auth::Reply& reply);
  void clearChallenge();
  void clearSession();
  auth::State fail(const char* message);
};
}
