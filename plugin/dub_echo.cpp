// dub_echo.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
// Fase 1: de DSP is aangesloten. Parameters zijn CLAP-params (id = nummer in SPEC.md, waarden in hun eigen eenheid),
// events gaan op hun sample (het blok wordt bij elk event gesplitst), state zoals ARCHITECTUUR.md.
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "entry.h"
#include "ontwerpen/keten_dub_echo.h"

namespace {

using pb::Ontwerp;

#ifdef PB_KANARIE
// De kanarie voor RTSan: een allocatie in process, met de pointer in een globale variabele zodat de compiler hem
// niet weg kan laten. Een RTSan-run die deze build niet afbreekt, bewaakt niets.
void* volatile g_kanarie = nullptr;
#endif

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO,
                                nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT,       Ontwerp::META.clapId, Ontwerp::META.naam, Ontwerp::META.maker, Ontwerp::META.url, "", "",
    Ontwerp::META.versie,    Ontwerp::META.uitleg, FEATURES};

constexpr uint32_t KANALEN = 2;
constexpr int N = Ontwerp::N_PARAMS;
constexpr uint32_t STATE_VERSIE = 1;

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp ontwerp;                        // groot: Plugin wordt op de heap gemaakt (main thread)
  std::vector<unsigned char> geheugen;    // vooraf gereserveerd in activate, nooit in process
  std::vector<float> kopie;               // invoerkopie (vooraf gereserveerd) als in- en uitgang buffers delen
  double sr = 48000;
  bool actief = false;                    // alleen de main thread leest en schrijft dit
  std::atomic<double> staart{0.0};        // uitgerekend op de audiothread / in activate, gelezen door tail.get
  // De waarde per parameter zoals de host hem ziet (get_value, state save). De DSP krijgt zijn waarden alleen via
  // events (process/flush) of, bij een state load terwijl hij draait, via `teLaden` aan het begin van process.
  std::atomic<double> waarde[N];
  std::atomic<bool> teLaden{false};
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

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

// zet één parameter in de DSP en in de host-kopie (audiothread, of main thread als de plugin niet draait)
void zet(Plugin* s, int i, double v) {
  v = klem(i, v);
  s->ontwerp.zetParam(i, v);
  s->waarde[i].store(v, std::memory_order_relaxed);
}

void doeEvent(Plugin* s, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
  const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
  const int i = indexVan(e->param_id);
  if (i >= 0) zet(s, i, e->value);
}

void laadUitHostKopie(Plugin* s) {
  for (int i = 0; i < N; ++i) s->ontwerp.zetParam(i, s->waarde[i].load(std::memory_order_relaxed));
}

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* p) { delete zelf(p); }

bool activate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t maxFrames) {
  Plugin* s = zelf(p);
  s->sr = sr;
  s->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);
  s->kopie.assign((size_t)KANALEN * (maxFrames > 0 ? maxFrames : 1), 0.0f);
  pb::Geheugen g;
  g.p = s->geheugen.data();
  g.over = (unsigned long)s->geheugen.size();
  s->ontwerp.activeer(sr, minFrames, g);
  if (s->teLaden.exchange(false)) laadUitHostKopie(s);
  s->staart.store(s->ontwerp.staartSec());
  s->actief = true;
  return true;
}
void deactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { zelf(p)->ontwerp.reset(); }

clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
#ifdef PB_KANARIE
  g_kanarie = std::malloc(64);
#endif
  Plugin* s = zelf(p);
  if (s->teLaden.exchange(false, std::memory_order_acquire)) laadUitHostKopie(s);

  const uint32_t n = pr->frames_count;
  const clap_input_events_t* ev = pr->in_events;
  const uint32_t nEv = ev ? ev->size(ev) : 0;
  uint32_t e = 0;

  float* uit[KANALEN] = {nullptr, nullptr};
  const float* in[KANALEN] = {nullptr, nullptr};
  bool audio = pr->audio_outputs_count >= 1 && pr->audio_outputs[0].data32 && pr->audio_outputs[0].channel_count > 0;
  if (audio) {
    const clap_audio_buffer_t& u = pr->audio_outputs[0];
    for (uint32_t c = 0; c < KANALEN; ++c) uit[c] = u.data32[c < u.channel_count ? c : u.channel_count - 1];
    const clap_audio_buffer_t* ib = pr->audio_inputs_count > 0 ? &pr->audio_inputs[0] : nullptr;
    for (uint32_t c = 0; c < KANALEN; ++c)
      in[c] = (ib && ib->data32 && ib->channel_count > 0) ? ib->data32[c < ib->channel_count ? c : ib->channel_count - 1]
                                                         : nullptr;
    for (uint32_t c = 0; c < KANALEN; ++c) if (!uit[c]) audio = false;
  }
  // De DSP schrijft per sample kanaal 0 vóór hij kanaal 1 leest. Deelt een invoerbuffer zijn geheugen met een
  // uitgang van een ander kanaal (of ontbreekt hij), dan rekenen we op een vooraf gereserveerde kopie.
  bool viaKopie = false;
  if (audio) {
    for (uint32_t c = 0; c < KANALEN; ++c) {
      if (!in[c]) viaKopie = true;
      for (uint32_t d = 0; d < KANALEN; ++d) if (d != c && in[c] == uit[d]) viaKopie = true;
    }
  }
  const uint32_t stuk = viaKopie ? (uint32_t)(s->kopie.size() / KANALEN) : n;

  uint32_t pos = 0;
  while (pos < n || e < nEv) {
    // alle events op of vóór deze sample toepassen
    while (e < nEv) {
      const clap_event_header_t* h = ev->get(ev, e);
      if (h->time > pos && pos < n) break;
      doeEvent(s, h);
      ++e;
    }
    if (pos >= n) break;
    uint32_t tot = n;
    if (e < nEv) {
      const uint32_t t = ev->get(ev, e)->time;
      if (t < tot) tot = t;
    }
    if (tot - pos > stuk) tot = pos + stuk;
    const uint32_t m = tot - pos;
    if (audio) {
      if (viaKopie) {
        float* k[KANALEN];
        for (uint32_t c = 0; c < KANALEN; ++c) {
          k[c] = s->kopie.data() + (size_t)c * stuk;
          if (in[c]) std::memcpy(k[c], in[c] + pos, m * sizeof(float));
          else std::memset(k[c], 0, m * sizeof(float));
        }
        const float* ki[KANALEN] = {k[0], k[1]};
        float* ku[KANALEN] = {uit[0] + pos, uit[1] + pos};
        s->ontwerp.verwerk(ki, ku, (int)KANALEN, m);
      } else {
        const float* ki[KANALEN] = {in[0] + pos, in[1] + pos};
        float* ku[KANALEN] = {uit[0] + pos, uit[1] + pos};
        s->ontwerp.verwerk(ki, ku, (int)KANALEN, m);
      }
    }
    pos = tot;
  }
  s->staart.store(s->ontwerp.staartSec(), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

// --- params ---------------------------------------------------------------------------------------------------
uint32_t paramsCount(const clap_plugin_t*) { return (uint32_t)N; }
bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)N) return false;
  const auto& p = Ontwerp::PARAMS[index];
  std::memset(info, 0, sizeof *info);
  info->id = p.id;
  info->flags = (p.vlaggen & pb::AUTOMATEERBAAR) ? CLAP_PARAM_IS_AUTOMATABLE : 0;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof info->name, "%s", p.naam);
  info->module[0] = 0;
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}
bool paramsValue(const clap_plugin_t* pl, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(pl)->waarde[i].load(std::memory_order_relaxed);
  return true;
}
bool paramsToText(const clap_plugin_t*, clap_id id, double v, char* t, uint32_t cap) {
  const int i = indexVan(id);
  if (i < 0 || cap == 0) return false;
  const char* eenheid = Ontwerp::PARAMS[i].eenheid;
  std::snprintf(t, cap, *eenheid ? "%.6g %s" : "%.6g", v, eenheid);
  return true;
}
bool paramsFromText(const clap_plugin_t*, clap_id id, const char* t, double* v) {
  if (indexVan(id) < 0 || !t) return false;
  char* eind = nullptr;
  const double x = std::strtod(t, &eind);
  if (eind == t || !std::isfinite(x)) return false;
  *v = x;
  return true;
}
void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
  Plugin* s = zelf(p);
  if (s->teLaden.exchange(false)) laadUitHostKopie(s);
  const uint32_t n = in ? in->size(in) : 0;
  for (uint32_t i = 0; i < n; ++i) doeEvent(s, in->get(in, i));
}
const clap_plugin_params_t PARAMS_EXT = {paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush};

// --- state ----------------------------------------------------------------------------------------------------
// "PBST", versie (u32), aantal (u32), dan per parameter (id u32, waarde f64); little-endian.
void zet32(unsigned char* b, uint32_t v) { for (int k = 0; k < 4; ++k) b[k] = (unsigned char)(v >> (8 * k)); }
uint32_t lees32(const unsigned char* b) {
  uint32_t v = 0;
  for (int k = 0; k < 4; ++k) v |= (uint32_t)b[k] << (8 * k);
  return v;
}
bool schrijfAlles(const clap_ostream_t* os, const unsigned char* b, size_t n) {
  while (n > 0) {
    const int64_t w = os->write(os, b, n);
    if (w <= 0) return false;
    b += w; n -= (size_t)w;
  }
  return true;
}
bool leesAlles(const clap_istream_t* is, unsigned char* b, size_t n) {
  while (n > 0) {
    const int64_t r = is->read(is, b, n);
    if (r <= 0) return false;
    b += r; n -= (size_t)r;
  }
  return true;
}

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* os) {
  const Plugin* s = zelf(p);
  unsigned char b[12 + 12 * N];
  std::memcpy(b, "PBST", 4);
  zet32(b + 4, STATE_VERSIE);
  zet32(b + 8, (uint32_t)N);
  for (int i = 0; i < N; ++i) {
    unsigned char* q = b + 12 + 12 * i;
    zet32(q, Ontwerp::PARAMS[i].id);
    const double v = s->waarde[i].load(std::memory_order_relaxed);
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    zet32(q + 4, (uint32_t)bits);
    zet32(q + 8, (uint32_t)(bits >> 32));
  }
  return schrijfAlles(os, b, sizeof b);
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* is) {
  Plugin* s = zelf(p);
  unsigned char kop[12];
  if (!leesAlles(is, kop, sizeof kop) || std::memcmp(kop, "PBST", 4)) return false;
  const uint32_t versie = lees32(kop + 4), aantal = lees32(kop + 8);
  if (versie < 1 || aantal > 100000) return false;
  double nieuw[N];
  for (int i = 0; i < N; ++i) nieuw[i] = Ontwerp::PARAMS[i].standaard;  // een ontbrekend id houdt zijn standaard
  for (uint32_t k = 0; k < aantal; ++k) {
    unsigned char q[12];
    if (!leesAlles(is, q, sizeof q)) return false;
    const uint64_t bits = (uint64_t)lees32(q + 4) | ((uint64_t)lees32(q + 8) << 32);
    double v;
    std::memcpy(&v, &bits, 8);
    const int i = indexVan(lees32(q));
    if (i >= 0) nieuw[i] = klem(i, v);  // een onbekend id (nieuwere versie) wordt overgeslagen
  }
  for (int i = 0; i < N; ++i) s->waarde[i].store(nieuw[i], std::memory_order_relaxed);
  if (s->actief) {
    s->teLaden.store(true, std::memory_order_release);  // de audiothread neemt ze over aan het begin van process
  } else {
    laadUitHostKopie(s);
  }
  if (s->host) {
    auto* hp = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
    if (hp && hp->rescan) hp->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
  }
  return true;
}
const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- audio-ports ----------------------------------------------------------------------------------------------
uint32_t portsCount(const clap_plugin_t*, bool) { return 1; }
bool portsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (index != 0) return false;
  info->id = 0;
  std::snprintf(info->name, sizeof info->name, "%s", isInput ? "Ingang" : "Uitgang");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = KANALEN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = 0;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {portsCount, portsGet};

// --- latency en tail ------------------------------------------------------------------------------------------
uint32_t latencyGet(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY = {latencyGet};

uint32_t tailGet(const clap_plugin_t* p) {
  const Plugin* s = zelf(p);
  const double sec = s->staart.load();
  if (sec < 0) return UINT32_MAX;  // oneindig (lusversterking >= 1)
  const double samples = sec * s->sr;
  if (!(samples < 4.0e9)) return UINT32_MAX;
  return samples > 0 ? (uint32_t)(samples + 0.5) : 0;
}
const clap_plugin_tail_t TAIL = {tailGet};

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  return nullptr;
}
void onMainThread(const clap_plugin_t*) {}

// --- factory --------------------------------------------------------------------------------------------------
uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id)) return nullptr;
  auto* s = new (std::nothrow) Plugin();
  if (!s) return nullptr;
  s->host = host;
  for (int i = 0; i < N; ++i) s->waarde[i].store(Ontwerp::PARAMS[i].standaard);
  s->clap = {&DESCRIPTOR, s, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset,
             process, getExtension, onMainThread};
  return &s->clap;
}
const clap_plugin_factory_t FACTORY = {factoryCount, factoryDescriptor, factoryCreate};

}  // namespace

bool pb_entry_init(const char*) { return true; }
void pb_entry_deinit(void) {}
const void* pb_entry_get_factory(const char* id) {
  return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &FACTORY : nullptr;
}
