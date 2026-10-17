// plugin.cpp — de CLAP-schil rond het Ontwerp uit dsp/ (fase 0: het skelet).
// In deze fase geeft process de invoer door; het Ontwerp wordt al gemaakt en geactiveerd (geheugen vooraf, op de
// mainthread), zodat fase 1 alleen verwerk() hoeft aan te roepen en de parameters hoeft te koppelen.
#include <clap/clap.h>

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

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp* ontwerp = nullptr;
  unsigned char* geheugen = nullptr;
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

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
// Fase 0 heeft nog geen parameters: het aantal is 0. Laden slaat onbekende id's over (vooruit-compatibel).
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
bool stateSave(const clap_plugin_t*, const clap_ostream_t* s) {
  const uint32_t kop[3] = {STATE_MAGIE, STATE_VERSIE, 0};
  return schrijfAlles(s, kop, sizeof kop);
}
bool stateLoad(const clap_plugin_t*, const clap_istream_t* s) {
  uint32_t kop[3];
  if (!leesAlles(s, kop, sizeof kop) || kop[0] != STATE_MAGIE) return false;
  for (uint32_t i = 0; i < kop[2]; ++i) {
    uint32_t id; double waarde;
    if (!leesAlles(s, &id, sizeof id) || !leesAlles(s, &waarde, sizeof waarde)) return false;
    (void)id; (void)waarde;  // nog geen parameters in fase 0
  }
  return true;
}
const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t* p) {
  zelf(p)->ontwerp = new (std::nothrow) Ontwerp();
  return zelf(p)->ontwerp != nullptr;
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
  z->ontwerp->activeer(sr, minFrames, g);
  return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { zelf(p)->ontwerp->reset(); }

clap_process_status process(const clap_plugin_t*, const clap_process_t* pr) {
#if PB_KANARIE
  pb_kanarie = std::malloc(64);
#endif
  if (pr->audio_inputs_count < 1 || pr->audio_outputs_count < 1) return CLAP_PROCESS_CONTINUE;
  const clap_audio_buffer_t& in = pr->audio_inputs[0];
  const clap_audio_buffer_t& uit = pr->audio_outputs[0];
  if (!uit.data32) return CLAP_PROCESS_CONTINUE;
  for (uint32_t c = 0; c < uit.channel_count; ++c) {
    float* u = uit.data32[c];
    if (in.data32 && c < in.channel_count) {
      const float* i = in.data32[c];
      if (i != u) std::memcpy(u, i, pr->frames_count * sizeof(float));
    } else {
      std::memset(u, 0, pr->frames_count * sizeof(float));
    }
  }
  return CLAP_PROCESS_CONTINUE;
}

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
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
