#pragma once
#include <array>
#include <stdexcept>
#include <fstream>
#include <cstdint>
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
 public:
  void init() {}
  void setRotation(int rotation) { if (rotation != 1) throw std::runtime_error("wrong rotation"); }
  void setTextColor(uint16_t, uint16_t) {}
  void fillScreen(uint16_t color) { ui::pixels.fill(color); ui::glyphCount = ui::missing = 0; }
  void drawChar(uint16_t, int, int, int) {
    throw std::runtime_error("legacy font must not be mixed with 16px glyphs");
  }
  void drawBitmap(int x, int y, const uint8_t* data, int width, int height, uint16_t color) {
    if (height != 16) throw std::runtime_error("inconsistent glyph height");
    ++ui::glyphCount;
    for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col)
      if ((data[row * ((width + 7) / 8) + col / 8] >> (7 - col % 8)) & 1)
        ui::pixel(x + col, y + row, color);
  }
  void drawRect(int, int, int, int, uint16_t) { ++ui::missing; ++ui::glyphCount; }
};
