#include <iostream>
#include <stdexcept>
#include "../src/status-display.cpp"

void checkLine(const std::string& value) {
  unsigned expected = 0;
  for (unsigned char c : value) if ((c & 0xc0) != 0x80) ++expected;
  ui::glyphCount = ui::missing = 0;
  line(0, value.c_str());
  if (ui::glyphCount != expected) throw std::runtime_error("clipped text: " + value);
  if (ui::missing) throw std::runtime_error("missing glyph: " + value);
}
int main(int argc, char** argv) {
  using namespace povo;
  try {
    beginDisplay();
    const char* messages[] = {text::title, text::unknown, text::pending, text::stale,
      text::noStatus, text::configuring, text::wifi, text::clock, text::connection,
      text::unauthorized, text::unavailable, text::invalid, text::directMode, text::precision,
      text::setupTitle, text::setupWifi, text::setupOpen, text::portalError, text::storageError};
    for (const char* message : messages) { checkLine(message); drawDisplay(nullptr, 0, message); }
    for (const char* source : text::sources) checkLine(std::string(text::expiry) + "12/31 23:59 JST [" + source + "]");
    char buffer[96];
    snprintf(buffer, sizeof(buffer), text::remaining, 2932896ULL, 23ULL); checkLine(buffer);
    snprintf(buffer, sizeof(buffer), text::minutes, 70389527ULL, 59ULL); checkLine(buffer);
    snprintf(buffer, sizeof(buffer), text::sync, 4223371679ULL); checkLine(buffer);
    Status s;
    s.serverTimeMs = 1788572400000ULL; s.receivedAtMs = s.serverTimeMs;
    s.expiryAtMs = s.serverTimeMs + 3 * 86400000ULL + 14 * 3600000ULL + 12 * 60000;
    s.expirySource = ExpirySource::Server;
    for (int source = 0; source < 2; ++source) {
      s.expirySource = static_cast<ExpirySource>(source);
      for (uint64_t elapsed : {0ULL, 900000ULL, 500000000ULL}) {
        drawDisplay(&s, elapsed, text::connection);
        if (ui::missing) throw std::runtime_error("unknown glyph in display");
      }
    }
    s.expirySource = ExpirySource::Server;
    drawDisplay(&s, 120000, nullptr);
    if (argc > 1) ui::ppm(argv[1]);
    drawSetup("povo-setup-ABCD", "ABCDEFGH23456789");
    if (ui::missing) throw std::runtime_error("unknown glyph in setup display");
    if (argc > 2) ui::ppm(argv[2]);
    s.expiryAtMs = 0; drawDisplay(&s, 0, nullptr);
    if (ui::missing) throw std::runtime_error("unknown glyph in null expiry");
    std::cout << "UI bounds, full text, glyph coverage, expiry states and setup screen passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
