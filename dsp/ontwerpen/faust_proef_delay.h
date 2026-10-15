// faust_proef_delay.h — de Faust-kandidaat van fase B in ONZE CLAP-schil: de door Faust gegenereerde C++
// (stackproef/faust/gen/proef-double.cpp, uit proef_delay.dsp) achter het Ontwerp-contract. Alleen native:
// de browserkant van deze kandidaat is Faust' eigen WASM (faustwasm), niet onze wasm32-bouw.
#pragma once
#define FAUSTFLOAT double
#include "../pb_core.h"
#include "../../stackproef/faust/arch.h"
#define virtual
#include "../../stackproef/faust/gen/proef-double.cpp"
#undef virtual

#define PB_NAAM "PB Proef Delay Faust"
#define PB_CLAP_ID "nl.varve.pluginbouwer.proefdelay.faust"
#define PB_VERSIE "0.1.0"
#define PB_AU_MAKER "Vrve"
#define PB_AU_SUBTYPE "PbPf"

namespace pb {

struct Meta {
  const char* clapId; const char* naam; const char* maker; const char* url; const char* versie; const char* uitleg;
  const char* auMaker; const char* auSubtype;
};

struct Ontwerp {
  static constexpr Meta META = {PB_CLAP_ID, PB_NAAM, "Varve", "https://varve.nl", PB_VERSIE,
                                "Proefbouw B: dezelfde keten, gegenereerd door Faust", PB_AU_MAKER, PB_AU_SUBTYPE};
  static constexpr int N_PARAMS = 4;
  static constexpr ParamInfo PARAMS[N_PARAMS] = {
      {1, "Frequentie", "", "Hz", 20.0, 20000.0, 4000.0, 0, AUTOMATEERBAAR},
      {2, "Resonantie", "", "Q", 0.25, 10.0, 0.70703125, 0, AUTOMATEERBAAR},
      {3, "Tijd", "", "ms", 1.0, 1000.0, 250.0, 0, AUTOMATEERBAAR},
      {4, "Mix", "", "%", 0.0, 1.0, 0.5, 0, AUTOMATEERBAAR},
  };
  FaustProef d;
  UI ui;
  FAUSTFLOAT* zone[N_PARAMS] = {};
  double* buf[4] = {};
  uint32_t maxBlok = 0;
  double waarde[N_PARAMS];

  static unsigned long geheugenNodig(double) { return 4 * bytesVoor<double>(8192); }
  void activeer(double sr, uint32_t, Geheugen g) {
    for (auto& b : buf) b = g.neem<double>(8192);
    maxBlok = 8192;
    ui.zones.clear();
    d.init((int)sr);
    d.buildUserInterface(&ui);
    const char* namen[N_PARAMS] = {"Frequentie", "Resonantie", "Tijd", "Mix"};
    for (int i = 0; i < N_PARAMS; ++i) { zone[i] = ui.zones.at(namen[i]); *zone[i] = waarde[i]; }
  }
  Ontwerp() { for (int i = 0; i < N_PARAMS; ++i) waarde[i] = PARAMS[i].standaard; }
  void reset() { d.instanceClear(); }
  void zetParam(int i, double v) { if (i < 0 || i >= N_PARAMS) return; waarde[i] = klem(v, PARAMS[i].min, PARAMS[i].max); if (zone[i]) *zone[i] = waarde[i]; }
  double leesParam(int i) const { return (i >= 0 && i < N_PARAMS) ? waarde[i] : 0.0; }
  uint32_t latentie() const { return 0; }
  double staartSec() const { return 1.05; }
  void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t n) {
    for (uint32_t pos = 0; pos < n;) {
      uint32_t m = n - pos < maxBlok ? n - pos : maxBlok;
      for (uint32_t s = 0; s < m; ++s) { buf[0][s] = in[0][pos + s]; buf[1][s] = in[kanalen > 1 ? 1 : 0][pos + s]; }
      double* ip[2] = {buf[0], buf[1]}; double* op[2] = {buf[2], buf[3]};
      d.compute((int)m, ip, op);
      for (int c = 0; c < kanalen; ++c) for (uint32_t s = 0; s < m; ++s) uit[c][pos + s] = (float)op[c][s];
      pos += m;
    }
  }
};

}  // namespace pb
