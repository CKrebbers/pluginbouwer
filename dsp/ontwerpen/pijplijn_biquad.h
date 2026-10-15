// pijplijn_biquad.h — het ontwerp van fase A: één laagdoorfilter per kanaal. Bestaat om de pijplijn te bewijzen
// (WASM ↔ CLAP/VST3/AU, validators, null-test), niet om mooi te klinken.
//
// Het contract van een ontwerp (elk bestand in dsp/ontwerpen/ volgt het; plugin/ en dsp/pb_wasm.cpp kennen alleen dit):
//   struct Ontwerp {
//     static constexpr Meta META;                     // namen en codes voor CLAP, VST3 en AU
//     static const ParamInfo PARAMS[]; static constexpr int N_PARAMS;
//     void activeer(double sr, uint32_t maxBlok);     // het enige moment waarop geheugen geregeld mag worden
//     void reset();                                   // staart weg, smoothers springen bij de volgende sample
//     void zetParam(int index, double waarde);        // in de eenheid van PARAMS (Hz, ms, …), niet genormaliseerd
//     double leesParam(int index) const;
//     void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t n);  // kanalen 1 of 2
//     uint32_t latentie() const;                      // samples
//     double staartSec() const;                       // hoe lang er na stilte nog iets komt (0 = niets, -1 = oneindig)
//   };
#pragma once
#include "../pb_biquad.h"

namespace pb {

// De namen en codes ook als #define: plugin/CMakeLists.txt leest ze hieruit (één plek voor de AU-codes).
#define PB_NAAM "PB Pijplijn"
#define PB_CLAP_ID "nl.varve.pluginbouwer.pijplijn"
#define PB_VERSIE "0.1.0"
#define PB_AU_MAKER "Vrve"
#define PB_AU_SUBTYPE "PbPj"

struct Meta {
  const char* clapId; const char* naam; const char* maker; const char* url; const char* versie; const char* uitleg;
  const char* auMaker; const char* auSubtype;  // vier tekens
};

struct Ontwerp {
  static constexpr Meta META = {PB_CLAP_ID, PB_NAAM, "Varve", "https://varve.nl", PB_VERSIE,
                                "Laagdoorfilter dat de bouwstraat van de Pluginbouwer bewijst", PB_AU_MAKER, PB_AU_SUBTYPE};
  enum { P_FREQ = 0, P_Q = 1 };
  static constexpr int N_PARAMS = 2;
  // Grenzen en standaarden zijn exact in float32 (0.70703125 = 181/256 ≈ 1/√2): een AU geeft parameters als float
  // door, CLAP en VST3 als double. Zo krijgt elk formaat bit voor bit dezelfde waarde.
  static constexpr ParamInfo PARAMS[N_PARAMS] = {
      {1, "Frequentie", "Waar het filter de hoge tonen begint weg te halen", "Hz", 20.0, 20000.0, 1000.0, 20.0,
       AUTOMATEERBAAR | GLIJDEN},
      {2, "Resonantie", "Hoe scherp de knik is; hoger = een piek op de frequentie", "Q", 0.25, 10.0, 0.70703125,
       20.0, AUTOMATEERBAAR | GLIJDEN},
  };

  double sr = 48000;
  Glijder glij[N_PARAMS];
  Biquad bq[2];
  bool vers = true;

  void activeer(double samplerate, uint32_t) {
    sr = samplerate;
    for (int i = 0; i < N_PARAMS; ++i) glij[i].zetTijd(PARAMS[i].glijMs, sr);
    reset();
  }
  void reset() { bq[0].reset(); bq[1].reset(); vers = true; }
  void zetParam(int i, double v) { if (i >= 0 && i < N_PARAMS) glij[i].doel = klem(v, PARAMS[i].min, PARAMS[i].max); }
  double leesParam(int i) const { return (i >= 0 && i < N_PARAMS) ? glij[i].doel : 0.0; }
  uint32_t latentie() const { return 0; }
  double staartSec() const { return 0.05; }

  void herbereken() {
    for (int c = 0; c < 2; ++c) bq[c].zet(BiquadSoort::laagdoor, glij[P_FREQ].nu, glij[P_Q].nu, sr);
  }

  void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t n) {
    for (uint32_t s = 0; s < n; ++s) {
      bool anders = false;
      if (vers) { for (auto& g : glij) g.spring(); vers = false; anders = true; }
      for (auto& g : glij) anders |= g.stap();
      if (anders) herbereken();
      for (int c = 0; c < kanalen; ++c) uit[c][s] = (float)bq[c].stap(invoer(in[c][s]));
    }
  }
};

}  // namespace pb
