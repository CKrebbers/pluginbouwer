// paneel.h — het frontpaneel uit design.md, zonder platformcode: kleuren, indeling, knop <-> waarde en slepen.
// De Cocoa-view (paneel_mac.mm) tekent en luistert naar de muis; de plugin (dub_echo.cpp) levert een `Bediening`.
// De UI raakt nooit de DSP: hij leest de hostwaarde (atomisch) en stuurt veranderingen als gebaar/waarde naar de
// plugin, die ze op de audiothread (process of params.flush) doorgeeft aan de host én toepast, net als een host-event.
#pragma once

#include <cmath>
#include <cstdint>

namespace pb_ui {

// design.md, tokens
struct Kleur { double r, g, b; };
constexpr Kleur hex(uint32_t c) {
  return {((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0};
}
constexpr Kleur PANEEL = hex(0x232629);  // achtergrond
constexpr Kleur INKT = hex(0xe7e5dd);    // tekst, knopranden
constexpr Kleur TAPE = hex(0xf2c200);    // naamlabel, de boog van de gekozen knop
constexpr Kleur SPOOR = hex(0x40cbda);   // boog van de knoppen
constexpr Kleur SCHADUW = hex(0x000000); // harde schaduw, 3 px 3 px 0
constexpr double RAND = 2;               // rand 2 px
constexpr double SCHADUW_DX = 3, SCHADUW_DY = 3;

// Wat de UI van de plugin nodig heeft. Alles op de main thread.
struct Knop {
  uint32_t id;
  const char* naam;
  const char* uitleg;
  double min, max, standaard;
  bool logaritmisch;  // Hz en ms: de knop loopt in octaven, niet lineair
};
class Bediening {
 public:
  virtual ~Bediening() = default;
  virtual int aantal() const = 0;
  virtual Knop knop(int k) const = 0;
  virtual double waarde(int k) const = 0;  // zoals de host hem ziet
  virtual bool wachtend(int k) const = 0;  // de UI stuurde iets dat de audiothread nog niet verwerkte
  virtual void alsTekst(int k, double v, char* t, uint32_t cap) const = 0;  // dezelfde tekst als de host toont
  virtual void gebaarBegin(int k) = 0;
  virtual void zet(int k, double v) = 0;
  virtual void gebaarEinde(int k) = 0;
};

// --- knop <-> 0..1 (de stand van de knop; de host normaliseert zelf lineair, dit is alleen de draaihoek) ----------
inline double naarStand(const Knop& k, double v) {
  if (!(v == v)) v = k.standaard;
  v = v < k.min ? k.min : (v > k.max ? k.max : v);
  if (k.logaritmisch && k.min > 0) return std::log(v / k.min) / std::log(k.max / k.min);
  return (v - k.min) / (k.max - k.min);
}
inline double uitStand(const Knop& k, double s) {
  s = s < 0 ? 0 : (s > 1 ? 1 : s);
  double v = k.logaritmisch && k.min > 0 ? k.min * std::exp(s * std::log(k.max / k.min)) : k.min + s * (k.max - k.min);
  return v < k.min ? k.min : (v > k.max ? k.max : v);
}

// 270 graden: -135 (links onder) tot +135 (rechts onder), 0 = recht omhoog
constexpr double BOOG_GRADEN = 270;
inline double hoekVan(double stand) { return (-135 + BOOG_GRADEN * stand) * 3.14159265358979323846 / 180; }

// Slepen: omhoog of naar rechts = meer. Een hele slag kost 200 punten; met shift 10x fijner.
constexpr double SLEEP_PUNTEN = 200;
constexpr double FIJN = 10;
inline double sleepStand(double standBijStart, double dx, double dyOmhoog, bool fijn) {
  const double d = (dx + dyOmhoog) / SLEEP_PUNTEN / (fijn ? FIJN : 1);
  const double s = standBijStart + d;
  return s < 0 ? 0 : (s > 1 ? 1 : s);
}

// --- indeling ----------------------------------------------------------------------------------------------------
// Ontworpen op 720 x 270 punten; het paneel schaalt gelijkmatig mee (en staat dan in het midden).
// design.md: minimaal 560 x 220, schaalbaar.
constexpr double BASIS_B = 720, BASIS_H = 270;
constexpr uint32_t MIN_B = 560, MIN_H = 220, MAX_B = 2880, MAX_H = 1080;

struct Vak { double x, y, b, h; };
inline bool binnen(const Vak& r, double x, double y) { return x >= r.x && x < r.x + r.b && y >= r.y && y < r.y + r.h; }

struct Indeling {
  double schaal = 1, ox = 0, oy = 0;  // basis -> view: x' = ox + schaal * x (y omlaag, linksboven = 0,0)
  Vak naamlabel, rij;                // in view-punten
  Vak cel[16];
  double knopX[16], knopY[16], knopR = 0;  // middelpunt en straal van de boog
  Vak naam[16], waarde[16];
  double kopGrootte = 0, naamGrootte = 0, waardeGrootte = 0, onderGrootte = 0;
  int n = 0;
};

inline Indeling indeling(double b, double h, int n) {
  Indeling d;
  d.n = n < 16 ? n : 16;
  const double s = std::fmin(b / BASIS_B, h / BASIS_H);
  d.schaal = s;
  d.ox = (b - s * BASIS_B) / 2;
  d.oy = (h - s * BASIS_H) / 2;
  auto r = [&](double x, double y, double bb, double hh) { return Vak{d.ox + s * x, d.oy + s * y, s * bb, s * hh}; };
  d.kopGrootte = 16 * s;     // Silkscreen is een pixelletter van 8 punten: veelvouden blijven scherp
  d.naamGrootte = 8 * s;
  d.waardeGrootte = 12 * s;  // IBM Plex Mono
  d.onderGrootte = 10 * s;
  d.naamlabel = r(20, 18, 150, 34);
  d.rij = r(20, 70, 680, 180);
  const double cb = 680.0 / d.n;
  for (int k = 0; k < d.n; ++k) {
    const double cx = 20 + cb * k;
    d.cel[k] = r(cx, 70, cb, 180);
    d.naam[k] = r(cx + 2, 84, cb - 4, 14);
    d.knopX[k] = d.ox + s * (cx + cb / 2);
    d.knopY[k] = d.oy + s * 150;
    d.waarde[k] = r(cx + 2, 206, cb - 4, 22);
  }
  d.knopR = s * 28;
  return d;
}

// welke knop zit onder (x, y)? -1 = geen. De hele cel telt, zodat je ook op de naam of de waarde kunt slepen.
inline int knopOnder(const Indeling& d, double x, double y) {
  for (int k = 0; k < d.n; ++k)
    if (binnen(d.cel[k], x, y)) return k;
  return -1;
}

inline void klemMaat(uint32_t* b, uint32_t* h) {
  if (*b < MIN_B) *b = MIN_B;
  if (*h < MIN_H) *h = MIN_H;
  if (*b > MAX_B) *b = MAX_B;
  if (*h > MAX_H) *h = MAX_H;
}

}  // namespace pb_ui
