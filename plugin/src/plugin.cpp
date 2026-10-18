// plugin.cpp — de CLAP-schil van PB Toetsenpad (fase 0: skelet).
//
// Een instrument: geen audio-ingang, één stereo-uitgang, één note port (alleen het CLAP-dialect, valkuil 19).
// Het Ontwerp uit dsp/ontwerpen/keten_toetsenpad.h levert naam, id en maker, en krijgt in activate zijn geheugen;
// process schrijft in deze fase nog stilte (de DSP aansluiten is fase 1, de null-test bewijst het dan).
//
// Draden (ARCHITECTUUR.md "Threads"): activate reserveert al het geheugen op de mainthread; process alloceert niet,
// lockt niet en doet geen syscalls (RTSan in CI). Met PB_KANARIE alloceert process wel: die bouw MOET falen in RTSan.
#include <clap/clap.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "ontwerpen/keten_toetsenpad.h"
#include "entry.h"

#ifdef PB_KANARIE
// de kanarie: een allocatie in process, bewaard in een globale variabele zodat de compiler hem niet wegoptimaliseert
void* volatile pbKanarie = nullptr;
#endif

namespace {

using pb::Ontwerp;

constexpr uint32_t KANALEN = 2;

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
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

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp ontwerp;
  std::vector<unsigned char> geheugen;  // het blok van Ontwerp::geheugenNodig, gereserveerd in activate
  bool actief = false;
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// --- audio-ports: 0 ingangen, 1 stereo-uitgang ------------------------------------------------------------------
uint32_t apCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }
bool apGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (isInput || index != 0) return false;
  info->id = 0;
  std::strncpy(info->name, "Uit", sizeof info->name);
  info->name[sizeof info->name - 1] = 0;
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = KANALEN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {apCount, apGet};

// --- note-ports: één ingang, alleen het CLAP-dialect (valkuil 19) ------------------------------------------------
uint32_t npCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
bool npGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
  if (!isInput || index != 0) return false;
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  std::strncpy(info->name, "Noten", sizeof info->name);
  info->name[sizeof info->name - 1] = 0;
  return true;
}
const clap_plugin_note_ports_t NOTE_PORTS = {npCount, npGet};

// --- latency en tail ---------------------------------------------------------------------------------------------
uint32_t latGet(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY = {latGet};

uint32_t tailGet(const clap_plugin_t*) { return 0; }  // fase 0: stilte heeft geen staart
const clap_plugin_tail_t TAIL = {tailGet};

// --- plugin ------------------------------------------------------------------------------------------------------
bool plInit(const clap_plugin_t*) { return true; }

void plDestroy(const clap_plugin_t* p) { delete zelf(p); }

bool plActivate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t) {
  Plugin* s = zelf(p);
  const unsigned long nodig = Ontwerp::geheugenNodig(sr);
  if (s->geheugen.size() < nodig) s->geheugen.assign(nodig, 0);
  pb::Geheugen g;
  g.p = s->geheugen.data();
  g.over = (unsigned long)s->geheugen.size();
  s->ontwerp.activeer(sr, minFrames, g);
  s->actief = true;
  return true;
}

void plDeactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }

bool plStartProcessing(const clap_plugin_t*) { return true; }
void plStopProcessing(const clap_plugin_t*) {}
void plReset(const clap_plugin_t* p) { zelf(p)->ontwerp.reset(); }

clap_process_status plProcess(const clap_plugin_t*, const clap_process_t* pr) {
#ifdef PB_KANARIE
  pbKanarie = std::malloc(64);
#endif
  // fase 0: stilte op elke uitgang; de events (noten, transport) worden nog niet gelezen
  for (uint32_t b = 0; b < pr->audio_outputs_count; ++b) {
    const clap_audio_buffer_t& uit = pr->audio_outputs[b];
    for (uint32_t c = 0; c < uit.channel_count; ++c) {
      if (uit.data32 && uit.data32[c]) std::memset(uit.data32[c], 0, sizeof(float) * pr->frames_count);
      if (uit.data64 && uit.data64[c]) std::memset(uit.data64[c], 0, sizeof(double) * pr->frames_count);
    }
    pr->audio_outputs[b].constant_mask = ~0ull;
  }
  return CLAP_PROCESS_CONTINUE;
}

const void* plGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  return nullptr;
}

void plOnMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  Plugin* s = new (std::nothrow) Plugin();
  if (!s) return nullptr;
  s->host = host;
  s->clap = {&DESCRIPTOR, s,           plInit,    plDestroy,      plActivate,     plDeactivate, plStartProcessing,
             plStopProcessing, plReset, plProcess, plGetExtension, plOnMainThread};
  return &s->clap;
}

// --- factory -----------------------------------------------------------------------------------------------------
uint32_t fabCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* fabDescriptor(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* fabCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id)) return nullptr;
  return maakPlugin(host);
}
const clap_plugin_factory_t FACTORY = {fabCount, fabDescriptor, fabCreate};

}  // namespace

// Geen globale objecten die init vult (valkuil 16): de AU roept init al tijdens de statische initialisatie aan.
bool pbEntryInit(const char*) { return true; }
void pbEntryDeinit() {}
const void* pbEntryGetFactory(const char* factoryId) {
  if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) return &FACTORY;
  return nullptr;
}
