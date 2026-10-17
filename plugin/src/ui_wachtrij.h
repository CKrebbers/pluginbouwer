// ui_wachtrij.h — wat het frontpaneel wil veranderen, op weg naar de thread die het Ontwerp bezit.
// Eén schrijver (de mainthread: het paneel), één lezer (process op de audiothread, of params.flush; die lopen nooit
// tegelijk, CLAP garandeert het). Lock-vrij en zonder allocatie: een vaste ring met twee atomische tellers.
// De lezer maakt van elk bericht een CLAP-uitvoerevent voor de host (gebaar begin/eind, waarde) en past de waarde toe
// zoals een event van de host. Zo verandert een knop nooit rechtstreeks de DSP.
#pragma once
#include <atomic>
#include <cstdint>

namespace paneel {

struct Bericht {
  enum Soort : uint32_t { BEGIN, WAARDE, EIND } soort;
  uint32_t id;   // parameter-id (SPEC.md), niet de index
  double waarde; // in de eenheid van PARAMS (alleen bij WAARDE)
};

class Wachtrij {
 public:
  static constexpr uint32_t GROOTTE = 512;  // macht van 2

  // mainthread; false als hij vol is (dan valt het bericht weg; een volgende sleep stuurt een nieuwe waarde). Een
  // BEGIN en een WAARDE laten plek over voor het EIND erna, zodat een gebaar bij de host altijd sluit.
  bool zet(const Bericht& b) {
    const uint32_t s = schrijf_.load(std::memory_order_relaxed);
    const uint32_t reserve = b.soort == Bericht::BEGIN ? 2 : b.soort == Bericht::WAARDE ? 1 : 0;
    if (s - lees_.load(std::memory_order_acquire) + reserve >= GROOTTE) return false;
    ring_[s & (GROOTTE - 1)] = b;
    schrijf_.store(s + 1, std::memory_order_release);
    return true;
  }
  // de thread die het Ontwerp bezit: eerst kijken, en pas overslaan als het uitvoerevent bij de host is (is het
  // uitvoerkanaal vol, dan blijft het bericht voor de volgende process of flush)
  bool kijk(Bericht& b) const {
    const uint32_t l = lees_.load(std::memory_order_relaxed);
    if (l == schrijf_.load(std::memory_order_acquire)) return false;
    b = ring_[l & (GROOTTE - 1)];
    return true;
  }
  void sla_over() { lees_.store(lees_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

 private:
  Bericht ring_[GROOTTE];
  std::atomic<uint32_t> schrijf_{0}, lees_{0};
};

}  // namespace paneel
