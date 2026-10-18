// dub_echo.cpp — de CLAP-schil rond het Ontwerp (dsp/ontwerpen/keten_dub_echo.h). clap-wrapper maakt er VST3 en AUv2 van.
//
// Draden (ARCHITECTUUR.md "Threads", valkuil 11): zolang de plugin actief is raakt alleen de audiothread (process, en
// flush als die daar loopt) het Ontwerp aan. De mainthread leest een atomaire kopie per parameter (`kopie`);
// state.load terwijl de plugin actief is schrijft die kopie en zet een vlag, en process/flush neemt de waarden over
// vóór de events van dat blok.
//
// Fase 1: process splitst het blok bij elk event en laat het Ontwerp elk stuk rekenen (verwerk). De staart rekent de
// audiothread aan het eind van process uit (staartSec leest de smoothers); tail.get leest die atomaire waarde.
//
// Fase 3: het frontpaneel (paneel.h, paneel_mac.mm) stuurt parameters via de host (valkuil 14). Het zet gebaren in
// de brievenbus en vraagt request_flush; process of params.flush leegt hem, stuurt GESTURE_BEGIN/PARAM_VALUE/
// GESTURE_END naar de host en past de waarde zelf toe, zoals een event van de host. Het paneel leest alleen `kopie`.
//
// Fase 4: fabriekspresets (presets.h/.cpp, uit presets/*.json gebakken). preset-load gaat hetzelfde pad als
// state.load (neemOver): ontbrekende namen krijgen hun standaard, daarna rescan en preset_load->loaded.
#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "dub_echo_entry.h"
#include "ontwerpen/keten_dub_echo.h"
#include "paneel.h"
#include "presets.h"
#include "tekst.h"

namespace {

using pb::Ontwerp;
constexpr int N = Ontwerp::N_PARAMS;
constexpr uint32_t STATE_VERSIE = 1;

#ifdef PB_KANARIE
// De kanarie voor RTSan: een malloc in process, met de pointer in een globale variabele zodat de compiler hem niet
// weghaalt. RTSan MOET dit vangen; doet hij dat niet, dan bewaakt de RTSan-job niets (valkuil 4).
void* volatile pbKanarie = nullptr;
#endif

int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id) return i;
  return -1;
}
double klem(int i, double v) {
  const auto& p = Ontwerp::PARAMS[i];
  if (!(v == v)) return p.standaard;  // NaN
  return v < p.min ? p.min : (v > p.max ? p.max : v);
}

// constant geïnitialiseerd (valkuil 16: de AU roept init al tijdens de statische initialisatie aan)
const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY,
                                CLAP_PLUGIN_FEATURE_STEREO, nullptr};
const clap_plugin_descriptor_t DESC = {CLAP_VERSION_INIT, PB_CLAP_ID, PB_NAAM, PB_MAKER, "https://varve.nl", "", "",
                                       PB_VERSIE, PB_UITLEG, FEATURES};

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;

  std::unique_ptr<Ontwerp> ontwerp;  // groot: op de heap, gemaakt in init
  std::vector<unsigned char> geheugen;  // in activate, nooit in process
  double sr = 48000;

  double waarde[N];                  // van de audiothread zolang actief
  std::atomic<double> kopie[N];      // voor get_value en state.save (elke draad)
  std::atomic<bool> stateKlaar{false};  // state.load terwijl actief: process/flush neemt `kopie` over
  std::atomic<bool> actief{false};
  std::atomic<uint32_t> staart{0};   // in samples, uit staartSec() (audiothread; in activate ook)
  std::vector<float> kopieIn;        // invoer die ook uitvoer is (in-place) en voor twee DSP-ingangen dient; uit activate
  dub::Bus bus;                      // van het paneel naar process/flush
  dub::PaneelPlek paneel;

  Plugin() {
    for (int i = 0; i < N; ++i) { waarde[i] = Ontwerp::PARAMS[i].standaard; kopie[i].store(waarde[i]); }
    paneel.kopie = kopie;
    paneel.bus = &bus;
  }

  void zet(int i, double v) {
    v = klem(i, v);
    waarde[i] = v;
    kopie[i].store(v, std::memory_order_relaxed);
    ontwerp->zetParam(i, v);
  }
  void neemStateOver() {
    if (!stateKlaar.exchange(false, std::memory_order_acquire)) return;
    for (int i = 0; i < N; ++i) zet(i, kopie[i].load(std::memory_order_relaxed));
  }
  // wat het paneel klaarzette: naar de host als gebaar en waarde, en zelf toepassen (een CLAP-host stuurt een
  // uitvoerevent van de plugin niet terug). Op sample 0, vóór de events van de host in dit blok.
  void neemPaneelOver(const clap_output_events_t* uit) {
    bus.leeg([&](dub::Bus::Soort soort, int i, double v) {
      const clap_id id = Ontwerp::PARAMS[i].id;
      if (soort == dub::Bus::WAARDE) {
        zet(i, v);
        clap_event_param_value_t e{};
        e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id;
        e.cookie = nullptr;
        e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
        e.value = waarde[i];
        if (uit) uit->try_push(uit, &e.header);
      } else {
        clap_event_param_gesture_t e{};
        e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID,
                    (uint16_t)(soort == dub::Bus::BEGIN ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END), 0};
        e.param_id = id;
        if (uit) uit->try_push(uit, &e.header);
      }
    });
  }
  void pasToe(const clap_event_header_t* h) {
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
    const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
    const int i = indexVan(e->param_id);
    if (i >= 0) zet(i, e->value);
  }

  void zetStaart() {
    const double t = ontwerp->staartSec();  // −1 = oneindig (lusversterking ≥ 1); CLAP: ≥ INT32_MAX is oneindig
    const double n = t * sr;
    staart.store(t < 0 || n >= (double)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)(n + 0.5), std::memory_order_relaxed);
  }

  // [pos, pos + n) van het blok door het Ontwerp. De DSP heeft twee kanalen; een kanaal dat de host niet geeft
  // leest het laatste ingangskanaal (of stilte). De DSP schrijft per sample eerst uitgang 0: dient één hostbuffer
  // als twee DSP-ingangen en is hij ook uitgang 0, dan eerst de invoer kopiëren (vooraf gereserveerd in activate).
  void verwerkStuk(const clap_process_t* pr, uint32_t pos, uint32_t n) {
    if (pr->audio_outputs_count == 0 || n == 0) return;
    const clap_audio_buffer_t& uit = pr->audio_outputs[0];
    if (!uit.data32 || uit.channel_count == 0) return;
    const clap_audio_buffer_t* in = pr->audio_inputs_count ? &pr->audio_inputs[0] : nullptr;
    const uint32_t nIn = (in && in->data32) ? in->channel_count : 0;
    const int kanalen = uit.channel_count >= 2 ? 2 : 1;
    const float* x[2];
    float* u[2];
    for (int c = 0; c < kanalen; ++c) u[c] = uit.data32[c] + pos;
    if (nIn == 0) {
      for (int c = 0; c < kanalen; ++c) std::memset(u[c], 0, n * sizeof(float));  // stilte als invoer
      x[0] = x[1] = u[0];
      if (kanalen == 2) {
        // u[0] wordt per sample overschreven: lees de stilte uit een eigen buffer
        std::memset(kopieIn.data(), 0, n * sizeof(float));
        x[0] = x[1] = kopieIn.data();
      }
    } else {
      for (int c = 0; c < kanalen; ++c) x[c] = in->data32[(uint32_t)c < nIn ? c : nIn - 1] + pos;
      if (kanalen == 2 && x[1] == u[0]) {
        std::memcpy(kopieIn.data(), x[1], n * sizeof(float));
        if (x[0] == x[1]) x[0] = kopieIn.data();
        x[1] = kopieIn.data();
      }
    }
    ontwerp->verwerk(x, u, kanalen, n);
    // meer uitgangskanalen dan de DSP heeft: kopie van het laatste
    for (uint32_t c = (uint32_t)kanalen; c < uit.channel_count; ++c)
      std::memcpy(uit.data32[c] + pos, u[kanalen - 1], n * sizeof(float));
  }

  clap_process_status process(const clap_process_t* pr) {
    neemStateOver();
    neemPaneelOver(pr->out_events);
#ifdef PB_KANARIE
    pbKanarie = std::malloc(64);
#endif
    const uint32_t n = pr->frames_count;
    const clap_input_events_t* ev = pr->in_events;
    const uint32_t nEv = ev ? ev->size(ev) : 0;
    uint32_t e = 0, pos = 0;
    // het blok wordt bij elk event gesplitst: een event geldt vanaf zijn eigen sample
    while (pos < n) {
      while (e < nEv) {
        const clap_event_header_t* h = ev->get(ev, e);
        if (h->time > pos) break;
        pasToe(h);
        ++e;
      }
      uint32_t eind = n;
      if (e < nEv) { const uint32_t t = ev->get(ev, e)->time; if (t < eind) eind = t; }
      // nooit meer dan maxFrames uit activate tegelijk (de kopiebuffer is zo groot)
      for (uint32_t p = pos; p < eind;) {
        const uint32_t m = eind - p < (uint32_t)kopieIn.size() ? eind - p : (uint32_t)kopieIn.size();
        verwerkStuk(pr, p, m);
        p += m;
      }
      pos = eind;
    }
    for (; e < nEv; ++e) pasToe(ev->get(ev, e));
    zetStaart();
    return CLAP_PROCESS_CONTINUE;
  }

  // --- state: "PBST", versie u32, aantal u32, dan per parameter id u32 + waarde f64 (little-endian) -------------
  bool bewaar(const clap_ostream_t* s) const {
    unsigned char b[12 + N * 12];
    size_t p = 0;
    auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) b[p++] = (unsigned char)(v >> (8 * k)); };
    auto f64 = [&](double v) { uint64_t u; std::memcpy(&u, &v, 8); for (int k = 0; k < 8; ++k) b[p++] = (unsigned char)(u >> (8 * k)); };
    std::memcpy(b, "PBST", 4); p = 4;
    u32(STATE_VERSIE); u32((uint32_t)N);
    for (int i = 0; i < N; ++i) { u32(Ontwerp::PARAMS[i].id); f64(kopie[i].load(std::memory_order_relaxed)); }
    size_t weg = 0;
    while (weg < p) {
      const int64_t r = s->write(s, b + weg, p - weg);
      if (r <= 0) return false;
      weg += (size_t)r;
    }
    return true;
  }
  bool laad(const clap_istream_t* s) {
    std::vector<unsigned char> b;
    unsigned char buf[256];
    for (;;) {
      const int64_t r = s->read(s, buf, sizeof buf);
      if (r < 0) return false;
      if (r == 0) break;
      b.insert(b.end(), buf, buf + r);
      if (b.size() > (1u << 20)) return false;
    }
    auto u32 = [&](size_t o) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= (uint32_t)b[o + k] << (8 * k); return v; };
    if (b.size() < 12 || std::memcmp(b.data(), "PBST", 4) != 0) return false;
    const uint32_t versie = u32(4), aantal = u32(8);
    if (versie == 0) return false;
    if ((uint64_t)(b.size() - 12) < (uint64_t)aantal * 12) return false;
    double nieuw[N];
    for (int i = 0; i < N; ++i) nieuw[i] = Ontwerp::PARAMS[i].standaard;  // een ontbrekend id houdt zijn standaard
    for (uint32_t k = 0; k < aantal; ++k) {
      const size_t o = 12 + (size_t)k * 12;
      const int i = indexVan(u32(o));
      if (i < 0) continue;  // onbekend id (nieuwere versie): overslaan
      uint64_t u = 0;
      for (int j = 0; j < 8; ++j) u |= (uint64_t)b[o + 4 + j] << (8 * j);
      double v; std::memcpy(&v, &u, 8);
      nieuw[i] = klem(i, v);
    }
    neemOver(nieuw);
    return true;
  }
  // nieuwe waarden van de mainthread (state.load, preset-load): inactief direct, actief via `kopie` en de vlag
  void neemOver(const double nieuw[N]) {
    if (actief.load()) {
      for (int i = 0; i < N; ++i) kopie[i].store(nieuw[i], std::memory_order_relaxed);
      stateKlaar.store(true, std::memory_order_release);
    } else {
      for (int i = 0; i < N; ++i) zet(i, nieuw[i]);
    }
    if (hostParams) hostParams->rescan(host, CLAP_PARAM_RESCAN_VALUES);
    if (actief.load() && hostParams) hostParams->request_flush(host);
  }
  bool laadPreset(uint32_t soort, const char* locatie, const char* sleutel) {
    const dub::Preset* p =
        soort == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && locatie == nullptr ? dub::presetBijSleutel(sleutel) : nullptr;
    const auto* hp = host ? static_cast<const clap_host_preset_load_t*>(host->get_extension(host, CLAP_EXT_PRESET_LOAD))
                          : nullptr;
    if (!hp && host)
      hp = static_cast<const clap_host_preset_load_t*>(host->get_extension(host, CLAP_EXT_PRESET_LOAD_COMPAT));
    if (!p) {
      if (hp && hp->on_error) hp->on_error(host, soort, locatie, sleutel, 0, "onbekende fabriekspreset");
      return false;
    }
    double nieuw[N];
    dub::presetWaarden(*p, nieuw);
    for (int i = 0; i < N; ++i) nieuw[i] = klem(i, nieuw[i]);
    neemOver(nieuw);
    if (hp && hp->loaded) hp->loaded(host, soort, locatie, sleutel);
    return true;
  }
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

}  // namespace

dub::PaneelPlek* dub::paneelPlek(const clap_plugin_t* p) { return p ? &zelf(p)->paneel : nullptr; }

namespace {

// --- params ------------------------------------------------------------------------------------------------
uint32_t paramsCount(const clap_plugin_t*) { return N; }
bool paramsInfo(const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
  if (i >= (uint32_t)N) return false;
  const auto& p = Ontwerp::PARAMS[i];
  std::memset(info, 0, sizeof *info);
  info->id = p.id;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  info->cookie = nullptr;
  std::strncpy(info->name, p.naam, CLAP_NAME_SIZE - 1);
  info->module[0] = 0;
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}
bool paramsValue(const clap_plugin_t* pl, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(pl)->kopie[i].load(std::memory_order_relaxed);
  return true;
}
bool paramsValueToText(const clap_plugin_t*, clap_id id, double v, char* t, uint32_t n) {
  const int i = indexVan(id);
  if (i < 0 || !t || n == 0) return false;
  const std::string s = dub::waardeNaarTekst(Ontwerp::PARAMS[i].eenheid, v);
  if (s.size() + 1 > n) return false;
  std::memcpy(t, s.c_str(), s.size() + 1);
  return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* t, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  const auto& p = Ontwerp::PARAMS[i];
  return dub::tekstNaarWaarde(p.eenheid, t, p.min, p.max, *v);
}
void paramsFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t* uit) {
  Plugin* z = zelf(pl);
  z->neemStateOver();
  z->neemPaneelOver(uit);
  const uint32_t n = in ? in->size(in) : 0;
  for (uint32_t k = 0; k < n; ++k) z->pasToe(in->get(in, k));
}
const clap_plugin_params_t PARAMS_EXT = {paramsCount, paramsInfo, paramsValue, paramsValueToText, paramsTextToValue,
                                         paramsFlush};

// --- state -------------------------------------------------------------------------------------------------
bool stateSave(const clap_plugin_t* pl, const clap_ostream_t* s) { return zelf(pl)->bewaar(s); }
bool stateLoad(const clap_plugin_t* pl, const clap_istream_t* s) { return zelf(pl)->laad(s); }
const clap_plugin_state_t STATE_EXT = {stateSave, stateLoad};

// --- preset-load (fabriekspresets uit de preset-discovery-factory) -------------------------------------------
bool presetLaad(const clap_plugin_t* pl, uint32_t soort, const char* locatie, const char* sleutel) {
  return zelf(pl)->laadPreset(soort, locatie, sleutel);
}
const clap_plugin_preset_load_t PRESET_LOAD_EXT = {presetLaad};

// --- audio-ports: één stereo in, één stereo uit -----------------------------------------------------------------
uint32_t poortenCount(const clap_plugin_t*, bool) { return 1; }
bool poortenGet(const clap_plugin_t*, uint32_t i, bool isIn, clap_audio_port_info_t* info) {
  if (i != 0) return false;
  std::memset(info, 0, sizeof *info);
  info->id = 0;
  std::strncpy(info->name, isIn ? "Ingang" : "Uitgang", CLAP_NAME_SIZE - 1);
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = 0;
  return true;
}
const clap_plugin_audio_ports_t POORTEN_EXT = {poortenCount, poortenGet};

// --- latency en tail ---------------------------------------------------------------------------------------
uint32_t latentie(const clap_plugin_t* pl) { return zelf(pl)->ontwerp->latentie(); }
const clap_plugin_latency_t LATENTIE_EXT = {latentie};
uint32_t staart(const clap_plugin_t* pl) { return zelf(pl)->staart.load(std::memory_order_relaxed); }
const clap_plugin_tail_t STAART_EXT = {staart};

// --- plugin ------------------------------------------------------------------------------------------------
bool plInit(const clap_plugin_t* pl) {
  Plugin* z = zelf(pl);
  z->hostParams = static_cast<const clap_host_params_t*>(z->host->get_extension(z->host, CLAP_EXT_PARAMS));
  z->paneel.host = z->host;
  z->paneel.hostParams = z->hostParams;
  z->ontwerp.reset(new Ontwerp());
  for (int i = 0; i < N; ++i) z->ontwerp->zetParam(i, z->waarde[i]);
  return true;
}
void plDestroy(const clap_plugin_t* pl) {
  if (const clap_plugin_gui_t* g = dub::paneelGui()) g->destroy(pl);  // een host die het paneel open liet
  delete zelf(pl);
}
bool plActivate(const clap_plugin_t* pl, double sr, uint32_t minFrames, uint32_t maxFrames) {
  (void)minFrames;
  Plugin* z = zelf(pl);
  z->sr = sr;
  z->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);
  pb::Geheugen g;
  g.p = z->geheugen.data();
  g.over = (unsigned long)z->geheugen.size();
  z->ontwerp->activeer(sr, maxFrames, g);
  z->neemStateOver();
  for (int i = 0; i < N; ++i) z->ontwerp->zetParam(i, z->waarde[i]);
  z->kopieIn.assign(maxFrames > 0 ? maxFrames : 1, 0.0f);
  z->zetStaart();
  z->actief.store(true);
  return true;
}
void plDeactivate(const clap_plugin_t* pl) {
  Plugin* z = zelf(pl);
  z->actief.store(false);
  z->neemStateOver();
}
bool plStart(const clap_plugin_t*) { return true; }
void plStop(const clap_plugin_t*) {}
void plReset(const clap_plugin_t* pl) { zelf(pl)->ontwerp->reset(); }
clap_process_status plProcess(const clap_plugin_t* pl, const clap_process_t* pr) { return zelf(pl)->process(pr); }
const void* plExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE_EXT;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD) || !std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &PRESET_LOAD_EXT;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &POORTEN_EXT;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENTIE_EXT;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &STAART_EXT;
  if (!std::strcmp(id, CLAP_EXT_GUI)) return dub::paneelGui();  // nullptr waar geen paneel is (alleen macOS heeft er een)
  return nullptr;
}
void plMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  auto* z = new Plugin();
  z->host = host;
  z->clap = {&DESC, z, plInit, plDestroy, plActivate, plDeactivate, plStart, plStop, plReset, plProcess, plExtension,
             plMainThread};
  return &z->clap;
}

// --- factory -----------------------------------------------------------------------------------------------
uint32_t fabCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* fabDesc(const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &DESC : nullptr; }
const clap_plugin_t* fabCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!host || !id || std::strcmp(id, DESC.id) != 0) return nullptr;
  if (!clap_version_is_compatible(host->clap_version)) return nullptr;
  return maakPlugin(host);
}
const clap_plugin_factory_t FABRIEK = {fabCount, fabDesc, fabCreate};

}  // namespace

bool dub_echo_init(const char*) { return true; }
void dub_echo_deinit(void) {}
const void* dub_echo_get_factory(const char* id) {
  if (id && !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return &FABRIEK;
  if (id && (!std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) || !std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT)))
    return dub::presetFabriek();
  return nullptr;
}
