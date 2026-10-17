// plugin.cpp — de CLAP-schil rond het Ontwerp uit dsp/ (fase 2: parameters af, tekst in gewone eenheden).
// process roept Ontwerp::verwerk aan en splitst het blok bij elk parameter-event, zodat elke waarde op zijn sample
// ingaat. Parameters: CLAP-params met id, bereik en standaard uit Ontwerp::PARAMS (de DSP neemt de INDEX).
// Threads (ARCHITECTUUR.md): de audiothread schrijft het Ontwerp alleen in process/flush; de mainthread leest en
// schrijft parameterwaarden via een atomische spiegel, en een state-load gaat via een vlag naar de audiothread.
// Fase 3: het frontpaneel (PB_PANEEL, paneel.cpp + venster_*.cpp/mm) zet geen DSP-waarden. Het meldt gebaren
// (begin/waarde/eind) via atomics; process of flush past ze toe zoals een host-event en meldt ze als
// output-events aan de host (die er automatisering van maakt). Het paneel leest alleen de atomische spiegel.
// PB_ZONDER_PARAM=<id> bouwt een schil die parameter <id> niet kent (zoals een oudere versie): alleen voor de
// state-toets (plugin/toets), nooit voor een uitgeleverde plugin.
// Fase 4: fabriekspresets (presets/*.json, in de bundel gekopieerd) via de preset-discovery-factory en preset-load.
#include <clap/clap.h>

#include <atomic>
#include <climits>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "entry.h"
#include "ontwerpen/keten_dub_echo.h"
#if PB_PANEEL
#include "paneel.h"
#endif

#if PB_KANARIE
// De kanarie voor RTSan: een malloc in process, de pointer in een globale variabele zodat de compiler hem niet
// wegoptimaliseert. Deze build MOET door RTSan worden afgebroken (zie .github/workflows/ci.yml).
void* volatile pb_kanarie = nullptr;
#endif

namespace {

using pb::Ontwerp;

constexpr uint32_t STATE_MAGIE = 0x54534250u;  // "PBST" (little-endian op schijf)
constexpr uint32_t STATE_VERSIE = 1;

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY,
                                CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT,
    Ontwerp::META.clapId,
    Ontwerp::META.naam,
    Ontwerp::META.maker,
    Ontwerp::META.url,
    "",
    "",
    Ontwerp::META.versie,
    Ontwerp::META.uitleg,
    FEATURES,
};

constexpr int N = Ontwerp::N_PARAMS;
constexpr uint32_t STAART_ONEINDIG = UINT32_MAX;

// verborgen: de parameter die deze build niet kent (alleen met PB_ZONDER_PARAM, voor de state-toets)
constexpr bool verborgen(int i) {
#ifdef PB_ZONDER_PARAM
  return Ontwerp::PARAMS[i].id == (clap_id)PB_ZONDER_PARAM;
#else
  return (void)i, false;
#endif
}
constexpr int telZichtbaar() {
  int n = 0;
  for (int i = 0; i < N; ++i) n += verborgen(i) ? 0 : 1;
  return n;
}
constexpr int N_ZICHTBAAR = telZichtbaar();
#ifdef PB_ZONDER_PARAM
static_assert(N_ZICHTBAAR == N - 1, "PB_ZONDER_PARAM is geen parameter-id uit PARAMS");
#endif

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp* ontwerp = nullptr;
  unsigned char* geheugen = nullptr;
  double sr = 48000;
  std::atomic<bool> actief{false};              // gezet op de mainthread (activate/deactivate)
  std::atomic<double> waarde[N];                // spiegel van elke parameter (doelwaarde), voor get_value en state
  std::atomic<bool> stateNieuw{false};          // mainthread laadde state terwijl actief: process neemt de spiegel over
  std::atomic<uint32_t> staart{0};              // in samples; berekend op de audiothread (staartSec leest smoothers)
  const clap_host_params_t* hostParams = nullptr;

  // Van het paneel (mainthread) naar process/flush; alleen tellers en waarden, geen locks. Per parameter:
  // uiBegin/uiEind tellen de gebaren, uiTeller de nieuwe waarden (uiWaarde); uiGezien = de teller die process
  // heeft toegepast (zolang die achterloopt toont het paneel uiWaarde in plaats van de spiegel).
  std::atomic<double> uiWaarde[N];
  std::atomic<uint32_t> uiBegin[N], uiEind[N], uiTeller[N], uiGezien[N];
  // alleen in process/flush (nooit tegelijk): wat de host al gemeld kreeg
  uint32_t gemeldBegin[N] = {}, gemeldTeller[N] = {};
  bool gebaarOpen[N] = {};

  // het paneel (alleen mainthread)
  uint32_t guiB = 0, guiH = 0;
#if PB_PANEEL
  pb::PaneelBron* bron = nullptr;
  pb::Paneel* paneel = nullptr;
  pb::Venster* venster = nullptr;
#endif
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }
#if PB_PANEEL
void guiOpruimen(Plugin* z);
#endif

// de INDEX in PARAMS bij een parameter-id, of -1 (ook voor een verborgen parameter)
int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id) return verborgen(i) ? -1 : i;
  return -1;
}
// de INDEX in PARAMS bij de positie in de lijst die de host ziet (verborgen parameters overgeslagen), of -1
int indexOpPositie(uint32_t positie) {
  for (int i = 0; i < N; ++i)
    if (!verborgen(i) && positie-- == 0) return i;
  return -1;
}
// binnen het bereik; een parameter met STAPPEN alleen op hele getallen
double klem(int i, double v) {
  const auto& pi = Ontwerp::PARAMS[i];
  if (pi.vlaggen & pb::STAPPEN) v = std::floor(v + 0.5);
  if (!(v >= pi.min)) return pi.min;  // ook NaN
  return v > pi.max ? pi.max : v;
}

// de staart voor de host, uit Ontwerp::staartSec (s, −1 = oneindig); alleen op de audiothread of als niets draait
void rekenStaart(Plugin* z) {
  const double t = z->ontwerp->staartSec();
  uint32_t n = STAART_ONEINDIG;
  if (t >= 0) {
    const double s = std::ceil(t * z->sr);
    n = s >= 4294967294.0 ? STAART_ONEINDIG - 1 : (uint32_t)s;
  }
  z->staart.store(n, std::memory_order_relaxed);
}

// één event: alleen parameterwaarden uit de core-namespace
void doeEvent(Plugin* z, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
  const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
  const int i = indexVan(e->param_id);
  if (i < 0) return;
  const double v = klem(i, e->value);
  z->ontwerp->zetParam(i, v);
  z->waarde[i].store(v, std::memory_order_relaxed);
}
// De gebaren van het paneel: toegepast als een event op sample 0 en aan de host gemeld (gebaar begin, waarde,
// gebaar eind), zodat de host ze opneemt als automatisering. Alleen in process of flush.
bool meld(const clap_output_events_t* uit, uint16_t type, clap_id id, double v) {
  if (!uit) return false;
  if (type == CLAP_EVENT_PARAM_VALUE) {
    clap_event_param_value_t e{};
    e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, type, 0};
    e.param_id = id;
    e.cookie = nullptr;
    e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
    e.value = v;
    return uit->try_push(uit, &e.header);
  }
  clap_event_param_gesture_t e{};
  e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, type, 0};
  e.param_id = id;
  return uit->try_push(uit, &e.header);
}
void neemPaneelOver(Plugin* z, const clap_output_events_t* uit) {
  for (int i = 0; i < N; ++i) {
    // eerst het einde, dan het begin, dan de waarde: wie een einde ziet, ziet ook alles wat het paneel ervoor schreef
    const uint32_t eind = z->uiEind[i].load(std::memory_order_acquire);
    const uint32_t begin = z->uiBegin[i].load(std::memory_order_acquire);
    const uint32_t teller = z->uiTeller[i].load(std::memory_order_acquire);
    const clap_id id = Ontwerp::PARAMS[i].id;
    if (begin != z->gemeldBegin[i] && !z->gebaarOpen[i]) {
      meld(uit, CLAP_EVENT_PARAM_GESTURE_BEGIN, id, 0);
      z->gebaarOpen[i] = true;
    }
    z->gemeldBegin[i] = begin;
    if (teller != z->gemeldTeller[i]) {
      z->gemeldTeller[i] = teller;
      const double v = klem(i, z->uiWaarde[i].load(std::memory_order_relaxed));
      z->ontwerp->zetParam(i, v);
      z->waarde[i].store(v, std::memory_order_relaxed);
      z->uiGezien[i].store(teller, std::memory_order_release);
      meld(uit, CLAP_EVENT_PARAM_VALUE, id, v);
    }
    if (z->gebaarOpen[i] && begin == eind) {
      meld(uit, CLAP_EVENT_PARAM_GESTURE_END, id, 0);
      z->gebaarOpen[i] = false;
    }
  }
}

void neemStateOver(Plugin* z) {
  if (!z->stateNieuw.exchange(false, std::memory_order_acquire)) return;
  for (int i = 0; i < N; ++i) z->ontwerp->zetParam(i, z->waarde[i].load(std::memory_order_relaxed));
}

// --- tekst <-> waarde in gewone eenheden ---------------------------------------------------------------------
// Tijd in ms (vanaf 1 s in s), frequenties in Hz (vanaf 1 kHz in kHz), Verzadiging in dB, Resonantie als Q, en de
// parameters zonder eenheid (0..1, Terugkoppeling 0..1,25) in procenten. De waarde wordt eerst afgerond op wat de
// tekst laat zien en pas dan krijgt hij zijn eenheid; zo geeft tekst → waarde → tekst altijd dezelfde tekst
// (clap-validator param-conversions). Altijd een punt als decimaalteken, ook als de host een locale zet; bij het
// lezen mag ook een komma.
enum class Soort { MS, HZ, DB, Q, PROCENT, ANDERS };
Soort soortVan(int i) {
  const char* e = Ontwerp::PARAMS[i].eenheid;
  if (!std::strcmp(e, "ms")) return Soort::MS;
  if (!std::strcmp(e, "Hz")) return Soort::HZ;
  if (!std::strcmp(e, "dB")) return Soort::DB;
  if (!std::strcmp(e, "Q")) return Soort::Q;
  if (!*e) return Soort::PROCENT;
  return Soort::ANDERS;
}
// afgerond op d decimalen (d < 0: op tientallen e.d.); +0.0 maakt van −0 een 0
double rondAf(double v, int d) {
  if (d < 0) {
    const double stap = std::pow(10.0, -d);
    return std::floor(v / stap + 0.5) * stap + 0.0;
  }
  const double schaal = std::pow(10.0, d);
  return std::floor(v * schaal + 0.5) / schaal + 0.0;
}
void schrijfGetal(char* tekst, uint32_t grootte, const char* formaat, double v, const char* eenheid) {
  std::snprintf(tekst, grootte, formaat, v, eenheid);
  for (char* c = tekst; *c; ++c)
    if (*c == ',') *c = '.';  // een host-locale met een komma
}
void naarTekst(int i, double v, char* tekst, uint32_t grootte) {
  if (Ontwerp::PARAMS[i].vlaggen & pb::STAPPEN) {
    schrijfGetal(tekst, grootte, *Ontwerp::PARAMS[i].eenheid ? "%.0f %s" : "%.0f%s", rondAf(v, 0), Ontwerp::PARAMS[i].eenheid);
    return;
  }
  switch (soortVan(i)) {
    case Soort::MS: {
      if (rondAf(v, 1) < 100) return schrijfGetal(tekst, grootte, "%.1f %s", rondAf(v, 1), "ms");
      if (rondAf(v, 0) < 1000) return schrijfGetal(tekst, grootte, "%.0f %s", rondAf(v, 0), "ms");
      return schrijfGetal(tekst, grootte, "%.3f %s", rondAf(v, 0) / 1000, "s");
    }
    case Soort::HZ: {
      if (rondAf(v, 1) < 100) return schrijfGetal(tekst, grootte, "%.1f %s", rondAf(v, 1), "Hz");
      if (rondAf(v, 0) < 1000) return schrijfGetal(tekst, grootte, "%.0f %s", rondAf(v, 0), "Hz");
      return schrijfGetal(tekst, grootte, "%.2f %s", rondAf(v, -1) / 1000, "kHz");
    }
    case Soort::DB: return schrijfGetal(tekst, grootte, "%.1f %s", rondAf(v, 1), "dB");
    case Soort::Q: return schrijfGetal(tekst, grootte, "%.3f%s", rondAf(v, 3), "");
    case Soort::PROCENT: return schrijfGetal(tekst, grootte, "%.1f %s", rondAf(v * 100, 1), "%");
    case Soort::ANDERS: return schrijfGetal(tekst, grootte, "%.4g %s", v, Ontwerp::PARAMS[i].eenheid);
  }
}
// een decimaal getal (punt of komma), zonder locale: m / 10^k. Geeft de positie na het getal, of nullptr.
const char* leesGetal(const char* t, double* m, int* k) {
  bool negatief = false;
  if (*t == '+' || *t == '-') negatief = *t++ == '-';
  *m = 0; *k = 0;
  int cijfers = 0;
  bool punt = false;
  for (;; ++t) {
    if (*t >= '0' && *t <= '9') {
      if (cijfers < 17) {
        *m = *m * 10 + (*t - '0');
        if (punt) ++*k;
      } else if (!punt) {
        *m *= 10;  // te veel cijfers: de rest telt alleen nog als grootte
      }
      ++cijfers;
    } else if ((*t == '.' || *t == ',') && !punt) {
      punt = true;
    } else {
      break;
    }
  }
  if (cijfers == 0) return nullptr;
  if (negatief) *m = -*m;
  return t;
}
bool gelijkLetters(const char* a, const char* b) {  // hoofdletterongevoelig
  for (; *a && *b; ++a, ++b)
    if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
  return !*a && !*b;
}
bool uitTekst(int i, const char* tekst, double* v) {
  while (std::isspace((unsigned char)*tekst)) ++tekst;
  double m; int k;
  const char* rest = leesGetal(tekst, &m, &k);
  if (!rest) return false;
  char eenheid[16]; uint32_t n = 0;
  for (; *rest; ++rest)
    if (!std::isspace((unsigned char)*rest)) {
      if (n + 1 >= sizeof eenheid) return false;
      eenheid[n++] = *rest;
    }
  eenheid[n] = 0;
  const double tienMacht = std::pow(10.0, k);
  double w;
  const bool kaal = n == 0;
  switch (Ontwerp::PARAMS[i].vlaggen & pb::STAPPEN ? Soort::ANDERS : soortVan(i)) {
    case Soort::MS:
      if (kaal || gelijkLetters(eenheid, "ms")) w = m / tienMacht;
      else if (gelijkLetters(eenheid, "s")) w = m * 1000 / tienMacht;
      else return false;
      break;
    case Soort::HZ:
      if (kaal || gelijkLetters(eenheid, "Hz")) w = m / tienMacht;
      else if (gelijkLetters(eenheid, "kHz") || gelijkLetters(eenheid, "k")) w = m * 1000 / tienMacht;
      else return false;
      break;
    case Soort::DB:
      if (!kaal && !gelijkLetters(eenheid, "dB")) return false;
      w = m / tienMacht;
      break;
    case Soort::Q:
      if (!kaal && !gelijkLetters(eenheid, "Q")) return false;
      w = m / tienMacht;
      break;
    case Soort::PROCENT:  // de tekst is in procenten, ook zonder "%"
      if (!kaal && std::strcmp(eenheid, "%")) return false;
      w = m / (tienMacht * 100);
      break;
    case Soort::ANDERS:
      if (!kaal && !gelijkLetters(eenheid, Ontwerp::PARAMS[i].eenheid)) return false;
      w = m / tienMacht;
      break;
  }
  if (!std::isfinite(w)) return false;
  *v = klem(i, w);
  return true;
}

// --- params -------------------------------------------------------------------------------------------------
uint32_t paramAantal(const clap_plugin_t*) { return N_ZICHTBAAR; }
bool paramInfo(const clap_plugin_t*, uint32_t positie, clap_param_info_t* info) {
  const int index = indexOpPositie(positie);
  if (index < 0) return false;
  const auto& pi = Ontwerp::PARAMS[index];
  std::memset(info, 0, sizeof *info);
  info->id = pi.id;
  info->flags = (pi.vlaggen & pb::AUTOMATEERBAAR) ? CLAP_PARAM_IS_AUTOMATABLE : 0;
  if (pi.vlaggen & pb::STAPPEN) info->flags |= CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof info->name, "%s", pi.naam);
  info->module[0] = 0;
  info->min_value = pi.min;
  info->max_value = pi.max;
  info->default_value = pi.standaard;
  return true;
}
bool paramWaarde(const clap_plugin_t* p, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(p)->waarde[i].load(std::memory_order_relaxed);
  return true;
}
bool paramNaarTekst(const clap_plugin_t*, clap_id id, double v, char* tekst, uint32_t grootte) {
  const int i = indexVan(id);
  if (i < 0 || !tekst || grootte == 0) return false;
  naarTekst(i, v, tekst, grootte);
  return true;
}
bool paramUitTekst(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !tekst || !v) return false;
  return uitTekst(i, tekst, v);
}
void paramFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* uit) {
  // [actief ? audiothread : mainthread]; in beide gevallen draait process niet tegelijk
  // Zonder process: de host zet waarden terwijl er geen audio loopt (een project openen, automatisering lezen
  // terwijl het transport stilstaat). De events gaan in de DSP en de spiegel; de staart volgt als hij actief is.
  Plugin* z = zelf(p);
  neemStateOver(z);
  neemPaneelOver(z, uit);
  if (in) {
    const uint32_t n = in->size(in);
    for (uint32_t k = 0; k < n; ++k) doeEvent(z, in->get(in, k));
  }
  if (z->actief.load(std::memory_order_relaxed)) rekenStaart(z);
}
const clap_plugin_params_t PARAMS_EXT = {paramAantal, paramInfo, paramWaarde, paramNaarTekst, paramUitTekst, paramFlush};

// --- tail ---------------------------------------------------------------------------------------------------
uint32_t staart(const clap_plugin_t* p) { return zelf(p)->staart.load(std::memory_order_relaxed); }
const clap_plugin_tail_t TAIL = {staart};

// --- audio-ports: één stereo in, één stereo uit -------------------------------------------------------------
uint32_t poortAantal(const clap_plugin_t*, bool) { return 1; }
bool poortInfo(const clap_plugin_t*, uint32_t index, bool isIn, clap_audio_port_info_t* info) {
  if (index != 0) return false;
  info->id = isIn ? 0 : 1;
  std::snprintf(info->name, sizeof info->name, "%s", isIn ? "Ingang" : "Uitgang");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {poortAantal, poortInfo};

// --- latency ------------------------------------------------------------------------------------------------
uint32_t latentie(const clap_plugin_t* p) { return zelf(p)->ontwerp->latentie(); }
const clap_plugin_latency_t LATENCY = {latentie};

// --- state: "PBST", versie, aantal, dan per parameter (id u32, waarde f64) -----------------------------------
// Laden gaat per id: een onbekend id wordt overgeslagen, een ontbrekend id houdt zijn standaard (vooruit-compatibel).
bool schrijfAlles(const clap_ostream_t* s, const void* p, uint64_t n) {
  auto* c = static_cast<const unsigned char*>(p);
  while (n > 0) {
    int64_t w = s->write(s, c, n);
    if (w <= 0) return false;
    c += w; n -= (uint64_t)w;
  }
  return true;
}
bool leesAlles(const clap_istream_t* s, void* p, uint64_t n) {
  auto* c = static_cast<unsigned char*>(p);
  while (n > 0) {
    int64_t r = s->read(s, c, n);
    if (r <= 0) return false;
    c += r; n -= (uint64_t)r;
  }
  return true;
}
// nieuwe waarden voor alle parameters (state of preset) [mainthread]: in de spiegel, en in de DSP direct als niets
// draait, anders via process/flush; daarna de host om een rescan (en een flush) vragen
void zetAlleWaarden(Plugin* z, const double* nieuw) {
  for (int i = 0; i < N; ++i) z->waarde[i].store(nieuw[i], std::memory_order_relaxed);
  if (z->actief) z->stateNieuw.store(true, std::memory_order_release);  // process (of flush) neemt het over
  else for (int i = 0; i < N; ++i) z->ontwerp->zetParam(i, nieuw[i]);  // niets draait: direct
  // de host moet de nieuwe waarden opnieuw lezen (en, als hij actief is, flush of process aanroepen)
  auto* hp = z->host ? static_cast<const clap_host_params_t*>(z->host->get_extension(z->host, CLAP_EXT_PARAMS)) : nullptr;
  if (hp) {
    if (hp->rescan) hp->rescan(z->host, CLAP_PARAM_RESCAN_VALUES);
    if (z->actief && hp->request_flush) hp->request_flush(z->host);
  }
}
bool stateSave(const clap_plugin_t* p, const clap_ostream_t* s) {
  Plugin* z = zelf(p);
  const uint32_t kop[3] = {STATE_MAGIE, STATE_VERSIE, (uint32_t)N_ZICHTBAAR};
  if (!schrijfAlles(s, kop, sizeof kop)) return false;
  for (int i = 0; i < N; ++i) {
    if (verborgen(i)) continue;
    const uint32_t id = Ontwerp::PARAMS[i].id;
    const double waarde = z->waarde[i].load(std::memory_order_relaxed);
    if (!schrijfAlles(s, &id, sizeof id) || !schrijfAlles(s, &waarde, sizeof waarde)) return false;
  }
  return true;
}
bool stateLoad(const clap_plugin_t* p, const clap_istream_t* s) {
  Plugin* z = zelf(p);
  uint32_t kop[3];
  if (!leesAlles(s, kop, sizeof kop) || kop[0] != STATE_MAGIE) return false;
  double nieuw[N];
  for (int i = 0; i < N; ++i) nieuw[i] = Ontwerp::PARAMS[i].standaard;
  for (uint32_t k = 0; k < kop[2]; ++k) {
    uint32_t id; double waarde;
    if (!leesAlles(s, &id, sizeof id) || !leesAlles(s, &waarde, sizeof waarde)) return false;
    const int i = indexVan(id);
    if (i >= 0) nieuw[i] = klem(i, waarde);
  }
  zetAlleWaarden(z, nieuw);
  return true;
}
const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- presets: de fabriekspresets, één JSON-bestand per preset naast de plugin -------------------------------------
// Een preset is `{"naam": ..., "uitleg": ..., "waarden": {"<parameternaam>": getal, ...}}` (presets/*.json). CMake
// kopieert ze in de bundel: macOS `<bundel>/Contents/Resources/presets`, elders de map `<naam>-presets` naast de .clap.
// CLAP preset-discovery: één provider, één locatie van het soort FILE (die map, als hij bestaat), bestandstype json;
// elk bestand is één preset (geen laadsleutel). preset-load leest het bestand en zet de waarden via hetzelfde pad als
// een state: een onbekende naam wordt overgeslagen, wat de preset niet noemt krijgt zijn standaard. clap-wrapper maakt
// er AU factory presets van (patches/clap-wrapper-auv2-presets.patch). Alles hier draait op de mainthread.
// gezet in pb_entry_init. Een functie-static, geen globale: de AU van clap-wrapper roept clap_entry.init al aan
// tijdens de statische initialisatie (zijn globale Clap::Library), mogelijk vóór de constructor van een globale
// std::string in dit bestand, die het pad dan weer leeg zou maken.
std::string& presetMap() {
  static std::string map;
  return map;
}

struct PresetBestand {
  std::string naam, uitleg;
  std::vector<std::pair<std::string, double>> waarden;
};
// een kleine JSON-lezer voor dit ene formaat; getallen zonder locale (een host kan een komma-locale zetten)
class JsonLezer {
 public:
  JsonLezer(const char* p, const char* eind) : p_(p), eind_(eind) {}
  bool leesPreset(PresetBestand* uit) {
    if (!teken('{')) return false;
    if (teken('}')) return eindeTekst();
    do {
      std::string sleutel;
      if (!tekst(&sleutel) || !teken(':')) return false;
      if (sleutel == "naam") {
        if (!tekst(&uit->naam)) return false;
      } else if (sleutel == "uitleg") {
        if (!tekst(&uit->uitleg)) return false;
      } else if (sleutel == "waarden") {
        if (!teken('{')) return false;
        if (!teken('}')) {
          do {
            std::string param;
            double v;
            if (!tekst(&param) || !teken(':') || !getal(&v)) return false;
            uit->waarden.emplace_back(param, v);
          } while (teken(','));
          if (!teken('}')) return false;
        }
      } else if (!slaOver(0)) {
        return false;
      }
    } while (teken(','));
    return teken('}') && eindeTekst() && !uit->naam.empty();
  }

 private:
  void wit() {
    while (p_ < eind_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
  }
  bool eindeTekst() { wit(); return p_ == eind_; }
  bool teken(char c) {
    wit();
    if (p_ < eind_ && *p_ == c) { ++p_; return true; }
    return false;
  }
  static void utf8(std::string* t, uint32_t c) {
    if (c < 0x80) { *t += (char)c; return; }
    if (c < 0x800) { *t += (char)(0xc0 | (c >> 6)); }
    else {
      if (c < 0x10000) { *t += (char)(0xe0 | (c >> 12)); }
      else { *t += (char)(0xf0 | (c >> 18)); *t += (char)(0x80 | ((c >> 12) & 0x3f)); }
      *t += (char)(0x80 | ((c >> 6) & 0x3f));
    }
    *t += (char)(0x80 | (c & 0x3f));
  }
  bool hex4(uint32_t* c) {
    if (eind_ - p_ < 4) return false;
    *c = 0;
    for (int i = 0; i < 4; ++i, ++p_) {
      const char h = *p_;
      *c <<= 4;
      if (h >= '0' && h <= '9') *c |= (uint32_t)(h - '0');
      else if (h >= 'a' && h <= 'f') *c |= (uint32_t)(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') *c |= (uint32_t)(h - 'A' + 10);
      else return false;
    }
    return true;
  }
  bool tekst(std::string* t) {
    if (!teken('"')) return false;
    t->clear();
    while (p_ < eind_ && *p_ != '"') {
      const unsigned char c = (unsigned char)*p_++;
      if (c < 0x20) return false;
      if (c != '\\') { *t += (char)c; continue; }
      if (p_ >= eind_) return false;
      switch (*p_++) {
        case '"': *t += '"'; break;
        case '\\': *t += '\\'; break;
        case '/': *t += '/'; break;
        case 'b': *t += '\b'; break;
        case 'f': *t += '\f'; break;
        case 'n': *t += '\n'; break;
        case 'r': *t += '\r'; break;
        case 't': *t += '\t'; break;
        case 'u': {
          uint32_t u;
          if (!hex4(&u)) return false;
          if (u >= 0xd800 && u < 0xdc00) {  // een surrogaatpaar
            uint32_t laag;
            if (eind_ - p_ < 2 || p_[0] != '\\' || p_[1] != 'u') return false;
            p_ += 2;
            if (!hex4(&laag) || laag < 0xdc00 || laag >= 0xe000) return false;
            u = 0x10000 + ((u - 0xd800) << 10) + (laag - 0xdc00);
          } else if (u >= 0xdc00 && u < 0xe000) {
            return false;
          }
          utf8(t, u);
          break;
        }
        default: return false;
      }
    }
    return teken('"');
  }
  // -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?, als m · 10^n met één correct afgeronde stap (m < 2^53, |n| ≤ 22)
  bool getal(double* v) {
    wit();
    bool negatief = false;
    if (p_ < eind_ && *p_ == '-') { negatief = true; ++p_; }
    double m = 0;
    int n = 0, cijfers = 0;
    auto cijfer = [&](bool fractie) {
      if (m < 1e15) { m = m * 10 + (*p_ - '0'); if (fractie) --n; }
      else if (!fractie) ++n;
      ++cijfers; ++p_;
    };
    if (p_ < eind_ && *p_ == '0') cijfer(false);
    else if (p_ < eind_ && *p_ >= '1' && *p_ <= '9') while (p_ < eind_ && *p_ >= '0' && *p_ <= '9') cijfer(false);
    else return false;
    if (p_ < eind_ && *p_ == '.') {
      ++p_;
      const int voor = cijfers;
      while (p_ < eind_ && *p_ >= '0' && *p_ <= '9') cijfer(true);
      if (cijfers == voor) return false;
    }
    if (p_ < eind_ && (*p_ == 'e' || *p_ == 'E')) {
      ++p_;
      bool min = false;
      if (p_ < eind_ && (*p_ == '+' || *p_ == '-')) min = *p_++ == '-';
      if (p_ >= eind_ || *p_ < '0' || *p_ > '9') return false;
      int e = 0;
      while (p_ < eind_ && *p_ >= '0' && *p_ <= '9') { if (e < 1000) e = e * 10 + (*p_ - '0'); ++p_; }
      n += min ? -e : e;
    }
    double w = m;
    if (n > 0) w = m * std::pow(10.0, n);
    if (n < 0) w = -n <= 22 ? m / std::pow(10.0, -n) : m * std::pow(10.0, n);  // 10^k is exact tot k = 22
    if (!std::isfinite(w)) return false;
    *v = negatief ? -w : w;
    return true;
  }
  // een waarde die deze lezer niet nodig heeft
  bool slaOver(int diepte) {
    if (diepte > 32) return false;
    wit();
    if (p_ >= eind_) return false;
    if (*p_ == '"') { std::string t; return tekst(&t); }
    if (*p_ == '{' || *p_ == '[') {
      const char sluit = *p_ == '{' ? '}' : ']';
      ++p_;
      if (teken(sluit)) return true;
      do {
        if (sluit == '}') { std::string t; if (!tekst(&t) || !teken(':')) return false; }
        if (!slaOver(diepte + 1)) return false;
      } while (teken(','));
      return teken(sluit);
    }
    for (const char* woord : {"true", "false", "null"}) {
      const size_t n = std::strlen(woord);
      if ((size_t)(eind_ - p_) >= n && !std::memcmp(p_, woord, n)) { p_ += n; return true; }
    }
    double v;
    return getal(&v);
  }
  const char* p_;
  const char* eind_;
};
bool leesPresetBestand(const char* pad, PresetBestand* uit, std::string* fout) {
  FILE* f = std::fopen(pad, "rb");
  if (!f) { *fout = std::string("kan de preset niet openen: ") + pad; return false; }
  std::string inhoud;
  char buf[4096];
  size_t r;
  while ((r = std::fread(buf, 1, sizeof buf, f)) > 0 && inhoud.size() < (1u << 20)) inhoud.append(buf, r);
  std::fclose(f);
  JsonLezer lezer(inhoud.data(), inhoud.data() + inhoud.size());
  if (!lezer.leesPreset(uit)) { *fout = std::string("geen geldige preset (JSON met naam en waarden): ") + pad; return false; }
  return true;
}
// de INDEX in PARAMS bij een parameternaam, of -1 (ook voor een verborgen parameter)
int indexVanNaam(const std::string& naam) {
  for (int i = 0; i < N; ++i)
    if (naam == Ontwerp::PARAMS[i].naam) return verborgen(i) ? -1 : i;
  return -1;
}

bool presetLaad(const clap_plugin_t* p, uint32_t soort, const char* plek, const char* sleutel) {
  Plugin* z = zelf(p);
  auto* hp = z->host ? static_cast<const clap_host_preset_load_t*>(z->host->get_extension(z->host, CLAP_EXT_PRESET_LOAD)) : nullptr;
  if (!hp && z->host) hp = static_cast<const clap_host_preset_load_t*>(z->host->get_extension(z->host, CLAP_EXT_PRESET_LOAD_COMPAT));
  PresetBestand preset;
  std::string fout = "alleen presets uit een bestand (CLAP_PRESET_DISCOVERY_LOCATION_FILE)";
  if (soort != CLAP_PRESET_DISCOVERY_LOCATION_FILE || !plek || !leesPresetBestand(plek, &preset, &fout)) {
    if (hp && hp->on_error) hp->on_error(z->host, soort, plek, sleutel, 0, fout.c_str());
    return false;
  }
  double nieuw[N];
  for (int i = 0; i < N; ++i) nieuw[i] = Ontwerp::PARAMS[i].standaard;
  for (const auto& w : preset.waarden) {
    const int i = indexVanNaam(w.first);
    if (i >= 0) nieuw[i] = klem(i, w.second);
  }
  zetAlleWaarden(z, nieuw);
  if (hp && hp->loaded) hp->loaded(z->host, soort, plek, sleutel);
  return true;
}
const clap_plugin_preset_load_t PRESET_LOAD = {presetLaad};

const clap_preset_discovery_provider_descriptor_t PROVIDER_DESC = {CLAP_VERSION_INIT, PB_CLAP_ID ".fabriekspresets",
                                                                   PB_NAAM " fabriekspresets", PB_MAKER};
struct Provider {
  clap_preset_discovery_provider_t clap{};
  const clap_preset_discovery_indexer_t* indexer = nullptr;
};
bool providerInit(const clap_preset_discovery_provider_t* p) {
  const auto* ix = static_cast<const Provider*>(p->provider_data)->indexer;
  if (!ix || !ix->declare_filetype || !ix->declare_location) return false;
  const clap_preset_discovery_filetype_t type = {"Dub-echo-preset", "parameterwaarden per naam (JSON)", "json"};
  if (!ix->declare_filetype(ix, &type)) return false;
  struct stat st;
  if (presetMap().empty() || stat(presetMap().c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return true;  // geen presets
  const clap_preset_discovery_location_t plek = {CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Fabriekspresets",
                                                 CLAP_PRESET_DISCOVERY_LOCATION_FILE, presetMap().c_str()};
  return ix->declare_location(ix, &plek);
}
void providerWeg(const clap_preset_discovery_provider_t* p) { delete static_cast<Provider*>(p->provider_data); }
bool providerMetadata(const clap_preset_discovery_provider_t*, uint32_t soort, const char* plek,
                      const clap_preset_discovery_metadata_receiver_t* ontvanger) {
  if (soort != CLAP_PRESET_DISCOVERY_LOCATION_FILE || !plek || !ontvanger) return false;
  PresetBestand preset;
  std::string fout;
  if (!leesPresetBestand(plek, &preset, &fout)) {
    ontvanger->on_error(ontvanger, 0, fout.c_str());
    return false;
  }
  if (!ontvanger->begin_preset(ontvanger, preset.naam.c_str(), nullptr)) return true;
  const clap_universal_plugin_id_t clapId = {"clap", Ontwerp::META.clapId};
  ontvanger->add_plugin_id(ontvanger, &clapId);
  ontvanger->set_flags(ontvanger, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
  ontvanger->add_creator(ontvanger, Ontwerp::META.maker);
  if (!preset.uitleg.empty()) ontvanger->set_description(ontvanger, preset.uitleg.c_str());
  return true;
}
const void* providerExtensie(const clap_preset_discovery_provider_t*, const char*) { return nullptr; }

uint32_t providerAantal(const clap_preset_discovery_factory_t*) { return 1; }
const clap_preset_discovery_provider_descriptor_t* providerBeschrijving(const clap_preset_discovery_factory_t*,
                                                                        uint32_t i) {
  return i == 0 ? &PROVIDER_DESC : nullptr;
}
const clap_preset_discovery_provider_t* maakProvider(const clap_preset_discovery_factory_t*,
                                                     const clap_preset_discovery_indexer_t* indexer, const char* id) {
  if (!indexer || !id || std::strcmp(id, PROVIDER_DESC.id)) return nullptr;
  Provider* pr = new (std::nothrow) Provider();
  if (!pr) return nullptr;
  pr->indexer = indexer;
  pr->clap = {&PROVIDER_DESC, pr, providerInit, providerWeg, providerMetadata, providerExtensie};
  return &pr->clap;
}
const clap_preset_discovery_factory_t PRESET_FACTORY = {providerAantal, providerBeschrijving, maakProvider};

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t* p) {
  Plugin* z = zelf(p);
  z->ontwerp = new (std::nothrow) Ontwerp();
  if (!z->ontwerp) return false;
  for (int i = 0; i < N; ++i) {
    z->waarde[i].store(Ontwerp::PARAMS[i].standaard, std::memory_order_relaxed);
    z->ontwerp->zetParam(i, Ontwerp::PARAMS[i].standaard);
    z->uiWaarde[i].store(Ontwerp::PARAMS[i].standaard, std::memory_order_relaxed);
    z->uiBegin[i].store(0); z->uiEind[i].store(0); z->uiTeller[i].store(0); z->uiGezien[i].store(0);
  }
  if (z->host) z->hostParams = static_cast<const clap_host_params_t*>(z->host->get_extension(z->host, CLAP_EXT_PARAMS));
  return true;
}
void destroy(const clap_plugin_t* p) {
  Plugin* z = zelf(p);
#if PB_PANEEL
  guiOpruimen(z);  // de host hoort gui.destroy eerst te doen
#endif
  delete z->ontwerp;
  delete[] z->geheugen;
  delete z;
}
bool activate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t) {
  Plugin* z = zelf(p);
  delete[] z->geheugen;
  const unsigned long n = Ontwerp::geheugenNodig(sr);
  z->geheugen = new (std::nothrow) unsigned char[n];
  if (!z->geheugen) return false;
  pb::Geheugen g;
  g.p = z->geheugen;
  g.over = n;
  z->sr = sr;
  z->ontwerp->activeer(sr, minFrames, g);
  z->stateNieuw.store(false, std::memory_order_relaxed);
  for (int i = 0; i < N; ++i) z->ontwerp->zetParam(i, z->waarde[i].load(std::memory_order_relaxed));
  rekenStaart(z);
  z->actief = true;
  return true;
}
void deactivate(const clap_plugin_t* p) {
  Plugin* z = zelf(p);
  z->actief = false;
  neemStateOver(z);  // een state die tijdens het actief zijn kwam en nog niet is overgenomen
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { zelf(p)->ontwerp->reset(); }

// De DSP rekent per sample; het blok wordt bij elk event gesplitst zodat elke waarde op zijn sample ingaat.
clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
#if PB_KANARIE
  pb_kanarie = std::malloc(64);
#endif
  Plugin* z = zelf(p);
  neemStateOver(z);
  neemPaneelOver(z, pr->out_events);
  const uint32_t n = pr->frames_count;
  const clap_input_events_t* ev = pr->in_events;
  const uint32_t nEv = ev ? ev->size(ev) : 0;

  const bool heeftUit = pr->audio_outputs_count >= 1 && pr->audio_outputs[0].data32 && pr->audio_outputs[0].channel_count >= 1;
  const bool heeftIn = pr->audio_inputs_count >= 1 && pr->audio_inputs[0].data32 && pr->audio_inputs[0].channel_count >= 1;
  if (!heeftUit) {  // niets te schrijven: wel de events, dan klopt de toestand
    for (uint32_t k = 0; k < nEv; ++k) doeEvent(z, ev->get(ev, k));
    return CLAP_PROCESS_CONTINUE;
  }
  const clap_audio_buffer_t& uit = pr->audio_outputs[0];
  if (!heeftIn) {
    for (uint32_t k = 0; k < nEv; ++k) doeEvent(z, ev->get(ev, k));
    for (uint32_t c = 0; c < uit.channel_count; ++c) std::memset(uit.data32[c], 0, n * sizeof(float));
    return CLAP_PROCESS_CONTINUE;
  }
  const clap_audio_buffer_t& in = pr->audio_inputs[0];
  const int kanalen = (int)(uit.channel_count < 2 ? uit.channel_count : 2);

  uint32_t pos = 0, k = 0;
  while (pos < n) {
    // alle events op of vóór deze sample toepassen
    while (k < nEv) {
      const clap_event_header_t* h = ev->get(ev, k);
      if (h->time > pos) break;
      doeEvent(z, h);
      ++k;
    }
    uint32_t eind = n;
    if (k < nEv) {
      const uint32_t t = ev->get(ev, k)->time;
      if (t < eind) eind = t;
    }
    const float* bi[2];
    float* bu[2];
    for (int c = 0; c < kanalen; ++c) {
      // mono invoer: beide kanalen lezen hetzelfde (de DSP leest per sample in[c] vóór hij uit[c] schrijft)
      bi[c] = in.data32[(uint32_t)c < in.channel_count ? c : 0] + pos;
      bu[c] = uit.data32[c] + pos;
    }
    z->ontwerp->verwerk(bi, bu, kanalen, eind - pos);
    pos = eind;
  }
  for (; k < nEv; ++k) doeEvent(z, ev->get(ev, k));  // events met time ≥ n (hoort niet, maar verlies ze niet)
  for (uint32_t c = (uint32_t)kanalen; c < uit.channel_count; ++c) std::memset(uit.data32[c], 0, n * sizeof(float));
  rekenStaart(z);
  return CLAP_PROCESS_CONTINUE;
}

// --- gui: het frontpaneel (design.md) ------------------------------------------------------------------------
#if PB_PANEEL
// Wat het paneel van de plugin ziet. Lezen: de atomische spiegel (of de eigen waarde die process nog niet nam).
// Schrijven: alleen de ui-tellers en een verzoek aan de host om flush (of process) aan te roepen.
class Bron : public pb::PaneelBron {
 public:
  explicit Bron(Plugin* z) : z_(z) {}
  int aantal() const override { return N_ZICHTBAAR; }
  pb::KnopInfo info(int k) const override {
    const auto& pi = Ontwerp::PARAMS[indexOpPositie((uint32_t)k)];
    const Soort s = soortVan(indexOpPositie((uint32_t)k));
    return {pi.naam, pi.min, pi.max, pi.standaard, (s == Soort::HZ || s == Soort::MS) && pi.min > 0};
  }
  double waarde(int k) const override {
    const int i = indexOpPositie((uint32_t)k);
    const uint32_t teller = z_->uiTeller[i].load(std::memory_order_acquire);
    if (z_->uiGezien[i].load(std::memory_order_acquire) != teller) return klem(i, z_->uiWaarde[i].load(std::memory_order_relaxed));
    return z_->waarde[i].load(std::memory_order_relaxed);
  }
  void tekst(int k, double v, char* t, uint32_t grootte) const override { naarTekst(indexOpPositie((uint32_t)k), v, t, grootte); }
  void begin(int k) override {
    z_->uiBegin[indexOpPositie((uint32_t)k)].fetch_add(1, std::memory_order_release);
    vraagFlush();
  }
  void zet(int k, double v) override {
    const int i = indexOpPositie((uint32_t)k);
    z_->uiWaarde[i].store(klem(i, v), std::memory_order_relaxed);
    z_->uiTeller[i].fetch_add(1, std::memory_order_release);
    vraagFlush();
  }
  void eind(int k) override {
    z_->uiEind[indexOpPositie((uint32_t)k)].fetch_add(1, std::memory_order_release);
    vraagFlush();
  }

 private:
  // de host roept daarna process of flush aan; daar gaan de waarden de DSP in en naar de host
  void vraagFlush() {
    if (z_->hostParams && z_->hostParams->request_flush) z_->hostParams->request_flush(z_->host);
  }
  Plugin* z_;
};

void guiOpruimen(Plugin* z) {
  if (z->paneel && z->paneel->sleept()) z->paneel->muisOp();  // een open gebaar netjes sluiten
  delete z->venster; z->venster = nullptr;
  delete z->paneel; z->paneel = nullptr;
  delete z->bron; z->bron = nullptr;
}
bool guiApi(const clap_plugin_t*, const char* api, bool zwevend) {
  return !zwevend && api && pb::vensterApi() && !std::strcmp(api, pb::vensterApi());
}
bool guiVoorkeur(const clap_plugin_t*, const char** api, bool* zwevend) {
  *api = pb::vensterApi();
  *zwevend = false;
  return *api != nullptr;
}
bool guiMaak(const clap_plugin_t* p, const char* api, bool zwevend) {
  Plugin* z = zelf(p);
  if (!guiApi(p, api, zwevend) || z->paneel) return false;
  if (!z->guiB) { z->guiB = pb::PANEEL_STD_B; z->guiH = pb::PANEEL_STD_H; }
  z->bron = new Bron(z);
  z->paneel = new pb::Paneel(*z->bron);
  z->venster = pb::maakVenster(*z->paneel, z->host, z->guiB, z->guiH);
  if (!z->venster) {
    guiOpruimen(z);
    return false;
  }
  return true;
}
void guiWeg(const clap_plugin_t* p) { guiOpruimen(zelf(p)); }
bool guiSchaal(const clap_plugin_t*, double) { return false; }  // grootte in punten (macOS) of pixels (X11)
bool guiGrootte(const clap_plugin_t* p, uint32_t* b, uint32_t* h) {
  Plugin* z = zelf(p);
  *b = z->guiB ? z->guiB : pb::PANEEL_STD_B;
  *h = z->guiH ? z->guiH : pb::PANEEL_STD_H;
  return true;
}
bool guiKanGroter(const clap_plugin_t*) { return true; }
bool guiHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
  hints->can_resize_horizontally = true;
  hints->can_resize_vertically = true;
  hints->preserve_aspect_ratio = false;  // het paneel schaalt gelijkmatig en staat in het midden
  hints->aspect_ratio_width = 0;
  hints->aspect_ratio_height = 0;
  return true;
}
bool guiPas(const clap_plugin_t*, uint32_t* b, uint32_t* h) {
  if (*b < pb::PANEEL_MIN_B) *b = pb::PANEEL_MIN_B;
  if (*h < pb::PANEEL_MIN_H) *h = pb::PANEEL_MIN_H;
  if (*b > 8192) *b = 8192;
  if (*h > 8192) *h = 8192;
  return true;
}
bool guiZetGrootte(const clap_plugin_t* p, uint32_t b, uint32_t h) {
  Plugin* z = zelf(p);
  guiPas(p, &b, &h);
  z->guiB = b; z->guiH = h;
  return z->venster ? z->venster->zetGrootte(b, h) : true;
}
bool guiOuder(const clap_plugin_t* p, const clap_window_t* w) {
  Plugin* z = zelf(p);
  return z->venster && w && w->api && pb::vensterApi() && !std::strcmp(w->api, pb::vensterApi()) && z->venster->zetOuder(w);
}
bool guiTijdelijk(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiTitel(const clap_plugin_t*, const char*) {}
bool guiToon(const clap_plugin_t* p) { return zelf(p)->venster && zelf(p)->venster->toon(); }
bool guiVerberg(const clap_plugin_t* p) { return zelf(p)->venster && zelf(p)->venster->verberg(); }
const clap_plugin_gui_t GUI = {guiApi, guiVoorkeur, guiMaak, guiWeg, guiSchaal, guiGrootte, guiKanGroter, guiHints,
                               guiPas, guiZetGrootte, guiOuder, guiTijdelijk, guiTitel, guiToon, guiVerberg};

#if !defined(__APPLE__) && !defined(_WIN32)
// timers en fd's van de host (Linux: het X11-venster hertekent en leest zijn events zo)
void opTimer(const clap_plugin_t* p, clap_id id) {
  if (zelf(p)->venster) zelf(p)->venster->opTimer(id);
}
const clap_plugin_timer_support_t TIMER = {opTimer};
void opFd(const clap_plugin_t* p, int fd, clap_posix_fd_flags_t vlaggen) {
  if (zelf(p)->venster) zelf(p)->venster->opFd(fd, vlaggen);
}
const clap_plugin_posix_fd_support_t FD = {opFd};
#endif
#endif

const void* getExtension(const clap_plugin_t*, const char* id) {
#if PB_PANEEL
  if (!std::strcmp(id, CLAP_EXT_GUI) && pb::vensterApi()) return &GUI;
#if !defined(__APPLE__) && !defined(_WIN32)
  if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &TIMER;
  if (!std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &FD;
#endif
#endif
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD) || !std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &PRESET_LOAD;
  return nullptr;
}
void onMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  Plugin* z = new (std::nothrow) Plugin();
  if (!z) return nullptr;
  z->host = host;
  z->clap = {&DESCRIPTOR, z,          init,    destroy,      activate,     deactivate,  startProcessing,
             stopProcessing, reset,   process, getExtension, onMainThread};
  return &z->clap;
}

// --- factory ------------------------------------------------------------------------------------------------
uint32_t aantalPlugins(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* descriptor(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* createPlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id)) return nullptr;
  return maakPlugin(host);
}
const clap_plugin_factory_t FACTORY = {aantalPlugins, descriptor, createPlugin};

}  // namespace

// pad: de bundel (macOS) of het bestand van de plugin (Linux, Windows); de presets staan ernaast (zie "presets")
bool pb_entry_init(const char* pad) {
  presetMap().clear();
  if (!pad || !*pad) return true;
  char echt[PATH_MAX];
  std::string p = realpath(pad, echt) ? echt : pad;  // een host kan een relatief pad geven; een FILE-locatie is absoluut
  while (p.size() > 1 && p.back() == '/') p.pop_back();
#ifdef __APPLE__
  presetMap() = p + "/Contents/Resources/presets";
#else
  const size_t schuin = p.find_last_of('/'), punt = p.find_last_of('.');
  presetMap() = (punt != std::string::npos && (schuin == std::string::npos || punt > schuin) ? p.substr(0, punt) : p) + "-presets";
#endif
  return true;
}
void pb_entry_deinit() {}
const void* pb_entry_get_factory(const char* id) {
  if (!std::strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return &FACTORY;
  if (!std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) || !std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT))
    return &PRESET_FACTORY;
  return nullptr;
}
