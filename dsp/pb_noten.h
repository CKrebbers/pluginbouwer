// pb_noten.h — MIDI-noten naar de toetsen-knoop van een instrument (18 okt 2026). De schil (plugin/src/plugin.cpp)
// gebruikt dit; de null-test rekent hetzelfde na in JS (tests/lib/noten.mjs, met exp_ via de WASM-export pb_t_exp).
//
// Monofoon en legato, zoals de toetsen in de tool: de laatst ingedrukte noot klinkt; laat je hem los terwijl er nog
// een vastgehouden wordt, dan gaat de toon terug naar die (de poort blijft open). Noot n wordt toon 2^((n − 57)/12):
// noot 57 (A3) is verhouding 1, dus een oscillator op 220 Hz speelt dan precies wat de toets zegt. De toon-knop gaat
// van 0,25 tot 4 (noot 33 tot 81); daarbuiten klemt de parameter.
#pragma once
#include "pb_core.h"

namespace pb {

constexpr int GRONDNOOT = 57;
inline double nootVerhouding(int n) { return exp_((double)(n - GRONDNOOT) / 12.0 * LN2); }

struct NootStapel {
  static constexpr int MAX = 16;
  int8_t n[MAX] = {};
  int aantal = 0;
  void haalWeg(int noot) { int j = 0; for (int i = 0; i < aantal; ++i) if (n[i] != noot) n[j++] = n[i]; aantal = j; }
  void aan(int noot) {
    haalWeg(noot);
    if (aantal == MAX) { for (int i = 1; i < MAX; ++i) n[i - 1] = n[i]; --aantal; }
    n[aantal++] = (int8_t)noot;
  }
  void uit(int noot) { haalWeg(noot); }
  void leeg() { aantal = 0; }
  int boven() const { return aantal ? n[aantal - 1] : -1; }
};

}  // namespace pb
