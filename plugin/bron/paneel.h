// paneel.h — het frontpaneel uit design.md, los van het venstersysteem.
// Het Paneel tekent zelf in een pixelbuffer (0x00RRGGBB per pixel, rij voor rij) en verwerkt muis-invoer. Het kent
// de plugin alleen via PaneelBron: waarden lezen, tekst maken, en gebaren melden (begin/zet/eind). Die gaan naar de
// plugin-schil, die ze in process/flush als events toepast en aan de host meldt; het paneel raakt de DSP nooit.
// Alles in dit bestand draait op de mainthread (de UI-thread van de host).
#pragma once

#include <clap/clap.h>

#include <cstdint>
#include <vector>

namespace pb {

// Wat het paneel van een parameter weet (uit PARAMS, door de schil doorgegeven).
struct KnopInfo {
  const char* naam;
  double min, max, standaard;
  bool logaritmisch;  // de knop draait in verhouding (Hz, ms): elke slag even ver op het gehoor
};

class PaneelBron {
 public:
  virtual ~PaneelBron() = default;
  virtual int aantal() const = 0;
  virtual KnopInfo info(int i) const = 0;
  virtual double waarde(int i) const = 0;                                  // wat de knop nu toont
  virtual void tekst(int i, double v, char* tekst, uint32_t grootte) const = 0;  // value_to_text van de schil
  virtual void begin(int i) = 0;                                           // gebaar begint (muis neer)
  virtual void zet(int i, double v) = 0;                                   // nieuwe waarde tijdens het gebaar
  virtual void eind(int i) = 0;                                            // gebaar klaar (muis op)
};

// De maten uit design.md, in logische pixels.
constexpr uint32_t PANEEL_MIN_B = 560, PANEEL_MIN_H = 220;
constexpr uint32_t PANEEL_STD_B = 700, PANEEL_STD_H = 275;  // 1,25 × het minimum: leesbaar op een gewoon scherm

struct Glief;
struct Lettertype;

class Paneel {
 public:
  explicit Paneel(PaneelBron& bron);
  ~Paneel();

  // logische grootte (punten) en pixels per punt (2 op een Retina-scherm)
  void zetGrootte(double b, double h, double schaal);
  int pixelB() const { return pb_; }
  int pixelH() const { return ph_; }
  const uint32_t* pixels() const { return pix_.data(); }

  // Tekent opnieuw als er iets veranderd is (waarden, keuze, grootte). Geeft true als de pixels nieuw zijn.
  bool teken();
  // Voor de timer van het venster: kijkt of de host waarden veranderde; true = opnieuw tekenen nodig.
  bool tik();

  // Muis in logische coördinaten (oorsprong linksboven).
  void muisNeer(double x, double y, bool fijn, bool dubbel);
  void muisSleep(double x, double y, bool fijn);
  void muisOp();
  bool sleept() const { return sleepKnop_ >= 0; }

  // de knop onder dit punt, of -1 (ook voor toetsen: waar staat knop i)
  int knopOp(double x, double y) const;
  void knopMidden(int i, double* x, double* y) const;

 private:
  double naarNorm(int i, double v) const;
  double uitNorm(int i, double n) const;
  void tekenAlles();
  // tekenen in pixels (alles al geschaald)
  void vul(double x0, double y0, double x1, double y1, uint32_t kleur);
  void kader(double x0, double y0, double x1, double y1, double dikte, uint32_t kleur);
  void schijf(double cx, double cy, double r, uint32_t kleur);
  void ring(double cx, double cy, double r, double dikte, uint32_t kleur);
  void boog(double cx, double cy, double r, double dikte, double a0, double a1, uint32_t kleur);
  void lijn(double x0, double y0, double x1, double y1, double dikte, uint32_t kleur);
  double tekstBreedte(Lettertype& f, double px, const char* s);
  void tekst(Lettertype& f, double px, double x, double yBasis, const char* s, uint32_t kleur, int uitlijn);
  void meng(int x, int y, uint32_t kleur, double dekking);

  PaneelBron& bron_;
  int n_ = 0;
  double b_ = PANEEL_STD_B, h_ = PANEEL_STD_H, schaal_ = 1;
  int pb_ = 0, ph_ = 0;
  std::vector<uint32_t> pix_;
  double k_ = 1, ox_ = 0, oy_ = 0;  // logisch → pixels: px = ox + k·x

  std::vector<double> getoond_;  // de waarden die nu in de pixels staan
  int gekozen_ = -1;             // de laatst aangeraakte knop (boog in "tape")
  int getoondGekozen_ = -2;
  bool vuil_ = true;

  int sleepKnop_ = -1;
  double sleepY_ = 0, sleepNorm_ = 0;

  Lettertype* plex_ = nullptr;
  Lettertype* plexMedium_ = nullptr;
  Lettertype* silk_ = nullptr;
};

// Het venster rond het Paneel; één implementatie per platform (venster_mac.mm, venster_x11.cpp).
class Venster {
 public:
  virtual ~Venster() = default;
  virtual bool zetOuder(const clap_window_t* ouder) = 0;
  virtual bool zetGrootte(uint32_t b, uint32_t h) = 0;
  virtual bool toon() = 0;
  virtual bool verberg() = 0;
  virtual void opTimer(clap_id) {}
  virtual void opFd(int, clap_posix_fd_flags_t) {}
};

// De CLAP-venster-API van dit platform (CLAP_WINDOW_API_COCOA / _X11), of nullptr als er geen paneel is.
const char* vensterApi();
// Een nieuw venster voor dit paneel; de host is nodig voor timers en fd's (Linux). nullptr als het niet lukt.
Venster* maakVenster(Paneel& paneel, const clap_host_t* host, uint32_t b, uint32_t h);

}  // namespace pb
