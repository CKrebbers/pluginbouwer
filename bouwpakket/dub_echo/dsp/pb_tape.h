// pb_tape.h — tape-saturatie en tape-beweging.
//
// Saturatie: tanh met eerste-orde antiderivative anti-aliasing (ADAA): J. Parker, V. Zavalishin, E. Le Bivic,
// "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution", DAFx 2016. In plaats van
// f(x[n]) neem je het gemiddelde van f over het lijnstuk x[n-1]..x[n]: (F(x[n]) − F(x[n-1])) / (x[n] − x[n-1]),
// met F = log cosh (de primitieve van tanh). Prijs: een halve sample vertraging (de dub-echo trekt die van de
// delaytijd af) en een lichte demping hoog. Geen hysterese: dit is een geheugenloze kromme, geen Jiles–Atherton.
//
// Beweging: wow (traag, ~0,6 Hz), flutter (snel, ~6,5 Hz) en een trage willekeurige drift uit een geseede
// xorshift. Alles in gehele getallen of eigen sin_: overal hetzelfde (CLAUDE.md regel 2).
#pragma once
#include "pb_core.h"

namespace pb {

struct TapeVerzadiging {
  double x1 = 0, F1 = 0;
  void reset() { x1 = 0; F1 = 0; }
  // u = signaal × drive; geeft tanh(u), anti-alias
  double stap(double u) {
    double F = logcosh_(u);
    double d = u - x1;
    double y = (d > 1e-6 || d < -1e-6) ? (F - F1) / d : tanh_(0.5 * (u + x1));
    x1 = u; F1 = F;
    return y;
  }
};

struct TapeBeweging {
  double fase1 = 0, fase2 = 0, stap1 = 0, stap2 = 0;
  double drift = 0, driftK = 0;
  Toeval toeval;
  void zet(double sr) {
    stap1 = TWO_PI * 0.6 / sr;   // wow  [ontwerpkeuze; RE-201-waarde niet bevestigd, zie SPEC.md]
    stap2 = TWO_PI * 6.5 / sr;   // flutter
    driftK = 1.0 - exp_(-TWO_PI * 1.5 / sr);
  }
  void reset() { fase1 = 0; fase2 = 1.0; drift = 0; toeval.zaai(0x51CEu); }
  // relatieve afwijking van de bandsnelheid bij diepte 1 (± ~0,55 %)
  double stap() {
    fase1 += stap1; if (fase1 >= TWO_PI) fase1 -= TWO_PI;
    fase2 += stap2; if (fase2 >= TWO_PI) fase2 -= TWO_PI;
    drift += (toeval.tussen(-1.0, 1.0) - drift) * driftK;
    return 0.0030 * sin_(fase1) + 0.0012 * sin_(fase2) + 0.0060 * drift;
  }
};

}  // namespace pb
