// pb_graaf.h — de ketenmotor: een signaalketen als DATA, per sample uitgevoerd. Dit is wat de ontwerper tekent.
//
// Dezelfde code draait in de tool (WASM, keten geladen tijdens het ontwerpen) en in de plugin (keten als constante,
// gegenereerd door tools/bouwpakket.mjs). Toets: de dub-echo als keten klinkt bit voor bit als de handgeschreven
// dsp/ontwerpen/dub_echo.h (tests/graaf.test.mjs).
//
// Beschrijving (gehele getallen en doubles, zodat JS en een gegenereerde header hem op dezelfde manier geven):
//   knopen in UITVOERVOLGORDE (de tool rekent die uit). Een ingang die naar een knoop wijst die LATER komt, leest
//   diens uitgang van de vorige sample: zo werkt een terugkoppellus (z⁻¹), zonder aparte vlag.
//   Gedeelde knopen (bandbeweging, LFO) rekenen één keer per sample, vóór de kanalen.
//   Een knoopparameter is een constante of een plugin-parameter (index), met optioneel een modulatiebron.
#pragma once
#include "pb_biquad.h"
#include "pb_delay.h"
#include "pb_svf.h"
#include "pb_tape.h"

namespace pb {

enum class Blok : int32_t {
  in = 0, uit = 1, som = 2, versterker = 3, tape = 4, vertraging = 5, hoogdoor1 = 6, svf = 7,
  bandbeweging = 8, mix = 9, biquad = 10, lfo = 11, laagdoor1 = 12, aantal
};
constexpr int MAX_KNOPEN = 32, MAX_IN = 2, MAX_UIT = 3, MAX_KP = 4, MAX_PARAMS = 32, MAX_VERTRAGING = 6;

struct Bron { int32_t knoop = -1, poort = 0; };                         // -1 = niets aangesloten (0)
struct KnoopParam {
  int32_t param = -1;     // index in de plugin-parameters, of -1 = constante
  double waarde = 0;      // de constante
  int32_t modKnoop = -1, modPoort = 0; double modDiepte = 0;  // optioneel: + diepte·(max−min)·m, m ∈ [−1, 1]
};
struct KnoopBeschrijving {
  Blok soort = Blok::in;
  Bron in[MAX_IN];
  KnoopParam kp[MAX_KP];
  double vast[2] = {0, 0};   // vaste eigenschappen (vertraging: max ms, compensatie in samples; svf: welke uitgang)
};
struct ParamBeschrijving { double min = 0, max = 1, standaard = 0, glijMs = 0; };
struct GraafBeschrijving {
  int32_t nKnopen = 0, nParams = 0;
  KnoopBeschrijving knoop[MAX_KNOPEN];
  ParamBeschrijving param[MAX_PARAMS];
};

// De platte vorm (één rij doubles), gedeeld door JS (tool) en de gegenereerde header (plugin):
//   [nKnopen, nParams,
//    per knoop: soort, in0.knoop, in0.poort, in1.knoop, in1.poort,
//               4× (param, waarde, modKnoop, modPoort, modDiepte), vast0, vast1,     → 27 getallen
//    per param: min, max, standaard, glijMs]                                         →  4 getallen
// Geeft het aantal gelezen getallen terug, of 0 als de beschrijving niet klopt.
constexpr int GETALLEN_PER_KNOOP = 27, GETALLEN_PER_PARAM = 4;
inline int leesBeschrijving(const double* d, int n, GraafBeschrijving& g) {
  if (n < 2) return 0;
  int nk = (int)d[0], np = (int)d[1];
  if (nk < 0 || nk > MAX_KNOPEN || np < 0 || np > MAX_PARAMS) return 0;
  int nodig = 2 + nk * GETALLEN_PER_KNOOP + np * GETALLEN_PER_PARAM;
  if (n < nodig) return 0;
  g.nKnopen = nk; g.nParams = np;
  const double* p = d + 2;
  for (int k = 0; k < nk; ++k, p += GETALLEN_PER_KNOOP) {
    auto& kn = g.knoop[k];
    int soort = (int)p[0];
    if (soort < 0 || soort >= (int)Blok::aantal) return 0;
    kn.soort = (Blok)soort;
    for (int i = 0; i < MAX_IN; ++i) {
      kn.in[i].knoop = (int32_t)p[1 + 2 * i]; kn.in[i].poort = (int32_t)p[2 + 2 * i];
      if (kn.in[i].knoop >= nk || kn.in[i].poort < 0 || kn.in[i].poort >= MAX_UIT) return 0;
    }
    for (int j = 0; j < MAX_KP; ++j) {
      const double* q = p + 5 + 5 * j;
      kn.kp[j].param = (int32_t)q[0]; kn.kp[j].waarde = q[1];
      kn.kp[j].modKnoop = (int32_t)q[2]; kn.kp[j].modPoort = (int32_t)q[3]; kn.kp[j].modDiepte = q[4];
      if (kn.kp[j].param >= np || kn.kp[j].modKnoop >= nk) return 0;
    }
    kn.vast[0] = p[25]; kn.vast[1] = p[26];
  }
  for (int i = 0; i < np; ++i, p += GETALLEN_PER_PARAM) {
    g.param[i].min = p[0]; g.param[i].max = p[1]; g.param[i].standaard = p[2]; g.param[i].glijMs = p[3];
  }
  return nodig;
}

inline bool isGedeeld(Blok b) { return b == Blok::bandbeweging || b == Blok::lfo; }

// geheugen dat een beschrijving nodig heeft op samplerate sr (vertragingslijnen, 2 kanalen)
inline unsigned long graafGeheugen(const GraafBeschrijving& g, double sr) {
  unsigned long n = 0;
  for (int k = 0; k < g.nKnopen; ++k)
    if (g.knoop[k].soort == Blok::vertraging) n += 2 * Vertraging::geheugen(g.knoop[k].vast[0] * 0.001 * sr * 1.02 + 8);
  return n;
}

struct Graaf {
  const GraafBeschrijving* b = nullptr;
  double sr = 48000;
  Glijder glij[MAX_PARAMS];
  bool vers = true;
  // toestand per knoop en kanaal
  double uitw[MAX_KNOPEN][2][MAX_UIT];
  Biquad bq[MAX_KNOPEN][2];
  Svf svf[MAX_KNOPEN][2];
  EenPool een[MAX_KNOPEN][2];
  TapeVerzadiging sat[MAX_KNOPEN][2];
  Vertraging vt[MAX_KNOPEN][2];
  TapeBeweging band[MAX_KNOPEN];
  double lfoFase[MAX_KNOPEN];
  double drive[MAX_KNOPEN], invDrive[MAX_KNOPEN];

  void koppel(const GraafBeschrijving* beschr) {
    b = beschr;
    for (int i = 0; i < b->nParams; ++i) glij[i].doel = glij[i].nu = b->param[i].standaard;
  }
  void activeer(double samplerate, Geheugen g) {
    sr = samplerate;
    for (int i = 0; i < b->nParams; ++i) glij[i].zetTijd(b->param[i].glijMs, sr);
    for (int k = 0; k < b->nKnopen; ++k) {
      const auto& kn = b->knoop[k];
      if (kn.soort == Blok::vertraging) for (auto& v : vt[k]) v.koppel(g, kn.vast[0] * 0.001 * sr * 1.02 + 8);
      if (kn.soort == Blok::bandbeweging) band[k].zet(sr);
    }
    reset();
  }
  void reset() {
    for (int k = 0; k < MAX_KNOPEN; ++k) {
      for (int c = 0; c < 2; ++c) {
        for (auto& u : uitw[k][c]) u = 0;
        bq[k][c].reset(); svf[k][c].reset(); een[k][c].reset(); sat[k][c].reset(); vt[k][c].reset();
      }
      band[k].reset(); lfoFase[k] = 0;
    }
    vers = true;
  }
  void zetParam(int i, double v) { if (i >= 0 && i < b->nParams) glij[i].doel = klem(v, b->param[i].min, b->param[i].max); }
  double leesParam(int i) const { return (i >= 0 && i < b->nParams) ? glij[i].doel : 0.0; }

  // de waarde van knoopparameter j van knoop k, nu (zonder modulatie: die komt per kanaal in waardeMod)
  double basis(int k, int j) const { const auto& p = b->knoop[k].kp[j]; return p.param >= 0 ? glij[p.param].nu : p.waarde; }
  bool beweegt(int k, int j) const {
    const auto& p = b->knoop[k].kp[j];
    return p.modKnoop >= 0;
  }
  double waardeMod(int k, int j, int c) const {
    const auto& p = b->knoop[k].kp[j];
    double v = basis(k, j);
    if (p.modKnoop >= 0) {
      const auto& pb = b->param[p.param >= 0 ? p.param : 0];
      double bereik = p.param >= 0 ? pb.max - pb.min : 1.0;
      v += p.modDiepte * bereik * uitw[p.modKnoop][c][p.modPoort];
      if (p.param >= 0) v = klem(v, pb.min, pb.max);
    }
    return v;
  }
  double ingang(int k, int i, int c) const {
    const auto& s = b->knoop[k].in[i];
    return s.knoop < 0 ? 0.0 : uitw[s.knoop][c][s.poort];
  }

  // coëfficiënten opnieuw: alleen knopen waarvan een (niet-gemoduleerde) parameter veranderde
  void herbereken(int k, bool alles) {
    const auto& kn = b->knoop[k];
    switch (kn.soort) {
      case Blok::tape: drive[k] = dbNaarVersterking(basis(k, 0)); invDrive[k] = 1.0 / drive[k]; break;
      case Blok::hoogdoor1: case Blok::laagdoor1: if (!beweegt(k, 0)) for (auto& e : een[k]) e.zet(basis(k, 0), sr); break;
      case Blok::svf: if (!beweegt(k, 0) && !beweegt(k, 1)) for (auto& s : svf[k]) s.zet(basis(k, 0), basis(k, 1), sr); break;
      case Blok::biquad:
        if (!beweegt(k, 0) && !beweegt(k, 1)) for (auto& q : bq[k]) q.zet((BiquadSoort)(int)kn.vast[0], basis(k, 0), basis(k, 1), sr);
        break;
      default: break;
    }
    (void)alles;
  }

  void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t n) {
    const int N = b->nKnopen;
    for (uint32_t s = 0; s < n; ++s) {
      bool anders[MAX_PARAMS];
      bool eerste = vers;
      if (vers) { for (int i = 0; i < b->nParams; ++i) glij[i].spring(); vers = false; }
      for (int i = 0; i < b->nParams; ++i) anders[i] = glij[i].stap() || eerste;
      for (int k = 0; k < N; ++k) {
        bool her = eerste;
        for (int j = 0; j < MAX_KP; ++j) { int p = b->knoop[k].kp[j].param; if (p >= 0 && anders[p]) her = true; }
        if (her) herbereken(k, eerste);
      }
      // gedeelde knopen: één keer per sample (beide kanalen krijgen dezelfde waarde)
      for (int k = 0; k < N; ++k) {
        const auto& kn = b->knoop[k];
        if (kn.soort == Blok::bandbeweging) { double v = band[k].stap() * basis(k, 0); uitw[k][0][0] = uitw[k][1][0] = v; }
        else if (kn.soort == Blok::lfo) {
          lfoFase[k] += TWO_PI * basis(k, 0) / sr; if (lfoFase[k] >= TWO_PI) lfoFase[k] -= TWO_PI;
          double v = sin_(lfoFase[k]); uitw[k][0][0] = uitw[k][1][0] = v;
        }
      }
      for (int c = 0; c < kanalen; ++c) {
        for (int k = 0; k < N; ++k) {
          const auto& kn = b->knoop[k];
          double* u = uitw[k][c];
          switch (kn.soort) {
            case Blok::in: u[0] = invoer(in[c][s]); break;
            case Blok::uit: uit[c][s] = uitvoer(ingang(k, 0, c)); break;
            case Blok::som: u[0] = ingang(k, 0, c) + ingang(k, 1, c); break;
            case Blok::versterker: u[0] = flush(waardeMod(k, 0, c) * ingang(k, 0, c)); break;
            case Blok::tape: u[0] = sat[k][c].stap(ingang(k, 0, c) * drive[k]) * invDrive[k]; break;
            case Blok::vertraging: {
              double d = waardeMod(k, 0, c) * 0.001 * sr * (1.0 + ingang(k, 1, c)) - kn.vast[1];
              vt[k][c].schrijf(ingang(k, 0, c));
              u[0] = vt[k][c].leesLagrange(d);
              break;
            }
            case Blok::hoogdoor1: case Blok::laagdoor1: {
              if (beweegt(k, 0)) een[k][c].zet(waardeMod(k, 0, c), sr);
              double x = ingang(k, 0, c);
              u[0] = kn.soort == Blok::hoogdoor1 ? een[k][c].hoog(x) : een[k][c].laag(x);
              break;
            }
            case Blok::svf: {
              if (beweegt(k, 0) || beweegt(k, 1)) svf[k][c].zet(waardeMod(k, 0, c), waardeMod(k, 1, c), sr);
              svf[k][c].stap(ingang(k, 0, c));
              u[0] = svf[k][c].lp; u[1] = svf[k][c].bp; u[2] = svf[k][c].hp;
              break;
            }
            case Blok::biquad: {
              if (beweegt(k, 0) || beweegt(k, 1)) bq[k][c].zet((BiquadSoort)(int)kn.vast[0], waardeMod(k, 0, c), waardeMod(k, 1, c), sr);
              u[0] = bq[k][c].stap(ingang(k, 0, c));
              break;
            }
            case Blok::mix: { double m = waardeMod(k, 0, c); u[0] = (1.0 - m) * ingang(k, 0, c) + m * ingang(k, 1, c); break; }
            default: break;   // gedeelde knopen zijn al gerekend
          }
        }
      }
    }
  }

  // staart (s): de som van de vertragingen in een lus, met de lusversterking. Grof maar veilig: ≥ 1 = oneindig.
  double staartSec() const {
    double lus = 1.0, tijd = 0.0; bool vertraging = false;
    for (int k = 0; k < b->nKnopen; ++k) {
      const auto& kn = b->knoop[k];
      if (kn.soort == Blok::vertraging) { tijd += basis(k, 0) * 0.001 * 1.01; vertraging = true; }
      if (kn.soort == Blok::versterker) {
        // alleen versterkers in een terugkoppeling tellen: hun uitgang gaat naar een EERDERE knoop
        bool terug = false;
        for (int j = 0; j < b->nKnopen; ++j)
          for (const auto& i : b->knoop[j].in) if (i.knoop == k && j < k) terug = true;
        if (terug) lus *= fabs_(basis(k, 0));
      }
    }
    if (!vertraging) return 0.05;
    if (lus >= 0.999) return -1;
    if (lus < 1e-3) return tijd + 0.1;
    double n = -4.5 / (log_(lus) * 0.43429448190325182765);
    double t = (n + 1.0) * tijd + 0.1;
    return t > 60.0 ? 60.0 : t;
  }
};

}  // namespace pb
