// png.h — schrijft een RGB-pixelbuffer als PNG, zonder bibliotheken (deflate met "stored" blokken: groot, maar
// geldig). Voor de schermafbeeldingen van het frontpaneel in CI (gereedschap/pb-paneel.cpp).
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

namespace png {

inline uint32_t crc(const unsigned char* d, size_t n, uint32_t c = 0xffffffffu) {
  for (size_t i = 0; i < n; ++i) {
    c ^= d[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return c;
}

// px: 0xAARRGGBB per pixel, rij na rij; alpha wordt genegeerd
inline bool schrijf(const char* pad, const uint32_t* px, int w, int h) {
  std::vector<unsigned char> rauw;  // per rij: filter 0, dan RGB
  rauw.reserve((size_t)h * (size_t)(w * 3 + 1));
  for (int y = 0; y < h; ++y) {
    rauw.push_back(0);
    for (int x = 0; x < w; ++x) {
      const uint32_t p = px[(size_t)y * (size_t)w + (size_t)x];
      rauw.push_back((unsigned char)(p >> 16));
      rauw.push_back((unsigned char)(p >> 8));
      rauw.push_back((unsigned char)p);
    }
  }
  std::vector<unsigned char> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (unsigned char c : rauw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  for (size_t o = 0; o < rauw.size(); o += 65535) {  // rauw is nooit leeg (elke rij heeft een filterbyte)
    const size_t n = rauw.size() - o < 65535 ? rauw.size() - o : 65535;
    z.push_back(o + n >= rauw.size() ? 1 : 0);
    z.push_back((unsigned char)n); z.push_back((unsigned char)(n >> 8));
    z.push_back((unsigned char)~n); z.push_back((unsigned char)(~n >> 8));
    z.insert(z.end(), rauw.begin() + (long)o, rauw.begin() + (long)(o + n));
  }
  const uint32_t adler = (b << 16) | a;
  for (int s = 24; s >= 0; s -= 8) z.push_back((unsigned char)(adler >> s));

  FILE* f = std::fopen(pad, "wb");
  if (!f) return false;
  auto be32 = [&](uint32_t v) { unsigned char q[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16),
                                                     (unsigned char)(v >> 8), (unsigned char)v};
                                std::fwrite(q, 1, 4, f); };
  auto blok = [&](const char* soort, const std::vector<unsigned char>& d) {
    be32((uint32_t)d.size());
    std::vector<unsigned char> t(soort, soort + 4);
    t.insert(t.end(), d.begin(), d.end());
    std::fwrite(t.data(), 1, t.size(), f);
    be32(crc(t.data(), t.size()) ^ 0xffffffffu);
  };
  std::fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  std::vector<unsigned char> ihdr = {(unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8),
                                     (unsigned char)w, (unsigned char)(h >> 24), (unsigned char)(h >> 16),
                                     (unsigned char)(h >> 8), (unsigned char)h, 8, 2, 0, 0, 0};
  blok("IHDR", ihdr);
  blok("IDAT", z);
  blok("IEND", {});
  return std::fclose(f) == 0;
}

}  // namespace png
