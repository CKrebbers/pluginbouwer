// tekenaar.h — een klein software-tekenbord voor het frontpaneel: vlakken, ringen, bogen, lijnen en tekst in een
// pixelbuffer (0xAARRGGBB, rij na rij). Zo tekent het paneel op elk platform pixel voor pixel hetzelfde, en kan een
// toets het zonder venster vergelijken. Het platform (Cocoa, X11) zet de buffer alleen op het scherm.
// Randen worden met dekking (anti-aliasing) getekend; geen verlopen (design.md).
#pragma once
#include <cstdint>
#include <vector>

namespace paneel {

using Kleur = uint32_t;  // 0xRRGGBB

struct Lettertype;  // stb_truetype achter de schermen (tekenaar.cpp)
const Lettertype* plexMono();
const Lettertype* silkscreen();

class Beeld {
 public:
  void maat(int w, int h);
  int breedte() const { return w_; }
  int hoogte() const { return h_; }
  const uint32_t* pixels() const { return px_.data(); }

  void vul(Kleur k);
  void rechthoek(double x0, double y0, double x1, double y1, Kleur k);  // op hele pixels (harde randen)
  void schijf(double cx, double cy, double r, Kleur k);
  void ring(double cx, double cy, double r, double dikte, Kleur k);
  // boog van hoek a0 tot a1 (radialen, met de klok mee, 0 = boven), rechte uiteinden
  void boog(double cx, double cy, double r, double dikte, double a0, double a1, Kleur k);
  void lijn(double x0, double y0, double x1, double y1, double dikte, Kleur k);

  // tekst (UTF-8) met de basislijn op y; geeft de breedte terug. `midden`: x is het midden van de tekst.
  double tekst(const Lettertype* f, double px, double x, double y, const char* t, Kleur k, bool midden = false);
  static double tekstBreedte(const Lettertype* f, double px, const char* t);

 private:
  void meng(int x, int y, Kleur k, double dekking);
  int w_ = 0, h_ = 0;
  std::vector<uint32_t> px_;
  std::vector<unsigned char> glyph_;
};

}  // namespace paneel
