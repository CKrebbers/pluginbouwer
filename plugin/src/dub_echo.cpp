// dub_echo.cpp — de CLAP-schil om het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
//
// Fase 1: het Ontwerp draait in process. Parameters zijn CLAP-params (id = het nummer uit SPEC.md, bereik en
// standaard uit Ontwerp::PARAMS); een parameter-event wordt op zijn sample toegepast (het blok wordt bij elk event
// gesplitst). latency en tail komen uit het Ontwerp, state is het formaat uit ARCHITECTUUR.md.
//
// Threads: het Ontwerp wordt alleen aangeraakt door process/flush (audiothread, of de mainthread als er niet
// verwerkt wordt; CLAP garandeert dat die twee nooit tegelijk lopen) en door activate (dan loopt process niet).
// De mainthread (get_value, state) leest een kopie van de waarden in atomics. Een state-load op de mainthread zet
// de nieuwe waarden in die atomics plus een bit in `vuil`; process, flush of activate geeft ze door aan het Ontwerp.
// Geen locks, geen allocatie in process.
//
// PB_KANARIE (alleen voor de RTSan-toets in CI): process doet een malloc. RTSan MOET dat vangen; doet hij het niet,
// dan bewaakt de RTSan-stap niets.
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
constexpr int N_PARAMS = pb::Ontwerp::N_PARAMS;
static_assert(N_PARAMS <= 32, "vuil is een 32-bits masker");

// state (ARCHITECTUUR.md): "PBST", versie u32, aantal u32, dan per parameter id u32 + waarde f64; little-endian
constexpr unsigned char STATE_MAGIE[4] = {'P', 'B', 'S', 'T'};
constexpr uint32_t STATE_VERSIE = 1;

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

// parameter-id → index in Ontwerp::PARAMS (de DSP rekent met de index), -1 als het id niet bestaat
int indexVan(clap_id id) {
  for (int i = 0; i < N_PARAMS; ++i)
    if (pb::Ontwerp::PARAMS[i].id == id) return i;
  return -1;
}

double klem(double v, const pb::ParamInfo& p) { return v < p.min ? p.min : (v > p.max ? p.max : v); }

void zetLE32(unsigned char* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
uint32_t leesLE32(const unsigned char* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i);
  return v;
}
void zetLE64(unsigned char* p, double d) {
  uint64_t v;
  std::memcpy(&v, &d, 8);
  for (int i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
double leesLE64(const unsigned char* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
  double d;
  std::memcpy(&d, &v, 8);
  return d;
}

struct DubEcho {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;
  std::unique_ptr<pb::Ontwerp> ontwerp;
  std::vector<unsigned char> geheugen;  // het blok voor de vertragingslijnen; alleen in activate gereserveerd
  double sr = 48000;

  std::atomic<double> waarde[N_PARAMS];   // wat de host ziet (get_value, state)
  std::atomic<uint32_t> vuil{0};          // bit i: waarde[i] is op de mainthread gezet en nog niet in het Ontwerp
  std::atomic<uint32_t> staart{0};        // tail in samples (UINT32_MAX = oneindig), na elk blok bijgewerkt

  DubEcho() {
    for (int i = 0; i < N_PARAMS; ++i) waarde[i].store(pb::Ontwerp::PARAMS[i].standaard);
  }

  // --- levensloop ---------------------------------------------------------------------------------------------
  bool init() {
    ontwerp.reset(new (std::nothrow) pb::Ontwerp());
    if (!ontwerp) return false;
    hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
    return true;
  }

  bool activate(double samplerate, uint32_t minFrames, uint32_t maxFrames) {
    (void)minFrames;
    sr = samplerate;
    geheugen.assign(pb::Ontwerp::geheugenNodig(sr), 0);
    pb::Geheugen g;
    g.p = geheugen.data();
    g.over = geheugen.size();
    ontwerp->activeer(sr, maxFrames, g);
    neemVuil();
    werkStaartBij();
    return true;
  }

  void deactivate() {}

  void reset() { ontwerp->reset(); }

  // --- parameters ---------------------------------------------------------------------------------------------
  // waarden die de mainthread zette (state) doorgeven aan het Ontwerp; alleen waar process niet tegelijk loopt
  void neemVuil() {
    uint32_t m = vuil.exchange(0);
    for (int i = 0; m; ++i, m >>= 1)
      if (m & 1u) ontwerp->zetParam(i, waarde[i].load());
  }

  // één event uit process of flush
  void verwerkEvent(const clap_event_header_t* h) {
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
    auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
    int i = indexVan(e->param_id);
    if (i < 0) return;
    double v = klem(e->value, pb::Ontwerp::PARAMS[i]);
    waarde[i].store(v);
    ontwerp->zetParam(i, v);
  }

  void flush(const clap_input_events_t* in) {
    neemVuil();
    const uint32_t n = in ? in->size(in) : 0;
    for (uint32_t k = 0; k < n; ++k) verwerkEvent(in->get(in, k));
  }

  void werkStaartBij() {
    double s = ontwerp->staartSec();
    uint32_t t;
    if (s < 0) t = UINT32_MAX;
    else {
      double x = std::ceil(s * sr);
      t = x >= 4294967294.0 ? 4294967294u : (uint32_t)x;
    }
    staart.store(t);
  }

  // --- audio --------------------------------------------------------------------------------------------------
  clap_process_status process(const clap_process_t* pr) {
#ifdef PB_KANARIE
    pb_kanarie = std::malloc(64);
#endif
    neemVuil();
    const uint32_t n = pr->frames_count;
    const clap_input_events_t* ev = pr->in_events;
    const uint32_t nEv = ev ? ev->size(ev) : 0;

    float* uit[KANALEN] = {nullptr, nullptr};
    const float* in[KANALEN] = {nullptr, nullptr};
    int kanalen = 0;
    if (pr->audio_outputs_count >= 1 && pr->audio_outputs[0].data32) {
      const clap_audio_buffer_t& bu = pr->audio_outputs[0];
      const clap_audio_buffer_t* bi =
          (pr->audio_inputs_count >= 1 && pr->audio_inputs[0].data32) ? &pr->audio_inputs[0] : nullptr;
      kanalen = (int)(bu.channel_count < KANALEN ? bu.channel_count : KANALEN);
      for (int c = 0; c < kanalen; ++c) {
        uit[c] = bu.data32[c];
        in[c] = (bi && (uint32_t)c < bi->channel_count) ? bi->data32[c] : nullptr;
        if (!in[c]) {  // geen invoer voor dit kanaal: stilte erin (in-place, de DSP leest vóór hij schrijft)
          std::memset(uit[c], 0, n * sizeof(float));
          in[c] = uit[c];
        }
      }
      for (uint32_t c = KANALEN; c < bu.channel_count; ++c)
        if (bu.data32[c]) std::memset(bu.data32[c], 0, n * sizeof(float));
    }

    // het blok splitsen bij elk event: tot de sample van het event rekenen, dan de waarde zetten
    uint32_t pos = 0;
    for (uint32_t k = 0; k < nEv; ++k) {
      const clap_event_header_t* h = ev->get(ev, k);
      uint32_t t = h->time < n ? h->time : n;
      if (t > pos) {
        verwerk(in, uit, kanalen, pos, t - pos);
        pos = t;
      }
      verwerkEvent(h);
    }
    if (pos < n) verwerk(in, uit, kanalen, pos, n - pos);

    werkStaartBij();
    return CLAP_PROCESS_CONTINUE;
  }

  void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t vanaf, uint32_t m) {
    if (kanalen <= 0) return;  // geen uitgang (mag niet bij onze audio-ports, maar een host kan het sturen)
    const float* i2[KANALEN];
    float* u2[KANALEN];
    for (int c = 0; c < kanalen; ++c) {
      i2[c] = in[c] + vanaf;
      u2[c] = uit[c] + vanaf;
    }
    ontwerp->verwerk(i2, u2, kanalen, m);
  }

  // --- state --------------------------------------------------------------------------------------------------
  bool bewaar(const clap_ostream_t* s) {
    unsigned char b[12 + N_PARAMS * 12];
    std::memcpy(b, STATE_MAGIE, 4);
    zetLE32(b + 4, STATE_VERSIE);
    zetLE32(b + 8, (uint32_t)N_PARAMS);
    for (int i = 0; i < N_PARAMS; ++i) {
      zetLE32(b + 12 + i * 12, pb::Ontwerp::PARAMS[i].id);
      zetLE64(b + 16 + i * 12, waarde[i].load());
    }
    size_t klaar = 0;
    while (klaar < sizeof b) {
      int64_t r = s->write(s, b + klaar, sizeof b - klaar);
      if (r <= 0) return false;
      klaar += (size_t)r;
    }
    return true;
  }

  static bool leesAlles(const clap_istream_t* s, unsigned char* p, size_t n) {
    size_t klaar = 0;
    while (klaar < n) {
      int64_t r = s->read(s, p + klaar, n - klaar);
      if (r <= 0) return false;
      klaar += (size_t)r;
    }
    return true;
  }

  bool laad(const clap_istream_t* s) {
    unsigned char kop[12];
    if (!leesAlles(s, kop, sizeof kop) || std::memcmp(kop, STATE_MAGIE, 4)) return false;
    // de versie bepaalt (nu nog) niets aan de opbouw: elke versie is een lijst (id, waarde)
    const uint32_t aantal = leesLE32(kop + 8);
    if (aantal > 100000) return false;
    double nieuw[N_PARAMS];
    for (int i = 0; i < N_PARAMS; ++i) nieuw[i] = pb::Ontwerp::PARAMS[i].standaard;  // ontbrekend id: standaard
    for (uint32_t k = 0; k < aantal; ++k) {
      unsigned char r[12];
      if (!leesAlles(s, r, sizeof r)) return false;
      int i = indexVan(leesLE32(r));
      double v = leesLE64(r + 4);
      if (i < 0 || !std::isfinite(v)) continue;  // onbekend id (nieuwere versie): overslaan
      nieuw[i] = klem(v, pb::Ontwerp::PARAMS[i]);
    }
    for (int i = 0; i < N_PARAMS; ++i) waarde[i].store(nieuw[i]);
    vuil.fetch_or((N_PARAMS == 32) ? 0xffffffffu : ((1u << N_PARAMS) - 1u));
    if (hostParams) {
      hostParams->rescan(host, CLAP_PARAM_RESCAN_VALUES);
      hostParams->request_flush(host);
    }
    return true;
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

// --- params ---------------------------------------------------------------------------------------------------
uint32_t paramsAantal(const clap_plugin_t*) { return (uint32_t)N_PARAMS; }
bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)N_PARAMS) return false;
  const pb::ParamInfo& p = pb::Ontwerp::PARAMS[index];
  std::memset(info, 0, sizeof(*info));
  info->id = p.id;
  info->flags = 0;
  if (p.vlaggen & pb::AUTOMATEERBAAR) info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
  if (p.vlaggen & pb::STAPPEN) info->flags |= CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof(info->name), "%s", p.naam);
  info->module[0] = '\0';
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}
bool paramsWaarde(const clap_plugin_t* pl, clap_id id, double* v) {
  int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(pl)->waarde[i].load();
  return true;
}
// tekst ↔ waarde: getal plus eenheid; fase 2 maakt dit af (ms/Hz/dB-opmaak, stapjes)
bool paramsNaarTekst(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t grootte) {
  int i = indexVan(id);
  if (i < 0 || grootte == 0) return false;
  const char* e = pb::Ontwerp::PARAMS[i].eenheid;
  std::snprintf(uit, grootte, (e && *e) ? "%.17g %s" : "%.17g", v, e);
  return true;
}
bool paramsVanTekst(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  int i = indexVan(id);
  if (i < 0 || !tekst) return false;
  char* eind = nullptr;
  double x = std::strtod(tekst, &eind);
  if (eind == tekst || !std::isfinite(x)) return false;
  *v = klem(x, pb::Ontwerp::PARAMS[i]);
  return true;
}
void paramsFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t*) {
  zelf(pl)->flush(in);
}
const clap_plugin_params_t PARAMS = {paramsAantal, paramsInfo, paramsWaarde, paramsNaarTekst, paramsVanTekst,
                                     paramsFlush};

// --- state ----------------------------------------------------------------------------------------------------
bool stateBewaar(const clap_plugin_t* p, const clap_ostream_t* s) { return zelf(p)->bewaar(s); }
bool stateLaad(const clap_plugin_t* p, const clap_istream_t* s) { return zelf(p)->laad(s); }
const clap_plugin_state_t STATE = {stateBewaar, stateLaad};

// --- latency en tail ------------------------------------------------------------------------------------------
uint32_t latentie(const clap_plugin_t* p) { return zelf(p)->ontwerp->latentie(); }
const clap_plugin_latency_t LATENCY = {latentie};

uint32_t staartLees(const clap_plugin_t* p) { return zelf(p)->staart.load(); }
const clap_plugin_tail_t TAIL = {staartLees};

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
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
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
