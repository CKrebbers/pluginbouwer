// paneel.cpp — het frontpaneel: tekenen (in software) en muis → gebaren. Zie paneel.h.
//
// Maten en kleuren komen uit design.md en frontpaneel.svg. De tekening volgt de mockup in zijn eigen eenheden (560 × 220)
// en schaalt met s = min(b / 560, h / 220); het paneelvlak zelf rekt mee met het venster.
#include "paneel.h"

#include <cmath>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb/stb_truetype.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// ingebakken lettertypen (CMake maakt paneel_fonts.cpp uit plugin/extern/fonts/*.ttf)
extern const unsigned char pb_font_plexmono[];
extern const unsigned char pb_font_silkscreen[];

namespace pb_paneel {
namespace {

// --- design.md: tokens --------------------------------------------------------------------------------------------
struct Kleur { uint8_t r, g, b; };
constexpr Kleur PANEEL{0x23, 0x26, 0x29};
constexpr Kleur INKT{0xe7, 0xe5, 0xdd};
constexpr Kleur TAPE{0xf2, 0xc2, 0x00};
constexpr Kleur SPOOR{0x40, 0xcb, 0xda};
constexpr Kleur GROEF{0x3a, 0x3e, 0x42};
constexpr Kleur SCHADUW{0x0d, 0x0e, 0x0f};

// --- frontpaneel.svg: geometrie (in mockup-eenheden) --------------------------------------------------------------
constexpr double KOLOM = 76;            // kolom per knop
constexpr double KNOP0_X = 62, KNOP_Y = 130;
constexpr double BOOG_R = 22, BOOG_W = 3;
constexpr double KNOP_R = 16, RAND_W = 2;
constexpr double WIJZER_R = 14, WIJZER_W = 2;
constexpr double LABEL_Y = -34, WAARDE_Y = 44;
constexpr double TEKST_PX = 11, NAAM_PX = 14;
constexpr double HOEK = 135;            // 270°: −135° tot +135°, 0 = boven
constexpr double SCHADUW_D = 3;
constexpr double SLEEP_PX = 200;        // 200 px omhoog = het hele bereik
constexpr double FIJN = 10;             // Shift: 10× fijner
constexpr double PI = 3.14159265358979323846;

double klem01(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

// --- een canvas in pixels -----------------------------------------------------------------------------------------
struct Doek {
  uint8_t* p;
  int b, h;

  void meng(int x, int y, Kleur k, double a) {
    if (x < 0 || y < 0 || x >= b || y >= h || a <= 0) return;
    if (a > 1) a = 1;
    uint8_t* q = p + 4 * ((size_t)y * b + x);
    q[0] = (uint8_t)(q[0] + (k.r - q[0]) * a + 0.5);
    q[1] = (uint8_t)(q[1] + (k.g - q[1]) * a + 0.5);
    q[2] = (uint8_t)(q[2] + (k.b - q[2]) * a + 0.5);
    q[3] = 255;
  }

  // rechthoek met fractionele randen (dekking = overlap van de pixel met het vlak)
  void vlak(double x0, double y0, double x1, double y1, Kleur k) {
    const int i0 = (int)std::floor(x0), i1 = (int)std::ceil(x1), j0 = (int)std::floor(y0), j1 = (int)std::ceil(y1);
    for (int j = j0; j < j1; ++j) {
      const double dy = std::fmin(y1, j + 1.0) - std::fmax(y0, (double)j);
      if (dy <= 0) continue;
      for (int i = i0; i < i1; ++i) {
        const double dx = std::fmin(x1, i + 1.0) - std::fmax(x0, (double)i);
        if (dx > 0) meng(i, j, k, dx * dy);
      }
    }
  }

  // rand van een rechthoek, lijndikte w gecentreerd op de rand (zoals SVG stroke)
  void rand(double x0, double y0, double x1, double y1, double w, Kleur k) {
    const double h = w / 2;
    vlak(x0 - h, y0 - h, x1 + h, y0 + h, k);
    vlak(x0 - h, y1 - h, x1 + h, y1 + h, k);
    vlak(x0 - h, y0 + h, x0 + h, y1 - h, k);
    vlak(x1 - h, y0 + h, x1 + h, y1 - h, k);
  }

  // een vorm via zijn afstandsfunctie (negatief = binnen), anti-aliasing over één pixel
  template <class F>
  void vorm(double x0, double y0, double x1, double y1, Kleur k, F afstand) {
    const int i0 = (int)std::floor(x0) - 1, i1 = (int)std::ceil(x1) + 1;
    const int j0 = (int)std::floor(y0) - 1, j1 = (int)std::ceil(y1) + 1;
    for (int j = j0; j <= j1; ++j)
      for (int i = i0; i <= i1; ++i) {
        const double a = klem01(0.5 - afstand(i + 0.5, j + 0.5));
        if (a > 0) meng(i, j, k, a);
      }
  }

  void schijf(double cx, double cy, double r, Kleur k) {
    vorm(cx - r, cy - r, cx + r, cy + r, k, [=](double x, double y) { return std::hypot(x - cx, y - cy) - r; });
  }

  void ring(double cx, double cy, double r, double w, Kleur k) {
    const double R = r + w / 2;
    vorm(cx - R, cy - R, cx + R, cy + R, k,
         [=](double x, double y) { return std::fabs(std::hypot(x - cx, y - cy) - r) - w / 2; });
  }

  // boog met rechte uiteinden (SVG butt), hoeken in graden vanaf boven, met de klok mee
  void boog(double cx, double cy, double r, double a0, double a1, double w, Kleur k) {
    if (a1 - a0 < 1e-6) return;  // een boog van lengte 0 tekent SVG niet
    const double R = r + w / 2;
    const double r0 = a0 * PI / 180, r1 = a1 * PI / 180;
    vorm(cx - R, cy - R, cx + R, cy + R, k, [=](double x, double y) {
      const double dx = x - cx, dy = y - cy, d = std::hypot(dx, dy);
      const double radiaal = std::fabs(d - r) - w / 2;
      const double t = std::atan2(dx, -dy);  // 0 = boven, met de klok mee (y omlaag)
      // hoekafstand tot het dichtstbijzijnde uiteinde, als afstand in pixels (negatief = binnen de boog)
      double binnen = std::fmin(t - r0, r1 - t);
      if (binnen > PI / 2) binnen = PI / 2;
      if (binnen < -PI / 2) binnen = -PI / 2;
      return std::fmax(radiaal, -d * std::sin(binnen));
    });
  }

  // rechte lijn met rechte uiteinden
  void lijn(double xa, double ya, double xb, double yb, double w, Kleur k) {
    const double L = std::hypot(xb - xa, yb - ya);
    if (L < 1e-9) return;
    const double ux = (xb - xa) / L, uy = (yb - ya) / L;
    vorm(std::fmin(xa, xb) - w, std::fmin(ya, yb) - w, std::fmax(xa, xb) + w, std::fmax(ya, yb) + w, k,
         [=](double x, double y) {
           const double px = x - xa, py = y - ya;
           const double langs = px * ux + py * uy, dwars = std::fabs(-px * uy + py * ux);
           return std::fmax(dwars - w / 2, std::fmax(-langs, langs - L));
         });
  }
};

// --- tekst --------------------------------------------------------------------------------------------------------
struct Letter {
  stbtt_fontinfo info;
  bool ok = false;
  explicit Letter(const unsigned char* data) {
    ok = stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0;
  }
};

// functie-statics, geen globals (valkuil 16: de AU initialiseert vroeg)
Letter& plexMono() {
  static Letter l(pb_font_plexmono);
  return l;
}
Letter& silkscreen() {
  static Letter l(pb_font_silkscreen);
  return l;
}

int volgendeTeken(const char*& s) {
  const unsigned char c = (unsigned char)*s++;
  if (c < 0x80) return c;
  int n = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : 1);
  int cp = c & (0x3F >> n);
  while (n-- && (*s & 0xC0) == 0x80) cp = (cp << 6) | (*s++ & 0x3F);
  return cp;
}

double tekstBreedte(Letter& l, const char* s, double px) {
  if (!l.ok) return 0;
  const float sc = stbtt_ScaleForMappingEmToPixels(&l.info, (float)px);
  double b = 0;
  int vorige = 0;
  while (*s) {
    const int cp = volgendeTeken(s);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&l.info, cp, &adv, &lsb);
    if (vorige) b += sc * stbtt_GetCodepointKernAdvance(&l.info, vorige, cp);
    b += sc * adv;
    vorige = cp;
  }
  return b;
}

// tekst met de basislijn op y; midden = text-anchor middle, anders start
void tekst(Doek& d, Letter& l, const char* s, double x, double y, double px, Kleur k, bool midden) {
  if (!l.ok) return;
  const float sc = stbtt_ScaleForMappingEmToPixels(&l.info, (float)px);
  double pen = midden ? x - tekstBreedte(l, s, px) / 2 : x;
  const int basis = (int)std::floor(y + 0.5);
  std::vector<unsigned char> bm;
  int vorige = 0;
  while (*s) {
    const int cp = volgendeTeken(s);
    if (vorige) pen += sc * stbtt_GetCodepointKernAdvance(&l.info, vorige, cp);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&l.info, cp, &adv, &lsb);
    const int px0 = (int)std::floor(pen);
    const float shift = (float)(pen - px0);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBoxSubpixel(&l.info, cp, sc, sc, shift, 0, &x0, &y0, &x1, &y1);
    const int bw = x1 - x0, bh = y1 - y0;
    if (bw > 0 && bh > 0) {
      bm.assign((size_t)bw * bh, 0);
      stbtt_MakeCodepointBitmapSubpixel(&l.info, bm.data(), bw, bh, bw, sc, sc, shift, 0, cp);
      for (int j = 0; j < bh; ++j)
        for (int i = 0; i < bw; ++i)
          if (bm[(size_t)j * bw + i]) d.meng(px0 + x0 + i, basis + y0 + j, k, bm[(size_t)j * bw + i] / 255.0);
    }
    pen += sc * adv;
    vorige = cp;
  }
}

}  // namespace

// --- Paneel -------------------------------------------------------------------------------------------------------
Paneel::Paneel(PaneelBron& bron, const Knop* knoppen, int n) : bron_(bron), n_(n > MAX_KNOPPEN ? MAX_KNOPPEN : n) {
  for (int k = 0; k < n_; ++k) {
    knop_[k] = knoppen[k];
    lokaal_[k] = knoppen[k].standaard;
    getekend_[k] = NAN;
  }
}

void Paneel::maat(double b, double h) {
  b_ = b > 1 ? b : 1;
  h_ = h > 1 ? h : 1;
}

double Paneel::schaalOntwerp() const { return std::fmin(b_ / MOCKUP_B, h_ / MOCKUP_H); }

void Paneel::knopMidden(int k, double* x, double* y) const {
  const double s = schaalOntwerp();
  *x = (KNOP0_X + KOLOM * k) * s;
  *y = KNOP_Y * s;
}

void Paneel::knopVlak(int k, double* x, double* y, double* b, double* h) const {
  const double s = schaalOntwerp();
  double cx, cy;
  knopMidden(k, &cx, &cy);
  *x = cx - KOLOM / 2 * s;
  *y = cy - 50 * s;
  *b = KOLOM * s;
  *h = 100 * s;
}

int Paneel::knopOp(double x, double y) const {
  for (int k = 0; k < n_; ++k) {
    double kx, ky, kb, kh;
    knopVlak(k, &kx, &ky, &kb, &kh);
    if (x >= kx && x < kx + kb && y >= ky && y < ky + kh) return k;
  }
  return -1;
}

double Paneel::getoond(int k) {
  if (k < 0 || k >= n_) return 0;
  // tijdens het slepen, en zolang wat het paneel stuurde nog niet is toegepast: de eigen waarde; anders wat de host ziet
  if (k == sleept_ || bron_.uitstaand(k)) return lokaal_[k];
  return bron_.waarde(k);
}

void Paneel::zet(int k, double v) {
  const Knop& kn = knop_[k];
  v = v < kn.min ? kn.min : (v > kn.max ? kn.max : v);
  if (v == lokaal_[k]) return;  // tegen de grens aan: niets nieuws
  if (!begonnen_) {
    bron_.stuur(BEGIN, k, 0);
    begonnen_ = true;
  }
  lokaal_[k] = v;
  bron_.stuur(WAARDE, k, v);
}

bool Paneel::muis(Muis soort, double x, double y, bool shift) {
  switch (soort) {
    case NEER: {
      if (sleept_ >= 0 && begonnen_) bron_.stuur(EIND, sleept_, 0);
      begonnen_ = false;
      sleept_ = knopOp(x, y);
      if (sleept_ < 0) return false;
      gekozen_ = sleept_;
      // slepen begint bij wat er nu staat: wat de host ziet, of de eigen waarde als die nog onderweg is
      if (!bron_.uitstaand(sleept_)) lokaal_[sleept_] = bron_.waarde(sleept_);
      vorigeY_ = y;
      return true;
    }
    case SLEEP: {
      if (sleept_ < 0) return false;
      const Knop& kn = knop_[sleept_];
      // lineair in de CLAP-waarde: 200 px omhoog = het hele bereik, met Shift 10× fijner
      const double stap = (vorigeY_ - y) / SLEEP_PX * (shift ? 1.0 / FIJN : 1.0) * (kn.max - kn.min);
      vorigeY_ = y;
      if (stap != 0) zet(sleept_, lokaal_[sleept_] + stap);
      return true;
    }
    case OP: {
      if (sleept_ < 0) return false;
      if (begonnen_) bron_.stuur(EIND, sleept_, 0);
      begonnen_ = false;
      sleept_ = -1;
      return true;
    }
    case DUBBEL: {
      // dubbelklik = standaard (één gebaar)
      if (sleept_ >= 0 && begonnen_) bron_.stuur(EIND, sleept_, 0);
      begonnen_ = false;
      sleept_ = -1;
      const int k = knopOp(x, y);
      if (k < 0) return false;
      gekozen_ = k;
      bron_.stuur(BEGIN, k, 0);
      lokaal_[k] = knop_[k].standaard;
      bron_.stuur(WAARDE, k, lokaal_[k]);
      bron_.stuur(EIND, k, 0);
      return true;
    }
  }
  return false;
}

bool Paneel::veranderd() {
  if (getekendGekozen_ != gekozen_) return true;
  for (int k = 0; k < n_; ++k) {
    const double v = getoond(k);
    if (!(v == getekend_[k])) return true;
  }
  return false;
}

void Paneel::teken(uint8_t* rgba, int bw, int bh, double schaal) {
  if (!rgba || bw <= 0 || bh <= 0) return;
  Doek d{rgba, bw, bh};
  const double s = schaalOntwerp() * schaal;  // pixels per mockup-eenheid
  const double B = b_ * schaal, H = h_ * schaal;

  // achtergrond, harde schaduw (3 px rechts en onder), paneel met rand 2 px; hoeken 0, geen gradiënten
  for (int i = 0; i < bw * bh; ++i) {
    rgba[4 * i] = PANEEL.r;
    rgba[4 * i + 1] = PANEEL.g;
    rgba[4 * i + 2] = PANEEL.b;
    rgba[4 * i + 3] = 255;
  }
  const double pb = B - 2 * SCHADUW_D * s, ph = H - 2 * SCHADUW_D * s;
  d.vlak(SCHADUW_D * s, SCHADUW_D * s, pb + SCHADUW_D * s, ph + SCHADUW_D * s, SCHADUW);
  d.vlak(0, 0, pb, ph, PANEEL);
  d.rand(0, 0, pb, ph, RAND_W * s, INKT);

  // naamlabel op tape. De mockup geeft de tape 129 breed, maar Silkscreen op 14 px is breder dan dat (de mockup rekende
  // met een vervangend lettertype): de tape groeit mee met de tekst, met aan beide kanten 6 marge zoals in de svg.
  const double tapeB = std::fmax(129.0, tekstBreedte(silkscreen(), "PB TOETSENPAD", NAAM_PX) + 12);
  d.vlak(24 * s, 18 * s, (24 + tapeB) * s, (18 + 24) * s, TAPE);
  tekst(d, silkscreen(), "PB TOETSENPAD", 30 * s, 35 * s, NAAM_PX * s, PANEEL, false);

  char t[64];
  for (int k = 0; k < n_; ++k) {
    const Knop& kn = knop_[k];
    const double v = getoond(k);
    getekend_[k] = v;
    const double cx = (KNOP0_X + KOLOM * k) * s, cy = KNOP_Y * s;
    const double n = kn.max > kn.min ? klem01((v - kn.min) / (kn.max - kn.min)) : 0;
    const double hoek = -HOEK + 2 * HOEK * n;

    tekst(d, plexMono(), kn.label, cx, cy + LABEL_Y * s, TEKST_PX * s, INKT, true);
    d.boog(cx, cy, BOOG_R * s, -HOEK, HOEK, BOOG_W * s, GROEF);
    d.boog(cx, cy, BOOG_R * s, -HOEK, hoek, BOOG_W * s, k == gekozen_ ? TAPE : SPOOR);
    d.schijf(cx, cy, (KNOP_R + RAND_W / 2) * s, INKT);
    d.schijf(cx, cy, (KNOP_R - RAND_W / 2) * s, PANEEL);
    const double hr = hoek * PI / 180;
    d.lijn(cx, cy, cx + WIJZER_R * s * std::sin(hr), cy - WIJZER_R * s * std::cos(hr), WIJZER_W * s, INKT);
    bron_.tekst(k, v, t, sizeof t);
    tekst(d, plexMono(), t, cx, cy + WAARDE_Y * s, TEKST_PX * s, INKT, true);
  }
  getekendGekozen_ = gekozen_;
}

// --- PNG (zonder compressie) --------------------------------------------------------------------------------------
namespace {
uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return c;
}
void zetBE(std::vector<uint8_t>& v, uint32_t x) {
  for (int k = 3; k >= 0; --k) v.push_back((uint8_t)(x >> (8 * k)));
}
void blok(std::vector<uint8_t>& png, const char* soort, const std::vector<uint8_t>& data) {
  zetBE(png, (uint32_t)data.size());
  std::vector<uint8_t> c(soort, soort + 4);
  c.insert(c.end(), data.begin(), data.end());
  png.insert(png.end(), c.begin(), c.end());
  zetBE(png, crc32(c.data(), c.size()) ^ 0xFFFFFFFFu);
}
}  // namespace

std::vector<uint8_t> maakPng(const uint8_t* rgba, int b, int h) {
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  zetBE(ihdr, (uint32_t)b);
  zetBE(ihdr, (uint32_t)h);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8 bit, RGBA
  blok(png, "IHDR", ihdr);

  std::vector<uint8_t> ruw;
  for (int j = 0; j < h; ++j) {
    ruw.push_back(0);
    ruw.insert(ruw.end(), rgba + (size_t)j * b * 4, rgba + (size_t)(j + 1) * b * 4);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a1 = 1, a2 = 0;
  for (uint8_t x : ruw) {
    a1 = (a1 + x) % 65521;
    a2 = (a2 + a1) % 65521;
  }
  for (size_t pos = 0; pos < ruw.size() || pos == 0;) {
    const size_t n = ruw.size() - pos > 65535 ? 65535 : ruw.size() - pos;
    const bool laatste = pos + n >= ruw.size();
    z.push_back(laatste ? 1 : 0);
    z.push_back((uint8_t)(n & 0xFF));
    z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)(~n & 0xFF));
    z.push_back((uint8_t)((~n >> 8) & 0xFF));
    z.insert(z.end(), ruw.begin() + pos, ruw.begin() + pos + n);
    pos += n;
    if (laatste) break;
  }
  zetBE(z, (a2 << 16) | a1);
  blok(png, "IDAT", z);
  blok(png, "IEND", {});
  return png;
}

}  // namespace pb_paneel
