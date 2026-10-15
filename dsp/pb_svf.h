// pb_svf.h — filters met topologie-behoudende transformatie (TPT): stabiel als je ze snel moduleert.
// V. Zavalishin, "The Art of VA Filter Design" rev. 2.1.2, hfst. 3 (één-pool) en 4 (SVF); de vorm per sample zoals
// A. Simper (Cytomic, "SvfLinearTrapOptimised2", 2013) hem uitschrijft. Beide zijn de bilineaire transformatie met
// voorvervorming van het analoge prototype; tests/dubecho.test.mjs legt de respons tegen precies die formule.
#pragma once
#include "pb_core.h"

namespace pb {

// SVF: laagdoor/banddoor/hoogdoor uit één toestand. k = 1/Q (de demping), g = tan(π·fc/fs).
struct Svf {
  double a1 = 1, a2 = 0, a3 = 0, k = 1.4142135623730951;
  double ic1 = 0, ic2 = 0;
  double lp = 0, bp = 0, hp = 0;
  void reset() { ic1 = ic2 = 0; }
  void zet(double fc, double Q, double sr) {
    fc = klem(fc, 1.0, 0.49 * sr);
    double g = tan_(PI * fc / sr);
    k = 1.0 / (Q < 0.05 ? 0.05 : Q);
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  void stap(double x) {
    double v3 = x - ic2;
    double v1 = a1 * ic1 + a2 * v3;
    double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = flush(2.0 * v1 - ic1);
    ic2 = flush(2.0 * v2 - ic2);
    lp = v2; bp = v1; hp = x - k * v1 - v2;
  }
};

// één-pool TPT: laagdoor en hoogdoor (hoogdoor = x − laagdoor). G = g/(1+g).
struct EenPool {
  double G = 0, s = 0;
  void reset() { s = 0; }
  void zet(double fc, double sr) { fc = klem(fc, 1.0, 0.49 * sr); double g = tan_(PI * fc / sr); G = g / (1.0 + g); }
  double laag(double x) { double v = (x - s) * G; double y = v + s; s = flush(y + v); return y; }
  double hoog(double x) { return x - laag(x); }
};

}  // namespace pb
