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
  virtual ~TFT_eSPI() = default;
  void init() {}
  void setRotation(int rotation) {
    if (rotation != 1 && rotation != 3) throw std::runtime_error("wrong rotation");
    width_ = 320; height_ = 240;
  }
  void setTextColor(uint16_t, uint16_t) {}
  // 実TFT_eSPIと同様、基底fillScreenは非virtualの初期幅240を参照する。
  void fillScreen(uint16_t color) {
    fillRect(0, 0, width_, height_, color);
    ui::glyphCount = ui::missing = 0;
  }
  virtual void drawPixel(int x, int y, uint16_t color) { ui::pixel(x, y, color); }
  virtual void fillRect(int x, int y, int width, int height, uint16_t color) {
    for (int row = 0; row < height; ++row)
      for (int col = 0; col < width; ++col) drawPixel(x + col, y + row, color);
  }
  void writecommand(uint8_t) {}
  void drawChar(uint16_t, int, int, int) {
    throw std::runtime_error("legacy font must not be mixed with 16px glyphs");
  }
  void drawBitmap(int x, int y, const uint8_t* data, int width, int height, uint16_t color) {
    if (height != 16) throw std::runtime_error("inconsistent glyph height");
    ++ui::glyphCount;
    for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col)
      if ((data[row * ((width + 7) / 8) + col / 8] >> (7 - col % 8)) & 1)
        drawPixel(x + col, y + row, color);
  }
  void drawRect(int x, int y, int width, int height, uint16_t color) {
    for (int col = 0; col < width; ++col) {
      drawPixel(x + col, y, color);
      if (height > 1) drawPixel(x + col, y + height - 1, color);
    }
    for (int row = 1; row + 1 < height; ++row) {
      drawPixel(x, y + row, color);
      if (width > 1) drawPixel(x + width - 1, y + row, color);
    }
  }
 protected:
  int width_ = 240, height_ = 320;
};

class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI*) {}
  void setColorDepth(int) {}
  void* createSprite(int width, int height) {
    if (width != 320 || height != 240) throw std::runtime_error("wrong sprite size");
    return bytes_.data();
  }
  void* getPointer() { return bytes_.data(); }
  void drawPixel(int x, int y, uint16_t color) override {
    if (x < 0 || x >= 320 || y < 0 || y >= 240) return;
    const size_t index = static_cast<size_t>(y * 320 + x);
    spritePixels_[index] = color;
    bytes_[index] = static_cast<uint8_t>(((color & 0xE000) >> 8) |
        ((color & 0x0700) >> 6) | ((color & 0x0018) >> 3));
  }
  void fillRect(int x, int y, int width, int height, uint16_t color) override {
    for (int row = 0; row < height; ++row)
      for (int col = 0; col < width; ++col) drawPixel(x + col, y + row, color);
  }
  bool pushSprite(int dx, int dy, int sx, int sy, int width, int height) {
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 ||
        sx + width > 320 || dx + width > 320 ||
        sy + height > 240 || dy + height > 240) return false;
    for (int row = 0; row < height; ++row)
      for (int col = 0; col < width; ++col)
        ui::pixel(dx + col, dy + row,
                  spritePixels_[static_cast<size_t>((sy + row) * 320 + sx + col)]);
    return true;
  }
  void pushSprite(int x, int y) { (void)pushSprite(x, y, 0, 0, 320, 240); }
 private:
  std::array<uint8_t, 320 * 240> bytes_{};
  std::array<uint16_t, 320 * 240> spritePixels_{};
};
