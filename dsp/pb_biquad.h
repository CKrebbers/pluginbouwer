// pb_biquad.h — biquad naar de RBJ-cookbook (W3C-kopie: https://www.w3.org/TR/audio-eq-cookbook/).
// Transposed direct form II in double. Coëfficiënten met pb::sin_/cos_ (geen libm, zie pb_math.h).
#pragma once
#include "pb_core.h"

namespace pb {

enum class BiquadSoort : int { laagdoor = 0, hoogdoor = 1, banddoor = 2, kerf = 3 };

struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;  // genormaliseerd op a0
  double z1 = 0, z2 = 0;

  void reset() { z1 = z2 = 0; }

  void zet(BiquadSoort soort, double f0, double Q, double sr) {
    f0 = klem(f0, 1.0, 0.49 * sr);
    if (Q < 1e-3) Q = 1e-3;
    double w0 = TWO_PI * f0 / sr;
    double cw = cos_(w0), sw = sin_(w0);
    double alpha = sw / (2.0 * Q);
    double a0 = 1.0 + alpha;
    double B0, B1, B2;
    switch (soort) {
      case BiquadSoort::laagdoor: B0 = (1.0 - cw) * 0.5; B1 = 1.0 - cw; B2 = B0; break;
      case BiquadSoort::hoogdoor: B0 = (1.0 + cw) * 0.5; B1 = -(1.0 + cw); B2 = B0; break;
      case BiquadSoort::banddoor: B0 = alpha; B1 = 0.0; B2 = -alpha; break;  // constante 0 dB piekversterking
      default: B0 = 1.0; B1 = -2.0 * cw; B2 = 1.0; break;
    }
    b0 = B0 / a0; b1 = B1 / a0; b2 = B2 / a0;
    a1 = (-2.0 * cw) / a0; a2 = (1.0 - alpha) / a0;
  }

  double stap(double x) {
    double y = b0 * x + z1;
    z1 = flush(b1 * x - a1 * y + z2);
    z2 = flush(b2 * x - a2 * y);
    return y;
  }
};

}  // namespace pb
