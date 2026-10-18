// paneel_model.h — het frontpaneel zonder tekenwerk: maten uit de mockup (frontpaneel.svg), knop ↔ hoek, en wat
// muis en dubbelklik doen (design.md). Platformonafhankelijk, zodat de toets het op elke runner kan narekenen; het
// tekenen en de muis van het systeem zitten in paneel_mac.mm.
//
// Het model leest de waarden uit de atomaire kopie van de plugin en schrijft wijzigingen alleen in de brievenbus
// (valkuil 14): nooit het Ontwerp, nooit iets van de audiothread.
#pragma once
#include <atomic>

#include "brievenbus.h"
#include "ontwerpen/keten_dub_echo.h"

namespace dub {

constexpr int N_KNOPPEN = pb::Ontwerp::N_PARAMS;
using Bus = Brievenbus<N_KNOPPEN>;

// --- maten in mockup-pixels (frontpaneel.svg op ware grootte; de schaduw valt binnen de maat) -------------------
constexpr double ONTWERP_B = 732, ONTWERP_H = 220;
constexpr double PANEEL_B = 726, PANEEL_H = 214;  // de rand; de schaduw 3 px rechts en onder
constexpr double KNOP_X0 = 62, KNOP_DX = 76, KNOP_Y = 130;  // kolom per knop 76 px
constexpr double BOOG_R = 22, KNOP_R = 16, WIJZER_R = 14;
constexpr double LABEL_DY = -34, WAARDE_DY = 44;
constexpr int BEGIN_B = 732, BEGIN_H = 220;  // beginmaat = de mockup op ware grootte
constexpr int MIN_B = 560, MIN_H = 220;
constexpr double SLEEP_PX = 200;  // 200 px omhoog = het hele bereik
constexpr double FIJN = 10;       // Shift: 10× fijner

// labels op het paneel (design.md: hooguit 11 tekens; de volle naam staat in PARAMS en in de tooltip)
extern const char* const LABELS[N_KNOPPEN];

// schaal en verschuiving van mockup naar view (gelijkmatig, in het midden)
struct Schaal {
  double s, ox, oy;
};
Schaal schaalVoor(double breedte, double hoogte);

double genormaliseerd(int i, double waarde);  // 0..1, lineair in de CLAP-waarde (zoals de host normaliseert)
double hoekVoor(double t);                     // radialen vanaf boven, met de klok mee: −135° .. +135°
double klemWaarde(int i, double v);
int knopOp(double x, double y);  // mockup-coördinaten → knopindex, of −1

struct PaneelModel {
  // van de plugin: alleen lezen (kopie) en klaarzetten (bus)
  const std::atomic<double>* kopie = nullptr;
  Bus* bus = nullptr;
  void (*vraagFlush)(void*) = nullptr;
  void* ctx = nullptr;

  double getoond[N_KNOPPEN] = {};  // wat het paneel laat zien
  int gekozen = -1;                // de laatst aangeraakte knop (boog in tape)
  int sleept = -1;
  double laatsteY = 0;

  void begin();
  // y in view-punten, omlaag positief (zoals een flipped NSView); x/y van de klik in mockup-coördinaten
  bool muisNeer(double x, double y, double viewY, int klikken);
  bool muisSleep(double viewY, bool fijn);
  bool muisOp();
  bool ververs();  // waarden van de plugin overnemen (timer); true = opnieuw tekenen

 private:
  void stuur(int i, double v);
  void flush();
};

}  // namespace dub
