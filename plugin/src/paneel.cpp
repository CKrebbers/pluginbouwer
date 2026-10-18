// paneel.cpp — het model van het frontpaneel (zie paneel.h). Indeling en maten komen uit frontpaneel.svg.
#include "paneel.h"

#include <cmath>

namespace pb {
namespace paneel {

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double R_BOOG = 22, R_KNOP = 16, R_WIJZER = 14;
constexpr int TIK_LOKAAL = 15;  // ±0,5 s: zo lang toont het paneel wat het stuurde als de host het nog niet overnam

std::pair<double, double> opCirkel(double cx, double cy, double r, double graden) {
  const double a = graden * PI / 180.0;
  return {cx + r * std::sin(a), cy - r * std::cos(a)};
}

// een boog als open lijn, van hoek a naar hoek b (graden, 0 = boven, met de klok mee)
std::vector<std::pair<double, double>> boog(double cx, double cy, double r, double a, double b) {
  const int n = 1 + (int)std::ceil(std::fabs(b - a) / 2.0);
  std::vector<std::pair<double, double>> p;
  p.reserve((size_t)n + 1);
  for (int k = 0; k <= n; ++k) p.push_back(opCirkel(cx, cy, r, a + (b - a) * k / n));
  return p;
}
}  // namespace

Paneel::Paneel(const Koppeling& k, std::vector<Knop> knoppen, std::string naam,
               std::function<double(Letter, const std::string&)> meet)
    : k_(k), knoppen_(std::move(knoppen)), naam_(std::move(naam)), meet_(std::move(meet)) {
  lokaal_.assign(knoppen_.size(), 0.0);
  lokaalTik_.assign(knoppen_.size(), 0);
  getekend_.assign(knoppen_.size(), NAN);
  zetMaat(START_B, START_H);
}

void Paneel::zetMaat(double b, double h) {
  if (!(b > 0) || !(h > 0)) return;
  s_ = std::fmin(b / ONTWERP_B, h / ONTWERP_H);
  ox_ = (b - ONTWERP_B * s_) / 2;
  oy_ = (h - ONTWERP_H * s_) / 2;
}

double Paneel::klem(int i, double v) const {
  const Knop& kn = knoppen_[(size_t)i];
  return v < kn.min ? kn.min : (v > kn.max ? kn.max : v);
}

double Paneel::stand(int i, double v) const {
  const Knop& kn = knoppen_[(size_t)i];
  if (!(kn.max > kn.min)) return 0;
  const double t = (v - kn.min) / (kn.max - kn.min);
  return t < 0 ? 0 : (t > 1 ? 1 : t);
}

double Paneel::getoond(int i) const {
  if (sleep_ == i || lokaalTik_[(size_t)i] > 0) return lokaal_[(size_t)i];
  return k_.waarde ? k_.waarde(k_.ctx, i) : knoppen_[(size_t)i].standaard;
}

std::string Paneel::label(int i) const {
  const std::string& n = knoppen_[(size_t)i].naam;
  // hooguit LABEL_MAX tekens (UTF-8: niet midden in een teken knippen)
  size_t pos = 0;
  int tekens = 0;
  while (pos < n.size() && tekens < LABEL_MAX) {
    ++pos;
    while (pos < n.size() && ((unsigned char)n[pos] & 0xC0) == 0x80) ++pos;
    ++tekens;
  }
  return n.substr(0, pos);
}

std::string Paneel::waardeTekst(int i) const {
  char t[64] = {0};
  if (!k_.tekst || !k_.tekst(k_.ctx, i, getoond(i), t, sizeof t)) return "";
  return t;
}

std::vector<Opdracht> Paneel::teken() const {
  std::vector<Opdracht> o;
  auto vlak = [&](double x, double y, double b, double h, Kleur c) {
    Opdracht d{Opdracht::Vlak};
    d.x = x; d.y = y; d.b = b; d.h = h; d.kleur = c;
    o.push_back(d);
  };
  auto tekst = [&](double x, double y, std::string t, Letter l, Kleur c, bool midden) {
    Opdracht d{Opdracht::Tekst};
    d.x = x; d.y = y; d.tekst = std::move(t); d.letter = l; d.kleur = c; d.midden = midden;
    o.push_back(d);
  };
  auto pad = [&](std::vector<std::pair<double, double>> p, Kleur c, double dikte) {
    Opdracht d{Opdracht::Pad};
    d.punten = std::move(p); d.kleur = c; d.dikte = dikte;
    o.push_back(d);
  };

  // het paneel met harde schaduw (3 px rechts en onder) en een rand van 2 px
  vlak(3, 3, ONTWERP_B - 6, ONTWERP_H - 6, SCHADUW);
  vlak(0, 0, ONTWERP_B - 6, ONTWERP_H - 6, PANEEL);
  {
    Opdracht d{Opdracht::Rand};
    d.x = 0; d.y = 0; d.b = ONTWERP_B - 6; d.h = ONTWERP_H - 6; d.kleur = INKT; d.dikte = 2;
    o.push_back(d);
  }

  // naamlabel: tape, de naam in Silkscreen
  std::string naam;
  for (char c : naam_) naam += (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
  const double tb = meet_ ? meet_(Letter::Kop, naam) : 9.0 * (double)naam.size();
  vlak(24, 18, tb + 12, 24, TAPE);
  tekst(30, 35, naam, Letter::Kop, PANEEL, false);

  for (int i = 0; i < aantal(); ++i) {
    const double cx = KNOP_X0 + KOLOM * i, cy = KNOP_Y;
    const double v = getoond(i);
    const double a = hoek(stand(i, v));
    getekend_[(size_t)i] = v;
    tekst(cx, cy - 34, label(i), Letter::Mono, INKT, true);
    pad(boog(cx, cy, R_BOOG, -135, 135), GROEF, 3);
    if (a > -135) pad(boog(cx, cy, R_BOOG, -135, a), i == gekozen_ ? TAPE : SPOOR, 3);
    Opdracht c{Opdracht::Cirkel};
    c.x = cx; c.y = cy; c.b = R_KNOP; c.kleur = PANEEL; c.rand = INKT; c.dikte = 2;
    o.push_back(c);
    pad({{cx, cy}, opCirkel(cx, cy, R_WIJZER, a)}, INKT, 2);
    tekst(cx, cy + 44, waardeTekst(i), Letter::Mono, INKT, true);
  }
  getekendGekozen_ = gekozen_;
  return o;
}

int Paneel::knopOp(double x, double y) const {
  const double dx = (x - ox_) / s_, dy = (y - oy_) / s_;
  for (int i = 0; i < aantal(); ++i) {
    const double cx = KNOP_X0 + KOLOM * i;
    if (dx >= cx - KOLOM / 2 && dx < cx + KOLOM / 2 && dy >= KNOP_Y - 50 && dy <= KNOP_Y + 56) return i;
  }
  return -1;
}

Paneel::Vlak Paneel::knopVlak(int i) const {
  const double cx = KNOP_X0 + KOLOM * i;
  return {ox_ + (cx - KOLOM / 2) * s_, oy_ + (KNOP_Y - 50) * s_, KOLOM * s_, 106 * s_};
}

void Paneel::stuur(Soort s, int i, double v) {
  if (k_.stuur) k_.stuur(k_.ctx, Wijziging{s, i, v});
}

void Paneel::muisNeer(double x, double y, int klikken, bool shift) {
  if (sleep_ >= 0) muisOp();  // een gemiste muisOp: eerst het oude gebaar sluiten
  const int i = knopOp(x, y);
  if (i < 0) return;
  gekozen_ = i;
  if (klikken >= 2) {  // dubbelklik = standaard
    const double v = knoppen_[(size_t)i].standaard;
    lokaal_[(size_t)i] = v;
    lokaalTik_[(size_t)i] = TIK_LOKAAL;
    stuur(Soort::Begin, i, 0);
    stuur(Soort::Waarde, i, v);
    stuur(Soort::Eind, i, 0);
    return;
  }
  startW_ = getoond(i);
  lokaal_[(size_t)i] = startW_;
  sleep_ = i;
  startY_ = vorigeY_ = y;
  shift_ = shift;
  stuur(Soort::Begin, i, 0);
}

void Paneel::muisSleep(double, double y, bool shift) {
  const int i = sleep_;
  if (i < 0) return;
  if (shift != shift_) {  // Shift tijdens het slepen: verder vanaf de vorige plek, zonder sprong
    startW_ = lokaal_[(size_t)i];
    startY_ = vorigeY_;
    shift_ = shift;
  }
  vorigeY_ = y;
  const Knop& kn = knoppen_[(size_t)i];
  const double v = klem(i, startW_ + (startY_ - y) / SLEEP_BEREIK * (kn.max - kn.min) / (shift ? SHIFT_FIJNER : 1.0));
  if (v == lokaal_[(size_t)i]) return;
  lokaal_[(size_t)i] = v;
  lokaalTik_[(size_t)i] = TIK_LOKAAL;
  stuur(Soort::Waarde, i, v);
}

void Paneel::muisOp() {
  if (sleep_ < 0) return;
  const int i = sleep_;
  sleep_ = -1;
  stuur(Soort::Eind, i, 0);
}

void Paneel::stop() { muisOp(); }

bool Paneel::tik() {
  if (k_.tik) k_.tik(k_.ctx);
  bool opnieuw = getekendGekozen_ != gekozen_;
  for (int i = 0; i < aantal(); ++i) {
    int& t = lokaalTik_[(size_t)i];
    if (t > 0 && sleep_ != i) {
      // de host nam de waarde over: vanaf nu weer de spiegel; anders na TIK_LOKAAL tikken (dan kwam hij niet aan)
      if (k_.waarde && k_.waarde(k_.ctx, i) == lokaal_[(size_t)i]) t = 0;
      else --t;
    }
    const double v = getoond(i), g = getekend_[(size_t)i];
    if (!(v == g)) opnieuw = true;
  }
  return opnieuw;
}

}  // namespace paneel
}  // namespace pb
