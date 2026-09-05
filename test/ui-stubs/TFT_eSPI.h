#pragma once
#include <array>
#include <stdexcept>
#include <fstream>
#include <cstdint>
#include <Fonts/glcdfont.c>
namespace ui {
inline std::array<uint16_t, 320 * 240> pixels{};
inline unsigned glyphCount = 0;
inline unsigned missing = 0;
inline void pixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= 320 || y < 0 || y >= 240) throw std::runtime_error("drawing outside display");
  pixels[static_cast<size_t>(y * 320 + x)] = color;
}
inline void ppm(const char* path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot write preview");
  out << "P6\n320 240\n255\n";
  for (uint16_t color : pixels) {
    out.put(static_cast<char>((color >> 11) * 255 / 31));
    out.put(static_cast<char>(((color >> 5) & 63) * 255 / 63));
    out.put(static_cast<char>((color & 31) * 255 / 31));
  }
}
}
class TFT_eSPI {
  uint16_t foreground = 0xffff;
 public:
  void init() {}
  void setRotation(int rotation) { if (rotation != 1) throw std::runtime_error("wrong rotation"); }
  void setTextColor(uint16_t color, uint16_t) { foreground = color; }
  void fillScreen(uint16_t color) { ui::pixels.fill(color); ui::glyphCount = ui::missing = 0; }
  void drawChar(uint16_t c, int x, int y, int) {
    ++ui::glyphCount;
    for (int col = 0; col < 5; ++col) for (int row = 0; row < 8; ++row)
      if ((font[c * 5 + col] >> row) & 1) ui::pixel(x + col, y + row, foreground);
  }
  void drawBitmap(int x, int y, const uint8_t* data, int width, int height, uint16_t color) {
    ++ui::glyphCount;
    for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col)
      if ((data[row * ((width + 7) / 8) + col / 8] >> (7 - col % 8)) & 1)
        ui::pixel(x + col, y + row, color);
  }
  void drawRect(int, int, int, int, uint16_t) { ++ui::missing; ++ui::glyphCount; }
};
