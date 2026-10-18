// brievenbus.h — van het paneel (mainthread) naar process/flush, zonder locks en zonder geheugen (valkuil 14).
//
// Het paneel raakt het Ontwerp nooit aan. Het zet per parameter een gebaar klaar (begin, waarde, eind) en vraagt de
// host om een flush; wie het Ontwerp bezit (process op de audiothread, of params.flush) leegt de bus, stuurt de
// gebaren als CLAP_EVENT_PARAM_GESTURE_BEGIN / PARAM_VALUE / GESTURE_END naar de host en past de waarde zelf toe.
//
// Per parameter alleen tellers en de laatste waarde, geen rij: hij loopt nooit vol. Komt de lezer een tijd niet langs
// (een AU die niet rendert), dan worden de tussenliggende waarden samengevoegd tot de laatste; begin en eind blijven
// in paren.
//
// Eén schrijver (de mainthread) en één lezer tegelijk (de host roept process en flush nooit tegelijk aan).
#pragma once
#include <atomic>
#include <cstdint>

namespace dub {

template <int N>
struct Brievenbus {
  enum Soort { BEGIN, WAARDE, EIND };

  // --- schrijver: het paneel (mainthread) ---
  void begin(int i) { vak[i].begin.fetch_add(1, std::memory_order_release); }
  void zet(int i, double v) {
    vak[i].waarde.store(v, std::memory_order_relaxed);
    vak[i].seq.fetch_add(1, std::memory_order_release);
  }
  void eind(int i) { vak[i].eind.fetch_add(1, std::memory_order_release); }
  // staat er voor i een waarde klaar die de lezer nog niet heeft toegepast?
  bool onderweg(int i) const {
    return vak[i].verwerkt.load(std::memory_order_acquire) != vak[i].seq.load(std::memory_order_relaxed);
  }

  // --- lezer: process of flush. f(Soort, index, waarde) ---
  template <class F>
  void leeg(F&& f) {
    for (int i = 0; i < N; ++i) {
      Vak& k = vak[i];
      // eind eerst: alles wat het paneel vóór dat eind schreef (de laatste waarde) is dan zichtbaar
      const uint32_t e = k.eind.load(std::memory_order_acquire);
      const uint32_t s = k.seq.load(std::memory_order_acquire);
      const double v = k.waarde.load(std::memory_order_relaxed);
      const uint32_t b = k.begin.load(std::memory_order_acquire);
      const bool nieuwEind = e != zagEind[i], nieuweWaarde = s != zagSeq[i];
      if ((nieuwEind || nieuweWaarde) && !open[i]) { f(BEGIN, i, 0.0); open[i] = true; }
      if (nieuweWaarde) {
        f(WAARDE, i, v);
        zagSeq[i] = s;
        k.verwerkt.store(s, std::memory_order_release);
      }
      if (nieuwEind) { f(EIND, i, 0.0); open[i] = false; zagEind[i] = e; }
      // b is na e gelezen: b != e betekent dat het paneel nu een gebaar open heeft
      if (b != e && !open[i]) { f(BEGIN, i, 0.0); open[i] = true; }
    }
  }

 private:
  struct Vak {
    std::atomic<uint32_t> begin{0}, eind{0}, seq{0}, verwerkt{0};
    std::atomic<double> waarde{0.0};
  };
  Vak vak[N];
  // van de lezer
  uint32_t zagEind[N] = {}, zagSeq[N] = {};
  bool open[N] = {};
};

}  // namespace dub
