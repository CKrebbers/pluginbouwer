// plugin.cpp — de CLAP-schil van Toetsenpad (fase 0: skelet). Een instrument zonder audio-ingang: één stereo-uitgang en
// één note port (alleen het CLAP-dialect, valkuil 19). Het Ontwerp uit dsp/ krijgt in activate zijn geheugen, maar
// process schrijft in deze fase stilte: de DSP aansluiten is fase 1, zodat de null-test het bewijst.
#include <clap/clap.h>
#include <cstdlib>
#include <cstring>
#include <new>

#include "ontwerpen/keten_toetsenpad.h"
#include "entry.h"

#ifdef PB_KANARIE
// RTSan-kanarie: een malloc in process die RTSan MOET vangen (valkuil 4). De pointer gaat naar een globale variabele,
// zodat de compiler de malloc niet wegoptimaliseert. Alleen in het CI-doel toetsenpad_kanarie.
void* volatile pbKanariePointer = nullptr;
#endif

namespace {

using pb::Ontwerp;

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp* ontwerp = nullptr;
  unsigned char* geheugen = nullptr;
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

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

// --- audio-ports: 0 ingangen, 1 stereo-uitgang -------------------------------------------------------------
uint32_t apCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }
bool apGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (isInput || index != 0) return false;
  info->id = 0;
  std::strncpy(info->name, "Uit", sizeof(info->name));
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {apCount, apGet};

// --- note-ports: één ingang, alleen het CLAP-dialect ----------------------------------------------------------
uint32_t npCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
bool npGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
  if (!isInput || index != 0) return false;
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  std::strncpy(info->name, "Noten", sizeof(info->name));
  return true;
}
const clap_plugin_note_ports_t NOTE_PORTS = {npCount, npGet};

// --- latency ------------------------------------------------------------------------------------------------
uint32_t latGet(const clap_plugin_t* p) { return zelf(p)->ontwerp->latentie(); }
const clap_plugin_latency_t LATENCY = {latGet};

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->ontwerp = new (std::nothrow) Ontwerp();
  return s->ontwerp != nullptr;
}

void destroy(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  delete s->ontwerp;
  std::free(s->geheugen);
  delete s;
}

bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t maxFrames) {
  Plugin* s = zelf(p);
  const unsigned long n = Ontwerp::geheugenNodig(sr);
  std::free(s->geheugen);
  s->geheugen = static_cast<unsigned char*>(std::calloc(n ? n : 1, 1));
  if (!s->geheugen) return false;
  pb::Geheugen g;
  g.p = s->geheugen;
  g.over = n;
  s->ontwerp->activeer(sr, maxFrames, g);
  return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { zelf(p)->ontwerp->reset(); }

clap_process_status process(const clap_plugin_t*, const clap_process_t* pr) {
#ifdef PB_KANARIE
  pbKanariePointer = std::malloc(16);
#endif
  for (uint32_t b = 0; b < pr->audio_outputs_count; ++b) {
    const clap_audio_buffer_t& uit = pr->audio_outputs[b];
    for (uint32_t c = 0; c < uit.channel_count; ++c) {
      if (uit.data32 && uit.data32[c]) std::memset(uit.data32[c], 0, sizeof(float) * pr->frames_count);
      if (uit.data64 && uit.data64[c]) std::memset(uit.data64[c], 0, sizeof(double) * pr->frames_count);
    }
    pr->audio_outputs[b].constant_mask = ~(uint64_t)0;
  }
  return CLAP_PROCESS_CONTINUE;
}

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  return nullptr;
}

void onMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  Plugin* s = new (std::nothrow) Plugin();
  if (!s) return nullptr;
  s->host = host;
  s->clap = {&DESCRIPTOR, s, init, destroy, activate, deactivate, startProcessing, stopProcessing,
             reset, process, getExtension, onMainThread};
  return &s->clap;
}

// --- factory ------------------------------------------------------------------------------------------------
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

// Geen globale std::string die init vult (valkuil 16): de AU roept init mogelijk al tijdens de statische initialisatie aan.
bool pb_entry_init(const char*) { return true; }
void pb_entry_deinit() {}
const void* pb_entry_get_factory(const char* id) {
  return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &FACTORY;
}
