// paneel.cpp — het frontpaneel uit design.md: tekenen en muis, zonder venstersysteem (zie paneel.h).
// Alles is plat (geen gradiënten), hoeken 0, randen 2 px, harde schaduw 3 px 3 px; letters IBM Plex Mono
// (monospace, dus tabulaire cijfers) en Silkscreen voor de kop. De lettertypes zitten in de plugin (OFL).
#include "paneel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

// gemaakt door CMake uit plugin/extern/lettertypes (lettertypes.cpp in de build)
namespace pb {
extern const unsigned char LETTER_PLEX_REGULAR[];
extern const unsigned char LETTER_PLEX_MEDIUM[];
extern const unsigned char LETTER_SILKSCREEN[];
}  // namespace pb

namespace pb {

namespace {
// de tokens uit design.md
constexpr uint32_t PANEEL = 0x232629;
constexpr uint32_t INKT = 0xe7e5dd;
constexpr uint32_t TAPE = 0xf2c200;
constexpr uint32_t SPOOR = 0x40cbda;
// afgeleid (plat, geen verloop): schaduw, de lege boog en het knoplijf
constexpr uint32_t SCHADUW = 0x0f1112;
constexpr uint32_t BOOG_LEEG = 0x3b4045;
constexpr uint32_t KNOP = 0x2c3034;

constexpr double PI = 3.14159265358979323846;
constexpr double HOEK = 135.0 * PI / 180.0;  // de knop draait van −135° tot +135° (0 = boven)

// de indeling op het minimale paneel (560 × 220); groter schaalt alles mee
constexpr double MARGE = 12;
constexpr double KAST_BOVEN = 48, KAST_ONDER = 206;
constexpr double NAAM_Y = 72, KNOP_Y = 122, WAARDE_Y = 178;
constexpr double KNOP_R = 18, BOOG_R = 25, BOOG_DIK = 4;
constexpr double PIXELS_PER_SLAG = 200;  // zoveel punten slepen = het hele bereik; Shift = 10× fijner

double klem01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
}  // namespace

struct Glief {
  int x0 = 0, y0 = 0, b = 0, h = 0;
  std::vector<unsigned char> dekking;
};

struct Lettertype {
  stbtt_fontinfo info{};
  bool goed = false;
  std::unordered_map<uint64_t, Glief> cache;

  explicit Lettertype(const unsigned char* data) {
    goed = stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0;
  }
  double schaal(double px) { return stbtt_ScaleForMappingEmToPixels(&info, (float)px); }
  double voortgang(double px, int cp) {
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&info, cp, &adv, &lsb);
    return adv * schaal(px);
  }
  // de glief op deze grootte en een kwart-pixel verschuiving
  const Glief& glief(double px, int cp, int kwart) {
    const uint64_t sleutel = ((uint64_t)std::lround(px * 4) << 32) | ((uint64_t)(uint32_t)cp << 2) | (uint64_t)kwart;
    auto it = cache.find(sleutel);
    if (it != cache.end()) return it->second;
    if (cache.size() > 2000) cache.clear();  // andere schaal: oude maten weg
    Glief g;
    const float s = (float)schaal(px);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBoxSubpixel(&info, cp, s, s, kwart * 0.25f, 0, &x0, &y0, &x1, &y1);
    g.x0 = x0; g.y0 = y0; g.b = x1 - x0; g.h = y1 - y0;
    if (g.b > 0 && g.h > 0) {
      g.dekking.assign((size_t)g.b * g.h, 0);
      stbtt_MakeCodepointBitmapSubpixel(&info, g.dekking.data(), g.b, g.h, g.b, s, s, kwart * 0.25f, 0, cp);
    }
    return cache.emplace(sleutel, std::move(g)).first->second;
  }
};

Paneel::Paneel(PaneelBron& bron) : bron_(bron) {
  n_ = bron_.aantal();
  getoond_.assign((size_t)n_, NAN);
  plex_ = new Lettertype(LETTER_PLEX_REGULAR);
  plexMedium_ = new Lettertype(LETTER_PLEX_MEDIUM);
  silk_ = new Lettertype(LETTER_SILKSCREEN);
  zetGrootte(PANEEL_STD_B, PANEEL_STD_H, 1);
}
Paneel::~Paneel() {
  delete plex_;
  delete plexMedium_;
  delete silk_;
}

void Paneel::zetGrootte(double b, double h, double schaal) {
  if (!(b >= 1)) b = 1;
  if (!(h >= 1)) h = 1;
  if (!(schaal > 0) || schaal > 8) schaal = 1;
  const int nb = (int)std::lround(b * schaal), nh = (int)std::lround(h * schaal);
  if (nb == pb_ && nh == ph_ && b == b_ && h == h_ && schaal == schaal_) return;
  b_ = b; h_ = h; schaal_ = schaal;
  pb_ = nb; ph_ = nh;
  pix_.assign((size_t)pb_ * ph_, PANEEL);
  // het paneel schaalt gelijkmatig mee en staat in het midden; de rest is paneelkleur
  const double kl = std::min(b / PANEEL_MIN_B, h / PANEEL_MIN_H);
  k_ = kl * schaal;
  ox_ = (pb_ - PANEEL_MIN_B * k_) / 2;
  oy_ = (ph_ - PANEEL_MIN_H * k_) / 2;
  vuil_ = true;
}

double Paneel::naarNorm(int i, double v) const {
  const KnopInfo in = bron_.info(i);
  if (in.logaritmisch && in.min > 0) return klem01(std::log(v / in.min) / std::log(in.max / in.min));
  return klem01((v - in.min) / (in.max - in.min));
}
double Paneel::uitNorm(int i, double n) const {
  const KnopInfo in = bron_.info(i);
  n = klem01(n);
  double v = in.logaritmisch && in.min > 0 ? in.min * std::exp(n * std::log(in.max / in.min))
                                            : in.min + n * (in.max - in.min);
  return std::min(in.max, std::max(in.min, v));
}

// --- de indeling --------------------------------------------------------------------------------------------
int Paneel::knopOp(double x, double y) const {
  if (n_ <= 0) return -1;
  // logisch → paneelcoördinaten (560 × 220)
  const double s = k_ / schaal_;
  const double px = (x - ox_ / schaal_) / s, py = (y - oy_ / schaal_) / s;
  if (py < KAST_BOVEN || py > KAST_ONDER || px < MARGE || px > PANEEL_MIN_B - MARGE) return -1;
  const double kolom = (PANEEL_MIN_B - 2 * MARGE) / n_;
  const int i = (int)((px - MARGE) / kolom);
  return i < 0 ? 0 : (i >= n_ ? n_ - 1 : i);
}
void Paneel::knopMidden(int i, double* x, double* y) const {
  const double kolom = (PANEEL_MIN_B - 2 * MARGE) / n_;
  const double s = k_ / schaal_;
  *x = ox_ / schaal_ + (MARGE + kolom * (i + 0.5)) * s;
  *y = oy_ / schaal_ + KNOP_Y * s;
}

// --- muis ---------------------------------------------------------------------------------------------------
void Paneel::muisNeer(double x, double y, bool fijn, bool dubbel) {
  (void)fijn;
  const int i = knopOp(x, y);
  if (i < 0) return;
  if (sleepKnop_ >= 0) muisOp();
  gekozen_ = i;
  vuil_ = true;
  if (dubbel) {  // dubbelklik = standaard, als één gebaar
    bron_.begin(i);
    bron_.zet(i, bron_.info(i).standaard);
    bron_.eind(i);
    return;
  }
  sleepKnop_ = i;
  sleepY_ = y;
  sleepNorm_ = naarNorm(i, bron_.waarde(i));
  bron_.begin(i);
}
void Paneel::muisSleep(double, double y, bool fijn) {
  if (sleepKnop_ < 0) return;
  const double norm = klem01(sleepNorm_ + (sleepY_ - y) / (PIXELS_PER_SLAG * (fijn ? 10 : 1)));
  sleepY_ = y;
  if (norm == sleepNorm_) return;
  sleepNorm_ = norm;
  bron_.zet(sleepKnop_, uitNorm(sleepKnop_, norm));
  vuil_ = true;
}
void Paneel::muisOp() {
  if (sleepKnop_ < 0) return;
  bron_.eind(sleepKnop_);
  sleepKnop_ = -1;
}

// --- tekenen ------------------------------------------------------------------------------------------------
bool Paneel::tik() {
  for (int i = 0; i < n_; ++i)
    if (bron_.waarde(i) != getoond_[(size_t)i]) vuil_ = true;
  return vuil_;
}
bool Paneel::teken() {
  tik();
  if (!vuil_ && getoondGekozen_ == gekozen_) return false;
  tekenAlles();
  vuil_ = false;
  getoondGekozen_ = gekozen_;
  return true;
}

void Paneel::meng(int x, int y, uint32_t kleur, double a) {
  if (x < 0 || y < 0 || x >= pb_ || y >= ph_ || a <= 0) return;
  uint32_t& p = pix_[(size_t)y * pb_ + x];
  if (a >= 1) { p = kleur; return; }
  const int ia = (int)(a * 256 + 0.5);
  auto kanaal = [&](int s) {
    const int o = (int)((p >> s) & 0xff), n = (int)((kleur >> s) & 0xff);
    return (uint32_t)((o * (256 - ia) + n * ia) >> 8) << s;
  };
  p = kanaal(16) | kanaal(8) | kanaal(0);
}

void Paneel::vul(double x0, double y0, double x1, double y1, uint32_t kleur) {
  const int ix0 = std::max(0, (int)std::floor(x0)), ix1 = std::min(pb_, (int)std::ceil(x1));
  const int iy0 = std::max(0, (int)std::floor(y0)), iy1 = std::min(ph_, (int)std::ceil(y1));
  for (int y = iy0; y < iy1; ++y) {
    const double dy = std::min<double>(y + 1, y1) - std::max<double>(y, y0);
    for (int x = ix0; x < ix1; ++x) {
      const double dx = std::min<double>(x + 1, x1) - std::max<double>(x, x0);
      meng(x, y, kleur, dx * dy);
    }
  }
}
void Paneel::kader(double x0, double y0, double x1, double y1, double d, uint32_t kleur) {
  vul(x0, y0, x1, y0 + d, kleur);
  vul(x0, y1 - d, x1, y1, kleur);
  vul(x0, y0 + d, x0 + d, y1 - d, kleur);
  vul(x1 - d, y0 + d, x1, y1 - d, kleur);
}
void Paneel::schijf(double cx, double cy, double r, uint32_t kleur) {
  const int x0 = (int)std::floor(cx - r - 1), x1 = (int)std::ceil(cx + r + 1);
  const int y0 = (int)std::floor(cy - r - 1), y1 = (int)std::ceil(cy + r + 1);
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
      meng(x, y, kleur, std::min(1.0, r - d + 0.5));
    }
}
void Paneel::ring(double cx, double cy, double r, double dik, uint32_t kleur) {
  boog(cx, cy, r, dik, -PI - 0.01, PI + 0.01, kleur);
}
// een boog van hoek a0 tot a1 (radialen, 0 = boven, met de klok mee), rechte uiteinden
void Paneel::boog(double cx, double cy, double r, double dik, double a0, double a1, uint32_t kleur) {
  if (a1 <= a0) return;
  const double ruim = r + dik / 2 + 1;
  const int x0 = (int)std::floor(cx - ruim), x1 = (int)std::ceil(cx + ruim);
  const int y0 = (int)std::floor(cy - ruim), y1 = (int)std::ceil(cy + ruim);
  const bool rond = a1 - a0 >= 2 * PI;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      const double rho = std::hypot(dx, dy);
      const double radiaal = std::min(1.0, dik / 2 - std::fabs(rho - r) + 0.5);
      if (radiaal <= 0) continue;
      double hoekig = 1;
      if (!rond) {
        const double th = std::atan2(dx, -dy);
        // afstand tot het dichtstbijzijnde uiteinde (negatief = buiten de boog), alleen dichtbij geldig
        const double da = std::min(th - a0, a1 - th);
        hoekig = da >= PI / 2 ? 1.0 : (da <= -PI / 2 ? 0.0 : std::min(1.0, rho * std::sin(da) + 0.5));
      }
      if (hoekig > 0) meng(x, y, kleur, radiaal * hoekig);
    }
}
void Paneel::lijn(double x0, double y0, double x1, double y1, double dik, uint32_t kleur) {
  const double ruim = dik / 2 + 1;
  const int bx0 = (int)std::floor(std::min(x0, x1) - ruim), bx1 = (int)std::ceil(std::max(x0, x1) + ruim);
  const int by0 = (int)std::floor(std::min(y0, y1) - ruim), by1 = (int)std::ceil(std::max(y0, y1) + ruim);
  const double vx = x1 - x0, vy = y1 - y0, ll = vx * vx + vy * vy;
  for (int y = by0; y < by1; ++y)
    for (int x = bx0; x < bx1; ++x) {
      const double px = x + 0.5 - x0, py = y + 0.5 - y0;
      const double t = ll > 0 ? std::min(1.0, std::max(0.0, (px * vx + py * vy) / ll)) : 0;
      const double d = std::hypot(px - t * vx, py - t * vy);
      meng(x, y, kleur, std::min(1.0, dik / 2 - d + 0.5));
    }
}

double Paneel::tekstBreedte(Lettertype& f, double px, const char* s) {
  double b = 0;
  for (; *s; ++s) b += f.voortgang(px, (unsigned char)*s);
  return b;
}
// uitlijn: −1 links, 0 midden, 1 rechts; x en yBasis in pixels
void Paneel::tekst(Lettertype& f, double px, double x, double yBasis, const char* s, uint32_t kleur, int uitlijn) {
  if (!f.goed) return;
  if (uitlijn >= 0) x -= tekstBreedte(f, px, s) * (uitlijn == 0 ? 0.5 : 1.0);
  const int basis = (int)std::lround(yBasis);
  for (; *s; ++s) {
    const int cp = (unsigned char)*s;
    const double vx = std::floor(x);
    const int kwart = (int)((x - vx) * 4);
    const Glief& g = f.glief(px, cp, kwart);
    for (int gy = 0; gy < g.h; ++gy)
      for (int gx = 0; gx < g.b; ++gx) {
        const unsigned char d = g.dekking[(size_t)gy * g.b + gx];
        if (d) meng((int)vx + g.x0 + gx, basis + g.y0 + gy, kleur, d / 255.0);
      }
    x += f.voortgang(px, cp);
  }
}

void Paneel::tekenAlles() {
  std::fill(pix_.begin(), pix_.end(), PANEEL);
  const double k = k_;
  auto X = [&](double x) { return ox_ + x * k; };
  auto Y = [&](double y) { return oy_ + y * k; };
  const double rand = 2 * k, schaduw = 3 * k;

  // kop: het naamlabel op tape, met harde schaduw
  {
    const char* naam = "DUB-ECHO";
    const double px = 16 * k;
    const double b = tekstBreedte(*silk_, px, naam) + 16 * k;
    const double x0 = X(MARGE), y0 = Y(12), x1 = x0 + b, y1 = Y(36);
    vul(x0 + schaduw, y0 + schaduw, x1 + schaduw, y1 + schaduw, SCHADUW);
    vul(x0, y0, x1, y1, TAPE);
    kader(x0, y0, x1, y1, rand, INKT);
    tekst(*silk_, px, x0 + 8 * k, Y(30), naam, PANEEL, -1);
    tekst(*silk_, 8 * k, X(PANEEL_MIN_B - MARGE), Y(28), "PLUGINBOUWER", INKT, 1);
  }

  // de kast met de knoppen
  {
    const double x0 = X(MARGE), y0 = Y(KAST_BOVEN), x1 = X(PANEEL_MIN_B - MARGE), y1 = Y(KAST_ONDER);
    vul(x0 + schaduw, y0 + schaduw, x1 + schaduw, y1 + schaduw, SCHADUW);
    vul(x0, y0, x1, y1, PANEEL);
    kader(x0, y0, x1, y1, rand, INKT);
  }

  const double kolom = (PANEEL_MIN_B - 2 * MARGE) / n_;
  for (int i = 0; i < n_; ++i) {
    const KnopInfo in = bron_.info(i);
    const double v = bron_.waarde(i);
    getoond_[(size_t)i] = v;
    const double cxl = MARGE + kolom * (i + 0.5);
    const double cx = X(cxl), cy = Y(KNOP_Y);

    // naam erboven: Plex Mono Medium, kleiner als hij niet in de kolom past (Terugkoppeling)
    const double ruimte = kolom - 8;
    const double naamPx = std::min(10.5, ruimte / (std::strlen(in.naam) * 0.6)) * k;
    tekst(*plexMedium_, naamPx, cx, Y(NAAM_Y), in.naam, INKT, 0);

    // de boog: leeg spoor, dan de waarde (spoor; de gekozen knop in tape)
    const double n = naarNorm(i, v);
    const double a = -HOEK + n * 2 * HOEK;
    boog(cx, cy, BOOG_R * k, BOOG_DIK * k, -HOEK, HOEK, BOOG_LEEG);
    boog(cx, cy, BOOG_R * k, BOOG_DIK * k, -HOEK, a, i == gekozen_ ? TAPE : SPOOR);

    // de knop: harde schaduw, lijf, rand 2 px, wijzer
    schijf(cx + schaduw, cy + schaduw, KNOP_R * k, SCHADUW);
    schijf(cx, cy, KNOP_R * k, KNOP);
    ring(cx, cy, (KNOP_R - 1) * k, rand, INKT);
    const double sx = std::sin(a), sy = -std::cos(a);
    lijn(cx + sx * 5 * k, cy + sy * 5 * k, cx + sx * (KNOP_R - 4) * k, cy + sy * (KNOP_R - 4) * k, rand, INKT);

    // waarde eronder, in gewone eenheden (dezelfde tekst als de host laat zien)
    char t[64];
    bron_.tekst(i, v, t, sizeof t);
    const double waardePx = std::min(10.0, ruimte / (std::max<size_t>(1, std::strlen(t)) * 0.6)) * k;
    tekst(*plex_, waardePx, cx, Y(WAARDE_Y), t, INKT, 0);
  }
}

}  // namespace pb
