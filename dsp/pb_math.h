// pb_math.h — wiskunde zonder libm, bit voor bit gelijk in WASM en native.
//
// Waarom: libm verschilt per platform (glibc, Apple libm, de Math van een browser) in de laatste bits. In een
// feedbacklus groeit één bit uit tot een hoorbaar verschil, en dan haalt de null-test tussen browser en plugin
// −90 dBFS niet meer. Deze functies gebruiken alleen + − × ÷, sqrt en floor: die zijn in IEEE 754 exact afgerond,
// dus overal hetzelfde zolang de compiler niets samentrekt (-ffp-contract=off, geen -ffast-math).
//
// Kernpolynomen: fdlibm (Sun Microsystems, 1993; "Permission to use, copy, modify, and distribute this software is
// freely granted, provided that this notice is preserved."). De coëfficiënten zijn overgenomen, de rest is eigen code.
// Nauwkeurigheid getoetst tegen Math.* in tests/math.test.mjs.
#pragma once
#include <stdint.h>

namespace pb {

constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 6.28318530717958647692;
constexpr double LN2 = 0.69314718055994530942;

inline double fabs_(double x) { return x < 0 ? -x : x; }
inline double sqrt_(double x) { return __builtin_sqrt(x); }   // wordt f64.sqrt / fsqrt, geen libm-aanroep
inline double floor_(double x) { return __builtin_floor(x); }

inline uint64_t bits(double x) { uint64_t u; __builtin_memcpy(&u, &x, 8); return u; }
inline double vanBits(uint64_t u) { double x; __builtin_memcpy(&x, &u, 8); return x; }

// --- sin en cos -------------------------------------------------------------------------------------------
// Kern op [-π/4, π/4] (fdlibm __kernel_sin/__kernel_cos), argumentreductie met Cody-Waite in drie stukken van π/2.
// Goed tot |x| < 1e6; daarboven niet bedoeld (een fase hoort binnen [0, 2π) te blijven).
inline double kernSin(double x) {
  const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
               S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
               S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
  double z = x * x, v = z * x;
  double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  return x + v * (S1 + z * r);
}
inline double kernCos(double x) {
  const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
               C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
               C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
  double z = x * x;
  double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  double hz = 0.5 * z, w = 1.0 - hz;
  return w + (((1.0 - w) - hz) + z * r);
}
inline void reduceer(double x, double& r, int& kwadrant) {
  const double invpio2 = 6.36619772367581382433e-01;
  const double pio2_1 = 1.57079632673412561417e+00, pio2_2 = 6.07710050630396597660e-11,
               pio2_3 = 2.02226624871116645580e-21;
  double k = floor_(x * invpio2 + 0.5);
  r = ((x - k * pio2_1) - k * pio2_2) - k * pio2_3;
  int64_t ki = (int64_t)k;
  kwadrant = (int)(ki & 3);
}
inline double sin_(double x) {
  double r; int q; reduceer(x, r, q);
  switch (q) { case 0: return kernSin(r); case 1: return kernCos(r); case 2: return -kernSin(r); default: return -kernCos(r); }
}
inline double cos_(double x) {
  double r; int q; reduceer(x, r, q);
  switch (q) { case 0: return kernCos(r); case 1: return -kernSin(r); case 2: return -kernCos(r); default: return kernSin(r); }
}
inline double tan_(double x) { return sin_(x) / cos_(x); }

// --- exp ---------------------------------------------------------------------------------------------------
// x = k·ln2 + r met |r| ≤ ln2/2, dan de rationale benadering van fdlibm e_exp.c, en 2^k via de exponent-bits.
inline double exp_(double x) {
  if (x != x) return x;
  if (x > 709.78) return vanBits(0x7ff0000000000000ULL);   // +inf
  if (x < -745.13) return 0.0;
  const double ln2hi = 6.93147180369123816490e-01, ln2lo = 1.90821492927058770002e-10;
  const double invln2 = 1.44269504088896338700e+00;
  const double P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03,
               P3 = 6.61375632143793436117e-05, P4 = -1.65339022054652515390e-06,
               P5 = 4.13813679705723846039e-08;
  double k = floor_(x * invln2 + 0.5);
  double hi = x - k * ln2hi, lo = k * ln2lo, r = hi - lo;
  double t = r * r;
  double c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
  int ki = (int)k;
  // 2^k in twee stappen zodat ook subnormale uitkomsten kloppen
  if (ki < -1021) { y *= vanBits((uint64_t)(ki + 1000 + 1023) << 52); return y * vanBits((uint64_t)(-1000 + 1023) << 52); }
  if (ki > 1023) { y *= 2.0; ki -= 1; }
  return y * vanBits((uint64_t)(ki + 1023) << 52);
}

// --- log ---------------------------------------------------------------------------------------------------
// x = 2^k · (1+f) met √2/2 ≤ 1+f < √2, dan log(1+f) via s = f/(2+f) en de reeks Lg1..Lg7 (fdlibm e_log.c).
inline double log_(double x) {
  if (x != x || x < 0) return vanBits(0x7ff8000000000000ULL);  // NaN
  if (x == 0) return vanBits(0xfff0000000000000ULL);             // -inf
  if (bits(x) >= 0x7ff0000000000000ULL) return x;               // +inf
  const double Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01,
               Lg3 = 2.857142874366239149e-01, Lg4 = 2.222219843214978396e-01,
               Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
               Lg7 = 1.479819860511658591e-01;
  const double ln2hi = 6.93147180369123816490e-01, ln2lo = 1.90821492927058770002e-10;
  int k = 0;
  if (x < 2.2250738585072014e-308) { x *= 18014398509481984.0; k -= 54; }   // subnormaal: ×2^54
  uint64_t u = bits(x);
  k += (int)((u >> 52) & 0x7ff) - 1023;
  u &= 0x000fffffffffffffULL;
  double m = vanBits(u | 0x3ff0000000000000ULL);                // m in [1, 2)
  if (m > 1.41421356237309504880) { m *= 0.5; k += 1; }
  double f = m - 1.0;
  double s = f / (2.0 + f), z = s * s, w = z * z;
  double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
  double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
  double R = t2 + t1, hfsq = 0.5 * f * f;
  double dk = (double)k;
  return dk * ln2hi - ((hfsq - (s * (hfsq + R) + dk * ln2lo)) - f);
}

// log(1+y), nauwkeurig voor kleine y (Goldberg: de afrondingsfout van 1+y wordt rechtgezet).
inline double log1p_(double y) {
  double u = 1.0 + y;
  if (u == 1.0) return y;
  return log_(u) * y / (u - 1.0);
}

// --- tanh en log(cosh) --------------------------------------------------------------------------------------
inline double tanh_(double x) {
  double a = fabs_(x);
  if (a > 22.0) return x < 0 ? -1.0 : 1.0;
  double t;
  if (a < 1e-4) {
    double z = a * a;
    t = a * (1.0 - z * (1.0 / 3.0 - z * (2.0 / 15.0)));
  } else {
    double e = exp_(-2.0 * a);
    t = (1.0 - e) / (1.0 + e);
  }
  return x < 0 ? -t : t;
}
// F(x) = log(cosh x), de primitieve van tanh, voor ADAA. log cosh x = |x| + log1p(e^{-2|x|}) − ln2.
inline double logcosh_(double x) {
  double a = fabs_(x);
  if (a < 1e-4) { double z = a * a; return z * (0.5 - z * (1.0 / 12.0)); }
  return a + log1p_(exp_(-2.0 * a)) - LN2;
}

// --- hulp --------------------------------------------------------------------------------------------------
inline double dbNaarVersterking(double db) { return exp_(db * 0.11512925464970228420); }  // ln(10)/20
inline double klem(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

}  // namespace pb
