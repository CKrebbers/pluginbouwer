// dub_echo.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
// Fase 0: het skelet. process geeft de invoer ongewijzigd door; de DSP wordt in fase 1 aangesloten
// (dan bewijst de null-test dat de plugin klinkt als de WASM van de tool).
#include <clap/clap.h>

#include <atomic>
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

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp ontwerp;                        // groot: Plugin wordt op de heap gemaakt (main thread)
  std::vector<unsigned char> geheugen;    // vooraf gereserveerd in activate, nooit in process
  double sr = 48000;
  std::atomic<double> staart{0.0};        // uitgerekend op de audiothread / in activate, gelezen door tail.get
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* p) { delete zelf(p); }

bool activate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t) {
  Plugin* s = zelf(p);
  s->sr = sr;
  s->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);
  pb::Geheugen g;
  g.p = s->geheugen.data();
  g.over = (unsigned long)s->geheugen.size();
  s->ontwerp.activeer(sr, minFrames, g);
  s->staart.store(0.0);  // fase 0: de invoer gaat ongewijzigd door, dus geen staart
  return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { zelf(p)->ontwerp.reset(); }

clap_process_status process(const clap_plugin_t*, const clap_process_t* pr) {
#ifdef PB_KANARIE
  g_kanarie = std::malloc(64);
#endif
  const uint32_t n = pr->frames_count;
  if (pr->audio_outputs_count < 1) return CLAP_PROCESS_CONTINUE;
  const clap_audio_buffer_t& uit = pr->audio_outputs[0];
  const clap_audio_buffer_t* in = pr->audio_inputs_count > 0 ? &pr->audio_inputs[0] : nullptr;
  if (!uit.data32) return CLAP_PROCESS_CONTINUE;
  for (uint32_t c = 0; c < uit.channel_count; ++c) {
    float* d = uit.data32[c];
    if (!d) continue;
    if (in && in->data32 && in->channel_count > 0) {
      const float* b = in->data32[c < in->channel_count ? c : in->channel_count - 1];
      if (b && b != d) std::memmove(d, b, n * sizeof(float));
      else if (!b) std::memset(d, 0, n * sizeof(float));
    } else {
      std::memset(d, 0, n * sizeof(float));
    }
  }
  return CLAP_PROCESS_CONTINUE;
}

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
  const double samples = s->staart.load() * s->sr;
  if (!(samples < 4.0e9)) return UINT32_MAX;  // oneindig (lusversterking >= 1)
  return samples > 0 ? (uint32_t)(samples + 0.5) : 0;
}
const clap_plugin_tail_t TAIL = {tailGet};

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
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
