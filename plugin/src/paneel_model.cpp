// paneel_model.cpp — zie paneel_model.h.
#include "paneel_model.h"

namespace dub {

using pb::Ontwerp;

const char* const LABELS[N_KNOPPEN] = {"Tijd",       "Terug",       "Toon",    "Laag weg", "Resonantie",
                                       "Verzadiging", "Haperen",    "Mix",     "Ruis"};

Schaal schaalVoor(double breedte, double hoogte) {
  const double sb = breedte / ONTWERP_B, sh = hoogte / ONTWERP_H;
  const double s = sb < sh ? sb : sh;
  return {s, (breedte - ONTWERP_B * s) / 2, (hoogte - ONTWERP_H * s) / 2};
}

double genormaliseerd(int i, double waarde) {
  const auto& p = Ontwerp::PARAMS[i];
  const double t = (waarde - p.min) / (p.max - p.min);
  return t < 0 ? 0 : (t > 1 ? 1 : t);
}

double hoekVoor(double t) {
  const double graden = -135.0 + 270.0 * t;
  return graden * 3.14159265358979323846 / 180.0;
}

double klemWaarde(int i, double v) {
  const auto& p = Ontwerp::PARAMS[i];
  if (!(v == v)) return p.standaard;
  return v < p.min ? p.min : (v > p.max ? p.max : v);
}

int knopOp(double x, double y) {
  if (y < KNOP_Y - 50 || y > KNOP_Y + 60) return -1;
  const double k = (x - (KNOP_X0 - KNOP_DX / 2)) / KNOP_DX;
  if (k < 0) return -1;
  const int i = (int)k;
  return i < N_KNOPPEN ? i : -1;
}

void PaneelModel::begin() {
  for (int i = 0; i < N_KNOPPEN; ++i) getoond[i] = kopie[i].load(std::memory_order_relaxed);
}

void PaneelModel::flush() {
  if (vraagFlush) vraagFlush(ctx);
}

void PaneelModel::stuur(int i, double v) {
  getoond[i] = v;
  bus->zet(i, v);
}

bool PaneelModel::muisNeer(double x, double y, double viewY, int klikken) {
  const int i = knopOp(x, y);
  if (i < 0) return false;
  gekozen = i;
  if (sleept >= 0) {  // een eind dat ontbrak (muis buiten het venster losgelaten)
    bus->eind(sleept);
    sleept = -1;
  }
  bus->begin(i);
  if (klikken >= 2) {  // dubbelklik = standaard
    stuur(i, Ontwerp::PARAMS[i].standaard);
    bus->eind(i);
  } else {
    sleept = i;
    laatsteY = viewY;
  }
  flush();
  return true;
}

bool PaneelModel::muisSleep(double viewY, bool fijn) {
  if (sleept < 0) return false;
  const int i = sleept;
  const auto& p = Ontwerp::PARAMS[i];
  const double dy = laatsteY - viewY;  // omhoog = meer
  laatsteY = viewY;
  const double v = klemWaarde(i, getoond[i] + dy * (p.max - p.min) / SLEEP_PX / (fijn ? FIJN : 1.0));
  if (v == getoond[i]) return false;
  stuur(i, v);
  flush();
  return true;
}

bool PaneelModel::muisOp() {
  if (sleept < 0) return false;
  bus->eind(sleept);
  sleept = -1;
  flush();
  return true;
}

bool PaneelModel::ververs() {
  bool anders = false;
  for (int i = 0; i < N_KNOPPEN; ++i) {
    // tijdens het slepen, of zolang de host de laatste waarde nog niet heeft opgehaald: wat het paneel stuurde
    if (i == sleept || bus->onderweg(i)) continue;
    const double v = kopie[i].load(std::memory_order_relaxed);
    if (v != getoond[i]) { getoond[i] = v; anders = true; }
  }
  return anders;
}

}  // namespace dub
