// paneel.h — het frontpaneel (design.md, frontpaneel.svg), het deel zonder platform.
//
// De weg van een knop (valkuil 14): het paneel zet een verandering als BEGIN/WAARDE/EIND in een lock-vrije rij
// (één schrijver: de mainthread; één lezer: process of params.flush) en vraagt de host om een flush. process of flush
// pusht ze als CLAP_EVENT_PARAM_GESTURE_BEGIN/PARAM_VALUE/GESTURE_END naar de host en past de waarde zelf toe, zoals
// een event van de host. Het paneel raakt het Ontwerp nooit; het leest alleen de atomaire spiegel (wat de host ziet).
#pragma once
#include <atomic>
#include <cstdint>

namespace pb_paneel {

enum Soort : uint32_t { BEGIN = 0, WAARDE = 1, EIND = 2 };

struct Wijziging {
  uint32_t index;  // index in PARAMS, geen id
  uint32_t soort;  // Soort
  double waarde;   // alleen bij WAARDE
};

// Lock-vrije rij met één schrijver en één lezer. De tellers lopen door (ook over 2^32 heen); CAP is een macht van 2.
class Rij {
 public:
  static constexpr uint32_t CAP = 1024;
  bool zet(const Wijziging& w) {
    const uint32_t s = schrijf_.load(std::memory_order_relaxed);
    if (s - lees_.load(std::memory_order_acquire) >= CAP) return false;
    buf_[s % CAP] = w;
    schrijf_.store(s + 1, std::memory_order_release);
    return true;
  }
  bool pak(Wijziging& w) {
    const uint32_t l = lees_.load(std::memory_order_relaxed);
    if (l == schrijf_.load(std::memory_order_acquire)) return false;
    w = buf_[l % CAP];
    lees_.store(l + 1, std::memory_order_release);
    return true;
  }
  uint32_t geschreven() const { return schrijf_.load(std::memory_order_relaxed); }  // alleen de schrijver
  uint32_t gelezen() const { return lees_.load(std::memory_order_acquire); }

 private:
  Wijziging buf_[CAP];
  std::atomic<uint32_t> schrijf_{0}, lees_{0};
};

constexpr int MAX_KNOPPEN = 8;

struct Knop {
  const char* naam;  // volle naam (tooltip); het label is hooguit 11 tekens daarvan
  double min, max, standaard;
};

// Wat het paneel van de plugin mag gebruiken. Alles hier is veilig vanaf de mainthread terwijl process loopt.
struct Brug {
  void* ctx;
  int aantal;  // knoppen, in de volgorde van SPEC.md
  Knop knoppen[MAX_KNOPPEN];
  double (*waarde)(void* ctx, int i);                                // de atomaire spiegel
  bool (*tekst)(void* ctx, int i, double v, char* uit, uint32_t n);  // value_to_text van de plugin
  Rij* rij;                                                          // naar process/flush
  void (*vraagFlush)(void* ctx);                                     // host_params->request_flush
};

// Maten uit design.md en frontpaneel.svg (de mockup is 560 × 220 op ware grootte).
constexpr uint32_t BREEDTE = 640, HOOGTE = 250;          // beginmaat
constexpr uint32_t MIN_BREEDTE = 560, MIN_HOOGTE = 220;  // minimaal; ook de maat van de mockup
constexpr double KOLOM = 76, KNOP_X0 = 62, KNOP_Y = 130;  // kolom per knop, midden van de eerste knop
constexpr double BOOG_R = 22, KNOP_R = 16, WIJZER_R = 14;
constexpr double HOEK_BEGIN = -135, HOEK_BEREIK = 270;   // graden vanaf boven, met de klok mee
constexpr double SLEEP_PX = 200;                         // 200 px omhoog = het hele bereik
constexpr double FIJN = 10;                              // Shift: 10× fijner

// Het platformdeel (paneel_mac.mm). Alleen de mainthread.
struct Paneel;
bool paneelApi(const char* api);
Paneel* paneelMaak(const Brug& brug, uint32_t breedte, uint32_t hoogte);
void paneelWeg(Paneel* p);
bool paneelZetOuder(Paneel* p, void* ouder);
void paneelZetMaat(Paneel* p, uint32_t breedte, uint32_t hoogte);
void paneelToon(Paneel* p, bool zichtbaar);

}  // namespace pb_paneel
