// dub_echo.cpp — de CLAP-schil om het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
//
// Fase 0 (skelet): descriptor en AU-codes uit het Ontwerp, één stereo in- en uitgang, geheugen voor de DSP wordt in
// activate gereserveerd (nooit in process). process geeft de invoer nog ongewijzigd door; de DSP sluit fase 1 aan.
//
// PB_KANARIE (alleen voor de RTSan-toets in CI): process doet een malloc. RTSan MOET dat vangen; doet hij het niet,
// dan bewaakt de RTSan-stap niets.
#include <clap/clap.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include "ontwerpen/keten_dub_echo.h"
#include "entry.h"

#ifdef PB_KANARIE
void* volatile pb_kanarie = nullptr;  // globaal, zodat de compiler de malloc niet weg kan laten
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
  std::unique_ptr<pb::Ontwerp> ontwerp;
  std::vector<unsigned char> geheugen;  // het blok voor de vertragingslijnen; alleen in activate gereserveerd

  // --- levensloop ---------------------------------------------------------------------------------------------
  bool init() {
    ontwerp.reset(new (std::nothrow) pb::Ontwerp());
    return ontwerp != nullptr;
  }

  bool activate(double sr, uint32_t minFrames, uint32_t maxFrames) {
    (void)minFrames;
    geheugen.assign(pb::Ontwerp::geheugenNodig(sr), 0);
    pb::Geheugen g;
    g.p = geheugen.data();
    g.over = geheugen.size();
    ontwerp->activeer(sr, maxFrames, g);
    return true;
  }

  void deactivate() {}

  void reset() { ontwerp->reset(); }

  // --- audio --------------------------------------------------------------------------------------------------
  clap_process_status process(const clap_process_t* pr) {
#ifdef PB_KANARIE
    pb_kanarie = std::malloc(64);
#endif
    const uint32_t n = pr->frames_count;
    if (pr->audio_outputs_count < 1) return CLAP_PROCESS_CONTINUE;
    const clap_audio_buffer_t& uit = pr->audio_outputs[0];
    const clap_audio_buffer_t* in = pr->audio_inputs_count > 0 ? &pr->audio_inputs[0] : nullptr;
    for (uint32_t c = 0; c < uit.channel_count; ++c) {
      float* u = uit.data32 ? uit.data32[c] : nullptr;
      if (!u) continue;
      const float* i = (in && in->data32 && c < in->channel_count) ? in->data32[c] : nullptr;
      if (!i) std::memset(u, 0, n * sizeof(float));
      else if (i != u) std::memcpy(u, i, n * sizeof(float));
    }
    return CLAP_PROCESS_CONTINUE;
  }
};

DubEcho* zelf(const clap_plugin_t* p) { return static_cast<DubEcho*>(p->plugin_data); }

// --- audio-ports ----------------------------------------------------------------------------------------------
uint32_t poortenAantal(const clap_plugin_t*, bool) { return 1; }
bool poortenInfo(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (index != 0) return false;
  std::memset(info, 0, sizeof(*info));
  info->id = isInput ? 0 : 1;
  std::strncpy(info->name, isInput ? "In" : "Uit", sizeof(info->name) - 1);
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = KANALEN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = isInput ? 1 : 0;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {poortenAantal, poortenInfo};

// --- latency --------------------------------------------------------------------------------------------------
uint32_t latentie(const clap_plugin_t* p) { return zelf(p)->ontwerp->latentie(); }
const clap_plugin_latency_t LATENCY = {latentie};

// --- clap_plugin_t --------------------------------------------------------------------------------------------
bool plInit(const clap_plugin_t* p) { return zelf(p)->init(); }
void plDestroy(const clap_plugin_t* p) { delete zelf(p); }
bool plActivate(const clap_plugin_t* p, double sr, uint32_t mn, uint32_t mx) { return zelf(p)->activate(sr, mn, mx); }
void plDeactivate(const clap_plugin_t* p) { zelf(p)->deactivate(); }
bool plStart(const clap_plugin_t*) { return true; }
void plStop(const clap_plugin_t*) {}
void plReset(const clap_plugin_t* p) { zelf(p)->reset(); }
clap_process_status plProcess(const clap_plugin_t* p, const clap_process_t* pr) { return zelf(p)->process(pr); }
const void* plExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  return nullptr;
}
void plMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  auto* d = new (std::nothrow) DubEcho();
  if (!d) return nullptr;
  d->host = host;
  d->plugin = {&DESCRIPTOR, d,           plInit,    plDestroy,   plActivate, plDeactivate,
               plStart,     plStop,      plReset,   plProcess,   plExtension, plMainThread};
  return &d->plugin;
}

// --- factory --------------------------------------------------------------------------------------------------
uint32_t fabAantal(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* fabDescriptor(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* fabMaak(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id)) return nullptr;
  return maakPlugin(host);
}
const clap_plugin_factory_t FACTORY = {fabAantal, fabDescriptor, fabMaak};

}  // namespace

bool pb_entry_init(const char*) { return true; }
void pb_entry_deinit(void) {}
const void* pb_entry_get_factory(const char* factory_id) {
  if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &FACTORY;
  return nullptr;
}
