// dub_echo.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
//
// Fase 0 (skelet): de plugin laadt, meldt stereo in/uit en latency, reserveert in activate het geheugen van het
// Ontwerp en zet het klaar. process geeft de invoer nog ongewijzigd door; de DSP en de parameters komen in fase 1.
// De DSP in dsp/ is gegeven: hier wordt hij alleen aangeroepen, nooit aangepast.
#include <clap/clap.h>

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

struct DubEcho {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  pb::Ontwerp* ontwerp = nullptr;  // groot (vaste toestand per knoop): op de heap, in init
  unsigned char* geheugen = nullptr;
  unsigned long geheugenGrootte = 0;
};

DubEcho* zelf(const clap_plugin_t* p) { return static_cast<DubEcho*>(p->plugin_data); }

// --- plugin ------------------------------------------------------------------------------------------------

bool pluginInit(const clap_plugin_t* p) {
  auto* z = zelf(p);
  z->ontwerp = new (std::nothrow) pb::Ontwerp();
  return z->ontwerp != nullptr;
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
  pb::Geheugen g;
  g.p = z->geheugen;
  g.over = n;
  z->ontwerp->activeer(sr, maxFrames, g);
  return true;
}

void pluginDeactivate(const clap_plugin_t* p) { geefGeheugenVrij(zelf(p)); }

bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}
void pluginReset(const clap_plugin_t* p) { zelf(p)->ontwerp->reset(); }

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* pr) {
  (void)p;
#ifdef PB_KANARIE
  pbKanarie = std::malloc(64);
#endif
  const uint32_t n = pr->frames_count;
  if (pr->audio_outputs_count < 1) return CLAP_PROCESS_CONTINUE;
  const clap_audio_buffer_t& uit = pr->audio_outputs[0];
  const clap_audio_buffer_t* in = pr->audio_inputs_count > 0 ? &pr->audio_inputs[0] : nullptr;
  // fase 0: doorgeven (in fase 1 komt hier Ontwerp::verwerk, gesplitst op de events)
  for (uint32_t c = 0; c < uit.channel_count; ++c) {
    float* u = uit.data32 ? uit.data32[c] : nullptr;
    if (!u) continue;
    const float* i = (in && in->data32 && in->channel_count > 0)
                         ? in->data32[c < in->channel_count ? c : in->channel_count - 1]
                         : nullptr;
    if (!i) std::memset(u, 0, n * sizeof(float));
    else if (i != u) std::memcpy(u, i, n * sizeof(float));
  }
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

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
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
