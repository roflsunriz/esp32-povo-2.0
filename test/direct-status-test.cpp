#include <cassert>
#include <fstream>
#include <iterator>
#include "direct-status.h"

int main(int argc, char** argv) {
  uint64_t ms = 0;
  assert(povo::parseJapaneseExpiry("2026年 9月 5日 午後4:42\n残り 7 日間", ms));
  assert(ms == 1788594120000ULL);
  uint64_t noon, midnight;
  assert(povo::parseJapaneseExpiry("2026年 9月 5日 午後12:00", noon));
  assert(povo::parseJapaneseExpiry("2026年 9月 5日 午前12:00", midnight));
  assert(noon - midnight == 12 * 3600000ULL);
  assert(!povo::parseJapaneseExpiry("2026年 2月 29日 午後4:42", ms));
  assert(!povo::parseJapaneseExpiry("2026年 9月 5日 午後13:42", ms));
  assert(!povo::parseJapaneseExpiry("2026年 9月 5日 午後4:62", ms));
  assert(!povo::parseJapaneseExpiry("2026年 9月 5日 午後4:42 garbage", ms));
  const std::string item = R"({"type":"povo-tile-plan-detail","data":{"name":{"value":"適用中"},"remaining":{"value":"使い放題"},"expiry":{"value":"2026年 9月 5日 午後4:42\n残り 7 日間"}}})";
  povo::Status status;
  assert(povo::parseDirectStatus("{\"widgets\":[{\"components\":[" + item + "]}]}", 1788500000000ULL, status));
  assert(status.expiryAtMs == 1788594120000ULL);
  assert(!povo::parseDirectStatus("{\"code\":1,\"widgets\":[]}", 1788500000000ULL, status));
  assert(!povo::parseDirectStatus("{\"error\":{},\"widgets\":[]}", 1788500000000ULL, status));
  assert(status.expiryAtMs == 1788594120000ULL);
  assert(status.expirySource == povo::ExpirySource::Server);
  assert(!povo::parseDirectStatus("{}", 1788500000000ULL, status));
  assert(status.expiryAtMs == 1788594120000ULL);
  assert(povo::parseDirectStatus("{\"widgets\":[]}", 1788500000000ULL, status));
  assert(status.expiryAtMs == 0);
  if (argc > 1) {
    std::ifstream input(argv[1], std::ios::binary);
    const std::string actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    assert(input.good() || input.eof());
    assert(povo::parseDirectStatus(actual, 1788500000000ULL, status));
    assert(status.expiryAtMs > 1788500000000ULL);
  }
}
