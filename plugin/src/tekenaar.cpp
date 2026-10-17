// tekenaar.cpp — zie tekenaar.h. Lettertypen: IBM Plex Mono en Silkscreen (SIL OFL 1.1, plugin/fonts/), als bytes
// in de plugin gebakken (CMake maakt fonts_data.cpp), zodat geen bundel of systeemfont nodig is.
#include "tekenaar.h"

#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

extern const unsigned char pb_font_plexmono[];
extern const unsigned char pb_font_silkscreen[];

namespace paneel {

struct Lettertype {
  stbtt_fontinfo info;
  explicit Lettertype(const unsigned char* data) { stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)); }
};

const Lettertype* plexMono() { static const Lettertype f(pb_font_plexmono); return &f; }
const Lettertype* silkscreen() { static const Lettertype f(pb_font_silkscreen); return &f; }

namespace {
double klem01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// één codepunt uit UTF-8; schuift p op
int volgend(const char*& p) {
  const unsigned char c = (unsigned char)*p++;
  if (c < 0x80) return c;
  int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1, cp = c & (0x3f >> n);
  while (n-- > 0 && (*p & 0xc0) == 0x80) cp = (cp << 6) | (*p++ & 0x3f);
  return cp;
}
}  // namespace

void Beeld::maat(int w, int h) {
  w_ = w > 0 ? w : 1;
  h_ = h > 0 ? h : 1;
  px_.assign((size_t)w_ * (size_t)h_, 0xff000000u);
}

void Beeld::vul(Kleur k) {
  for (auto& p : px_) p = 0xff000000u | k;
}

void Beeld::meng(int x, int y, Kleur k, double a) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_ || a <= 0) return;
  uint32_t& p = px_[(size_t)y * (size_t)w_ + (size_t)x];
  if (a >= 1) { p = 0xff000000u | k; return; }
  auto kanaal = [&](int s) {
    const double o = (p >> s) & 0xff, n = (k >> s) & 0xff;
    return (uint32_t)(o + (n - o) * a + 0.5) << s;
  };
  p = 0xff000000u | kanaal(16) | kanaal(8) | kanaal(0);
}

void Beeld::rechthoek(double x0, double y0, double x1, double y1, Kleur k) {
  const int a = (int)std::lround(x0), b = (int)std::lround(y0), c = (int)std::lround(x1), d = (int)std::lround(y1);
  for (int y = b < 0 ? 0 : b; y < d && y < h_; ++y)
    for (int x = a < 0 ? 0 : a; x < c && x < w_; ++x) px_[(size_t)y * (size_t)w_ + (size_t)x] = 0xff000000u | k;
}

void Beeld::schijf(double cx, double cy, double r, Kleur k) {
  for (int y = (int)std::floor(cy - r - 1); y <= (int)std::ceil(cy + r + 1); ++y)
    for (int x = (int)std::floor(cx - r - 1); x <= (int)std::ceil(cx + r + 1); ++x) {
      const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
      meng(x, y, k, klem01(r - d + 0.5));
    }
}

void Beeld::ring(double cx, double cy, double r, double dikte, Kleur k) {
  const double R = r + dikte / 2 + 1;
  for (int y = (int)std::floor(cy - R); y <= (int)std::ceil(cy + R); ++y)
    for (int x = (int)std::floor(cx - R); x <= (int)std::ceil(cx + R); ++x) {
      const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
      meng(x, y, k, klem01(dikte / 2 - std::fabs(d - r) + 0.5));
    }
}

void Beeld::boog(double cx, double cy, double r, double dikte, double a0, double a1, Kleur k) {
  if (a1 <= a0) return;
  const double R = r + dikte / 2 + 1;
  for (int y = (int)std::floor(cy - R); y <= (int)std::ceil(cy + R); ++y)
    for (int x = (int)std::floor(cx - R); x <= (int)std::ceil(cx + R); ++x) {
      const double dx = x + 0.5 - cx, dy = y + 0.5 - cy, d = std::hypot(dx, dy);
      const double radiaal = klem01(dikte / 2 - std::fabs(d - r) + 0.5);
      if (radiaal <= 0) continue;
      const double th = std::atan2(dx, -dy);  // met de klok mee vanaf boven, (−π, π]
      const double langs = std::fmin(th - a0, a1 - th) * d;  // booglengte tot het dichtste uiteinde
      meng(x, y, k, radiaal * klem01(langs + 0.5));
    }
}

void Beeld::lijn(double x0, double y0, double x1, double y1, double dikte, Kleur k) {
  const double R = dikte / 2 + 1;
  const double lx = x1 - x0, ly = y1 - y0, l2 = lx * lx + ly * ly;
  for (int y = (int)std::floor(std::fmin(y0, y1) - R); y <= (int)std::ceil(std::fmax(y0, y1) + R); ++y)
    for (int x = (int)std::floor(std::fmin(x0, x1) - R); x <= (int)std::ceil(std::fmax(x0, x1) + R); ++x) {
      const double px = x + 0.5 - x0, py = y + 0.5 - y0;
      const double t = l2 > 0 ? klem01((px * lx + py * ly) / l2) : 0;
      const double d = std::hypot(px - t * lx, py - t * ly);
      meng(x, y, k, klem01(dikte / 2 - d + 0.5));
    }
}

double Beeld::tekstBreedte(const Lettertype* f, double px, const char* t) {
  const float s = stbtt_ScaleForPixelHeight(&f->info, (float)px);
  double b = 0;
  int vorig = 0;
  for (const char* p = t; *p;) {
    const int cp = volgend(p);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    if (vorig) b += s * stbtt_GetCodepointKernAdvance(&f->info, vorig, cp);
    b += s * adv;
    vorig = cp;
  }
  return b;
}

double Beeld::tekst(const Lettertype* f, double px, double x, double y, const char* t, Kleur k, bool midden) {
  const double b = tekstBreedte(f, px, t);
  if (midden) x -= b / 2;
  const float s = stbtt_ScaleForPixelHeight(&f->info, (float)px);
  double pen = x;
  int vorig = 0;
  for (const char* p = t; *p;) {
    const int cp = volgend(p);
    if (vorig) pen += s * stbtt_GetCodepointKernAdvance(&f->info, vorig, cp);
    const double gx = std::floor(pen), gy = std::floor(y);
    const float sx = (float)(pen - gx), sy = (float)(y - gy);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBoxSubpixel(&f->info, cp, s, s, sx, sy, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
      glyph_.assign((size_t)gw * (size_t)gh, 0);
      stbtt_MakeCodepointBitmapSubpixel(&f->info, glyph_.data(), gw, gh, gw, s, s, sx, sy, cp);
      for (int j = 0; j < gh; ++j)
        for (int i = 0; i < gw; ++i)
          meng((int)gx + x0 + i, (int)gy + y0 + j, k, glyph_[(size_t)j * (size_t)gw + (size_t)i] / 255.0);
    }
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    pen += s * adv;
    vorig = cp;
  }
  return b;
}

}  // namespace paneel
