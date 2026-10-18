// graaf_ontwerp.h — het Ontwerp-contract (zie ontwerpen/pijplijn_biquad.h) rond de ketenmotor, voor een keten die
// als CONSTANTE in een gegenereerde header staat (dsp/ontwerpen/keten_<id>.h, gemaakt door tools/bouwpakket.mjs).
// Vóór het includen van dit bestand definieert de header: PB_KETEN (double-rij), PB_KETEN_N, PB_N_PARAMS,
// PB_PARAMS (ParamInfo-lijst), PB_LATENTIE, en de PB_NAAM/PB_CLAP_ID/... van elk ontwerp.
#pragma once
#include "pb_graaf.h"

#define PB_HEEFT_TRANSPORT 1

namespace pb {

struct Meta {
  const char* clapId; const char* naam; const char* maker; const char* url; const char* versie; const char* uitleg;
  const char* auMaker; const char* auSubtype;
};

struct Ontwerp {
  static constexpr Meta META = {PB_CLAP_ID, PB_NAAM, PB_MAKER, "https://varve.nl", PB_VERSIE, PB_UITLEG, PB_AU_MAKER, PB_AU_SUBTYPE};
  static constexpr int N_PARAMS = PB_N_PARAMS;
  static constexpr ParamInfo PARAMS[N_PARAMS] = {PB_PARAMS};

  GraafBeschrijving beschr;
  Graaf graaf;

  Ontwerp() { leesBeschrijving(PB_KETEN, PB_KETEN_N, beschr); graaf.koppel(&beschr); }
  Ontwerp(const Ontwerp&) = delete;            // graaf wijst naar beschr van DIT object
  Ontwerp& operator=(const Ontwerp&) = delete;

  static unsigned long geheugenNodig(double sr) {
    static GraafBeschrijving b;                 // statisch: groot, en alleen gelezen (main thread, vóór activate)
    leesBeschrijving(PB_KETEN, PB_KETEN_N, b);
    return graafGeheugen(b, sr);
  }
  void activeer(double sr, uint32_t, Geheugen g) { graaf.activeer(sr, g); }
  void reset() { graaf.reset(); }
  void zetParam(int i, double v) { graaf.zetParam(i, v); }
  double leesParam(int i) const { return graaf.leesParam(i); }
  uint32_t latentie() const { return PB_LATENTIE; }
  double staartSec() const { return graaf.staartSec(); }
  void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t n) { graaf.verwerk(in, uit, kanalen, n); }
  // transport van de host: tempo en plek in de maat voor elke sequencer (pb_graaf.h zetTransport)
  void zetTransport(double bpm, double beat, bool speelt) { graaf.zetTransport(bpm, beat, speelt); }
};

}  // namespace pb
