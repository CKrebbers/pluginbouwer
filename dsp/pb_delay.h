// pb_delay.h — vertragingslijn met fractionele lezing.
// Lagrange 3e orde (4 punten), Laakso e.a. "Splitting the Unit Delay" (IEEE SP Magazine, 1996), §III-B:
// h(k) = Π_{j≠k} (D − j)/(k − j) voor k = 0..3. Wij lezen rond i = floor(d): punten i−1, i, i+1, i+2 met fractie
// f = d − i, dus de effectieve D = 1 + f ligt in [1, 2), het gebied met de kleinste fout van deze interpolator.
// d is minstens 1 sample (bij 1 is het eerste punt de sample die net geschreven is).
#pragma once
#include "pb_core.h"

namespace pb {

struct Vertraging {
  double* buf = nullptr;
  uint32_t masker = 0, pos = 0;

  // n = macht van twee, minstens maximale vertraging + 4
  static unsigned long geheugen(double maxSamples) { return bytesVoor<double>(machtVanTwee(maxSamples + 4)); }
  void koppel(Geheugen& g, double maxSamples) {
    uint32_t n = machtVanTwee(maxSamples + 4);
    buf = g.neem<double>(n);
    masker = buf ? n - 1 : 0;
    reset();
  }
  void reset() { if (buf) for (uint32_t i = 0; i <= masker; ++i) buf[i] = 0; pos = 0; }
  void schrijf(double x) { if (buf) { pos = (pos + 1) & masker; buf[pos] = x; } }
  double tik(uint32_t k) const { return buf[(pos - k) & masker]; }   // k samples geleden (0 = net geschreven)

  double leesLagrange(double d) const {
    if (!buf) return 0;
    if (d < 1.0) d = 1.0;
    if (d > (double)(masker - 3)) d = (double)(masker - 3);
    double fl = floor_(d);
    uint32_t i = (uint32_t)fl;
    double f = d - fl;
    double xm1 = tik(i - 1), x0 = tik(i), x1 = tik(i + 1), x2 = tik(i + 2);
    double fm1 = f - 1.0, fm2 = f - 2.0, fp1 = f + 1.0;
    double hm1 = -f * fm1 * fm2 * (1.0 / 6.0);
    double h0 = fp1 * fm1 * fm2 * 0.5;
    double h1 = -fp1 * f * fm2 * 0.5;
    double h2 = fp1 * f * fm1 * (1.0 / 6.0);
    return hm1 * xm1 + h0 * x0 + h1 * x1 + h2 * x2;
  }
};

}  // namespace pb
