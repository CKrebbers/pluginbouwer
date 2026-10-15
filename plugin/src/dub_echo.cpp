// dub_echo.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
//
// Fase 1: process stuurt de audio door het Ontwerp. Parameters zijn CLAP-params (id = nummer in SPEC.md, bereik en
// standaard uit Ontwerp::PARAMS); events worden op hun sample toegepast (het blok wordt bij elk event gesplitst).
// latency en tail komen uit het Ontwerp, state is het binaire formaat uit ARCHITECTUUR.md.
// De DSP in dsp/ is gegeven: hier wordt hij alleen aangeroepen, nooit aangepast.
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "ontwerpen/keten_dub_echo.h"
#include "dub_echo_entry.h"

#ifdef PB_KANARIE
// RTSan-kanarie (alleen in de aparte kanariebouw): een malloc in process, met de pointer in een globale variabele
// zodat de compiler hem niet weg kan optimaliseren. RTSan MOET hierop afbreken; doet hij dat niet, dan bewaakt de
// RTSan-stap in CI niets.
void* volatile pbKanarie = nullptr;
#endif

namespace {

constexpr uint32_t KANALEN = 2;
constexpr int N_PARAMS = pb::Ontwerp::N_PARAMS;

// state (ARCHITECTUUR.md): "PBST", versie (u32), aantal (u32), dan per parameter (id u32, waarde f64); little-endian
constexpr uint32_t STATE_VERSIE = 1;

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO,
                                nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT,
    pb::Ontwerp::META.clapId,
    pb::Ontwerp::META.naam,
    pb::Ontwerp::META.maker,
    pb::Ontwerp::META.url,
    "",
    "",
    pb::Ontwerp::META.versie,
    pb::Ontwerp::META.uitleg,
    FEATURES,
};

static_assert(std::atomic<double>::is_always_lock_free, "atomic<double> moet lock-free zijn (audiothread)");

struct DubEcho {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  pb::Ontwerp* ontwerp = nullptr;  // groot (vaste toestand per knoop): op de heap, in init
  unsigned char* geheugen = nullptr;
  unsigned long geheugenGrootte = 0;
  double sr = 48000;
  bool actief = false;  // alleen op de mainthread gelezen en geschreven

  // De waarden zoals de host ze ziet (get_value, state). De audiothread schrijft ze bij events; de mainthread leest
  // ze. Een geladen state gaat via `laden` naar de audiothread: die zet hem bij het begin van het volgende blok in
  // het Ontwerp. Zo raakt de mainthread het Ontwerp niet aan terwijl de plugin actief is.
  std::atomic<double> waarde[N_PARAMS];
  std::atomic<bool> laden{false};
  std::atomic<uint32_t> staart{0};  // tail in samples, bijgewerkt in process
};

DubEcho* zelf(const clap_plugin_t* p) { return static_cast<DubEcho*>(p->plugin_data); }

int indexVan(clap_id id) {
  for (int i = 0; i < N_PARAMS; ++i)
    if (pb::Ontwerp::PARAMS[i].id == id) return i;
  return -1;
}

double klem(int i, double v) {
  const auto& pi = pb::Ontwerp::PARAMS[i];
  if (!(v >= pi.min)) return pi.min;  // ook NaN
  if (v > pi.max) return pi.max;
  return v;
}

uint32_t staartInSamples(double sec, double sr) {
  if (sec < 0) return UINT32_MAX;  // oneindig (lusversterking ≥ 1)
  const double n = std::ceil(sec * sr);
  return n >= (double)(UINT32_MAX - 1) ? UINT32_MAX - 1 : (uint32_t)n;
}

// Eén CLAP-event toepassen (audiothread, of de mainthread in flush als de plugin niet actief is).
void pasToe(DubEcho* z, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
  const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
  const int i = indexVan(e->param_id);
  if (i < 0) return;
  const double v = klem(i, e->value);
  z->ontwerp->zetParam(i, v);
  z->waarde[i].store(v, std::memory_order_relaxed);
}

void haalStateBinnen(DubEcho* z) {
  if (!z->laden.exchange(false, std::memory_order_acquire)) return;
  for (int i = 0; i < N_PARAMS; ++i) z->ontwerp->zetParam(i, z->waarde[i].load(std::memory_order_relaxed));
}

// --- plugin ------------------------------------------------------------------------------------------------

bool pluginInit(const clap_plugin_t* p) {
  auto* z = zelf(p);
  z->ontwerp = new (std::nothrow) pb::Ontwerp();
  if (!z->ontwerp) return false;
  for (int i = 0; i < N_PARAMS; ++i) {
    z->ontwerp->zetParam(i, pb::Ontwerp::PARAMS[i].standaard);
    z->waarde[i].store(pb::Ontwerp::PARAMS[i].standaard, std::memory_order_relaxed);
  }
  return true;
}

void geefGeheugenVrij(DubEcho* z) {
  delete[] z->geheugen;
  z->geheugen = nullptr;
  z->geheugenGrootte = 0;
}

void pluginDestroy(const clap_plugin_t* p) {
  auto* z = zelf(p);
  geefGeheugenVrij(z);
  delete z->ontwerp;
  delete z;
}

bool pluginActivate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t maxFrames) {
  (void)minFrames;
  auto* z = zelf(p);
  // geheugen vooraf, op de mainthread (SPEC.md "Budgetten"); nooit in process
  geefGeheugenVrij(z);
  const unsigned long n = pb::Ontwerp::geheugenNodig(sr);
  z->geheugen = new (std::nothrow) unsigned char[n ? n : 1]();
  if (!z->geheugen) return false;
  z->geheugenGrootte = n;
  z->sr = sr;
  haalStateBinnen(z);  // de audiothread draait nog niet
  pb::Geheugen g;
  g.p = z->geheugen;
  g.over = n;
  z->ontwerp->activeer(sr, maxFrames, g);
  z->staart.store(staartInSamples(z->ontwerp->staartSec(), sr), std::memory_order_relaxed);
  z->actief = true;
  return true;
}

void pluginDeactivate(const clap_plugin_t* p) {
  auto* z = zelf(p);
  z->actief = false;
  geefGeheugenVrij(z);
}

bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}
void pluginReset(const clap_plugin_t* p) { zelf(p)->ontwerp->reset(); }

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* pr) {
  auto* z = zelf(p);
#ifdef PB_KANARIE
  pbKanarie = std::malloc(64);
#endif
  haalStateBinnen(z);
  const uint32_t n = pr->frames_count;
  const clap_input_events_t* ev = pr->in_events;
  const uint32_t nEv = ev ? ev->size(ev) : 0;

  if (pr->audio_outputs_count < 1 || !pr->audio_outputs[0].data32 || pr->audio_outputs[0].channel_count < 1) {
    for (uint32_t e = 0; e < nEv; ++e) pasToe(z, ev->get(ev, e));  // geen uitgang: alleen de parameters bijhouden
    return CLAP_PROCESS_CONTINUE;
  }
  const clap_audio_buffer_t& uitBuf = pr->audio_outputs[0];
  const clap_audio_buffer_t* inBuf = pr->audio_inputs_count > 0 ? &pr->audio_inputs[0] : nullptr;
  const int kanalen = (int)(uitBuf.channel_count < KANALEN ? uitBuf.channel_count : KANALEN);
  const float* in[KANALEN];
  float* uit[KANALEN];
  for (int c = 0; c < kanalen; ++c) {
    const float* i = (inBuf && inBuf->data32 && inBuf->channel_count > 0)
                         ? inBuf->data32[(uint32_t)c < inBuf->channel_count ? c : inBuf->channel_count - 1]
                         : nullptr;
    uit[c] = uitBuf.data32[c];
    if (!i) {  // geen invoer: stilte, in-place in de uitgang (het Ontwerp leest elke sample vóór het schrijft)
      std::memset(uit[c], 0, n * sizeof(float));
      i = uit[c];
    }
    in[c] = i;
  }

  // het blok splitsen bij elk event: alles vóór het event met de oude waarde, het event op zijn eigen sample
  uint32_t pos = 0, e = 0;
  while (pos < n) {
    while (e < nEv) {
      const clap_event_header_t* h = ev->get(ev, e);
      if (h->time > pos) break;
      pasToe(z, h);
      ++e;
    }
    uint32_t eind = n;
    if (e < nEv) {
      const uint32_t t = ev->get(ev, e)->time;
      if (t < eind) eind = t;
    }
    const float* ip[KANALEN];
    float* up[KANALEN];
    for (int c = 0; c < kanalen; ++c) {
      ip[c] = in[c] + pos;
      up[c] = uit[c] + pos;
    }
    z->ontwerp->verwerk(ip, up, kanalen, eind - pos);
    pos = eind;
  }
  for (; e < nEv; ++e) pasToe(z, ev->get(ev, e));  // events op of na het einde van het blok

  // extra uitgangskanalen (meer dan stereo) stil
  for (uint32_t c = KANALEN; c < uitBuf.channel_count; ++c)
    if (uitBuf.data32[c]) std::memset(uitBuf.data32[c], 0, n * sizeof(float));

  z->staart.store(staartInSamples(z->ontwerp->staartSec(), z->sr), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

// --- audio-ports: één stereo in, één stereo uit, in-place mogelijk ----------------------------------------

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (index != 0) return false;
  info->id = isInput ? 0 : 1;
  std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "In" : "Uit");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = KANALEN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = isInput ? 1 : 0;
  return true;
}

const clap_plugin_audio_ports_t AUDIO_PORTS = {audioPortsCount, audioPortsGet};

// --- latency -----------------------------------------------------------------------------------------------

uint32_t latencyGet(const clap_plugin_t* p) { return zelf(p)->ontwerp->latentie(); }

const clap_plugin_latency_t LATENCY = {latencyGet};

// --- tail --------------------------------------------------------------------------------------------------

uint32_t tailGet(const clap_plugin_t* p) {
  auto* z = zelf(p);
  // niet actief: geen audiothread, het Ontwerp mag hier gelezen worden
  if (!z->actief) return staartInSamples(z->ontwerp->staartSec(), z->sr);
  return z->staart.load(std::memory_order_relaxed);
}

const clap_plugin_tail_t TAIL = {tailGet};

// --- params ------------------------------------------------------------------------------------------------

uint32_t paramsCount(const clap_plugin_t*) { return (uint32_t)N_PARAMS; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)N_PARAMS) return false;
  const auto& pi = pb::Ontwerp::PARAMS[index];
  std::memset(info, 0, sizeof(*info));
  info->id = pi.id;
  info->flags = 0;
  if (pi.vlaggen & pb::AUTOMATEERBAAR) info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
  if (pi.vlaggen & pb::STAPPEN) info->flags |= CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof(info->name), "%s", pi.naam);
  info->module[0] = '\0';
  info->min_value = pi.min;
  info->max_value = pi.max;
  info->default_value = pi.standaard;
  return true;
}

bool paramsGetValue(const clap_plugin_t* p, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(p)->waarde[i].load(std::memory_order_relaxed);
  return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double v, char* tekst, uint32_t grootte) {
  const int i = indexVan(id);
  if (i < 0 || !tekst || grootte == 0) return false;
  const char* eenheid = pb::Ontwerp::PARAMS[i].eenheid;
  if (eenheid && *eenheid) std::snprintf(tekst, grootte, "%.3f %s", v, eenheid);
  else std::snprintf(tekst, grootte, "%.3f", v);
  return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !tekst) return false;
  char* eind = nullptr;
  const double w = std::strtod(tekst, &eind);
  if (eind == tekst || !std::isfinite(w)) return false;
  *v = klem(i, w);
  return true;
}

// [active ? audio-thread : main-thread]: in beide gevallen raakt alleen één thread het Ontwerp aan
void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
  auto* z = zelf(p);
  haalStateBinnen(z);
  if (!in) return;
  const uint32_t n = in->size(in);
  for (uint32_t e = 0; e < n; ++e) pasToe(z, in->get(in, e));
}

const clap_plugin_params_t PARAMS = {paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue,
                                     paramsFlush};

// --- state -------------------------------------------------------------------------------------------------

bool schrijfAlles(const clap_ostream_t* s, const unsigned char* d, uint64_t n) {
  while (n > 0) {
    const int64_t r = s->write(s, d, n);
    if (r <= 0) return false;
    d += r;
    n -= (uint64_t)r;
  }
  return true;
}

bool leesAlles(const clap_istream_t* s, unsigned char* d, uint64_t n) {
  while (n > 0) {
    const int64_t r = s->read(s, d, n);
    if (r <= 0) return false;
    d += r;
    n -= (uint64_t)r;
  }
  return true;
}

void zetU32(unsigned char* d, uint32_t v) {
  for (int b = 0; b < 4; ++b) d[b] = (unsigned char)(v >> (8 * b));
}
uint32_t leesU32(const unsigned char* d) {
  uint32_t v = 0;
  for (int b = 0; b < 4; ++b) v |= (uint32_t)d[b] << (8 * b);
  return v;
}
void zetF64(unsigned char* d, double v) {
  uint64_t u;
  std::memcpy(&u, &v, 8);
  for (int b = 0; b < 8; ++b) d[b] = (unsigned char)(u >> (8 * b));
}
double leesF64(const unsigned char* d) {
  uint64_t u = 0;
  for (int b = 0; b < 8; ++b) u |= (uint64_t)d[b] << (8 * b);
  double v;
  std::memcpy(&v, &u, 8);
  return v;
}

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* s) {
  auto* z = zelf(p);
  unsigned char kop[12] = {'P', 'B', 'S', 'T'};
  zetU32(kop + 4, STATE_VERSIE);
  zetU32(kop + 8, (uint32_t)N_PARAMS);
  if (!schrijfAlles(s, kop, sizeof kop)) return false;
  for (int i = 0; i < N_PARAMS; ++i) {
    unsigned char r[12];
    zetU32(r, pb::Ontwerp::PARAMS[i].id);
    zetF64(r + 4, z->waarde[i].load(std::memory_order_relaxed));
    if (!schrijfAlles(s, r, sizeof r)) return false;
  }
  return true;
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* s) {
  auto* z = zelf(p);
  unsigned char kop[12];
  if (!leesAlles(s, kop, sizeof kop) || std::memcmp(kop, "PBST", 4) != 0) return false;
  // de versie zegt niets over de indeling per parameter: die is vast (id, waarde); nieuwere versies voegen id's toe
  const uint32_t aantal = leesU32(kop + 8);
  if (aantal > 65536) return false;
  double nieuw[N_PARAMS];
  for (int i = 0; i < N_PARAMS; ++i) nieuw[i] = pb::Ontwerp::PARAMS[i].standaard;  // ontbrekend id: standaard
  for (uint32_t k = 0; k < aantal; ++k) {
    unsigned char r[12];
    if (!leesAlles(s, r, sizeof r)) return false;
    const int i = indexVan(leesU32(r));
    if (i < 0) continue;  // onbekend id (van een nieuwere versie): overslaan
    nieuw[i] = klem(i, leesF64(r + 4));
  }
  for (int i = 0; i < N_PARAMS; ++i) z->waarde[i].store(nieuw[i], std::memory_order_relaxed);
  if (z->actief) {
    z->laden.store(true, std::memory_order_release);  // de audiothread zet ze bij het volgende blok (of flush)
  } else {
    for (int i = 0; i < N_PARAMS; ++i) z->ontwerp->zetParam(i, nieuw[i]);
    z->laden.store(false, std::memory_order_relaxed);
  }
  if (z->host) {
    auto* hp = static_cast<const clap_host_params_t*>(z->host->get_extension(z->host, CLAP_EXT_PARAMS));
    if (hp && hp->rescan) hp->rescan(z->host, CLAP_PARAM_RESCAN_VALUES);
  }
  return true;
}

const clap_plugin_state_t STATE = {stateSave, stateLoad};

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  return nullptr;
}

void pluginOnMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  auto* z = new (std::nothrow) DubEcho();
  if (!z) return nullptr;
  z->host = host;
  z->plugin.desc = &DESCRIPTOR;
  z->plugin.plugin_data = z;
  z->plugin.init = pluginInit;
  z->plugin.destroy = pluginDestroy;
  z->plugin.activate = pluginActivate;
  z->plugin.deactivate = pluginDeactivate;
  z->plugin.start_processing = pluginStartProcessing;
  z->plugin.stop_processing = pluginStopProcessing;
  z->plugin.reset = pluginReset;
  z->plugin.process = pluginProcess;
  z->plugin.get_extension = pluginGetExtension;
  z->plugin.on_main_thread = pluginOnMainThread;
  return &z->plugin;
}

// --- factory -----------------------------------------------------------------------------------------------

uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t index) {
  return index == 0 ? &DESCRIPTOR : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version)) return nullptr;
  if (std::strcmp(id, DESCRIPTOR.id) != 0) return nullptr;
  return maakPlugin(host);
}

const clap_plugin_factory_t FACTORY = {factoryCount, factoryDescriptor, factoryCreate};

}  // namespace

bool dub_echo_entry_init(const char*) { return true; }
void dub_echo_entry_deinit(void) {}
const void* dub_echo_entry_get_factory(const char* factoryId) {
  if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) return &FACTORY;
  return nullptr;
}
