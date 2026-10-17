// plugin.cpp — de CLAP-schil rond het Ontwerp uit dsp/ (fase 1: de DSP aangesloten).
// process roept Ontwerp::verwerk aan en splitst het blok bij elk parameter-event, zodat elke waarde op zijn sample
// ingaat. Parameters: CLAP-params met id, bereik en standaard uit Ontwerp::PARAMS (de DSP neemt de INDEX).
// Threads (ARCHITECTUUR.md): de audiothread schrijft het Ontwerp alleen in process/flush; de mainthread leest en
// schrijft parameterwaarden via een atomische spiegel, en een state-load gaat via een vlag naar de audiothread.
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "entry.h"
#include "ontwerpen/keten_dub_echo.h"

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

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp* ontwerp = nullptr;
  unsigned char* geheugen = nullptr;
  double sr = 48000;
  bool actief = false;                          // mainthread
  std::atomic<double> waarde[N];                // spiegel van elke parameter (doelwaarde), voor get_value en state
  std::atomic<bool> stateNieuw{false};          // mainthread laadde state terwijl actief: process neemt de spiegel over
  std::atomic<uint32_t> staart{0};              // in samples; berekend op de audiothread (staartSec leest smoothers)
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// de INDEX in PARAMS bij een parameter-id, of -1
int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id) return i;
  return -1;
}
double klem(int i, double v) {
  const auto& pi = Ontwerp::PARAMS[i];
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
void neemStateOver(Plugin* z) {
  if (!z->stateNieuw.exchange(false, std::memory_order_acquire)) return;
  for (int i = 0; i < N; ++i) z->ontwerp->zetParam(i, z->waarde[i].load(std::memory_order_relaxed));
}

// --- params -------------------------------------------------------------------------------------------------
uint32_t paramAantal(const clap_plugin_t*) { return N; }
bool paramInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)N) return false;
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
// tekst in gewone eenheden; fase 2 maakt dit af (stapjes, ms/s, kHz)
bool paramNaarTekst(const clap_plugin_t*, clap_id id, double v, char* tekst, uint32_t grootte) {
  const int i = indexVan(id);
  if (i < 0 || grootte == 0) return false;
  const char* eenheid = Ontwerp::PARAMS[i].eenheid;
  std::snprintf(tekst, grootte, *eenheid ? "%.4g %s" : "%.4g", v, eenheid);
  return true;
}
bool paramUitTekst(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !tekst) return false;
  char* eind = nullptr;
  const double w = std::strtod(tekst, &eind);
  if (eind == tekst || !std::isfinite(w)) return false;
  *v = klem(i, w);
  return true;
}
void paramFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
  // [actief ? audiothread : mainthread]; in beide gevallen draait process niet tegelijk
  Plugin* z = zelf(p);
  neemStateOver(z);
  if (in) {
    const uint32_t n = in->size(in);
    for (uint32_t k = 0; k < n; ++k) doeEvent(z, in->get(in, k));
  }
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
bool stateSave(const clap_plugin_t* p, const clap_ostream_t* s) {
  Plugin* z = zelf(p);
  const uint32_t kop[3] = {STATE_MAGIE, STATE_VERSIE, (uint32_t)N};
  if (!schrijfAlles(s, kop, sizeof kop)) return false;
  for (int i = 0; i < N; ++i) {
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
  for (int i = 0; i < N; ++i) z->waarde[i].store(nieuw[i], std::memory_order_relaxed);
  if (z->actief) z->stateNieuw.store(true, std::memory_order_release);  // process (of flush) neemt het over
  else for (int i = 0; i < N; ++i) z->ontwerp->zetParam(i, nieuw[i]);  // niets draait: direct
  // de host moet de nieuwe waarden opnieuw lezen (en, als hij actief is, flush of process aanroepen)
  auto* hp = z->host ? static_cast<const clap_host_params_t*>(z->host->get_extension(z->host, CLAP_EXT_PARAMS)) : nullptr;
  if (hp) {
    if (hp->rescan) hp->rescan(z->host, CLAP_PARAM_RESCAN_VALUES);
    if (z->actief && hp->request_flush) hp->request_flush(z->host);
  }
  return true;
}
const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t* p) {
  Plugin* z = zelf(p);
  z->ontwerp = new (std::nothrow) Ontwerp();
  if (!z->ontwerp) return false;
  for (int i = 0; i < N; ++i) {
    z->waarde[i].store(Ontwerp::PARAMS[i].standaard, std::memory_order_relaxed);
    z->ontwerp->zetParam(i, Ontwerp::PARAMS[i].standaard);
  }
  return true;
}
void destroy(const clap_plugin_t* p) {
  Plugin* z = zelf(p);
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

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
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

bool pb_entry_init(const char*) { return true; }
void pb_entry_deinit() {}
const void* pb_entry_get_factory(const char* id) {
  return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &FACTORY;
}
