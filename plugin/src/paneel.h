// paneel.h — het frontpaneel uit design.md, los van het platform: indeling, tekenen (tekenaar.h) en de muis.
// Alles hier draait op de mainthread. Het paneel leest de parameterwaarden van de spiegel die de host ook leest
// (params.get) en stuurt veranderingen als berichten (ui_wachtrij.h); het raakt het Ontwerp en de audiothread nooit.
//
// Indeling: één rij van 8 draaiknoppen (270°, −135° tot +135°) in de volgorde van PARAMS (= SPEC.md), naam erboven,
// waarde in gewone eenheden eronder (param_tekst.h, dezelfde tekst als de host toont). Slepen omhoog = meer
// (200 punten = het hele bereik), Shift + slepen = tien keer fijner, dubbelklik = standaard.
// Minimaal 560 × 220 punten; groter schaalt alles mee (de indeling blijft gecentreerd).
#pragma once
#include "tekenaar.h"
#include "ui_wachtrij.h"

#include "ontwerpen/keten_dub_echo.h"

namespace paneel {

constexpr int N = pb::Ontwerp::N_PARAMS;
constexpr double MIN_B = 560, MIN_H = 220;        // design.md
constexpr double STANDAARD_B = 640, STANDAARD_H = 250;
constexpr double SLEEP_BEREIK = 200;              // punten slepen voor het hele bereik
constexpr double FIJN = 10;                       // Shift: zoveel keer fijner

// design.md
constexpr Kleur PANEEL = 0x232629, INKT = 0xe7e5dd, TAPE = 0xf2c200, SPOOR = 0x40cbda;
constexpr Kleur SCHADUW = 0x0d0e0f, GROEF = 0x3a3e42;

// wat het paneel van de plugin nodig heeft (alleen op de mainthread aangeroepen)
struct Koppeling {
  void* ctx;
  double (*lees)(void* ctx, int index);         // de waarde zoals de host hem ziet (params.get)
  bool (*stuur)(void* ctx, const Bericht& b);   // naar de host: wachtrij + host_params.request_flush; false: vol
};

class Paneel {
 public:
  explicit Paneel(Koppeling k);

  // logische maat in punten en het aantal pixels per punt; tekent opnieuw als er iets veranderde
  const Beeld& teken(double b, double h, double schaal);

  // de muis, in logische punten vanaf linksboven
  void muisNeer(double x, double y, bool shift, bool dubbel);
  void muisSleep(double x, double y, bool shift);
  void muisOp();

  // timer (±30 Hz): leest de spiegel; true als het paneel opnieuw getekend moet worden
  bool ververs();

  int knopOp(double x, double y) const;  // index in PARAMS, of −1

 private:
  struct Indeling { double s, ox, oy; };  // basis 560 × 220 → punten: x·s + ox
  Indeling indeling() const;
  void stuurWaarde(int i, double v);
  double getoond(int i) const;

  Koppeling k_;
  Beeld beeld_;
  bool vuil_ = true;
  double b_ = STANDAARD_B, h_ = STANDAARD_H, schaal_ = 1;
  double spiegel_[N];       // laatst gelezen van de host
  double verstuurd_[N];     // laatst door het paneel gestuurd
  double spiegelToen_[N];   // de spiegel op dat moment
  bool wacht_[N];           // gestuurd, maar de host heeft het nog niet teruggemeld
  int sleep_ = -1, gekozen_ = -1;
  double sleepY_ = 0, sleepWaarde_ = 0;
};

}  // namespace paneel
