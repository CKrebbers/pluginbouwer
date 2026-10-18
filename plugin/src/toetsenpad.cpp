// toetsenpad.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_toetsenpad.h.
//
// Een instrument zonder audio-ingang, één stereo-uitgang en één note port (alleen het CLAP-dialect, valkuil 19).
// Fase 1: het Ontwerp draait in process. Parameters zijn CLAP-params (id = nummer in SPEC.md, bereik en standaard uit
// PARAMS); events (parameters en noten) gaan op hun sample: het blok wordt bij elk event gesplitst. Noten volgen
// ARCHITECTUUR.md "Noten" (pb::NootStapel uit dsp/pb_noten.h). State zoals ARCHITECTUUR.md "State".
// Fase 2: tekst ↔ waarde volgens SPEC.md (komma, geen locale), params.flush buiten process. Getoetst door
// plugin/toets/statetoets.cpp (pb-statetoets) door het gebouwde .clap, en door clap-validator.
// Fase 3: het frontpaneel (paneel.h; op macOS de CLAP gui-extensie, Cocoa, paneel_mac.mm). Het paneel raakt het Ontwerp
// nooit aan (valkuil 14): een knop zet BEGIN/WAARDE/EIND in een lock-vrije rij en vraagt request_flush; process of
// params.flush stuurt ze als GESTURE_BEGIN/PARAM_VALUE/GESTURE_END naar de host en past de waarde zelf toe, zoals een
// event van de host. Het paneel leest alleen de atomaire spiegel.
// Fase 4: fabriekspresets uit presets/*.json (CMake bakt ze in als presets_tabel.h; ARCHITECTUUR.md "Presets"). De
// preset-discovery-factory heeft per preset een eigen provider met één locatie van soort PLUGIN en een container van
// één preset (valkuil 15); preset-load laadt hem langs hetzelfde pad als state.load (ontbrekende namen naar hun
// standaard), daarna rescan en loaded. De AU leest dezelfde factory (patches/clap-wrapper-auv2-presets.patch).
// Fase 5: stereo- én mono-uitgang (0 → 2 en 0 → 1; een instrument heeft geen ingang). CLAP kiest via
// audio-ports-config (plus audio-ports-config-info), de VST3 van clap-wrapper via configurable-audio-ports
// (setBusArrangements), de AU via patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch (valkuil 17). Mono is het
// linkerkanaal van de keten (verwerk met 1 kanaal). Bypass is een eigen CLAP-parameter (id BYPASS_ID, vlag
// CLAP_PARAM_IS_BYPASS): de DSP rekent door, de uitgang vloeit in 10 ms lineair over naar de droge invoer (stilte bij
// een instrument), en ook die menging gaat door pb::uitvoer (geen subnormale float naar de host).
//
// Draden (ARCHITECTUUR.md "Threads", valkuil 11): init, activate, state en tekst draaien op de mainthread; process
// alleen op de audiothread. Zolang de plugin actief is raakt alleen de audiothread het Ontwerp aan; de mainthread leest
// een atomaire spiegel per parameter (get_value, state.save) en state.load zet waarden klaar die process of flush
// overneemt. In process geen allocatie, locks of syscalls (RTSan bewaakt het in CI).
#include <clap/clap.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "ontwerpen/keten_toetsenpad.h"
#include "paneel.h"
#include "paneel_toets.h"
#include "pb_noten.h"
#include "presets_tabel.h"
#include "toetsenpad_entry.h"
#ifdef __APPLE__
#include "paneel_mac.h"
#endif

#ifdef PB_KANARIE
// Alleen in de kanarie-build van de RTSan-job: een malloc in process die RTSan MOET vangen (valkuil 4). De pointer
// gaat naar een globale variabele, zodat de compiler de malloc niet weg kan laten.
void* volatile pbKanarieGeheugen = nullptr;
#endif

namespace {

using pb::Ontwerp;

constexpr uint32_t KANALEN = 2;
constexpr int N = Ontwerp::N_PARAMS;
constexpr int NOOT = PB_NOOT_PARAM;    // index in PARAMS, geen id
constexpr int POORT = PB_POORT_PARAM;  // index in PARAMS, geen id

// Bypass: geen parameter van het Ontwerp maar van de schil. Zijn id ligt ver buiten de nummers van SPEC.md (1, 2, …),
// zodat een nieuwe parameter van het ontwerp er nooit mee botst; nooit hergebruiken of hernummeren.
constexpr clap_id BYPASS_ID = 1000;
constexpr int BYPASS = N;               // index in de CLAP-parameters (na de N van het Ontwerp)
constexpr int NP = N + 1;               // alle CLAP-parameters
constexpr double OVERVLOEI_SEC = 0.010;  // bypass aan/uit: 10 ms overvloeien

// state (ARCHITECTUUR.md "State"): "PBST", versie, aantal, dan per parameter (id u32, waarde f64), little-endian
constexpr uint32_t STATE_VERSIE = 1;

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                CLAP_PLUGIN_FEATURE_STEREO, CLAP_PLUGIN_FEATURE_MONO, nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT, Ontwerp::META.clapId, Ontwerp::META.naam, Ontwerp::META.maker, Ontwerp::META.url, "", "",
    Ontwerp::META.versie, Ontwerp::META.uitleg, FEATURES};

// --- de rij van het paneel naar process/flush (één schrijver: de mainthread; één lezer: process of flush) -------------
struct PaneelEvent {
  uint8_t gebaar;  // pb_paneel::Gebaar
  uint8_t knop;    // index in PARAMS
  double waarde;
};

class PaneelRij {
 public:
  static constexpr uint32_t GROOTTE = 1024;
  uint32_t vrij() const {
    return GROOTTE - (kop_.load(std::memory_order_relaxed) - staart_.load(std::memory_order_acquire));
  }
  bool zet(const PaneelEvent& e) {  // mainthread
    const uint32_t k = kop_.load(std::memory_order_relaxed);
    if (k - staart_.load(std::memory_order_acquire) >= GROOTTE) return false;
    e_[k % GROOTTE] = e;
    kop_.store(k + 1, std::memory_order_release);
    return true;
  }
  bool pak(PaneelEvent& e) {  // process of flush; lock-vrij, geen allocatie
    const uint32_t s = staart_.load(std::memory_order_relaxed);
    if (s == kop_.load(std::memory_order_acquire)) return false;
    e = e_[s % GROOTTE];
    staart_.store(s + 1, std::memory_order_release);
    return true;
  }

 private:
  PaneelEvent e_[GROOTTE];
  std::atomic<uint32_t> kop_{0}, staart_{0};
};

struct Toetsenpad;

// wat het paneel van de plugin mag: de spiegel lezen, tekst maken, en gebaren in de rij zetten (mainthread)
struct PaneelBron final : pb_paneel::PaneelBron {
  Toetsenpad* t = nullptr;
  bool gebaarInRij[pb_paneel::MAX_KNOPPEN] = {};  // BEGIN kwam in de rij (anders ook geen WAARDE en EIND)
  double waarde(int k) override;
  bool uitstaand(int k) override;
  void tekst(int k, double v, char* uit, uint32_t n) override;
  void stuur(pb_paneel::Gebaar g, int k, double v) override;
};

struct Toetsenpad {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;
  const clap_host_preset_load_t* hostPresetLoad = nullptr;
  Ontwerp ontwerp;
  pb::NootStapel stapel;                // alleen de audiothread (of de mainthread als de plugin niet actief is)
  std::vector<unsigned char> geheugen;  // het blok voor het Ontwerp, gereserveerd in activate (mainthread)
  std::vector<float> stil[KANALEN];     // de stille invoer voor verwerk (een instrument leest hem niet)
  double sr = 48000;
  bool actief = false;                  // alleen de mainthread leest en schrijft dit
  uint32_t kanalen = KANALEN;           // de gekozen uitgang: 2 (stereo) of 1 (mono); alleen te kiezen als niet actief

  // bypass (audiothread, of mainthread als niet actief): de overvloeiing loopt in hele samples van 0 (nat) tot
  // overvloeiLengte (droog), één stap per sample richting het doel
  bool bypassDoel = false;
  uint32_t overvloeiPos = 0, overvloeiLengte = 480;

  std::atomic<double> spiegel[NP];      // wat de host ziet (get_value, state.save); de audiothread werkt hem bij
  std::atomic<bool> stateKlaar{false};  // state.load terwijl actief: process/flush neemt de spiegel over
  std::atomic<uint32_t> staart{0};      // in samples, uitgerekend door de audiothread (tail.get leest het)

  // het paneel (mainthread); de rij en de tellers zijn de enige brug naar process/flush
  PaneelRij rij;
  std::atomic<int> uitstaand[N];        // WAARDE-events van het paneel die process/flush nog niet toepaste
  PaneelBron bron;
  std::unique_ptr<pb_paneel::Paneel> paneel;
  double paneelB = pb_paneel::Paneel::BEGIN_B, paneelH = pb_paneel::Paneel::BEGIN_H;
  void* view = nullptr;                 // macOS: de NSView (paneel_mac.mm)

  Toetsenpad() {
    for (int i = 0; i < N; ++i) spiegel[i].store(Ontwerp::PARAMS[i].standaard);
    spiegel[BYPASS].store(0.0);
    for (auto& u : uitstaand) u.store(0);
    bron.t = this;
  }
};

Toetsenpad* zelf(const clap_plugin_t* p) { return static_cast<Toetsenpad*>(p->plugin_data); }

// index in de CLAP-parameters: 0..N−1 de parameters van het Ontwerp (index in PARAMS), N de bypass
int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id) return i;
  return id == BYPASS_ID ? BYPASS : -1;
}

double klem(int i, double v) {
  if (i == BYPASS) return !(v == v) ? 0.0 : (v < 0 ? 0.0 : (v > 1 ? 1.0 : v));  // aan vanaf 0,5
  const auto& p = Ontwerp::PARAMS[i];
  if (!(v == v)) return p.standaard;  // NaN
  return v < p.min ? p.min : (v > p.max ? p.max : v);
}

// staart van het Ontwerp in samples (−1 = oneindig → INT32_MAX, CLAP: oneindig)
uint32_t staartSamples(const Toetsenpad* t) {
  const double s = t->ontwerp.staartSec();
  if (s < 0) return (uint32_t)INT32_MAX;
  const double n = s * t->sr;
  if (n >= (double)INT32_MAX) return (uint32_t)INT32_MAX;
  const uint32_t g = (uint32_t)n;
  return (double)g < n ? g + 1 : g;
}

// --- noten (ARCHITECTUUR.md "Noten") -----------------------------------------------------------------------------
// stap 2: de bovenste noot wordt Noot (verhouding) en Poort 1; geen noot: Poort 0. De spiegel blijft wat hij was.
void naNoot(Toetsenpad* t) {
  const int n = t->stapel.boven();
  if (n >= 0) {
    t->ontwerp.zetParam(NOOT, pb::nootVerhouding(n));
    t->ontwerp.zetParam(POORT, 1.0);
  } else {
    t->ontwerp.zetParam(POORT, 0.0);
  }
}

void nootAan(Toetsenpad* t, int key) {
  if (key < 0 || key > 127) return;  // buiten 0..127: negeren, geen stap 2
  t->stapel.aan(key);
  naNoot(t);
}

void nootUit(Toetsenpad* t, int key) {
  if (key < 0 || key > 127) return;
  t->stapel.uit(key);
  naNoot(t);
}

void parameter(Toetsenpad* t, clap_id id, double v) {
  const int i = indexVan(id);
  if (i < 0) return;
  v = klem(i, v);
  if (i == BYPASS) t->bypassDoel = v >= 0.5;  // de DSP rekent gewoon door; render vloeit over
  else t->ontwerp.zetParam(i, v);
  t->spiegel[i].store(v, std::memory_order_relaxed);
}

// één event uit process of flush (audiothread, of mainthread als de plugin niet actief is)
void doeEvent(Toetsenpad* t, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  switch (h->type) {
    case CLAP_EVENT_PARAM_VALUE: {
      const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
      parameter(t, e->param_id, e->value);
      break;
    }
    case CLAP_EVENT_NOTE_ON:
      nootAan(t, reinterpret_cast<const clap_event_note_t*>(h)->key);
      break;
    case CLAP_EVENT_NOTE_OFF:
      nootUit(t, reinterpret_cast<const clap_event_note_t*>(h)->key);
      break;
    case CLAP_EVENT_NOTE_CHOKE:
      t->stapel.leeg();
      naNoot(t);
      break;
    case CLAP_EVENT_MIDI: {
      const auto* e = reinterpret_cast<const clap_event_midi_t*>(h);
      const uint8_t status = e->data[0] & 0xF0, d1 = e->data[1], d2 = e->data[2];
      if (status == 0x90 && d2 > 0) nootAan(t, d1);
      else if (status == 0x80 || status == 0x90) nootUit(t, d1);
      else if (status == 0xB0 && (d1 == 120 || d1 == 123)) {
        t->stapel.leeg();
        naNoot(t);
      }
      break;
    }
    default:
      break;
  }
}

// state.load terwijl actief: de klaargezette waarden gaan vóór de events van dit blok het Ontwerp in
void neemStateOver(Toetsenpad* t) {
  if (!t->stateKlaar.exchange(false, std::memory_order_acquire)) return;
  for (int i = 0; i < N; ++i) t->ontwerp.zetParam(i, t->spiegel[i].load(std::memory_order_relaxed));
  t->bypassDoel = t->spiegel[BYPASS].load(std::memory_order_relaxed) >= 0.5;
}

// De gebaren van het paneel (valkuil 14): naar de host als events, en zelf toepassen zoals een event van de host.
// Audiothread (process, of flush als actief) of mainthread (flush als niet actief); lock-vrij, geen allocatie.
void paneelNaarHost(Toetsenpad* t, const clap_output_events_t* uit) {
  PaneelEvent e;
  while (t->rij.pak(e)) {
    const clap_id id = Ontwerp::PARAMS[e.knop].id;
    if (e.gebaar == pb_paneel::WAARDE) {
      parameter(t, id, e.waarde);
      clap_event_param_value_t ev{};
      ev.header.size = sizeof ev;
      ev.header.time = 0;
      ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      ev.header.type = CLAP_EVENT_PARAM_VALUE;
      ev.header.flags = 0;
      ev.param_id = id;
      ev.cookie = nullptr;
      ev.note_id = -1;
      ev.port_index = -1;
      ev.channel = -1;
      ev.key = -1;
      ev.value = t->spiegel[e.knop].load(std::memory_order_relaxed);
      if (uit) uit->try_push(uit, &ev.header);
      t->uitstaand[e.knop].fetch_sub(1, std::memory_order_release);
    } else {
      clap_event_param_gesture_t ev{};
      ev.header.size = sizeof ev;
      ev.header.time = 0;
      ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      ev.header.type = e.gebaar == pb_paneel::BEGIN ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
      ev.header.flags = 0;
      ev.param_id = id;
      if (uit) uit->try_push(uit, &ev.header);
    }
  }
}

// --- audio-ports: 0 ingangen, 1 uitgang, stereo of mono (SPEC.md "Instrument") ------------------------------------
void vulPoort(clap_audio_port_info_t* info, uint32_t kanalen) {
  std::memset(info, 0, sizeof(*info));
  info->id = 0;
  std::strncpy(info->name, "Uit", sizeof(info->name) - 1);
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = kanalen;
  info->port_type = kanalen == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
}
uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }
bool audioPortsGet(const clap_plugin_t* p, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (isInput || index != 0) return false;
  vulPoort(info, zelf(p)->kanalen);
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {audioPortsCount, audioPortsGet};

// --- audio-ports-config (CLAP en, via de patch, de AU): config 0 = 0 → 2 (stereo), config 1 = 0 → 1 (mono) ----------
constexpr clap_id CONFIG_STEREO = 0, CONFIG_MONO = 1;
uint32_t kanalenVan(clap_id config) { return config == CONFIG_MONO ? 1 : 2; }

uint32_t portsConfigCount(const clap_plugin_t*) { return 2; }
bool portsConfigGet(const clap_plugin_t*, uint32_t index, clap_audio_ports_config_t* c) {
  if (index > 1) return false;
  std::memset(c, 0, sizeof(*c));
  c->id = index == 0 ? CONFIG_STEREO : CONFIG_MONO;
  std::strncpy(c->name, index == 0 ? "Stereo" : "Mono", sizeof(c->name) - 1);
  c->input_port_count = 0;
  c->output_port_count = 1;
  c->has_main_input = false;
  c->main_input_channel_count = 0;
  c->main_input_port_type = nullptr;
  c->has_main_output = true;
  c->main_output_channel_count = kanalenVan(c->id);
  c->main_output_port_type = c->main_output_channel_count == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  return true;
}
// alleen als de plugin niet actief is (CLAP: [main-thread & !active])
bool portsConfigSelect(const clap_plugin_t* p, clap_id config) {
  auto* t = zelf(p);
  if (t->actief || (config != CONFIG_STEREO && config != CONFIG_MONO)) return false;
  t->kanalen = kanalenVan(config);
  return true;
}
const clap_plugin_audio_ports_config_t PORTS_CONFIG = {portsConfigCount, portsConfigGet, portsConfigSelect};

clap_id portsConfigCurrent(const clap_plugin_t* p) { return zelf(p)->kanalen == 1 ? CONFIG_MONO : CONFIG_STEREO; }
bool portsConfigInfoGet(const clap_plugin_t*, clap_id config, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if ((config != CONFIG_STEREO && config != CONFIG_MONO) || isInput || index != 0) return false;
  vulPoort(info, kanalenVan(config));
  return true;
}
const clap_plugin_audio_ports_config_info_t PORTS_CONFIG_INFO = {portsConfigCurrent, portsConfigInfoGet};

// --- configurable-audio-ports (de VST3 van clap-wrapper: setBusArrangements) ----------------------------------------
// Alleen de uitgang (poort 0) met 1 of 2 kanalen; een ingang vragen kan niet. Geeft het aantal kanalen, of 0.
uint32_t gevraagdeKanalen(const clap_plugin_t* p, const clap_audio_port_configuration_request* r, uint32_t n) {
  uint32_t k = zelf(p)->kanalen;
  for (uint32_t i = 0; i < n; ++i) {
    if (r[i].is_input || r[i].port_index != 0 || (r[i].channel_count != 1 && r[i].channel_count != 2)) return 0;
    if (r[i].port_type && std::strcmp(r[i].port_type, r[i].channel_count == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO))
      return 0;
    k = r[i].channel_count;
  }
  return k;
}
bool configurableCanApply(const clap_plugin_t* p, const clap_audio_port_configuration_request* r, uint32_t n) {
  return !zelf(p)->actief && gevraagdeKanalen(p, r, n) != 0;
}
bool configurableApply(const clap_plugin_t* p, const clap_audio_port_configuration_request* r, uint32_t n) {
  if (!configurableCanApply(p, r, n)) return false;
  zelf(p)->kanalen = gevraagdeKanalen(p, r, n);
  return true;
}
const clap_plugin_configurable_audio_ports_t CONFIGURABLE = {configurableCanApply, configurableApply};

// --- note-ports: één ingang, alleen het CLAP-dialect (valkuil 19) -------------------------------------------------
uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
  if (!isInput || index != 0) return false;
  std::memset(info, 0, sizeof(*info));
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  std::strncpy(info->name, "Noten", sizeof(info->name) - 1);
  return true;
}
const clap_plugin_note_ports_t NOTE_PORTS = {notePortsCount, notePortsGet};

// --- latency en tail -----------------------------------------------------------------------------------------------
uint32_t latencyGet(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY = {latencyGet};

uint32_t tailGet(const clap_plugin_t* p) { return zelf(p)->staart.load(std::memory_order_relaxed); }
const clap_plugin_tail_t TAIL = {tailGet};

// --- params --------------------------------------------------------------------------------------------------------
uint32_t paramsCount(const clap_plugin_t*) { return (uint32_t)NP; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)NP) return false;
  std::memset(info, 0, sizeof(*info));
  if (index == (uint32_t)BYPASS) {
    info->id = BYPASS_ID;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS;
    info->cookie = nullptr;
    std::strncpy(info->name, "Bypass", sizeof(info->name) - 1);
    info->module[0] = 0;
    info->min_value = 0;
    info->max_value = 1;
    info->default_value = 0;
    return true;
  }
  const auto& p = Ontwerp::PARAMS[index];
  info->id = p.id;
  info->flags = (p.vlaggen & pb::AUTOMATEERBAAR) ? CLAP_PARAM_IS_AUTOMATABLE : 0;
  if (p.vlaggen & pb::STAPPEN) info->flags |= CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::strncpy(info->name, p.naam, sizeof(info->name) - 1);
  info->module[0] = 0;
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}

bool paramsGetValue(const clap_plugin_t* pl, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(pl)->spiegel[i].load(std::memory_order_relaxed);
  return true;
}

// Tekst volgens SPEC.md "Tekst ↔ waarde": komma als decimaalteken, spatie voor de eenheid, geen locale.
// Eerst afronden op wat getoond wordt, dan pas de grens kiezen: 999,7 Hz is "1,00 kHz", niet "1000 Hz".
double rond(double v, double stap) { return std::floor(v / stap + 0.5) * stap; }

void metKomma(char* s) {
  for (; *s; ++s)
    if (*s == '.') *s = ',';
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t grootte) {
  const int i = indexVan(id);
  if (i < 0 || !uit || grootte == 0 || !(v == v)) return false;
  char b[64];
  if (i == BYPASS) {
    std::snprintf(uit, grootte, "%s", v >= 0.5 ? "Aan" : "Uit");
    return true;
  }
  const char* eenheid = Ontwerp::PARAMS[i].eenheid;
  if (!std::strcmp(eenheid, "Hz")) {  // onder 1000 hele Hz; vanaf 1000 kHz met 2 decimalen, vanaf 10 000 met 1
    if (rond(v, 1) < 1000) std::snprintf(b, sizeof b, "%.0f Hz", rond(v, 1) + 0.0);
    else if (rond(v, 10) < 10000) std::snprintf(b, sizeof b, "%.2f kHz", rond(v, 10) / 1000.0);
    else std::snprintf(b, sizeof b, "%.1f kHz", rond(v, 100) / 1000.0);
  } else if (!std::strcmp(eenheid, "ms")) {  // onder 100 één decimaal, vanaf 100 hele ms
    if (rond(v, 0.1) < 100) std::snprintf(b, sizeof b, "%.1f ms", rond(v * 10, 1) / 10.0 + 0.0);
    else std::snprintf(b, sizeof b, "%.0f ms", rond(v, 1));
  } else {  // eenheidloos: × 100, hele procenten
    std::snprintf(b, sizeof b, "%.0f %%", rond(v * 100.0, 1) + 0.0);
  }
  metKomma(b);
  std::snprintf(uit, grootte, "%s", b);
  return true;
}

// Een getal met komma of punt, zonder locale (een host kan de C-locale veranderd hebben). De cijfers worden als geheel
// getal gelezen (mant) met zijn deler (een macht van 10); de aanroeper deelt één keer, ook voor procenten, zodat "38 %"
// precies 0.38 wordt (zoals strtod het zou doen).
bool leesGetal(const char* s, double* mant, double* deler, const char** rest) {
  while (*s == ' ') ++s;
  bool neg = false;
  if (*s == '-' || *s == '+') neg = *s++ == '-';
  double x = 0, d = 1;
  int cijfers = 0;
  while (*s >= '0' && *s <= '9') { x = x * 10 + (*s++ - '0'); ++cijfers; }
  if (*s == ',' || *s == '.') {
    ++s;
    while (*s >= '0' && *s <= '9') { x = x * 10 + (*s++ - '0'); d *= 10; ++cijfers; }
  }
  if (!cijfers || cijfers > 15) return false;  // meer dan 15 cijfers is niet exact; zo lang is geen waarde van ons
  *mant = neg ? -x : x;
  *deler = d;
  *rest = s;
  return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !tekst) return false;
  if (i == BYPASS) {  // "Aan"/"Uit" (ook "On"/"Off"), of een getal (aan vanaf 0,5)
    const char* s = tekst;
    while (*s == ' ') ++s;
    size_t n = std::strlen(s);
    while (n && s[n - 1] == ' ') --n;
    auto is = [&](const char* w) {
      if (std::strlen(w) != n) return false;
      for (size_t k = 0; k < n; ++k)
        if ((s[k] | 0x20) != w[k]) return false;
      return true;
    };
    if (is("aan") || is("on")) { *v = 1.0; return true; }
    if (is("uit") || is("off")) { *v = 0.0; return true; }
  }
  double mant, deler, x;
  const char* r;
  if (!leesGetal(tekst, &mant, &deler, &r)) return false;
  while (*r == ' ') ++r;
  const char* eenheid = i == BYPASS ? "" : Ontwerp::PARAMS[i].eenheid;
  if (i == BYPASS) {
    x = mant / deler;
  } else if (!std::strcmp(eenheid, "Hz")) {  // "900", "900 Hz", "3k", "3,00 kHz"
    const bool kilo = *r == 'k' || *r == 'K';
    if (kilo) ++r;
    if ((r[0] == 'H' || r[0] == 'h') && (r[1] == 'z' || r[1] == 'Z')) r += 2;
    x = kilo ? mant * 1000.0 / deler : mant / deler;
  } else if (!std::strcmp(eenheid, "ms")) {  // "375", "375 ms", "20,0 ms"
    if (r[0] == 'm' && r[1] == 's') r += 2;
    x = mant / deler;
  } else {  // eenheidloos: procenten, met of zonder "%" ("38" en "38 %" zijn 0,38)
    if (*r == '%') ++r;
    x = mant / (deler * 100.0);
  }
  while (*r == ' ') ++r;
  if (*r) return false;
  *v = klem(i, x);
  return true;
}

// flush: buiten process. Actief: op de audiothread; niet actief: op de mainthread (dan draait er geen audio).
void paramsFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t* out) {
  auto* t = zelf(pl);
  neemStateOver(t);
  paneelNaarHost(t, out);
  const uint32_t n = in ? in->size(in) : 0;
  for (uint32_t k = 0; k < n; ++k) doeEvent(t, in->get(in, k));
}

const clap_plugin_params_t PARAMS_EXT = {paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
                                         paramsTextToValue, paramsFlush};

// --- state ---------------------------------------------------------------------------------------------------------
void zetU32(unsigned char* b, uint32_t v) {
  for (int k = 0; k < 4; ++k) b[k] = (unsigned char)(v >> (8 * k));
}
uint32_t leesU32(const unsigned char* b) {
  uint32_t v = 0;
  for (int k = 0; k < 4; ++k) v |= (uint32_t)b[k] << (8 * k);
  return v;
}

bool stateSave(const clap_plugin_t* pl, const clap_ostream_t* os) {
  auto* t = zelf(pl);
  std::vector<unsigned char> b(12 + 12 * NP);
  std::memcpy(b.data(), "PBST", 4);
  zetU32(&b[4], STATE_VERSIE);
  zetU32(&b[8], (uint32_t)NP);
  for (int i = 0; i < NP; ++i) {  // de parameters van het Ontwerp, dan de bypass (gewoon een id erbij)
    unsigned char* q = &b[12 + 12 * i];
    zetU32(q, i == BYPASS ? BYPASS_ID : Ontwerp::PARAMS[i].id);
    const double v = t->spiegel[i].load(std::memory_order_relaxed);
    uint64_t u;
    std::memcpy(&u, &v, 8);
    for (int k = 0; k < 8; ++k) q[4 + k] = (unsigned char)(u >> (8 * k));
  }
  size_t gedaan = 0;
  while (gedaan < b.size()) {
    const int64_t r = os->write(os, b.data() + gedaan, b.size() - gedaan);
    if (r <= 0) return false;
    gedaan += (size_t)r;
  }
  return true;
}

// Alle parameters tegelijk (state.load en preset-load; mainthread). Actief: in de spiegel, process of flush neemt ze
// over vóór de events van het volgende blok; niet actief: meteen het Ontwerp in. Daarna rescan, en als de plugin
// actief is request_flush.
void zetAlleWaarden(Toetsenpad* t, const double* w) {  // w: NP waarden (de bypass als laatste)
  for (int i = 0; i < NP; ++i) t->spiegel[i].store(w[i], std::memory_order_relaxed);
  if (t->actief) {
    t->stateKlaar.store(true, std::memory_order_release);  // process of flush neemt ze over
  } else {
    for (int i = 0; i < N; ++i) t->ontwerp.zetParam(i, w[i]);
    t->bypassDoel = w[BYPASS] >= 0.5;
  }
  if (t->hostParams) t->hostParams->rescan(t->host, CLAP_PARAM_RESCAN_VALUES);
  if (t->actief && t->hostParams) t->hostParams->request_flush(t->host);
}

bool stateLoad(const clap_plugin_t* pl, const clap_istream_t* is) {
  auto* t = zelf(pl);
  std::vector<unsigned char> b;
  unsigned char buf[4096];
  for (;;) {
    const int64_t r = is->read(is, buf, sizeof buf);
    if (r < 0) return false;
    if (r == 0) break;
    b.insert(b.end(), buf, buf + r);
    if (b.size() > (1u << 20)) return false;  // geen state van ons is zo groot
  }
  if (b.size() < 12 || std::memcmp(b.data(), "PBST", 4)) return false;
  const uint32_t versie = leesU32(&b[4]), aantal = leesU32(&b[8]);
  if (versie == 0) return false;
  if ((uint64_t)(b.size() - 12) < (uint64_t)aantal * 12) return false;

  // per id; een ontbrekend id houdt zijn standaard, een onbekend id wordt overgeslagen
  double w[NP];
  for (int i = 0; i < N; ++i) w[i] = Ontwerp::PARAMS[i].standaard;
  w[BYPASS] = 0.0;  // een oudere state zonder bypass: niet omzeild
  for (uint32_t k = 0; k < aantal; ++k) {
    const unsigned char* q = &b[12 + 12 * (size_t)k];
    const int i = indexVan(leesU32(q));
    if (i < 0) continue;
    uint64_t u = 0;
    for (int j = 0; j < 8; ++j) u |= (uint64_t)q[4 + j] << (8 * j);
    double v;
    std::memcpy(&v, &u, 8);
    w[i] = klem(i, v);
  }
  zetAlleWaarden(t, w);
  return true;
}

const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- fabriekspresets (ARCHITECTUUR.md "Presets") -------------------------------------------------------------------
// De tabel komt uit presets/*.json (presets_tabel.h, gemaakt door CMake). Een onbekende parameternaam of een waarde
// buiten het bereik is een bouwfout: dat toetsen deze static_asserts tegen PARAMS.
constexpr bool zelfdeTekst(const char* a, const char* b) {
  while (*a && *a == *b) ++a, ++b;
  return *a == *b;
}
constexpr int presetIndex(const char* naam) {
  for (int i = 0; i < N; ++i)
    if (zelfdeTekst(Ontwerp::PARAMS[i].naam, naam)) return i;
  return -1;
}
constexpr bool presetNamenBestaan() {
  for (const auto& p : pb_presets::PRESETS)
    for (int k = 0; k < p.aantal; ++k)
      if (presetIndex(p.waarden[k].naam) < 0) return false;
  return true;
}
constexpr bool presetWaardenInBereik() {
  for (const auto& p : pb_presets::PRESETS)
    for (int k = 0; k < p.aantal; ++k) {
      const int i = presetIndex(p.waarden[k].naam);
      if (i >= 0 && !(p.waarden[k].waarde >= Ontwerp::PARAMS[i].min && p.waarden[k].waarde <= Ontwerp::PARAMS[i].max))
        return false;
    }
  return true;
}
constexpr bool presetNaamTweeKeer() {
  for (const auto& p : pb_presets::PRESETS)
    for (int k = 0; k < p.aantal; ++k)
      for (int j = 0; j < k; ++j)
        if (zelfdeTekst(p.waarden[j].naam, p.waarden[k].naam)) return true;
  return false;
}
static_assert(presetNamenBestaan(), "presets/*.json: een naam in \"waarden\" is geen parameter (SPEC.md)");
static_assert(presetWaardenInBereik(), "presets/*.json: een waarde ligt buiten het bereik van zijn parameter");
static_assert(!presetNaamTweeKeer(), "presets/*.json: een parameter staat twee keer in \"waarden\"");

constexpr int P = pb_presets::AANTAL;

const pb_presets::Preset* presetBijSleutel(const char* sleutel) {
  if (!sleutel) return nullptr;
  for (const auto& p : pb_presets::PRESETS)
    if (!std::strcmp(p.sleutel, sleutel)) return &p;
  return nullptr;
}

// preset-load (mainthread): alleen locaties van soort PLUGIN (location == nullptr) met de sleutel als load key
bool presetLoadFromLocation(const clap_plugin_t* pl, uint32_t kind, const char* location, const char* loadKey) {
  auto* t = zelf(pl);
  const pb_presets::Preset* p = kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && !location ? presetBijSleutel(loadKey)
                                                                                         : nullptr;
  if (!p) {
    if (t->hostPresetLoad && t->hostPresetLoad->on_error)
      t->hostPresetLoad->on_error(t->host, kind, location, loadKey, 0, "PB Toetsenpad: onbekende preset");
    return false;
  }
  double w[NP];
  for (int i = 0; i < N; ++i) w[i] = Ontwerp::PARAMS[i].standaard;  // ontbrekende namen: hun standaard
  w[BYPASS] = t->spiegel[BYPASS].load(std::memory_order_relaxed);  // een preset verandert de bypass niet
  for (int k = 0; k < p->aantal; ++k) w[presetIndex(p->waarden[k].naam)] = p->waarden[k].waarde;
  zetAlleWaarden(t, w);
  if (t->hostPresetLoad && t->hostPresetLoad->loaded) t->hostPresetLoad->loaded(t->host, kind, location, loadKey);
  return true;
}
const clap_plugin_preset_load_t PRESET_LOAD = {presetLoadFromLocation};

// preset-discovery: per preset een provider (valkuil 15: clap-validator 0.4.1 loopt vast op een container met meer
// dan één preset). Alles hieronder is constant geïnitialiseerd (valkuil 16: de AU roept init al aan tijdens de
// statische initialisatie).
#define PB_PROVIDER_ID(sleutel) PB_CLAP_ID ".preset." sleutel,
constexpr const char* PROVIDER_IDS[P] = {PB_PRESET_SLEUTELS(PB_PROVIDER_ID)};
#undef PB_PROVIDER_ID

constexpr std::array<clap_preset_discovery_provider_descriptor_t, P> maakProviderDescs() {
  std::array<clap_preset_discovery_provider_descriptor_t, P> d{};
  for (int i = 0; i < P; ++i) d[i] = {CLAP_VERSION_INIT, PROVIDER_IDS[i], pb_presets::PRESETS[i].naam, PB_MAKER};
  return d;
}
constexpr std::array<clap_preset_discovery_provider_descriptor_t, P> PROVIDER_DESCS = maakProviderDescs();

const clap_universal_plugin_id_t PLUGIN_ID = {"clap", PB_CLAP_ID};

struct Provider {
  clap_preset_discovery_provider_t p;
  const clap_preset_discovery_indexer_t* indexer;
  int index;
};
const pb_presets::Preset& presetVan(const clap_preset_discovery_provider_t* p) {
  return pb_presets::PRESETS[static_cast<const Provider*>(p->provider_data)->index];
}

bool providerInit(const clap_preset_discovery_provider_t* p) {
  const auto* pr = static_cast<const Provider*>(p->provider_data);
  const clap_preset_discovery_location_t loc = {CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, presetVan(p).naam,
                                                CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
  return pr->indexer->declare_location(pr->indexer, &loc);
}
void providerDestroy(const clap_preset_discovery_provider_t* p) { delete static_cast<const Provider*>(p->provider_data); }
// één preset per locatie, met load key: een container van één (valkuil 15)
bool providerGetMetadata(const clap_preset_discovery_provider_t* p, uint32_t kind, const char* location,
                         const clap_preset_discovery_metadata_receiver_t* r) {
  if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location) return false;
  const pb_presets::Preset& pr = presetVan(p);
  if (!r->begin_preset(r, pr.naam, pr.sleutel)) return true;
  r->add_plugin_id(r, &PLUGIN_ID);
  r->set_flags(r, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
  r->add_creator(r, PB_MAKER);
  r->set_description(r, pr.uitleg);
  return true;
}
const void* providerGetExtension(const clap_preset_discovery_provider_t*, const char*) { return nullptr; }

uint32_t presetFactoryCount(const clap_preset_discovery_factory_t*) { return (uint32_t)P; }
const clap_preset_discovery_provider_descriptor_t* presetFactoryDescriptor(const clap_preset_discovery_factory_t*,
                                                                         uint32_t index) {
  return index < (uint32_t)P ? &PROVIDER_DESCS[index] : nullptr;
}
const clap_preset_discovery_provider_t* presetFactoryCreate(const clap_preset_discovery_factory_t*,
                                                           const clap_preset_discovery_indexer_t* indexer,
                                                           const char* providerId) {
  if (!indexer || !providerId) return nullptr;
  for (int i = 0; i < P; ++i) {
    if (std::strcmp(PROVIDER_IDS[i], providerId)) continue;
    auto* pr = new Provider{{&PROVIDER_DESCS[i], nullptr, providerInit, providerDestroy, providerGetMetadata,
                             providerGetExtension},
                            indexer, i};
    pr->p.provider_data = pr;
    return &pr->p;
  }
  return nullptr;
}
const clap_preset_discovery_factory_t PRESET_FACTORY = {presetFactoryCount, presetFactoryDescriptor,
                                                        presetFactoryCreate};

// --- plugin --------------------------------------------------------------------------------------------------------
bool pluginInit(const clap_plugin_t* p) {
  auto* t = zelf(p);
  t->hostParams = static_cast<const clap_host_params_t*>(t->host->get_extension(t->host, CLAP_EXT_PARAMS));
  t->hostPresetLoad =
      static_cast<const clap_host_preset_load_t*>(t->host->get_extension(t->host, CLAP_EXT_PRESET_LOAD));
  if (!t->hostPresetLoad)
    t->hostPresetLoad =
        static_cast<const clap_host_preset_load_t*>(t->host->get_extension(t->host, CLAP_EXT_PRESET_LOAD_COMPAT));
  return true;
}

void pluginDestroy(const clap_plugin_t* p) {
  auto* t = zelf(p);
#ifdef __APPLE__
  if (t->view) pbMacWeg(t->view);  // een host die gui.destroy vergat
#endif
  delete t;
}

bool pluginActivate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t maxFrames) {
  (void)minFrames;
  auto* t = zelf(p);
  t->sr = sr;
  t->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);
  for (auto& s : t->stil) s.assign(maxFrames > 0 ? maxFrames : 1, 0.0f);
  pb::Geheugen g;
  g.p = t->geheugen.data();
  g.over = (unsigned long)t->geheugen.size();
  t->ontwerp.activeer(sr, maxFrames, g);
  // wat state.load klaarzette terwijl we niet actief waren, staat al in het Ontwerp; dit is voor de zekerheid
  if (t->stateKlaar.exchange(false)) {
    for (int i = 0; i < N; ++i) t->ontwerp.zetParam(i, t->spiegel[i].load());
    t->bypassDoel = t->spiegel[BYPASS].load() >= 0.5;
  }
  // bypass: 10 ms in hele samples; na activate staat de overvloeiing meteen op het doel (geen overgang bij de start)
  const double l = std::floor(OVERVLOEI_SEC * sr + 0.5);
  t->overvloeiLengte = l < 1 ? 1u : (uint32_t)l;
  t->overvloeiPos = t->bypassDoel ? t->overvloeiLengte : 0;
  t->staart.store(staartSamples(t));
  t->actief = true;
  return true;
}

void pluginDeactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }

bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* p) {
  auto* t = zelf(p);
  t->ontwerp.reset();
  // de stapel leeg (ARCHITECTUUR.md "Noten" 4). Klonk er een noot, dan sluit de poort ook (zoals stap 2 na leeg),
  // anders blijft hij na een reset hangen; zonder noten blijft Poort wat de knop zegt.
  const bool hadNoot = t->stapel.boven() >= 0;
  t->stapel.leeg();
  if (hadNoot) naNoot(t);
  t->overvloeiPos = t->bypassDoel ? t->overvloeiLengte : 0;
}

// verwerk van pos tot eind (een stuk tussen twee events)
void render(Toetsenpad* t, const clap_audio_buffer_t& uit, uint32_t pos, uint32_t eind) {
  if (eind <= pos) return;
  const uint32_t n = eind - pos;
  const int kan = uit.channel_count < KANALEN ? (int)uit.channel_count : (int)KANALEN;
  const float* in[KANALEN];
  float* u[KANALEN];
  for (int c = 0; c < (int)KANALEN; ++c) in[c] = t->stil[c].data() + pos;
  for (int c = 0; c < kan; ++c) u[c] = uit.data32[c] + pos;
  t->ontwerp.verwerk(in, u, kan, n);

  // bypass: de DSP rekende door; vloei per sample over naar de droge invoer (bij een instrument de stille buffer).
  // Niet omzeild en geen overgang bezig: de uitvoer van het Ontwerp blijft bit voor bit wat hij is.
  if (!t->bypassDoel && t->overvloeiPos == 0) return;
  const uint32_t L = t->overvloeiLengte;
  for (uint32_t s = 0; s < n; ++s) {
    if (t->bypassDoel) {
      if (t->overvloeiPos < L) ++t->overvloeiPos;
    } else if (t->overvloeiPos > 0) {
      --t->overvloeiPos;
    }
    const double droog = (double)t->overvloeiPos / (double)L, nat = 1.0 - droog;
    for (int c = 0; c < kan; ++c) u[c][s] = pb::uitvoer((double)u[c][s] * nat + (double)in[c][s] * droog);
  }
}

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* pr) {
#ifdef PB_KANARIE
  pbKanarieGeheugen = std::malloc(64);
#endif
  auto* t = zelf(p);
  const uint32_t frames = pr->frames_count;

  // transport van de host (tempo en plek in de maat; deze keten heeft geen sequencer, maar het contract vraagt het)
  if (const clap_event_transport_t* tr = pr->transport) {
    const double bpm = (tr->flags & CLAP_TRANSPORT_HAS_TEMPO) ? tr->tempo : 0.0;
    const double beat = (tr->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
                            ? (double)tr->song_pos_beats / (double)CLAP_BEATTIME_FACTOR : 0.0;
    t->ontwerp.zetTransport(bpm, beat, (tr->flags & CLAP_TRANSPORT_IS_PLAYING) != 0);
  } else {
    t->ontwerp.zetTransport(0.0, 0.0, false);
  }

  neemStateOver(t);
  paneelNaarHost(t, pr->out_events);  // vóór de events van de host: die gaan voor als ze op dezelfde sample vallen

  const bool heeftUit = pr->audio_outputs_count > 0 && pr->audio_outputs[0].data32 &&
                        pr->audio_outputs[0].channel_count > 0;
  const uint32_t nEv = pr->in_events ? pr->in_events->size(pr->in_events) : 0;
  uint32_t pos = 0;
  for (uint32_t k = 0; k < nEv; ++k) {
    const clap_event_header_t* h = pr->in_events->get(pr->in_events, k);
    uint32_t tijd = h->time < frames ? h->time : frames;
    if (tijd > pos) {  // het blok splitsen: tot het event rekenen, dan het event toepassen
      if (heeftUit) render(t, pr->audio_outputs[0], pos, tijd);
      pos = tijd;
    }
    doeEvent(t, h);
  }
  if (heeftUit) render(t, pr->audio_outputs[0], pos, frames);

  // andere uitgangen en kanalen boven de stereo: stilte
  for (uint32_t b = 0; b < pr->audio_outputs_count; ++b) {
    const clap_audio_buffer_t& uit = pr->audio_outputs[b];
    for (uint32_t c = (b == 0 && heeftUit) ? KANALEN : 0; c < uit.channel_count; ++c) {
      if (uit.data32 && uit.data32[c]) std::memset(uit.data32[c], 0, sizeof(float) * frames);
      if (uit.data64 && uit.data64[c]) std::memset(uit.data64[c], 0, sizeof(double) * frames);
    }
  }

  t->staart.store(staartSamples(t), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

// --- het paneel: PaneelBron (mainthread) ---------------------------------------------------------------------------
double PaneelBron::waarde(int k) { return t->spiegel[k].load(std::memory_order_relaxed); }
bool PaneelBron::uitstaand(int k) { return t->uitstaand[k].load(std::memory_order_acquire) > 0; }
void PaneelBron::tekst(int k, double v, char* uit, uint32_t n) {
  if (!paramsValueToText(&t->plugin, Ontwerp::PARAMS[k].id, v, uit, n) && n) uit[0] = 0;
}
void PaneelBron::stuur(pb_paneel::Gebaar g, int k, double v) {
  if (k < 0 || k >= N) return;
  // Een volle rij (een host die niet flusht en niet rendert): een gebaar dat niet begon, krijgt ook geen WAARDE en EIND,
  // en voor EIND blijft altijd plaats (WAARDE neemt de laatste twee plaatsen niet, BEGIN de laatste drie niet).
  if (g == pb_paneel::BEGIN) {
    gebaarInRij[k] = t->rij.vrij() >= 3 && t->rij.zet({(uint8_t)g, (uint8_t)k, 0});
  } else if (g == pb_paneel::WAARDE) {
    if (!gebaarInRij[k] || t->rij.vrij() < 2) return;
    t->uitstaand[k].fetch_add(1, std::memory_order_relaxed);
    if (!t->rij.zet({(uint8_t)g, (uint8_t)k, klem(k, v)})) t->uitstaand[k].fetch_sub(1, std::memory_order_relaxed);
  } else {
    if (!gebaarInRij[k]) return;
    gebaarInRij[k] = false;
    t->rij.zet({(uint8_t)g, (uint8_t)k, 0});
  }
  // valkuil 14: de host roept flush (of process) aan; onder AUv2 doet dit niets en komt het bij de volgende Render
  if (t->hostParams) t->hostParams->request_flush(t->host);
}

pb_paneel::Paneel* maakPaneel(Toetsenpad* t) {
  if (!t->paneel) {
    pb_paneel::Knop k[N];
    for (int i = 0; i < N; ++i) {
      const auto& p = Ontwerp::PARAMS[i];
      k[i] = {p.naam, p.naam, p.min, p.max, p.standaard};
    }
    t->paneel.reset(new pb_paneel::Paneel(t->bron, k, N));
    t->paneel->maat(t->paneelB, t->paneelH);
  }
  return t->paneel.get();
}

// --- paneeltoets (paneel_toets.h): het paneel zonder venster, voor plugin/toets/paneeltoets.cpp --------------------
bool toetsMaak(const clap_plugin_t* p, double b, double h) {
  auto* t = zelf(p);
  if (!t->paneel) {
    t->paneelB = b;
    t->paneelH = h;
  }
  return maakPaneel(t) != nullptr;
}
void toetsWeg(const clap_plugin_t* p) {
  auto* t = zelf(p);
  if (!t->view) t->paneel.reset();
}
bool toetsTeken(const clap_plugin_t* p, uint8_t* rgba, uint32_t bw, uint32_t bh, double schaal) {
  auto* t = zelf(p);
  if (!t->paneel || !rgba) return false;
  t->paneel->teken(rgba, (int)bw, (int)bh, schaal);
  return true;
}
bool toetsMuis(const clap_plugin_t* p, int soort, double x, double y, bool shift) {
  auto* t = zelf(p);
  if (!t->paneel || soort < 0 || soort > 3) return false;
  return t->paneel->muis((pb_paneel::Muis)soort, x, y, shift);
}
bool toetsKnop(const clap_plugin_t* p, int k, double* x, double* y) {
  auto* t = zelf(p);
  if (!t->paneel || k < 0 || k >= t->paneel->aantal()) return false;
  t->paneel->knopMidden(k, x, y);
  return true;
}
int toetsGekozen(const clap_plugin_t* p) {
  auto* t = zelf(p);
  return t->paneel ? t->paneel->gekozen() : -1;
}
const pb_paneeltoets_t PANEELTOETS = {toetsMaak, toetsWeg, toetsTeken, toetsMuis, toetsKnop, toetsGekozen};

#ifdef __APPLE__
// --- gui (CLAP, Cocoa): clap-wrapper geeft hem door aan VST3 (IPlugView) en AUv2 (kAudioUnitProperty_CocoaUI) ----
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating) {
  return !floating && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA);
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating) {
  *api = CLAP_WINDOW_API_COCOA;
  *floating = false;
  return true;
}
bool guiCreate(const clap_plugin_t* p, const char* api, bool floating) {
  auto* t = zelf(p);
  if (!guiIsApiSupported(p, api, floating) || t->view) return false;
  t->view = pbMacMaak(maakPaneel(t));
  return t->view != nullptr;
}
// clap-wrapper (AUv2 Cleanup) kan destroy twee keer aanroepen: de tweede keer doet niets
void guiDestroy(const clap_plugin_t* p) {
  auto* t = zelf(p);
  if (t->view) pbMacWeg(t->view);
  t->view = nullptr;
  t->paneel.reset();
}
bool guiSetScale(const clap_plugin_t*, double) { return false; }  // Cocoa: logische punten, de view kent zijn schaal
bool guiGetSize(const clap_plugin_t* p, uint32_t* b, uint32_t* h) {
  auto* t = zelf(p);
  *b = (uint32_t)(t->paneelB + 0.5);
  *h = (uint32_t)(t->paneelH + 0.5);
  return true;
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
  hints->can_resize_horizontally = true;
  hints->can_resize_vertically = true;
  hints->preserve_aspect_ratio = false;
  hints->aspect_ratio_width = 0;
  hints->aspect_ratio_height = 0;
  return true;
}
bool guiAdjustSize(const clap_plugin_t*, uint32_t* b, uint32_t* h) {
  using P = pb_paneel::Paneel;
  *b = *b < (uint32_t)P::MIN_B ? (uint32_t)P::MIN_B : (*b > (uint32_t)P::MAX_B ? (uint32_t)P::MAX_B : *b);
  *h = *h < (uint32_t)P::MIN_H ? (uint32_t)P::MIN_H : (*h > (uint32_t)P::MAX_H ? (uint32_t)P::MAX_H : *h);
  return true;
}
bool guiSetSize(const clap_plugin_t* p, uint32_t b, uint32_t h) {
  auto* t = zelf(p);
  t->paneelB = b;
  t->paneelH = h;
  if (t->paneel) t->paneel->maat(b, h);
  pbMacMaat(t->view, b, h);
  return true;
}
bool guiSetParent(const clap_plugin_t* p, const clap_window_t* w) {
  auto* t = zelf(p);
  if (!t->view || !w || !w->cocoa) return false;
  pbMacOuder(t->view, w->cocoa);
  pbMacMaat(t->view, t->paneelB, t->paneelH);
  return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* p) {
  pbMacToon(zelf(p)->view, true);
  return true;
}
bool guiHide(const clap_plugin_t* p) {
  pbMacToon(zelf(p)->view, false);
  return true;
}
const clap_plugin_gui_t GUI = {guiIsApiSupported, guiGetPreferredApi, guiCreate,     guiDestroy,
                               guiSetScale,       guiGetSize,         guiCanResize,  guiGetResizeHints,
                               guiAdjustSize,     guiSetSize,         guiSetParent,  guiSetTransient,
                               guiSuggestTitle,   guiShow,            guiHide};
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG)) return &PORTS_CONFIG;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) || !std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT))
    return &PORTS_CONFIG_INFO;
  if (!std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS) || !std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS_COMPAT))
    return &CONFIGURABLE;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD) || !std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &PRESET_LOAD;
#ifdef __APPLE__
  if (!std::strcmp(id, CLAP_EXT_GUI)) return &GUI;
#endif
  if (!std::strcmp(id, PB_EXT_PANEELTOETS)) return &PANEELTOETS;
  return nullptr;
}

void pluginOnMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  auto* t = new Toetsenpad();
  t->host = host;
  t->plugin.desc = &DESCRIPTOR;
  t->plugin.plugin_data = t;
  t->plugin.init = pluginInit;
  t->plugin.destroy = pluginDestroy;
  t->plugin.activate = pluginActivate;
  t->plugin.deactivate = pluginDeactivate;
  t->plugin.start_processing = pluginStartProcessing;
  t->plugin.stop_processing = pluginStopProcessing;
  t->plugin.reset = pluginReset;
  t->plugin.process = pluginProcess;
  t->plugin.get_extension = pluginGetExtension;
  t->plugin.on_main_thread = pluginOnMainThread;
  return &t->plugin;
}

// --- factory -------------------------------------------------------------------------------------------------------
uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t index) {
  return index == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id)) return nullptr;
  return maakPlugin(host);
}
const clap_plugin_factory_t FACTORY = {factoryCount, factoryDescriptor, factoryCreate};

}  // namespace

// Geen globals die init vult (valkuil 16: de AU roept init al tijdens de statische initialisatie aan).
bool toetsenpad_entry_init(const char*) { return true; }
void toetsenpad_entry_deinit() {}
const void* toetsenpad_entry_get_factory(const char* factoryId) {
  if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) return &FACTORY;
  if (!std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) ||
      !std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT))
    return &PRESET_FACTORY;
  return nullptr;
}
