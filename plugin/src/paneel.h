// paneel.h — het frontpaneel van PB Toetsenpad (fase 3; design.md en frontpaneel.svg), zonder platform.
//
// Het model rekent de indeling, tekent naar een lijst opdrachten in ontwerpcoördinaten (die van de mockup:
// 560 × 220, y omlaag, zoals de SVG) en vertaalt de muis naar parameterwijzigingen. Het tekent zelf niets en raakt
// de plugin niet aan: het leest waarden en stuurt wijzigingen via een Koppeling. De plugin zet een wijziging in een
// lock-vrije rij; process of params.flush geeft hem als GESTURE_BEGIN/PARAM_VALUE/GESTURE_END aan de host en past
// hem toe zoals een event van de host (valkuil 14). Het paneel raakt het Ontwerp en de audiothread nooit.
// paneel_mac.mm tekent de opdrachten met CoreGraphics en geeft de muis door; pb-paneeltoets toetst het model.
//
// Alles hier draait op de mainthread (de thread van de GUI).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace pb {
namespace paneel {

// design.md: de mockup is de kleinste maat; de beginmaat is 640 × 250. Groter schaalt alles gelijk mee.
constexpr double ONTWERP_B = 560, ONTWERP_H = 220;
constexpr double START_B = 640, START_H = 250;
constexpr double KOLOM = 76;             // breedte per knop
constexpr double KNOP_X0 = 62, KNOP_Y = 130;
constexpr double SLEEP_BEREIK = 200;     // punten omhoog = het hele bereik
constexpr double SHIFT_FIJNER = 10;
constexpr int LABEL_MAX = 11;            // tekens op het paneel; de volle naam in de tooltip

struct Kleur { uint8_t r, g, b; };
constexpr Kleur PANEEL{0x23, 0x26, 0x29};
constexpr Kleur INKT{0xe7, 0xe5, 0xdd};
constexpr Kleur TAPE{0xf2, 0xc2, 0x00};
constexpr Kleur SPOOR{0x40, 0xcb, 0xda};
constexpr Kleur GROEF{0x3a, 0x3e, 0x42};
constexpr Kleur SCHADUW{0x0d, 0x0e, 0x0f};

enum class Soort : uint8_t { Begin, Waarde, Eind };
struct Wijziging {
  Soort soort;
  int index;       // in PARAMS, geen id
  double waarde;   // alleen bij Waarde
};

// Wat het paneel van de plugin mag. Alle functies op de mainthread.
struct Koppeling {
  void* ctx = nullptr;
  double (*waarde)(void* ctx, int index) = nullptr;                          // de spiegel (wat de host ziet)
  void (*stuur)(void* ctx, const Wijziging& w) = nullptr;                    // in de rij, en request_flush
  void (*tik)(void* ctx) = nullptr;                                          // wat niet in de rij paste opnieuw
  bool (*tekst)(void* ctx, int index, double v, char* uit, uint32_t grootte) = nullptr;  // value_to_text
};

struct Knop {
  std::string naam;  // de volle naam (tooltip, host)
  double min, max, standaard;
};

enum class Letter : uint8_t { Mono, Kop };  // IBM Plex Mono 11 px, Silkscreen 14 px
constexpr double LETTER_PX[] = {11, 14};

struct Opdracht {
  enum Type : uint8_t { Vlak, Rand, Pad, Cirkel, Tekst } type;
  Kleur kleur{};          // Vlak/Rand/Pad/Tekst; Cirkel: de vulling
  Kleur rand{};           // Cirkel: de rand
  double dikte = 0;       // Rand/Pad/Cirkel
  double x = 0, y = 0, b = 0, h = 0;  // Vlak/Rand: rechthoek; Cirkel: middelpunt (x, y), straal b; Tekst: basislijn
  std::vector<std::pair<double, double>> punten;  // Pad: open lijn, rechte uiteinden
  std::string tekst;
  Letter letter = Letter::Mono;
  bool midden = true;     // Tekst: x is het midden (anders het begin)
};

class Paneel {
 public:
  // meet(letter, tekst) = de breedte in ontwerp-px (de tekenlaag kent het lettertype); leeg = een schatting
  Paneel(const Koppeling& k, std::vector<Knop> knoppen, std::string naam,
         std::function<double(Letter, const std::string&)> meet = {});

  // maat van het venster in punten; het ontwerp schaalt gelijk en staat in het midden
  void zetMaat(double b, double h);
  double schaal() const { return s_; }
  double ox() const { return ox_; }
  double oy() const { return oy_; }

  std::vector<Opdracht> teken() const;

  // muis in venstercoördinaten (punten, y omlaag)
  void muisNeer(double x, double y, int klikken, bool shift);
  void muisSleep(double x, double y, bool shift);
  void muisOp();
  void stop();  // het venster gaat weg: een open gebaar sluiten

  // timer (±30 Hz): true als er opnieuw getekend moet worden
  bool tik();

  int knopOp(double x, double y) const;  // −1 = geen knop
  struct Vlak { double x, y, b, h; };
  Vlak knopVlak(int i) const;            // in venstercoördinaten (tooltip)
  int gekozen() const { return gekozen_; }
  int sleept() const { return sleep_; }
  double getoond(int i) const;           // de waarde die het paneel nu laat zien
  int aantal() const { return (int)knoppen_.size(); }
  const Knop& knop(int i) const { return knoppen_[(size_t)i]; }
  std::string label(int i) const;
  std::string waardeTekst(int i) const;

  // de stand van een knop: 0..1 (lineair in de CLAP-waarde) en de hoek in graden (−135 .. +135, 0 = boven)
  double stand(int i, double v) const;
  static double hoek(double stand) { return -135.0 + 270.0 * stand; }

 private:
  void stuur(Soort s, int i, double v);
  double klem(int i, double v) const;

  Koppeling k_;
  std::vector<Knop> knoppen_;
  std::string naam_;
  std::function<double(Letter, const std::string&)> meet_;
  double s_ = 1, ox_ = 0, oy_ = 0;
  int gekozen_ = -1, sleep_ = -1;
  double startY_ = 0, startW_ = 0, vorigeY_ = 0;
  bool shift_ = false;
  std::vector<double> lokaal_;    // wat het paneel stuurde en (nog) toont
  std::vector<int> lokaalTik_;    // zolang > 0 toont het paneel lokaal_ (tot de spiegel hem overnam)
  mutable std::vector<double> getekend_;
  mutable int getekendGekozen_ = -2;
};

}  // namespace paneel
}  // namespace pb
