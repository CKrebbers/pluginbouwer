// dub_echo.cpp — de CLAP-schil rond het Ontwerp uit dsp/ (ARCHITECTUUR.md). De DSP zelf staat in dsp/ en wordt
// hier alleen aangeroepen: parameters gaan erin via zetParam, audio via verwerk.
//
// Threads: process (audiothread) is de enige die het Ontwerp aanraakt zolang de plugin actief is. De mainthread
// leest parameterwaarden uit een atomaire kopie (`waarde`) en geeft een geladen state door via `wacht` + `wachtMasker`.
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "dub_echo_entry.h"
#include "ontwerpen/keten_dub_echo.h"

namespace {

using pb::Ontwerp;
constexpr int N = Ontwerp::N_PARAMS;
static_assert(N <= 32, "wachtMasker is 32 bits");

#ifdef DUB_ECHO_KANARIE
// RTSan-kanarie (CLAUDE.md valkuil 4): een malloc in process, de pointer naar een globale variabele zodat de
// compiler hem niet weg kan halen. pb-render met -fsanitize=realtime MOET hierop afbreken.
void* volatile kanarie = nullptr;
#endif

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO,
                                nullptr};

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

int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id) return i;
  return -1;
}

double klem(int i, double v) {
  const auto& p = Ontwerp::PARAMS[i];
  if (!(v >= p.min)) return p.min;  // ook NaN
  if (v > p.max) return p.max;
  return v;
}

struct Plugin {
  clap_plugin_t clap;
  const clap_host_t* host;
  Ontwerp* ontwerp = nullptr;

  // alleen mainthread
  bool actief = false;
  double sr = 48000;
  std::vector<unsigned char> geheugen;
  std::vector<float> stilte;  // invoer als de host geen invoerbuffer geeft

  // gedeeld, atomair
  std::atomic<double> waarde[N];      // huidige doelwaarde per parameter (voor get_value en state)
  std::atomic<double> wacht[N];       // een geladen state die process nog moet overnemen
  std::atomic<uint32_t> wachtMasker{0};
  std::atomic<uint32_t> staartFrames{0};

  Plugin() {
    for (int i = 0; i < N; ++i) {
      waarde[i].store(Ontwerp::PARAMS[i].standaard);
      wacht[i].store(Ontwerp::PARAMS[i].standaard);
    }
  }

  void zet(int i, double v) {  // op de thread die het Ontwerp nu bezit
    v = klem(i, v);
    ontwerp->zetParam(i, v);
    waarde[i].store(v, std::memory_order_relaxed);
  }

  void neemWachtendeOver() {
    uint32_t m = wachtMasker.exchange(0, std::memory_order_acquire);
    for (int i = 0; m && i < N; ++i)
      if (m & (1u << i)) zet(i, wacht[i].load(std::memory_order_relaxed));
  }

  void verwerkEvents(const clap_input_events_t* in) {
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t e = 0; e < n; ++e) verwerkEvent(in->get(in, e));
  }

  void verwerkEvent(const clap_event_header_t* h) {
    if (!h || h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
    const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
    int i = indexVan(ev->param_id);
    if (i >= 0) zet(i, ev->value);
  }

  void werkStaartBij() {
    double s = ontwerp->staartSec();
    uint32_t f;
    if (s < 0) f = UINT32_MAX;
    else {
      double fr = std::ceil(s * sr);
      f = fr >= (double)UINT32_MAX ? UINT32_MAX : (uint32_t)fr;
    }
    staartFrames.store(f, std::memory_order_relaxed);
  }
};

Plugin* van(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// --- audio-ports ----------------------------------------------------------------------------------------------------
uint32_t portsCount(const clap_plugin_t*, bool) { return 1; }
bool portsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (index != 0) return false;
  info->id = 0;
  std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "In" : "Uit");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = 0;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {portsCount, portsGet};

// --- params ---------------------------------------------------------------------------------------------------------
uint32_t paramsCount(const clap_plugin_t*) { return N; }
bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)N) return false;
  const auto& p = Ontwerp::PARAMS[index];
  std::memset(info, 0, sizeof(*info));
  info->id = p.id;
  info->flags = 0;
  if (p.vlaggen & pb::AUTOMATEERBAAR) info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
  if (p.vlaggen & pb::STAPPEN) info->flags |= CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof(info->name), "%s", p.naam);
  info->module[0] = 0;
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}
bool paramsValue(const clap_plugin_t* pl, clap_id id, double* v) {
  int i = indexVan(id);
  if (i < 0) return false;
  *v = van(pl)->waarde[i].load(std::memory_order_relaxed);
  return true;
}
bool paramsValueToText(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t n) {
  int i = indexVan(id);
  if (i < 0 || n == 0) return false;
  const char* e = Ontwerp::PARAMS[i].eenheid;
  if (e && *e) std::snprintf(uit, n, "%.4g %s", v, e);
  else std::snprintf(uit, n, "%.4g", v);
  return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  int i = indexVan(id);
  if (i < 0 || !tekst) return false;
  char* eind = nullptr;
  double w = std::strtod(tekst, &eind);
  if (eind == tekst) return false;
  *v = w;
  return true;
}
void paramsFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t*) {
  // actief: op de audiothread (CLAP), anders op de mainthread; in beide gevallen bezit deze thread het Ontwerp
  Plugin* p = van(pl);
  p->neemWachtendeOver();
  p->verwerkEvents(in);
}
const clap_plugin_params_t PARAMS = {paramsCount, paramsInfo, paramsValue, paramsValueToText, paramsTextToValue,
                                     paramsFlush};

// --- state (ARCHITECTUUR.md: "PBST", versie, aantal, dan per parameter id u32 + waarde f64) --------------------------
constexpr uint32_t STATE_VERSIE = 1;

bool schrijfAlles(const clap_ostream_t* s, const void* p, size_t n) {
  const auto* c = static_cast<const unsigned char*>(p);
  while (n > 0) {
    int64_t w = s->write(s, c, n);
    if (w <= 0) return false;
    c += w;
    n -= (size_t)w;
  }
  return true;
}
bool stateSave(const clap_plugin_t* pl, const clap_ostream_t* s) {
  Plugin* p = van(pl);
  unsigned char buf[12 + N * 12];
  std::memcpy(buf, "PBST", 4);
  uint32_t versie = STATE_VERSIE, aantal = N;
  std::memcpy(buf + 4, &versie, 4);
  std::memcpy(buf + 8, &aantal, 4);
  for (int i = 0; i < N; ++i) {
    uint32_t id = Ontwerp::PARAMS[i].id;
    double v = (p->wachtMasker.load() & (1u << i)) ? p->wacht[i].load() : p->waarde[i].load();
    std::memcpy(buf + 12 + i * 12, &id, 4);
    std::memcpy(buf + 16 + i * 12, &v, 8);
  }
  return schrijfAlles(s, buf, sizeof(buf));
}
bool stateLoad(const clap_plugin_t* pl, const clap_istream_t* s) {
  Plugin* p = van(pl);
  std::vector<unsigned char> d;
  unsigned char buf[4096];
  for (;;) {
    int64_t r = s->read(s, buf, sizeof(buf));
    if (r < 0) return false;
    if (r == 0) break;
    d.insert(d.end(), buf, buf + r);
    if (d.size() > (1u << 20)) return false;
  }
  if (d.size() < 12 || std::memcmp(d.data(), "PBST", 4) != 0) return false;
  uint32_t versie, aantal;
  std::memcpy(&versie, d.data() + 4, 4);
  std::memcpy(&aantal, d.data() + 8, 4);
  if (versie < 1 || d.size() < 12 + (size_t)aantal * 12) return false;
  // ontbrekende id's houden hun standaard; onbekende id's (nieuwere versie) worden overgeslagen
  double nieuw[N];
  for (int i = 0; i < N; ++i) nieuw[i] = Ontwerp::PARAMS[i].standaard;
  for (uint32_t k = 0; k < aantal; ++k) {
    uint32_t id;
    double v;
    std::memcpy(&id, d.data() + 12 + k * 12, 4);
    std::memcpy(&v, d.data() + 16 + k * 12, 8);
    int i = indexVan(id);
    if (i >= 0) nieuw[i] = klem(i, v);
  }
  if (p->actief) {
    // het Ontwerp is van de audiothread: geef de waarden door, process (of flush) neemt ze over
    for (int i = 0; i < N; ++i) {
      p->wacht[i].store(nieuw[i], std::memory_order_relaxed);
      p->waarde[i].store(nieuw[i], std::memory_order_relaxed);
    }
    p->wachtMasker.fetch_or((1u << N) - 1u, std::memory_order_release);
    if (auto* hp = static_cast<const clap_host_params_t*>(p->host->get_extension(p->host, CLAP_EXT_PARAMS)))
      hp->request_flush(p->host);
  } else {
    for (int i = 0; i < N; ++i) p->zet(i, nieuw[i]);
  }
  if (auto* hp = static_cast<const clap_host_params_t*>(p->host->get_extension(p->host, CLAP_EXT_PARAMS)))
    hp->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
  return true;
}
const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- latency en tail ------------------------------------------------------------------------------------------------
uint32_t latencyGet(const clap_plugin_t* pl) { return van(pl)->ontwerp->latentie(); }
const clap_plugin_latency_t LATENCY = {latencyGet};

uint32_t tailGet(const clap_plugin_t* pl) { return van(pl)->staartFrames.load(std::memory_order_relaxed); }
const clap_plugin_tail_t TAIL = {tailGet};

// --- plugin ---------------------------------------------------------------------------------------------------------
bool pInit(const clap_plugin_t*) { return true; }
void pDestroy(const clap_plugin_t* pl) {
  Plugin* p = van(pl);
  delete p->ontwerp;
  delete p;
}
bool pActivate(const clap_plugin_t* pl, double sr, uint32_t minFrames, uint32_t maxFrames) {
  Plugin* p = van(pl);
  p->sr = sr;
  // geheugen hier, nooit in process (SPEC.md "Budgetten"); op nul, zoals de WASM-schil het doet
  p->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);
  pb::Geheugen g;
  g.p = p->geheugen.data();
  g.over = p->geheugen.size();
  p->ontwerp->activeer(sr, maxFrames, g);
  p->stilte.assign(maxFrames > 0 ? maxFrames : 1, 0.0f);
  (void)minFrames;
  p->neemWachtendeOver();
  p->werkStaartBij();
  p->actief = true;
  return true;
}
void pDeactivate(const clap_plugin_t* pl) { van(pl)->actief = false; }
bool pStartProcessing(const clap_plugin_t*) { return true; }
void pStopProcessing(const clap_plugin_t*) {}
void pReset(const clap_plugin_t* pl) { van(pl)->ontwerp->reset(); }

clap_process_status pProcess(const clap_plugin_t* pl, const clap_process_t* pr) {
  Plugin* p = van(pl);
#ifdef DUB_ECHO_KANARIE
  kanarie = std::malloc(16);
#endif
  p->neemWachtendeOver();

  const uint32_t n = pr->frames_count;
  float* uit[2] = {nullptr, nullptr};
  const float* in[2] = {p->stilte.data(), p->stilte.data()};
  uint32_t kUit = 0;
  if (pr->audio_outputs_count > 0 && pr->audio_outputs[0].data32) {
    kUit = pr->audio_outputs[0].channel_count;
    for (uint32_t c = 0; c < kUit && c < 2; ++c) uit[c] = pr->audio_outputs[0].data32[c];
  }
  if (pr->audio_inputs_count > 0 && pr->audio_inputs[0].data32) {
    const uint32_t kIn = pr->audio_inputs[0].channel_count;
    for (uint32_t c = 0; c < 2 && kIn > 0; ++c) in[c] = pr->audio_inputs[0].data32[c < kIn ? c : kIn - 1];
  }
  const int kanalen = kUit > 2 ? 2 : (int)kUit;

  // events op hun sample: het blok splitsen bij elk event (de DSP rekent per sample, dus de uitvoer verandert niet)
  const clap_input_events_t* ev = pr->in_events;
  const uint32_t nEv = ev ? ev->size(ev) : 0;
  uint32_t pos = 0;
  auto verwerkTot = [&](uint32_t tot) {
    if (tot <= pos) return;
    if (kanalen > 0) {
      const float* ip[2] = {in[0] + pos, in[1] + pos};
      float* up[2] = {uit[0] + pos, uit[1] ? uit[1] + pos : nullptr};
      p->ontwerp->verwerk(ip, up, kanalen, tot - pos);
    }
    pos = tot;
  };
  for (uint32_t e = 0; e < nEv; ++e) {  // CLAP: events staan op volgorde van tijd
    const clap_event_header_t* h = ev->get(ev, e);
    verwerkTot(h->time < n ? h->time : n);
    p->verwerkEvent(h);
  }
  verwerkTot(n);
  for (uint32_t c = 2; c < kUit; ++c)
    if (pr->audio_outputs[0].data32[c]) std::memset(pr->audio_outputs[0].data32[c], 0, n * sizeof(float));

  p->werkStaartBij();
  return CLAP_PROCESS_CONTINUE;
}

const void* pGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  return nullptr;
}
void pOnMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maak(const clap_host_t* host) {
  auto* p = new Plugin();
  p->host = host;
  p->ontwerp = new Ontwerp();
  for (int i = 0; i < N; ++i) p->ontwerp->zetParam(i, Ontwerp::PARAMS[i].standaard);
  p->clap.desc = &DESCRIPTOR;
  p->clap.plugin_data = p;
  p->clap.init = pInit;
  p->clap.destroy = pDestroy;
  p->clap.activate = pActivate;
  p->clap.deactivate = pDeactivate;
  p->clap.start_processing = pStartProcessing;
  p->clap.stop_processing = pStopProcessing;
  p->clap.reset = pReset;
  p->clap.process = pProcess;
  p->clap.get_extension = pGetExtension;
  p->clap.on_main_thread = pOnMainThread;
  return &p->clap;
}

// --- factory --------------------------------------------------------------------------------------------------------
uint32_t fabCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* fabDescriptor(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* fabCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id) != 0) return nullptr;
  return maak(host);
}
const clap_plugin_factory_t FACTORY = {fabCount, fabDescriptor, fabCreate};

}  // namespace

bool dub_echo_entry_init(const char*) { return true; }
void dub_echo_entry_deinit() {}
const void* dub_echo_entry_get_factory(const char* id) {
  return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &FACTORY : nullptr;
}
