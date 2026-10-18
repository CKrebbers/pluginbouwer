// plugin.cpp — de CLAP-schil van PB Toetsenpad (fase 5: parameters met tekst ↔ waarde, state, params.flush, paneel,
// fabriekspresets via preset-discovery en preset-load, stereo- en mono-uitgang, bypass).
//
// Een instrument: geen audio-ingang, één uitgang (stereo of mono: audio-ports-config, en configurable-audio-ports voor
// de VST3 van clap-wrapper; valkuil 17), één note port (alleen het CLAP-dialect, valkuil 19).
// Bypass (CLAP_PARAM_IS_BYPASS, id 5, na de vier van SPEC.md): de DSP rekent door, de uitvoer vloeit in 10 ms lineair
// over naar de droge invoer (bij een instrument stilte), en ook die mix gaat door pb::uitvoer (geen subnormale float).
// Het Ontwerp uit dsp/ontwerpen/keten_toetsenpad.h levert naam, id, maker, parameters, latency en staart; process
// geeft `verwerk` een stille invoer en past parameter- en noot-events op hun sample toe (het blok wordt bij elk event
// gesplitst; de DSP rekent per sample, dus de uitvoer hangt niet af van het splitsen).
//
// Draden (ARCHITECTUUR.md "Threads", valkuil 11): zolang de plugin actief is, raakt alleen de audiothread (process,
// en params.flush) het Ontwerp aan. params.get_value en state.save lezen een atomaire kopie per parameter die de
// audiothread bijwerkt; state.load terwijl de plugin actief is zet waarden klaar plus een atomaire vlag, en
// process/flush neemt ze over vóór de events van dat blok. tail.get leest een staart die process uitrekende.
// activate reserveert al het geheugen op de mainthread; process alloceert niet, lockt niet en doet geen syscalls
// (RTSan in CI). Met PB_KANARIE alloceert process wel: die bouw MOET falen in RTSan.
//
// Het paneel (fase 3, paneel.h; op macOS de gui-extensie met paneel_mac.mm) stuurt parameters via de host (valkuil
// 14): een wijziging gaat als BEGIN/WAARDE/EIND in een lock-vrije rij (één schrijver: de mainthread; één lezer:
// process of params.flush, die nooit tegelijk draaien) en de plugin vraagt request_flush. process/flush geeft ze als
// CLAP_EVENT_PARAM_GESTURE_BEGIN/PARAM_VALUE/GESTURE_END aan de host en past de waarde toe zoals een event van de host.
// Het paneel leest alleen de atomaire spiegel; het raakt het Ontwerp en de audiothread nooit.
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "ontwerpen/keten_toetsenpad.h"
#include "pb_noten.h"
#include "entry.h"
#include "koppeling.h"
#ifdef __APPLE__
#include "paneel_mac.h"
#endif

#ifdef PB_KANARIE
// de kanarie: een allocatie in process, bewaard in een globale variabele zodat de compiler hem niet wegoptimaliseert
void* volatile pbKanarie = nullptr;
#endif

namespace {

using pb::Ontwerp;

constexpr uint32_t KANALEN = 2;       // hoogstens (stereo); mono is 1
constexpr int N = Ontwerp::N_PARAMS;  // de parameters van het Ontwerp (index 0..N−1)
constexpr int BYPASS = N;             // de index van Bypass: van de schil, niet van het Ontwerp
constexpr int NP = N + 1;             // alle parameters naar de host
constexpr int NOOT = PB_NOOT_PARAM;    // index in PARAMS, geen id
constexpr int POORT = PB_POORT_PARAM;  // index in PARAMS, geen id
constexpr double OVERVLOEI_SEC = 0.010;  // bypass aan of uit: 10 ms lineair overvloeien

// Ontwerp::PARAMS plus Bypass. Het id van Bypass is het eerste vrije na SPEC.md; nooit hergebruiken of hernummeren.
struct AlleParams {
  pb::ParamInfo p[NP];
};
constexpr AlleParams maakAlle() {
  AlleParams a{};
  for (int i = 0; i < N; ++i) a.p[i] = Ontwerp::PARAMS[i];
  uint32_t id = 1;
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id >= id) id = Ontwerp::PARAMS[i].id + 1;
  a.p[BYPASS] = {id, "Bypass", "", "", 0.0, 1.0, 0.0, 0.0, 0};
  return a;
}
constexpr AlleParams ALLE = maakAlle();
static_assert(ALLE.p[BYPASS].id == 5, "Bypass heeft id 5 (zie SPEC.md); een ander id breekt opgeslagen projecten");

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                CLAP_PLUGIN_FEATURE_STEREO, CLAP_PLUGIN_FEATURE_MONO, nullptr};

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

// de index in ALLE bij een parameter-id (de id's staan vast in SPEC.md), of −1; BYPASS voor Bypass
int indexVan(clap_id id) {
  for (int i = 0; i < NP; ++i)
    if (ALLE.p[i].id == id) return i;
  return -1;
}

// binnen het bereik; Bypass is getrapt (CLAP_PARAM_IS_STEPPED): 0 of 1, vanaf het midden aan (zoals de AU-wrapper
// kAudioUnitProperty_BypassEffect leest, en zoals VST3 een genormaliseerde waarde van een stap afrondt)
double klem(double v, int i) {
  const auto& p = ALLE.p[i];
  v = v < p.min ? p.min : (v > p.max ? p.max : v);
  if (i == BYPASS) v = v >= 0.5 ? 1.0 : 0.0;
  return v;
}

// De rij van het paneel naar process/flush: één schrijver (de mainthread), één lezer (process of params.flush; de
// host roept die nooit tegelijk aan). Lock-vrij en zonder allocatie aan de leeskant.
struct PaneelRij {
  static constexpr uint32_t GROOTTE = 1024;  // een macht van twee
  pb::paneel::Wijziging w[GROOTTE];
  std::atomic<uint32_t> kop{0};     // volgende plek om te schrijven (alleen de schrijver verhoogt hem)
  std::atomic<uint32_t> staart{0};  // volgende plek om te lezen (alleen de lezer verhoogt hem)
  bool zet(const pb::paneel::Wijziging& x) {
    const uint32_t k = kop.load(std::memory_order_relaxed);
    if (k - staart.load(std::memory_order_acquire) >= GROOTTE) return false;  // vol
    w[k % GROOTTE] = x;
    kop.store(k + 1, std::memory_order_release);
    return true;
  }
  bool pak(pb::paneel::Wijziging& x) {
    const uint32_t t = staart.load(std::memory_order_relaxed);
    if (t == kop.load(std::memory_order_acquire)) return false;  // leeg
    x = w[t % GROOTTE];
    staart.store(t + 1, std::memory_order_release);
    return true;
  }
};

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;
  const clap_host_preset_load_t* hostPresetLoad = nullptr;
  Ontwerp ontwerp;
  pb::NootStapel stapel;
  uint32_t kanalenUit = KANALEN;         // de gekozen uitgang: 2 (stereo) of 1 (mono); alleen te kiezen niet actief

  // bypass (audiothread): het doel (0 of 1) en de stand van het overvloeien; springt na activate/reset, zoals de
  // smoothers van het Ontwerp
  double bypassDoel = 0.0, bypassMix = 0.0, bypassStap = 1.0;
  bool bypassVers = true;
  std::vector<unsigned char> geheugen;   // het blok van Ontwerp::geheugenNodig, gereserveerd in activate
  std::vector<float> stil;               // de stille invoer voor verwerk (gereserveerd in activate)
  uint32_t stilLengte = 0;
  double sr = 48000;
  std::atomic<bool> actief{false};

  std::atomic<double> spiegel[NP];       // wat de host ziet (get_value, state.save); de audiothread werkt hem bij
  std::atomic<double> klaar[NP];         // state.load terwijl actief: overnemen in process/flush
  std::atomic<bool> klaarVlag{false};
  std::atomic<double> staart{0.0};       // seconden, −1 = oneindig; uitgerekend op de audiothread

  PaneelRij rij;                                  // paneel → process/flush
  std::vector<pb::paneel::Wijziging> wachtrij;    // mainthread: wat (nog) niet in de rij paste
#ifdef __APPLE__
  PbVenster* venster = nullptr;                   // de gui (mainthread)
  uint32_t guiB = (uint32_t)pb::paneel::START_B, guiH = (uint32_t)pb::paneel::START_H;
#endif

  Plugin() {
    for (int i = 0; i < NP; ++i) {
      spiegel[i].store(ALLE.p[i].standaard);
      klaar[i].store(ALLE.p[i].standaard);
    }
  }
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// --- events (audiothread, of de mainthread als de plugin niet actief is) -----------------------------------------
// een (geklemde) waarde toepassen: een parameter van het Ontwerp, of het doel van Bypass; en de spiegel bijwerken
void zetWaarde(Plugin* s, int i, double v) {
  if (i == BYPASS) s->bypassDoel = v;
  else s->ontwerp.zetParam(i, v);
  s->spiegel[i].store(v, std::memory_order_relaxed);
}

void neemKlaarOver(Plugin* s) {
  if (!s->klaarVlag.exchange(false, std::memory_order_acq_rel)) return;
  for (int i = 0; i < NP; ++i) zetWaarde(s, i, s->klaar[i].load(std::memory_order_relaxed));
}

// ARCHITECTUUR.md "Noten" stap 2: na elk noot-event
void nootNaarParams(Plugin* s) {
  const int n = s->stapel.boven();
  if (n >= 0) {
    s->ontwerp.zetParam(NOOT, pb::nootVerhouding(n));
    s->ontwerp.zetParam(POORT, 1.0);
  } else {
    s->ontwerp.zetParam(POORT, 0.0);
  }
}

// het paneel: wijzigingen uit de rij naar de host (als uitvoerevents op sample 0) en toepassen zoals een event van de
// host (valkuil 14). Audiothread (process), of params.flush; nooit tegelijk.
void neemPaneelOver(Plugin* s, const clap_output_events_t* uit) {
  pb::paneel::Wijziging w;
  while (s->rij.pak(w)) {
    if (w.index < 0 || w.index >= N) continue;
    const clap_id id = Ontwerp::PARAMS[w.index].id;
    if (w.soort == pb::paneel::Soort::Waarde) {
      if (!std::isfinite(w.waarde)) continue;
      const double v = klem(w.waarde, w.index);
      zetWaarde(s, w.index, v);
      if (uit) {
        clap_event_param_value_t e{};
        e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id;
        e.cookie = nullptr;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = v;
        uit->try_push(uit, &e.header);
      }
    } else if (uit) {
      clap_event_param_gesture_t e{};
      const uint16_t type = w.soort == pb::paneel::Soort::Begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                : CLAP_EVENT_PARAM_GESTURE_END;
      e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, type, 0};
      e.param_id = id;
      uit->try_push(uit, &e.header);
    }
  }
}

void pasToe(Plugin* s, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  switch (h->type) {
    case CLAP_EVENT_PARAM_VALUE: {
      const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
      const int i = indexVan(e->param_id);
      if (i < 0 || !std::isfinite(e->value)) return;
      zetWaarde(s, i, klem(e->value, i));
      return;
    }
    case CLAP_EVENT_NOTE_ON:
    case CLAP_EVENT_NOTE_OFF: {
      const auto* e = reinterpret_cast<const clap_event_note_t*>(h);
      if (e->key < 0 || e->key > 127) return;  // buiten 0..127: negeren, ook geen stap 2
      if (h->type == CLAP_EVENT_NOTE_ON) s->stapel.aan(e->key);
      else s->stapel.uit(e->key);
      nootNaarParams(s);
      return;
    }
    case CLAP_EVENT_NOTE_CHOKE:
      s->stapel.leeg();
      nootNaarParams(s);
      return;
    case CLAP_EVENT_MIDI: {
      const auto* e = reinterpret_cast<const clap_event_midi_t*>(h);
      const uint8_t st = e->data[0] & 0xF0, d1 = e->data[1], d2 = e->data[2];
      if (st == 0x90 || st == 0x80) {
        if (d1 > 127) return;
        if (st == 0x90 && d2 > 0) s->stapel.aan(d1);
        else s->stapel.uit(d1);
        nootNaarParams(s);
      } else if (st == 0xB0 && (d1 == 120 || d1 == 123)) {
        s->stapel.leeg();
        nootNaarParams(s);
      }
      return;
    }
    default:
      return;
  }
}

// --- audio-ports: 0 ingangen, 1 uitgang (stereo of mono, zoals gekozen) -----------------------------------------
uint32_t apCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }
bool apGet(const clap_plugin_t* pl, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (isInput || index != 0) return false;
  const uint32_t k = zelf(pl)->kanalenUit;
  info->id = 0;
  std::strncpy(info->name, "Uit", sizeof info->name);
  info->name[sizeof info->name - 1] = 0;
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = k;
  info->port_type = k == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {apCount, apGet};

// --- audio-ports-config: 0 → 2 (id 0, de standaard) en 0 → 1 (id 1) ----------------------------------------------
// Valkuil 17: CLAP-hosts kiezen hiermee, de AU van clap-wrapper ook (patches/clap-wrapper-auv2-volgt-presets-mono-
// stereo.patch maakt er de AU-kanaalindelingen van); de VST3 van clap-wrapper vraagt via configurable-audio-ports.
struct UitIndeling {
  clap_id id;
  const char* naam;
  uint32_t kanalen;
};
constexpr UitIndeling INDELINGEN[] = {{0, "Stereo", 2}, {1, "Mono", 1}};
constexpr uint32_t N_INDELINGEN = sizeof INDELINGEN / sizeof INDELINGEN[0];

uint32_t apcCount(const clap_plugin_t*) { return N_INDELINGEN; }
bool apcGet(const clap_plugin_t*, uint32_t index, clap_audio_ports_config_t* c) {
  if (index >= N_INDELINGEN || !c) return false;
  const UitIndeling& d = INDELINGEN[index];
  std::memset(c, 0, sizeof *c);
  c->id = d.id;
  std::snprintf(c->name, sizeof c->name, "%s", d.naam);
  c->input_port_count = 0;
  c->output_port_count = 1;
  c->has_main_input = false;
  c->main_input_channel_count = 0;
  c->main_input_port_type = nullptr;
  c->has_main_output = true;
  c->main_output_channel_count = d.kanalen;
  c->main_output_port_type = d.kanalen == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  return true;
}
bool apcSelect(const clap_plugin_t* pl, clap_id id) {
  Plugin* s = zelf(pl);
  if (s->actief.load()) return false;  // [main-thread & plugin-deactivated]
  for (const UitIndeling& d : INDELINGEN)
    if (d.id == id) {
      s->kanalenUit = d.kanalen;
      return true;
    }
  return false;
}
const clap_plugin_audio_ports_config_t AUDIO_PORTS_CONFIG = {apcCount, apcGet, apcSelect};

clap_id apciCurrent(const clap_plugin_t* pl) {
  for (const UitIndeling& d : INDELINGEN)
    if (d.kanalen == zelf(pl)->kanalenUit) return d.id;
  return CLAP_INVALID_ID;
}
bool apciGet(const clap_plugin_t* pl, clap_id id, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  for (const UitIndeling& d : INDELINGEN)
    if (d.id == id) {
      if (isInput || index != 0 || !info) return false;
      apGet(pl, 0, false, info);
      info->channel_count = d.kanalen;
      info->port_type = d.kanalen == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
      return true;
    }
  return false;
}
const clap_plugin_audio_ports_config_info_t AUDIO_PORTS_CONFIG_INFO = {apciCurrent, apciGet};

// --- configurable-audio-ports: de uitgang op 1 of 2 kanalen (de VST3-wrapper vraagt zo in setBusArrangements) -----
// Elk verzoek moet de enige uitgang (index 0) zijn, met 1 of 2 kanalen en een passend of leeg poorttype. Geen
// verzoeken: niets te veranderen.
bool capKan(const clap_audio_port_configuration_request_t* r, uint32_t n, uint32_t* kanalen) {
  if (n > 0 && !r) return false;
  for (uint32_t k = 0; k < n; ++k) {
    if (r[k].is_input || r[k].port_index != 0) return false;
    const uint32_t c = r[k].channel_count;
    if (c != 1 && c != 2) return false;
    const char* t = r[k].port_type;
    if (t && std::strcmp(t, c == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO)) return false;
    *kanalen = c;
  }
  return true;
}
bool capCan(const clap_plugin_t* pl, const clap_audio_port_configuration_request_t* r, uint32_t n) {
  uint32_t k = zelf(pl)->kanalenUit;
  return !zelf(pl)->actief.load() && capKan(r, n, &k);
}
bool capApply(const clap_plugin_t* pl, const clap_audio_port_configuration_request_t* r, uint32_t n) {
  Plugin* s = zelf(pl);
  uint32_t k = s->kanalenUit;
  if (s->actief.load() || !capKan(r, n, &k)) return false;
  s->kanalenUit = k;
  return true;
}
const clap_plugin_configurable_audio_ports_t CONFIGURABLE_AUDIO_PORTS = {capCan, capApply};

// --- note-ports: één ingang, alleen het CLAP-dialect (valkuil 19) ------------------------------------------------
uint32_t npCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
bool npGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
  if (!isInput || index != 0) return false;
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  std::strncpy(info->name, "Noten", sizeof info->name);
  info->name[sizeof info->name - 1] = 0;
  return true;
}
const clap_plugin_note_ports_t NOTE_PORTS = {npCount, npGet};

// --- params: id = het nummer in SPEC.md, waarden in hun eigen eenheid --------------------------------------------
uint32_t prCount(const clap_plugin_t*) { return NP; }
bool prInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)NP) return false;
  const auto& p = ALLE.p[index];
  std::memset(info, 0, sizeof *info);
  info->id = p.id;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  if ((int)index == BYPASS) info->flags |= CLAP_PARAM_IS_BYPASS | CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof info->name, "%s", p.naam);
  info->module[0] = 0;
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}
bool prValue(const clap_plugin_t* pl, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(pl)->spiegel[i].load(std::memory_order_relaxed);
  return true;
}

bool eenheidloos(int i) {
  const char* e = ALLE.p[i].eenheid;
  return !std::strcmp(e, "") || !std::strcmp(e, "×") || !std::strcmp(e, "%");
}

// v met `dec` decimalen, de komma als decimaalteken. Zelf gedaan in plaats van printf/strtod: die volgen de locale van
// de host (een Nederlandse Logic kan een komma verwachten, een andere een punt), en de tekst moet overal gelijk zijn.
void metDecimalen(double v, int dec, char* t, size_t grootte) {
  std::snprintf(t, grootte, "%.*f", dec, v);
  for (char* c = t; *c; ++c)
    if (*c == '.') *c = ',';
  if (t[0] == '-') {  // "-0" en "-0,0" na afronden zijn gewoon 0
    bool nul = true;
    for (const char* c = t + 1; *c; ++c)
      if (*c != '0' && *c != ',') nul = false;
    if (nul) std::memmove(t, t + 1, std::strlen(t));
  }
}

// de waarde van een tekst die metDecimalen schreef (komma of punt): wat afronden ervan overlaat
double alsGetal(const char* t) {
  double x = 0, schaal = 1;
  bool neg = *t == '-', na = false;
  for (const char* c = t + (neg ? 1 : 0); *c; ++c) {
    if (*c == ',' || *c == '.') na = true;
    else if (*c >= '0' && *c <= '9') {
      x = x * 10 + (*c - '0');
      if (na) schaal *= 10;
    }
  }
  return (neg ? -x : x) / schaal;
}

// SPEC.md "Tekst ↔ waarde": komma als decimaalteken, spatie voor de eenheid. De grens kiest op de afgeronde waarde,
// zodat 999,7 Hz "1,00 kHz" wordt en geen "1000 Hz".
bool prToText(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t grootte) {
  const int i = indexVan(id);
  if (i < 0 || !uit || grootte == 0 || !std::isfinite(v)) return false;
  if (i == BYPASS) {  // Bypass: "Aan" (omzeild) of "Uit"
    std::snprintf(uit, grootte, "%s", klem(v, i) >= 1.0 ? "Aan" : "Uit");
    return true;
  }
  const char* e = ALLE.p[i].eenheid;
  char g[48];
  const char* eenheid = e;
  if (eenheidloos(i)) {
    metDecimalen(v * 100.0, 0, g, sizeof g);
    eenheid = "%";
  } else if (!std::strcmp(e, "Hz")) {
    metDecimalen(v, 0, g, sizeof g);
    if (std::fabs(alsGetal(g)) >= 1000.0) {
      metDecimalen(v / 1000.0, 2, g, sizeof g);
      if (std::fabs(alsGetal(g)) >= 10.0) metDecimalen(v / 1000.0, 1, g, sizeof g);
      eenheid = "kHz";
    }
  } else if (!std::strcmp(e, "ms")) {
    metDecimalen(v, 1, g, sizeof g);
    if (std::fabs(alsGetal(g)) >= 100.0) metDecimalen(v, 0, g, sizeof g);
  } else metDecimalen(v, 2, g, sizeof g);
  std::snprintf(uit, grootte, "%s %s", g, eenheid);
  return true;
}

// Een getal met komma of punt, zonder locale (zie metDecimalen). Exact afgerond: mantisse (geheel, < 2^53) en een
// macht van tien zijn exact, en één deling of vermenigvuldiging rondt correct af, dus "0,38" geeft dezelfde double als
// 0.38 in de broncode. `macht` schuift de komma (−2 voor procenten, +3 voor kHz).
bool leesGetal(const char*& c, int macht, double* uit) {
  bool neg = false;
  if (*c == '+' || *c == '-') neg = *c++ == '-';
  uint64_t m = 0;
  int cijfers = 0, na = 0, decimalen = 0;
  bool punt = false;
  for (;; ++c) {
    if (*c >= '0' && *c <= '9') {
      if (m < (1ull << 53) / 10) {
        m = m * 10 + (uint64_t)(*c - '0');
        if (punt) ++decimalen;
      } else if (!punt) ++na;  // te veel cijfers voor exact: de rest telt als nullen
      ++cijfers;
    } else if ((*c == ',' || *c == '.') && !punt) punt = true;
    else break;
  }
  if (cijfers == 0) return false;
  const int exp = na - decimalen + macht;
  double x = (double)m;
  static const double MACHT[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
  if (exp > 22 || exp < -22) x = exp > 0 ? HUGE_VAL : 0.0;
  else x = exp >= 0 ? x * MACHT[exp] : x / MACHT[-exp];
  *uit = neg ? -x : x;
  return true;
}

bool gelijkZonderHoofdletters(const char* a, size_t n, const char* b) {
  if (std::strlen(b) != n) return false;
  for (size_t k = 0; k < n; ++k) {
    char x = a[k], y = b[k];
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

// SPEC.md: komma én punt, met of zonder eenheid, "k" voor kHz, procenten voor eenheidloos; buiten het bereik klemmen,
// onleesbaar = false
bool prFromText(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !tekst || !v) return false;
  const char* c = tekst;
  while (*c == ' ' || *c == '\t') ++c;
  // eerst de eenheid (achteraan, na het getal), dan weten we welke macht het getal krijgt
  const char* begin = c;
  while (*c == '+' || *c == '-' || *c == ',' || *c == '.' || (*c >= '0' && *c <= '9')) ++c;
  const char* rest = c;
  while (*rest == ' ' || *rest == '\t') ++rest;
  size_t n = std::strlen(rest);
  while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) --n;
  if (i == BYPASS && begin == rest) {  // Bypass ook als woord: aan/uit (en on/off)
    if (gelijkZonderHoofdletters(rest, n, "aan") || gelijkZonderHoofdletters(rest, n, "on")) *v = 1.0;
    else if (gelijkZonderHoofdletters(rest, n, "uit") || gelijkZonderHoofdletters(rest, n, "off")) *v = 0.0;
    else return false;
    return true;
  }
  const char* e = ALLE.p[i].eenheid;
  int macht = 0;
  if (i == BYPASS) {
    if (n != 0) return false;  // een getal: 0 of 1 (geklemd en afgerond)
  } else if (eenheidloos(i)) {
    if (!(n == 0 || gelijkZonderHoofdletters(rest, n, "%"))) return false;
    macht = -2;
  } else if (!std::strcmp(e, "Hz")) {
    if (gelijkZonderHoofdletters(rest, n, "k") || gelijkZonderHoofdletters(rest, n, "khz")) macht = 3;
    else if (!(n == 0 || gelijkZonderHoofdletters(rest, n, "hz"))) return false;
  } else if (!(n == 0 || gelijkZonderHoofdletters(rest, n, e))) return false;
  double x;
  const char* g = begin;
  if (!leesGetal(g, macht, &x) || g != c || !std::isfinite(x)) return false;
  *v = klem(x, i);
  return true;
}

void prFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t* out) {
  Plugin* s = zelf(pl);
  neemKlaarOver(s);
  neemPaneelOver(s, out);
  if (!in) return;
  const uint32_t n = in->size(in);
  for (uint32_t k = 0; k < n; ++k) pasToe(s, in->get(in, k));
}
const clap_plugin_params_t PARAMS_EXT = {prCount, prInfo, prValue, prToText, prFromText, prFlush};

// --- state: "PBST", versie (u32), aantal (u32), per parameter (id u32, waarde f64), little-endian -----------------
constexpr uint32_t STATE_VERSIE = 1;

void zet32(std::vector<unsigned char>& b, uint32_t v) {
  for (int k = 0; k < 4; ++k) b.push_back((unsigned char)(v >> (8 * k)));
}
uint32_t lees32(const unsigned char* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Nieuwe waarden voor alle parameters, Bypass ook (mainthread): state.load en preset-load gaan allebei hierlangs.
void laadWaarden(Plugin* s, const double* waarden) {
  if (s->actief.load()) {
    for (int i = 0; i < NP; ++i) {
      s->klaar[i].store(waarden[i], std::memory_order_relaxed);
      s->spiegel[i].store(waarden[i], std::memory_order_relaxed);
    }
    s->klaarVlag.store(true, std::memory_order_release);
  } else {
    // niet actief: alleen de mainthread raakt het Ontwerp aan
    for (int i = 0; i < NP; ++i) zetWaarde(s, i, waarden[i]);
  }
  if (s->hostParams) {
    s->hostParams->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
    if (s->actief.load()) s->hostParams->request_flush(s->host);
  }
}

bool stSave(const clap_plugin_t* pl, const clap_ostream_t* os) {
  Plugin* s = zelf(pl);
  std::vector<unsigned char> b;
  b.insert(b.end(), {'P', 'B', 'S', 'T'});
  zet32(b, STATE_VERSIE);
  zet32(b, (uint32_t)NP);
  for (int i = 0; i < NP; ++i) {
    zet32(b, ALLE.p[i].id);
    const double v = s->spiegel[i].load(std::memory_order_relaxed);
    uint64_t u;
    std::memcpy(&u, &v, 8);
    zet32(b, (uint32_t)u);
    zet32(b, (uint32_t)(u >> 32));
  }
  size_t pos = 0;
  while (pos < b.size()) {
    const int64_t w = os->write(os, b.data() + pos, b.size() - pos);
    if (w <= 0) return false;
    pos += (size_t)w;
  }
  return true;
}

bool stLoad(const clap_plugin_t* pl, const clap_istream_t* is) {
  Plugin* s = zelf(pl);
  std::vector<unsigned char> b;
  unsigned char buf[4096];
  for (;;) {
    const int64_t r = is->read(is, buf, sizeof buf);
    if (r < 0) return false;
    if (r == 0) break;
    b.insert(b.end(), buf, buf + r);
    if (b.size() > (1u << 20)) return false;  // een state van een parameterlijst is nooit zo groot
  }
  if (b.size() < 12 || std::memcmp(b.data(), "PBST", 4)) return false;
  const uint32_t versie = lees32(&b[4]), aantal = lees32(&b[8]);
  if (versie == 0) return false;
  if ((uint64_t)aantal * 12 > b.size() - 12) return false;  // korter dan het aantal belooft
  // per id: een onbekend id overslaan, een ontbrekend id houdt zijn standaard
  double waarden[NP];
  for (int i = 0; i < NP; ++i) waarden[i] = ALLE.p[i].standaard;
  for (uint32_t k = 0; k < aantal; ++k) {
    const unsigned char* p = &b[12 + 12 * (size_t)k];
    const int i = indexVan(lees32(p));
    if (i < 0) continue;
    const uint64_t u = (uint64_t)lees32(p + 4) | ((uint64_t)lees32(p + 8) << 32);
    double v;
    std::memcpy(&v, &u, 8);
    if (std::isfinite(v)) waarden[i] = klem(v, i);
  }
  laadWaarden(s, waarden);
  return true;
}
const clap_plugin_state_t STATE = {stSave, stLoad};

// --- fabriekspresets (fase 4): presets/*.json, door CMake als tabel in het binary (presets.cmake) -----------------
// Geen JSON-lezer en niets naast de plugin. Alles hier is constant (geen globale objecten die init vult; valkuil 16).
struct PresetWaarde {
  const char* naam;  // de parameternaam uit PARAMS
  double waarde;
};
struct FabrieksPreset {
  const char* sleutel;     // de bestandsnaam zonder .json: de load key
  const char* naam;
  const char* uitleg;
  const char* aanbieder;   // het id van de preset-discovery-provider van deze preset
  const PresetWaarde* waarden;
  int aantal;
};
#include "fabriekspresets.inc"
constexpr int N_PRESETS = (int)(sizeof FABRIEKSPRESETS / sizeof FABRIEKSPRESETS[0]);

constexpr bool zelfdeTekst(const char* a, const char* b) {
  while (*a && *a == *b) ++a, ++b;
  return *a == *b;
}
constexpr int indexVanNaam(const char* naam) {
  for (int i = 0; i < N; ++i)
    if (zelfdeTekst(Ontwerp::PARAMS[i].naam, naam)) return i;
  return -1;
}
// Een onbekende naam, een dubbele naam of een waarde buiten het bereik is een bouwfout (ARCHITECTUUR.md "Presets").
// Exact in float32, zoals de grenzen en standaarden (SPEC.md): een AU geeft parameters als float door.
constexpr bool presetsKloppen() {
  for (int p = 0; p < N_PRESETS; ++p) {
    const FabrieksPreset& f = FABRIEKSPRESETS[p];
    for (int k = 0; k < f.aantal; ++k) {
      const int i = indexVanNaam(f.waarden[k].naam);
      if (i < 0) return false;
      const double v = f.waarden[k].waarde;
      if (!(v >= Ontwerp::PARAMS[i].min && v <= Ontwerp::PARAMS[i].max)) return false;
      if ((double)(float)v != v) return false;
      for (int j = 0; j < k; ++j)
        if (zelfdeTekst(f.waarden[j].naam, f.waarden[k].naam)) return false;
    }
    for (int q = 0; q < p; ++q)
      if (zelfdeTekst(FABRIEKSPRESETS[q].sleutel, f.sleutel)) return false;
  }
  return true;
}
static_assert(presetsKloppen(), "presets/*.json: een onbekende parameternaam, een dubbele naam, of een waarde buiten "
                                "het bereik (of niet exact in float32)");

int presetVan(const char* sleutel) {
  if (!sleutel) return -1;
  for (int p = 0; p < N_PRESETS; ++p)
    if (!std::strcmp(FABRIEKSPRESETS[p].sleutel, sleutel)) return p;
  return -1;
}

// preset-load (mainthread): hetzelfde pad als state.load; wat de preset niet noemt gaat naar zijn standaard
bool plLaadPreset(const clap_plugin_t* pl, uint32_t soort, const char* plek, const char* sleutel) {
  Plugin* s = zelf(pl);
  const int p = soort == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN ? presetVan(sleutel) : -1;
  if (p < 0) {
    if (s->hostPresetLoad && s->hostPresetLoad->on_error)
      s->hostPresetLoad->on_error(s->host, soort, plek, sleutel, 0, "PB Toetsenpad: onbekende preset");
    return false;
  }
  double waarden[NP];  // Bypass ook naar zijn standaard, zoals elke parameter die de preset niet noemt
  for (int i = 0; i < NP; ++i) waarden[i] = ALLE.p[i].standaard;
  const FabrieksPreset& f = FABRIEKSPRESETS[p];
  for (int k = 0; k < f.aantal; ++k) waarden[indexVanNaam(f.waarden[k].naam)] = f.waarden[k].waarde;
  laadWaarden(s, waarden);
  if (s->hostPresetLoad && s->hostPresetLoad->loaded) s->hostPresetLoad->loaded(s->host, soort, plek, sleutel);
  return true;
}
const clap_plugin_preset_load_t PRESET_LOAD = {plLaadPreset};

// --- latency en tail ---------------------------------------------------------------------------------------------
uint32_t latGet(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY = {latGet};

uint32_t tailGet(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  const double t = s->staart.load(std::memory_order_relaxed);
  if (t < 0) return INT32_MAX;  // oneindig (lusversterking ≥ 1)
  const double n = std::ceil(t * s->sr);
  return n >= (double)INT32_MAX ? INT32_MAX : (uint32_t)n;
}
const clap_plugin_tail_t TAIL = {tailGet};

// --- plugin ------------------------------------------------------------------------------------------------------
bool plInit(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->hostParams = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
  s->hostPresetLoad =
      static_cast<const clap_host_preset_load_t*>(s->host->get_extension(s->host, CLAP_EXT_PRESET_LOAD));
  if (!s->hostPresetLoad)
    s->hostPresetLoad =
        static_cast<const clap_host_preset_load_t*>(s->host->get_extension(s->host, CLAP_EXT_PRESET_LOAD_COMPAT));
  return true;
}

void plDestroy(const clap_plugin_t* p) {
#ifdef __APPLE__
  if (zelf(p)->venster) pbVensterWeg(zelf(p)->venster);  // de host hoort gui.destroy eerst te roepen
#endif
  delete zelf(p);
}

bool plActivate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t maxFrames) {
  Plugin* s = zelf(p);
  const unsigned long nodig = Ontwerp::geheugenNodig(sr);
  if (s->geheugen.size() < nodig) s->geheugen.assign(nodig, 0);
  pb::Geheugen g;
  g.p = s->geheugen.data();
  g.over = (unsigned long)s->geheugen.size();
  neemKlaarOver(s);
  s->ontwerp.activeer(sr, minFrames, g);
  s->stapel.leeg();
  s->stilLengte = maxFrames > 0 ? maxFrames : 4096;
  s->stil.assign((size_t)s->stilLengte * KANALEN, 0.0f);
  s->sr = sr;
  s->bypassStap = 1.0 / (OVERVLOEI_SEC * sr);
  s->bypassVers = true;
  s->staart.store(s->ontwerp.staartSec());
  s->actief.store(true);
  return true;
}

void plDeactivate(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->actief.store(false);
  neemKlaarOver(s);  // een load die process niet meer zag
}

bool plStartProcessing(const clap_plugin_t*) { return true; }
void plStopProcessing(const clap_plugin_t*) {}
void plReset(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->ontwerp.reset();
  s->stapel.leeg();
  s->bypassVers = true;
}

// Bypass over [van, tot): de DSP rekende al (hij rekent altijd door), hier vloeit zijn uitvoer per sample over naar
// de droge invoer, en die mix gaat door pb::uitvoer. Een instrument heeft geen ingang: droog is stilte. Zonder bypass
// (mix 0, doel 0) blijft de uitvoer van de DSP bit voor bit staan.
void bypass(Plugin* s, float* const* uit, uint32_t kanalen, uint32_t van, uint32_t tot) {
  if (s->bypassVers) {  // na activate/reset: meteen op het doel (zoals de smoothers)
    s->bypassMix = s->bypassDoel;
    s->bypassVers = false;
  }
  if (s->bypassMix == 0.0 && s->bypassDoel == 0.0) return;
  constexpr double DROOG = 0.0;
  for (uint32_t i = van; i < tot; ++i) {
    if (s->bypassMix < s->bypassDoel) {
      s->bypassMix += s->bypassStap;
      if (s->bypassMix > s->bypassDoel) s->bypassMix = s->bypassDoel;
    } else if (s->bypassMix > s->bypassDoel) {
      s->bypassMix -= s->bypassStap;
      if (s->bypassMix < s->bypassDoel) s->bypassMix = s->bypassDoel;
    }
    const double g = s->bypassMix;
    for (uint32_t c = 0; c < kanalen; ++c) uit[c][i] = pb::uitvoer((1.0 - g) * (double)uit[c][i] + g * DROOG);
  }
}

// verwerk een stuk [van, tot) van het blok, in stukken van hooguit de stille buffer
void render(Plugin* s, float* const* uit, uint32_t kanalen, uint32_t van, uint32_t tot) {
  const float* in[KANALEN] = {s->stil.data(), s->stil.data() + s->stilLengte};
  const uint32_t begin = van;
  while (van < tot) {
    uint32_t n = tot - van;
    if (n > s->stilLengte) n = s->stilLengte;
    float* u[KANALEN];
    for (uint32_t c = 0; c < kanalen; ++c) u[c] = uit[c] + van;
    s->ontwerp.verwerk(in, u, (int)kanalen, n);
    van += n;
  }
  bypass(s, uit, kanalen, begin, tot);
}

clap_process_status plProcess(const clap_plugin_t* p, const clap_process_t* pr) {
  Plugin* s = zelf(p);
#ifdef PB_KANARIE
  pbKanarie = std::malloc(64);
#endif
  neemKlaarOver(s);
  neemPaneelOver(s, pr->out_events);

  // transport van de host: tempo en plek in de maat (alleen een sequencer leest het)
  if (pr->transport) {
    const clap_event_transport_t* t = pr->transport;
    const double bpm = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) ? t->tempo : 0.0;
    const double tel = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
                           ? (double)t->song_pos_beats / (double)CLAP_BEATTIME_FACTOR
                           : 0.0;
    s->ontwerp.zetTransport(bpm, tel, (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) &&
                                          (t->flags & CLAP_TRANSPORT_IS_PLAYING));
  }

  const uint32_t frames = pr->frames_count;
  float* leeg[KANALEN] = {nullptr, nullptr};
  float* const* uit = leeg;
  uint32_t kanalen = 0;
  if (pr->audio_outputs_count > 0 && pr->audio_outputs[0].data32) {
    uit = pr->audio_outputs[0].data32;
    kanalen = pr->audio_outputs[0].channel_count;
    if (kanalen > KANALEN) kanalen = KANALEN;
  }

  // events op hun sample: tot elk event renderen, dan het event toepassen
  const clap_input_events_t* in = pr->in_events;
  const uint32_t nEv = in ? in->size(in) : 0;
  uint32_t pos = 0;
  for (uint32_t k = 0; k < nEv; ++k) {
    const clap_event_header_t* h = in->get(in, k);
    uint32_t t = h->time < frames ? h->time : frames;
    if (t < pos) t = pos;
    if (t > pos && kanalen) render(s, uit, kanalen, pos, t);
    if (t > pos) pos = t;
    pasToe(s, h);
  }
  if (frames > pos && kanalen) render(s, uit, kanalen, pos, frames);

  // overige uitgangen (die er niet horen te zijn) stil
  for (uint32_t b = 0; b < pr->audio_outputs_count; ++b) {
    const clap_audio_buffer_t& ub = pr->audio_outputs[b];
    for (uint32_t c = (b == 0 ? kanalen : 0); c < ub.channel_count; ++c)
      if (ub.data32 && ub.data32[c]) std::memset(ub.data32[c], 0, sizeof(float) * frames);
    pr->audio_outputs[b].constant_mask = 0;
  }

  s->staart.store(s->ontwerp.staartSec(), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

// --- het paneel: de koppeling (mainthread) -------------------------------------------------------------------------
double kpWaarde(void* c, int i) {
  if (i < 0 || i >= N) return 0;
  return static_cast<Plugin*>(c)->spiegel[i].load(std::memory_order_relaxed);
}

// wat in de wachtrij staat de rij in, in volgorde; true als er iets bij kwam (dan request_flush)
bool schuifWachtrij(Plugin* s) {
  size_t k = 0;
  while (k < s->wachtrij.size() && s->rij.zet(s->wachtrij[k])) ++k;
  s->wachtrij.erase(s->wachtrij.begin(), s->wachtrij.begin() + (std::ptrdiff_t)k);
  return k > 0;
}

void kpStuur(void* c, const pb::paneel::Wijziging& w) {
  Plugin* s = static_cast<Plugin*>(c);
  s->wachtrij.push_back(w);
  if (schuifWachtrij(s) && s->hostParams) s->hostParams->request_flush(s->host);
}

void kpTik(void* c) {
  Plugin* s = static_cast<Plugin*>(c);
  if (!s->wachtrij.empty() && schuifWachtrij(s) && s->hostParams) s->hostParams->request_flush(s->host);
}

bool kpTekst(void* c, int i, double v, char* uit, uint32_t grootte) {
  if (i < 0 || i >= N) return false;
  return prToText(&static_cast<Plugin*>(c)->clap, Ontwerp::PARAMS[i].id, v, uit, grootte);
}

pb::paneel::Koppeling koppelingVan(Plugin* s) {
  pb::paneel::Koppeling k;
  k.ctx = s;
  k.waarde = kpWaarde;
  k.stuur = kpStuur;
  k.tik = kpTik;
  k.tekst = kpTekst;
  return k;
}

#ifdef __APPLE__
// --- gui: alleen Cocoa, ingebed (clap-wrapper geeft het door aan VST3 en AUv2) ----------------------------------
bool guiApi(const clap_plugin_t*, const char* api, bool zwevend) {
  return !zwevend && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA);
}
bool guiVoorkeur(const clap_plugin_t*, const char** api, bool* zwevend) {
  *api = CLAP_WINDOW_API_COCOA;
  *zwevend = false;
  return true;
}
bool guiMaak(const clap_plugin_t* p, const char* api, bool zwevend) {
  Plugin* s = zelf(p);
  if (!guiApi(p, api, zwevend) || s->venster) return false;
  s->venster = pbVensterMaak(koppelingVan(s), pbPaneelKnoppen(), pbPaneelNaam(), s->guiB, s->guiH);
  return s->venster != nullptr;
}
void guiWeg(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  if (s->venster) pbVensterWeg(s->venster);
  s->venster = nullptr;
}
bool guiSchaal(const clap_plugin_t*, double) { return false; }  // Cocoa rekent in punten
bool guiMaat(const clap_plugin_t* p, uint32_t* b, uint32_t* h) {
  *b = zelf(p)->guiB;
  *h = zelf(p)->guiH;
  return true;
}
bool guiKanSchalen(const clap_plugin_t*) { return true; }
bool guiHints(const clap_plugin_t*, clap_gui_resize_hints_t* r) {
  r->can_resize_horizontally = true;
  r->can_resize_vertically = true;
  r->preserve_aspect_ratio = false;  // het ontwerp schaalt gelijk mee en staat in het midden
  r->aspect_ratio_width = 0;
  r->aspect_ratio_height = 0;
  return true;
}
bool guiPasAan(const clap_plugin_t*, uint32_t* b, uint32_t* h) {  // design.md: minimaal 560 × 220
  if (*b < (uint32_t)pb::paneel::ONTWERP_B) *b = (uint32_t)pb::paneel::ONTWERP_B;
  if (*h < (uint32_t)pb::paneel::ONTWERP_H) *h = (uint32_t)pb::paneel::ONTWERP_H;
  return true;
}
bool guiZetMaat(const clap_plugin_t* p, uint32_t b, uint32_t h) {
  Plugin* s = zelf(p);
  if (b == 0 || h == 0) return false;
  s->guiB = b;
  s->guiH = h;
  if (s->venster) pbVensterMaat(s->venster, b, h);
  return true;
}
bool guiOuder(const clap_plugin_t* p, const clap_window_t* w) {
  Plugin* s = zelf(p);
  if (!s->venster || !w || !w->api || std::strcmp(w->api, CLAP_WINDOW_API_COCOA) || !w->cocoa) return false;
  return pbVensterOuder(s->venster, w->cocoa);
}
bool guiTijdelijk(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiTitel(const clap_plugin_t*, const char*) {}
bool guiToon(const clap_plugin_t* p) {
  if (!zelf(p)->venster) return false;
  pbVensterToon(zelf(p)->venster, true);
  return true;
}
bool guiVerberg(const clap_plugin_t* p) {
  if (!zelf(p)->venster) return false;
  pbVensterToon(zelf(p)->venster, false);
  return true;
}
const clap_plugin_gui_t GUI = {guiApi,    guiVoorkeur, guiMaak,   guiWeg,      guiSchaal,    guiMaat,  guiKanSchalen,
                               guiHints,  guiPasAan,   guiZetMaat, guiOuder,   guiTijdelijk, guiTitel, guiToon,
                               guiVerberg};
#endif

const void* plGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG)) return &AUDIO_PORTS_CONFIG;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) || !std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT))
    return &AUDIO_PORTS_CONFIG_INFO;
  if (!std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS) || !std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS_COMPAT))
    return &CONFIGURABLE_AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD) || !std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &PRESET_LOAD;
#ifdef __APPLE__
  if (!std::strcmp(id, CLAP_EXT_GUI)) return &GUI;
#endif
  return nullptr;
}

void plOnMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  Plugin* s = new (std::nothrow) Plugin();
  if (!s) return nullptr;
  s->host = host;
  s->clap = {&DESCRIPTOR, s,           plInit,    plDestroy,      plActivate,     plDeactivate, plStartProcessing,
             plStopProcessing, plReset, plProcess, plGetExtension, plOnMainThread};
  return &s->clap;
}

// --- factory -----------------------------------------------------------------------------------------------------
uint32_t fabCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* fabDescriptor(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* fabCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id)) return nullptr;
  return maakPlugin(host);
}
const clap_plugin_factory_t FACTORY = {fabCount, fabDescriptor, fabCreate};

// --- preset-discovery: één provider per preset, elk met een container van één (valkuil 15) -----------------------
// clap-validator 0.4.1 loopt vast op een container met meer presets. Elke provider meldt de plek van soort
// CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN (de preset zit in de plugin zelf) en daar één preset met zijn sleutel als load
// key. De AU-wrapper (patches/clap-wrapper-auv2-presets.patch) leest dezelfde factory voor zijn factory presets.
struct PresetAanbieders {
  clap_preset_discovery_provider_descriptor_t d[N_PRESETS];
};
constexpr PresetAanbieders maakAanbieders() {
  PresetAanbieders a{};
  for (int p = 0; p < N_PRESETS; ++p)
    a.d[p] = {CLAP_VERSION_INIT, FABRIEKSPRESETS[p].aanbieder, FABRIEKSPRESETS[p].naam, Ontwerp::META.maker};
  return a;
}
constexpr PresetAanbieders AANBIEDERS = maakAanbieders();

struct Aanbieder {
  clap_preset_discovery_provider_t clap{};
  const clap_preset_discovery_indexer_t* indexer = nullptr;
  int preset = 0;
};
Aanbieder* aanbieder(const clap_preset_discovery_provider_t* p) { return static_cast<Aanbieder*>(p->provider_data); }

bool aanInit(const clap_preset_discovery_provider_t* p) {
  const Aanbieder* a = aanbieder(p);
  const clap_preset_discovery_location_t plek = {CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, Ontwerp::META.naam,
                                                 CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
  return a->indexer->declare_location(a->indexer, &plek);
}
void aanWeg(const clap_preset_discovery_provider_t* p) { delete aanbieder(p); }
bool aanMetadata(const clap_preset_discovery_provider_t* p, uint32_t soort, const char* plek,
                 const clap_preset_discovery_metadata_receiver_t* r) {
  if (soort != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || plek != nullptr) return false;
  const FabrieksPreset& f = FABRIEKSPRESETS[aanbieder(p)->preset];
  if (!r->begin_preset(r, f.naam, f.sleutel)) return true;
  const clap_universal_plugin_id_t id = {"clap", Ontwerp::META.clapId};
  r->add_plugin_id(r, &id);
  r->set_flags(r, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
  r->add_creator(r, Ontwerp::META.maker);
  r->set_description(r, f.uitleg);
  return true;
}
const void* aanExtensie(const clap_preset_discovery_provider_t*, const char*) { return nullptr; }

uint32_t pdCount(const clap_preset_discovery_factory_t*) { return (uint32_t)N_PRESETS; }
const clap_preset_discovery_provider_descriptor_t* pdDescriptor(const clap_preset_discovery_factory_t*, uint32_t i) {
  return i < (uint32_t)N_PRESETS ? &AANBIEDERS.d[i] : nullptr;
}
const clap_preset_discovery_provider_t* pdCreate(const clap_preset_discovery_factory_t*,
                                                 const clap_preset_discovery_indexer_t* indexer, const char* id) {
  if (!indexer || !id) return nullptr;
  for (int p = 0; p < N_PRESETS; ++p) {
    if (std::strcmp(AANBIEDERS.d[p].id, id)) continue;
    Aanbieder* a = new (std::nothrow) Aanbieder();
    if (!a) return nullptr;
    a->indexer = indexer;
    a->preset = p;
    a->clap = {&AANBIEDERS.d[p], a, aanInit, aanWeg, aanMetadata, aanExtensie};
    return &a->clap;
  }
  return nullptr;
}
const clap_preset_discovery_factory_t PRESET_FACTORY = {pdCount, pdDescriptor, pdCreate};

}  // namespace

bool pbPaneelKoppeling(const clap_plugin_t* plugin, pb::paneel::Koppeling* k) {
  if (!plugin || !k || plugin->desc != &DESCRIPTOR || !plugin->plugin_data) return false;
  *k = koppelingVan(zelf(plugin));
  return true;
}

std::vector<pb::paneel::Knop> pbPaneelKnoppen() {
  std::vector<pb::paneel::Knop> k;
  for (int i = 0; i < N; ++i) {
    const auto& p = Ontwerp::PARAMS[i];
    k.push_back({p.naam, p.min, p.max, p.standaard});
  }
  return k;
}

std::string pbPaneelNaam() { return Ontwerp::META.naam; }

// Geen globale objecten die init vult (valkuil 16): de AU roept init al tijdens de statische initialisatie aan.
bool pbEntryInit(const char*) { return true; }
void pbEntryDeinit() {}
const void* pbEntryGetFactory(const char* factoryId) {
  if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) return &FACTORY;
  if (!std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) ||
      !std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT))
    return &PRESET_FACTORY;
  return nullptr;
}
