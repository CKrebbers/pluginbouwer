// toetsenpad.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_toetsenpad.h.
//
// Fase 0 (skelet): een instrument zonder audio-ingang, één stereo-uitgang en één note port (alleen het CLAP-dialect,
// valkuil 19). Het Ontwerp wordt gemaakt en in activate van geheugen voorzien, maar process schrijft nog stilte: de
// DSP aansluiten is fase 1, zodat de null-test bewijst dat het aansluiten klopt.
//
// Draden (ARCHITECTUUR.md "Threads"): init, activate en de extensies draaien op de mainthread; process alleen op de
// audiothread. In process geen allocatie, locks of syscalls (RTSan bewaakt het in CI).
#include <clap/clap.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "ontwerpen/keten_toetsenpad.h"
#include "toetsenpad_entry.h"

#ifdef PB_KANARIE
// Alleen in de kanarie-build van de RTSan-job: een malloc in process die RTSan MOET vangen (valkuil 4). De pointer
// gaat naar een globale variabele, zodat de compiler de malloc niet weg kan laten.
void* volatile pbKanarieGeheugen = nullptr;
#endif

namespace {

using pb::Ontwerp;

constexpr uint32_t KANALEN = 2;

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT, Ontwerp::META.clapId, Ontwerp::META.naam, Ontwerp::META.maker, Ontwerp::META.url, "", "",
    Ontwerp::META.versie, Ontwerp::META.uitleg, FEATURES};

struct Toetsenpad {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  Ontwerp ontwerp;
  std::vector<unsigned char> geheugen;  // het blok voor het Ontwerp, gereserveerd in activate (mainthread)
  bool actief = false;
};

Toetsenpad* zelf(const clap_plugin_t* p) { return static_cast<Toetsenpad*>(p->plugin_data); }

// --- audio-ports: 0 ingangen, 1 stereo-uitgang (SPEC.md "Instrument") ---------------------------------------------
uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (isInput || index != 0) return false;
  std::memset(info, 0, sizeof(*info));
  info->id = 0;
  std::strncpy(info->name, "Uit", sizeof(info->name) - 1);
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = KANALEN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {audioPortsCount, audioPortsGet};

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

// --- latency -------------------------------------------------------------------------------------------------------
uint32_t latencyGet(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY = {latencyGet};

// --- plugin --------------------------------------------------------------------------------------------------------
bool pluginInit(const clap_plugin_t*) { return true; }

void pluginDestroy(const clap_plugin_t* p) { delete zelf(p); }

bool pluginActivate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t maxFrames) {
  (void)minFrames;
  auto* t = zelf(p);
  t->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);
  pb::Geheugen g;
  g.p = t->geheugen.data();
  g.over = (unsigned long)t->geheugen.size();
  t->ontwerp.activeer(sr, maxFrames, g);
  t->actief = true;
  return true;
}

void pluginDeactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }

bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* p) { zelf(p)->ontwerp.reset(); }

clap_process_status pluginProcess(const clap_plugin_t*, const clap_process_t* pr) {
#ifdef PB_KANARIE
  pbKanarieGeheugen = std::malloc(64);
#endif
  // Fase 0: stilte op elke uitgang. Events (noten, parameters) worden pas in fase 1 gelezen.
  for (uint32_t b = 0; b < pr->audio_outputs_count; ++b) {
    const clap_audio_buffer_t& uit = pr->audio_outputs[b];
    for (uint32_t c = 0; c < uit.channel_count; ++c) {
      if (uit.data32 && uit.data32[c]) std::memset(uit.data32[c], 0, sizeof(float) * pr->frames_count);
      if (uit.data64 && uit.data64[c]) std::memset(uit.data64[c], 0, sizeof(double) * pr->frames_count);
    }
  }
  return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
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
  return !std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) ? &FACTORY : nullptr;
}
