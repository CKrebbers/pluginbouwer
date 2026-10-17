// paneel.cpp — zie paneel.h.
#include "paneel.h"

#include <cmath>

#include "param_tekst.h"

namespace paneel {

namespace {
using pb::Ontwerp;
constexpr double PI = 3.14159265358979323846;
constexpr double HOEK = 135 * PI / 180;  // de knop loopt van −135° tot +135°

// de basisindeling (560 × 220); Indeling schaalt hem naar punten
constexpr double RAAM_X0 = 12, RAAM_Y0 = 44, RAAM_X1 = 545, RAAM_Y1 = 205;
constexpr double CEL_X0 = RAAM_X0 + 2, CEL_B = (RAAM_X1 - RAAM_X0 - 4) / N;
constexpr double NAAM_Y = 68, KNOP_Y = 112, WAARDE_Y = 170;
constexpr double BOOG_R = 27, BOOG_D = 4, KNOP_R = 19, SCHADUW_D = 3;

double klem(int i, double v) {
  const auto& p = Ontwerp::PARAMS[i];
  return v < p.min ? p.min : v > p.max ? p.max : v;
}
double deel(int i, double v) {  // 0..1 over het bereik (lineair, zoals clap-wrapper normaliseert)
  const auto& p = Ontwerp::PARAMS[i];
  return (klem(i, v) - p.min) / (p.max - p.min);
}
}  // namespace

Paneel::Paneel(Koppeling k) : k_(k) {
  for (int i = 0; i < N; ++i) {
    spiegel_[i] = verstuurd_[i] = spiegelToen_[i] = k_.lees(k_.ctx, i);
    wacht_[i] = false;
  }
}

Paneel::Indeling Paneel::indeling() const {
  const double s = std::fmin(b_ / MIN_B, h_ / MIN_H);
  return {s, (b_ - MIN_B * s) / 2, (h_ - MIN_H * s) / 2};
}

int Paneel::knopOp(double x, double y) const {
  const Indeling d = indeling();
  const double bx = (x - d.ox) / d.s, by = (y - d.oy) / d.s;
  if (by < RAAM_Y0 || by >= RAAM_Y1 || bx < CEL_X0) return -1;
  const int i = (int)((bx - CEL_X0) / CEL_B);
  return i < N ? i : -1;
}

double Paneel::getoond(int i) const {
  if (i == sleep_) return sleepWaarde_;
  return wacht_[i] ? verstuurd_[i] : spiegel_[i];
}

void Paneel::stuurWaarde(int i, double v) {
  verstuurd_[i] = v;
  spiegelToen_[i] = spiegel_[i];
  wacht_[i] = true;
  k_.stuur(k_.ctx, {Bericht::WAARDE, Ontwerp::PARAMS[i].id, v});
  vuil_ = true;
}

void Paneel::muisNeer(double x, double y, bool shift, bool dubbel) {
  (void)shift;
  const int i = knopOp(x, y);
  if (i < 0 || sleep_ >= 0) return;
  gekozen_ = i;
  vuil_ = true;
  const uint32_t id = Ontwerp::PARAMS[i].id;
  if (dubbel) {  // dubbelklik = standaard, als één gebaar
    if (!k_.stuur(k_.ctx, {Bericht::BEGIN, id, 0})) return;
    stuurWaarde(i, Ontwerp::PARAMS[i].standaard);
    k_.stuur(k_.ctx, {Bericht::EIND, id, 0});
    return;
  }
  if (!k_.stuur(k_.ctx, {Bericht::BEGIN, id, 0})) return;  // de wachtrij is vol: geen half gebaar bij de host
  sleepWaarde_ = getoond(i);
  sleepY_ = y;
  sleep_ = i;
}

void Paneel::muisSleep(double x, double y, bool shift) {
  (void)x;
  if (sleep_ < 0) return;
  const auto& p = Ontwerp::PARAMS[sleep_];
  const double punten = SLEEP_BEREIK * indeling().s * (shift ? FIJN : 1);
  const double v = klem(sleep_, sleepWaarde_ + (sleepY_ - y) / punten * (p.max - p.min));
  sleepY_ = y;
  if (v == sleepWaarde_) return;
  sleepWaarde_ = v;
  stuurWaarde(sleep_, v);
}

void Paneel::muisOp() {
  if (sleep_ < 0) return;
  k_.stuur(k_.ctx, {Bericht::EIND, Ontwerp::PARAMS[sleep_].id, 0});
  sleep_ = -1;
  vuil_ = true;
}

bool Paneel::ververs() {
  bool anders = false;
  for (int i = 0; i < N; ++i) {
    const double voor = getoond(i);
    spiegel_[i] = k_.lees(k_.ctx, i);
    // de host meldt onze waarde terug, of zette zelf iets anders (automatisering, state): dan geldt de spiegel weer
    if (wacht_[i] && (spiegel_[i] == verstuurd_[i] || spiegel_[i] != spiegelToen_[i])) wacht_[i] = false;
    anders |= getoond(i) != voor;
  }
  if (anders) vuil_ = true;
  return anders;
}

const Beeld& Paneel::teken(double b, double h, double schaal) {
  if (b != b_ || h != h_ || schaal != schaal_) {
    b_ = b; h_ = h; schaal_ = schaal;
    vuil_ = true;
  }
  if (!vuil_) return beeld_;
  vuil_ = false;
  beeld_.maat((int)std::lround(b_ * schaal_), (int)std::lround(h_ * schaal_));
  beeld_.vul(PANEEL);
  const Indeling d = indeling();
  const double f = d.s * schaal_;  // pixels per basiseenheid
  auto X = [&](double x) { return (d.ox + x * d.s) * schaal_; };
  auto Y = [&](double y) { return (d.oy + y * d.s) * schaal_; };

  // naamlabel op tape, met harde schaduw
  const char* naam = Ontwerp::META.naam;
  const double tb = Beeld::tekstBreedte(silkscreen(), 16 * f, naam);
  const double lx0 = X(RAAM_X0), ly0 = Y(10), lx1 = lx0 + tb + 16 * f, ly1 = Y(32);
  beeld_.rechthoek(lx0 + SCHADUW_D * f, ly0 + SCHADUW_D * f, lx1 + SCHADUW_D * f, ly1 + SCHADUW_D * f, SCHADUW);
  beeld_.rechthoek(lx0, ly0, lx1, ly1, TAPE);
  beeld_.tekst(silkscreen(), 16 * f, lx0 + 8 * f, Y(27), naam, PANEEL);
  const char* maker = Ontwerp::META.maker;
  beeld_.tekst(plexMono(), 10 * f, X(RAAM_X1) - Beeld::tekstBreedte(plexMono(), 10 * f, maker), Y(26), maker, INKT);

  // het raam om de knoppen: rand 2, harde schaduw 3 3 0, hoeken 0
  const double r = 2 * f;
  beeld_.rechthoek(X(RAAM_X0) + SCHADUW_D * f, Y(RAAM_Y0) + SCHADUW_D * f, X(RAAM_X1) + SCHADUW_D * f,
                   Y(RAAM_Y1) + SCHADUW_D * f, SCHADUW);
  beeld_.rechthoek(X(RAAM_X0), Y(RAAM_Y0), X(RAAM_X1), Y(RAAM_Y1), INKT);
  beeld_.rechthoek(X(RAAM_X0) + r, Y(RAAM_Y0) + r, X(RAAM_X1) - r, Y(RAAM_Y1) - r, PANEEL);

  char tekst[64];
  for (int i = 0; i < N; ++i) {
    const auto& p = Ontwerp::PARAMS[i];
    const double cx = X(CEL_X0 + CEL_B * (i + 0.5)), cy = Y(KNOP_Y);
    beeld_.tekst(silkscreen(), 8 * f, cx, Y(NAAM_Y), p.naam, INKT, true);

    const double v = getoond(i), a = -HOEK + 2 * HOEK * deel(i, v);
    beeld_.boog(cx, cy, BOOG_R * f, BOOG_D * f, -HOEK, HOEK, GROEF);
    beeld_.boog(cx, cy, BOOG_R * f, BOOG_D * f, -HOEK, a, i == gekozen_ ? TAPE : SPOOR);

    beeld_.schijf(cx + SCHADUW_D * f, cy + SCHADUW_D * f, KNOP_R * f, SCHADUW);
    beeld_.schijf(cx, cy, KNOP_R * f, PANEEL);
    beeld_.ring(cx, cy, KNOP_R * f - r / 2, r, INKT);
    const double sx = std::sin(a), sy = -std::cos(a);
    beeld_.lijn(cx + sx * 6 * f, cy + sy * 6 * f, cx + sx * (KNOP_R - 4) * f, cy + sy * (KNOP_R - 4) * f, r, INKT);

    if (!tekst::schrijf(p, klem(i, v), tekst, sizeof tekst)) tekst[0] = 0;
    beeld_.tekst(plexMono(), 11 * f, cx, Y(WAARDE_Y), tekst, INKT, true);
  }
  return beeld_;
}

}  // namespace paneel
