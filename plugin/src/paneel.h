// paneel.h — het frontpaneel van Toetsenpad (design.md, frontpaneel.svg), los van het venstersysteem.
//
// Het paneel tekent zichzelf in software in een RGBA-buffer (vormen met anti-aliasing, tekst met stb_truetype en de
// ingebakken lettertypen IBM Plex Mono en Silkscreen) en vertaalt muisacties naar gebaren (BEGIN/WAARDE/EIND). Zo
// tekent het overal hetzelfde: in de Cocoa-view (paneel_mac.mm), en in de toets op Linux (plugin/toets/paneeltoets.cpp).
//
// Het paneel raakt het Ontwerp nooit aan (valkuil 14): het leest waarden via PaneelBron::waarde (de atomaire spiegel)
// en geeft een verandering door via PaneelBron::stuur (de lock-vrije rij in toetsenpad.cpp, die process of
// params.flush als events naar de host stuurt en zelf toepast). Alles hier draait op de mainthread.
#pragma once

#include <cstdint>
#include <vector>

namespace pb_paneel {

constexpr int MAX_KNOPPEN = 8;

// soorten gebaren voor PaneelBron::stuur
enum Gebaar { BEGIN = 0, WAARDE = 1, EIND = 2 };
// soorten muisacties voor Paneel::muis
enum Muis { NEER = 0, SLEEP = 1, OP = 2, DUBBEL = 3 };

struct Knop {
  const char* label;  // op het paneel (hooguit 11 tekens)
  const char* naam;   // volle naam (tooltip)
  double min, max, standaard;
};

struct PaneelBron {
  virtual ~PaneelBron() = default;
  virtual double waarde(int knop) = 0;                             // wat de host ziet (de spiegel)
  virtual bool uitstaand(int knop) = 0;                            // het paneel stuurde iets dat nog niet aankwam
  virtual void tekst(int knop, double v, char* uit, uint32_t n) = 0;  // value_to_text
  virtual void stuur(Gebaar g, int knop, double v) = 0;           // naar de rij (BEGIN/WAARDE/EIND)
};

class Paneel {
 public:
  static constexpr double MOCKUP_B = 560, MOCKUP_H = 220;  // frontpaneel.svg op ware grootte
  static constexpr double BEGIN_B = 640, BEGIN_H = 250;    // design.md "Beginmaat"
  static constexpr double MIN_B = 560, MIN_H = 220;        // design.md "Minimaal"
  static constexpr double MAX_B = 2240, MAX_H = 880;

  Paneel(PaneelBron& bron, const Knop* knoppen, int n);

  void maat(double b, double h);  // in punten (logisch); het paneel schaalt mee
  double breedte() const { return b_; }
  double hoogte() const { return h_; }

  // tekent het hele paneel in rgba (bw × bh pixels, 4 bytes per pixel, rij na rij), schaal = pixels per punt
  void teken(uint8_t* rgba, int bw, int bh, double schaal);

  // muisactie op (x, y) in punten, y omlaag. Geeft true als het paneel opnieuw getekend moet worden.
  bool muis(Muis soort, double x, double y, bool shift);

  // voor de timer van het venster: true als een getoonde waarde veranderde sinds de laatste teken()
  bool veranderd();

  // geometrie, voor tooltips en toetsen (in punten)
  int aantal() const { return n_; }
  void knopMidden(int k, double* x, double* y) const;
  void knopVlak(int k, double* x, double* y, double* b, double* h) const;
  const char* knopNaam(int k) const { return knop_[k].naam; }
  int gekozen() const { return gekozen_; }
  double getoond(int k);  // de waarde die het paneel nu toont

 private:
  double schaalOntwerp() const;
  int knopOp(double x, double y) const;
  void zet(int k, double v);

  PaneelBron& bron_;
  Knop knop_[MAX_KNOPPEN];
  int n_;
  double b_ = BEGIN_B, h_ = BEGIN_H;
  int gekozen_ = -1;   // de laatst aangeraakte knop (zijn boog is tape)
  int sleept_ = -1;    // knop die nu gesleept wordt
  bool begonnen_ = false;  // BEGIN is gestuurd voor sleept_
  double vorigeY_ = 0;
  double lokaal_[MAX_KNOPPEN] = {};  // laatst gestuurde waarde per knop
  double getekend_[MAX_KNOPPEN] = {};
  int getekendGekozen_ = -2;
};

// PNG zonder compressie (stored deflate), voor de schermafbeelding in de toets. rgba: b × h × 4.
std::vector<uint8_t> maakPng(const uint8_t* rgba, int b, int h);

}  // namespace pb_paneel
