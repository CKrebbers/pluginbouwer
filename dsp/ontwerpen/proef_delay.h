// proef_delay.h — de proefbouw van fase B: laagdoorfilter, dan een fractionele delay, droog en nat gemengd.
// Dezelfde keten staat in Faust in stackproef/faust/proef_delay.dsp. Zie KEUZE-STACK.md.
#pragma once
#include "../pb_biquad.h"
#include "../pb_delay.h"

#define PB_NAAM "PB Proef Delay"
#define PB_CLAP_ID "nl.varve.pluginbouwer.proefdelay"
#define PB_VERSIE "0.1.0"
#define PB_AU_MAKER "Vrve"
#define PB_AU_SUBTYPE "PbPd"

namespace pb {

struct Meta {
  const char* clapId; const char* naam; const char* maker; const char* url; const char* versie; const char* uitleg;
  const char* auMaker; const char* auSubtype;
};

struct Ontwerp {
  static constexpr Meta META = {PB_CLAP_ID, PB_NAAM, "Varve", "https://varve.nl", PB_VERSIE,
                                "Filter en fractionele delay: de proefbouw van de stackkeuze", PB_AU_MAKER, PB_AU_SUBTYPE};
  enum { P_FREQ = 0, P_Q, P_TIJD, P_MIX };
  static constexpr int N_PARAMS = 4;
  static constexpr double MAX_MS = 1000.0;
  // grenzen exact in float32 (zie pijplijn_biquad.h)
  static constexpr ParamInfo PARAMS[N_PARAMS] = {
      {1, "Frequentie", "Waar het filter de hoge tonen begint weg te halen", "Hz", 20.0, 20000.0, 4000.0, 20.0, AUTOMATEERBAAR | GLIJDEN},
      {2, "Resonantie", "Hoe scherp de knik is", "Q", 0.25, 10.0, 0.70703125, 20.0, AUTOMATEERBAAR | GLIJDEN},
      {3, "Tijd", "Hoe lang de echo na het origineel komt", "ms", 1.0, MAX_MS, 250.0, 50.0, AUTOMATEERBAAR | GLIJDEN},
      {4, "Mix", "Hoeveel echo je hoort naast het origineel", "%", 0.0, 1.0, 0.5, 20.0, AUTOMATEERBAAR | GLIJDEN},
  };

  double sr = 48000;
  Glijder glij[N_PARAMS];
  Biquad bq[2];
  Vertraging vt[2];
  bool vers = true;

  // Standaardwaarden bij constructie, dus in WASM en plugin hetzelfde (eerst vergeten: de WASM begon op 0).
  Ontwerp() { for (int i = 0; i < N_PARAMS; ++i) glij[i].doel = glij[i].nu = PARAMS[i].standaard; }
  static unsigned long geheugenNodig(double samplerate) { return 2 * Vertraging::geheugen(MAX_MS * 0.001 * samplerate); }
  void activeer(double samplerate, uint32_t, Geheugen g) {
    sr = samplerate;
    for (int i = 0; i < N_PARAMS; ++i) glij[i].zetTijd(PARAMS[i].glijMs, sr);
    for (auto& v : vt) v.koppel(g, MAX_MS * 0.001 * sr);
    reset();
  }
  void reset() { for (auto& b : bq) b.reset(); for (auto& v : vt) v.reset(); vers = true; }
  void zetParam(int i, double v) { if (i >= 0 && i < N_PARAMS) glij[i].doel = klem(v, PARAMS[i].min, PARAMS[i].max); }
  double leesParam(int i) const { return (i >= 0 && i < N_PARAMS) ? glij[i].doel : 0.0; }
  uint32_t latentie() const { return 0; }
  double staartSec() const { return MAX_MS * 0.001 + 0.05; }

  void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t n) {
    for (uint32_t s = 0; s < n; ++s) {
      bool anders = false;
      if (vers) { for (auto& g : glij) g.spring(); vers = false; anders = true; }
      for (int i = 0; i < 2; ++i) anders |= glij[i].stap();
      glij[P_TIJD].stap(); glij[P_MIX].stap();
      if (anders) for (auto& b : bq) b.zet(BiquadSoort::laagdoor, glij[P_FREQ].nu, glij[P_Q].nu, sr);
      double d = glij[P_TIJD].nu * 0.001 * sr, mix = glij[P_MIX].nu;
      for (int c = 0; c < kanalen; ++c) {
        double y = bq[c].stap(invoer(in[c][s]));
        vt[c].schrijf(y);
        uit[c][s] = uitvoer((1.0 - mix) * y + mix * vt[c].leesLagrange(d));
      }
    }
  }
};

}  // namespace pb
