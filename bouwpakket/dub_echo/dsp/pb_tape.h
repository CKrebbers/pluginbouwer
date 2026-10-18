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
  // Asymmetrisch (17 okt 2026, ref/referentie/RAPPORT.md §2): tanh(u + β) − tanh(β). Een echte band (Akai GX-4000D)
  // geeft een tweede harmonische van −49 tot −32 dB; een symmetrische tanh geeft er geen. Primitieve
  // F(u) = log cosh(u + β) − log cosh(β) − tanh(β)·u (F(0) = 0, zoals na reset), dus dezelfde ADAA. tb = tanh(β),
  // lcb = log cosh(β); de aanroeper deelt door 1 − tb² zodat de versterking voor kleine signalen 1 blijft (anders
  // verschuift de grens van zelfoscillatie). β = 0: gebruik stap().
  double stapAsym(double u, double beta, double tb, double lcb) {
    double F = logcosh_(u + beta) - lcb - tb * u;
    double d = u - x1;
    double y = (d > 1e-6 || d < -1e-6) ? (F - F1) / d : tanh_(0.5 * (u + x1) + beta) - tb;
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

// Bandsnelheid (17 okt 2026, ref/referentie/RAPPORT.md §1): de relatieve afwijking ε(t) van de bandsnelheid, naar
// een Akai GX-4000D (Aalto-dataset, gemeten met pulsen tot op een fractie van een sample). Breedbandige ruis van
// 0,3 tot 20 Hz plus een smalle lijn op 7,7 Hz (de capstan). Haperen 0,25 ≈ de Akai (0,13–0,15 % RMS gemeten als
// echo, los van de delaytijd); haperen 1 = vier keer zoveel (een versleten band).
// De ruis loopt op een vaste stuurfrequentie (1000 per s) en wordt lineair geïnterpoleerd: dan is de statistiek
// gelijk op elke samplerate en zijn de normalisaties exact (berekend uit de impulsrespons, zie RAPPORT.md).
struct TapeSnelheid {
  static constexpr double FS2 = 1000.0;
  static constexpr double NB = 9.914624198181448;    // 1 / RMS van het breedbandfilter op gelijkmatige ruis in [−1, 1]
  static constexpr double NJ = 43.70195519537765;    // 1 / RMS van het jitterfilter (0,5 Hz)
  static constexpr double A_B = 0.00361152;          // breedband bij haperen 1 (4 × 0,0903 %)
  static constexpr double A_L = 0.0020736;           // lijn op 7,7 Hz bij haperen 1 (4 × 0,0518 %)
  double k20 = 0, k03 = 0, k05 = 0, lp1 = 0, lp2 = 0, hp = 0, jl = 0, vorig = 0, nu = 0, acc = 0, stapAcc = 0;
  double fase = 0, stapF = 0;
  Toeval t;
  void zet(double sr) {
    k20 = 1.0 - exp_(-TWO_PI * 20.0 / FS2);
    k03 = 1.0 - exp_(-TWO_PI * 0.3 / FS2);
    k05 = 1.0 - exp_(-TWO_PI * 0.5 / FS2);
    stapAcc = FS2 / sr;
    stapF = TWO_PI * 7.7 / sr;
  }
  void reset() { lp1 = lp2 = hp = jl = vorig = nu = acc = fase = 0; t.zaai(0x5EED1u); }
  void besturing() {
    double u = t.tussen(-1.0, 1.0), u2 = t.tussen(-1.0, 1.0);
    lp1 += (u - lp1) * k20;
    lp2 += (lp1 - lp2) * k20;
    hp += (lp2 - hp) * k03;
    vorig = nu;
    nu = (lp2 - hp) * NB;
    jl += (u2 - jl) * k05;
  }
  // ε bij haperen 1 (de aanroeper vermenigvuldigt met haperen)
  double stap() {
    acc += stapAcc;
    while (acc >= 1.0) { acc -= 1.0; besturing(); }
    double breed = vorig + (nu - vorig) * acc;
    fase += stapF * (1.0 + 0.015 * jl * NJ);
    if (fase >= TWO_PI) fase -= TWO_PI;
    if (fase < 0.0) fase += TWO_PI;
    return A_B * breed + A_L * SQRT2 * sin_(fase);
  }
};

// Wat de band tussen opname- en weergavekop deed: E(t) = Σ ε (in samples). Een vertraging met bandsnelheid leest op
// d = T − (E(t) − E(t − T)): de toonhoogte wijkt dan af met ε(t) − ε(t − T), los van de delaytijd, zoals bij een
// echte band (de snelheid tijdens opnemen en tijdens weergeven). Eén per band, gedeeld door beide kanalen.
struct BandGeheugen {
  double* buf = nullptr;
  uint32_t masker = 0, pos = 0;
  double E = 0;
  static unsigned long geheugen(double maxSamples) { return bytesVoor<double>(machtVanTwee(maxSamples + 4)); }
  void koppel(Geheugen& g, double maxSamples) {
    uint32_t n = machtVanTwee(maxSamples + 4);
    buf = g.neem<double>(n);
    masker = buf ? n - 1 : 0;
    reset();
  }
  void reset() { if (buf) for (uint32_t i = 0; i <= masker; ++i) buf[i] = 0; pos = 0; E = 0; }
  void schrijf(double eps) { E += eps; if (buf) { pos = (pos + 1) & masker; buf[pos] = E; } }
  // E(t) − E(t − Ts), lineair tussen twee samples
  double verschil(double Ts) const {
    if (!buf) return 0.0;
    if (Ts < 0.0) Ts = 0.0;
    if (Ts > (double)(masker - 2)) Ts = (double)(masker - 2);
    double fl = floor_(Ts);
    uint32_t i = (uint32_t)fl;
    double f = Ts - fl, a = buf[(pos - i) & masker], b = buf[(pos - i - 1) & masker];
    return E - (a + (b - a) * f);
  }
};

// Bandruis (17 okt 2026, RAPPORT.md §3): de sis van een echte band, zonder brom. Gelijkmatige ruis door drie
// eenpolige laagdoorlaten (20, 200, 2000 Hz) plus een direct deel, gefit op a + b·f per terts (0,1 dB RMS-fout tegen
// het doel, binnen 0,6 dB van de Akai tussen 500 Hz en 16 kHz). Niveau 1 = de Akai: −66,9 dBFS RMS. De ruis wordt
// per sample geschaald met √(sr/48000), zodat de dichtheid per Hz op elke samplerate dezelfde is.
struct BandRuis {
  static constexpr double C0 = 0.00056969, C1 = 0.00640118, C2 = 0.00124725, C3 = 0.00035469;
  Toeval t;
  double k1 = 0, k2 = 0, k3 = 0, schaal = 1, l1 = 0, l2 = 0, l3 = 0;
  void zet(double sr) {
    k1 = 1.0 - exp_(-TWO_PI * 20.0 / sr);
    k2 = 1.0 - exp_(-TWO_PI * 200.0 / sr);
    k3 = 1.0 - exp_(-TWO_PI * 2000.0 / sr);
    schaal = sqrt_(sr / 48000.0);
  }
  void reset(uint32_t zaad) { t.zaai(zaad); l1 = l2 = l3 = 0; }
  double stap() {
    double w = t.tussen(-1.0, 1.0) * schaal;
    l1 = flush(l1 + (w - l1) * k1);
    l2 = flush(l2 + (w - l2) * k2);
    l3 = flush(l3 + (w - l3) * k3);
    return C0 * w + C1 * l1 + C2 * l2 + C3 * l3;
  }
};

}  // namespace pb
