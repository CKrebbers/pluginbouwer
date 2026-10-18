// plugin.cpp — de CLAP-schil rond een Ontwerp (dsp/ontwerpen/*.h). clap-wrapper maakt hier VST3 en AUv2 van.
// Hier staat geen klank: alles wat klinkt zit in het Ontwerp, en dat is dezelfde code als de WASM in de browser.
//
// Wat deze schil wel doet: parameters (in hun eigen eenheid, lineair zoals CLAP dat doorgeeft), events op hun sample
// (het blok wordt bij elk event gesplitst; het Ontwerp rekent per sample, dus dat verandert niets aan de uitvoer),
// state met versie en parameter-id's (onbekende id's worden overgeslagen, ontbrekende houden hun standaard),
// latency en tail melden. Een instrument (PB_INSTRUMENT, door de codegen gezet voor een ontwerp zonder ingang) heeft
// geen audio-ingang; met PB_NOTEN speelt het MIDI: noten worden op hun sample Noot en Poort (dsp/pb_noten.h). Een
// ketenontwerp krijgt het transport van de host (tempo en plek in de maat voor de sequencer).
#include <clap/clap.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <new>

#include PB_ONTWERP
#include "pb_noten.h"
#include "entry.h"

namespace {

using pb::Ontwerp;

#ifdef PB_INSTRUMENT
constexpr bool INSTRUMENT = true;
const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO,
                                CLAP_PLUGIN_FEATURE_MONO, nullptr};
#else
constexpr bool INSTRUMENT = false;
const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, CLAP_PLUGIN_FEATURE_MONO,
                                CLAP_PLUGIN_FEATURE_FILTER, nullptr};
#endif

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT, Ontwerp::META.clapId, Ontwerp::META.naam, Ontwerp::META.maker, Ontwerp::META.url, "", "",
    Ontwerp::META.versie, Ontwerp::META.uitleg, FEATURES};

constexpr uint32_t STATE_MAGIC = 0x54534250;  // "PBST"
constexpr uint32_t STATE_VERSIE = 1;
// Bypass is een parameter van de schil, niet van het Ontwerp (index N_PARAMS): zo kan een host (AU: BypassEffect,
// VST3/CLAP: de bypass-parameter) hem zetten. Uit = het pad van het Ontwerp precies zoals zonder bypass.
constexpr clap_id BYPASS_ID = 1000;
constexpr int NB = Ontwerp::N_PARAMS;       // index van bypass
constexpr int N_ALLE = Ontwerp::N_PARAMS + 1;

struct Plug {
  clap_plugin_t plugin;
  const clap_host_t* host;
  const clap_host_params_t* hostParams = nullptr;
  Ontwerp ontwerp;
  double sr = 48000;
  uint32_t kanalen = 2;
  bool actief = false;
  unsigned char* geheugen = nullptr;   // vóór activate gereserveerd (main thread), zie pb::Geheugen
  unsigned long geheugenGrootte = 0;
  // Draden: zolang de plugin actief is, raakt alleen de audiothread het Ontwerp aan. De mainthread leest en schrijft
  // parameterwaarden via deze spiegel; een geladen state gaat via het vuil-masker naar de audiothread (begin van het
  // volgende process of flush). De staart rekent de audiothread uit (staartSec leest de smoothers).
  std::atomic<double> spiegel[N_ALLE];
  // bypass (audiothread): doel aan/uit, g = aandeel droog 0..1, in 10 ms naar het doel; droog = kopie van de invoer
  bool bypassDoel = false; double bypassG = 0, bypassStap = 0;
  float* droog[2] = {nullptr, nullptr}; uint32_t droogMax = 0;
  std::atomic<uint64_t> vuil{0};
  std::atomic<uint32_t> staartSmp{0};
  pb::NootStapel noten;   // audiothread: welke toetsen er vastgehouden worden (instrument met PB_NOTEN)
};
static_assert(N_ALLE <= 64, "het vuil-masker heeft 64 bits");
static_assert(std::atomic<double>::is_always_lock_free, "de parameterspiegel moet lock-free zijn (real-time)");

#ifdef PB_RTSAN_KANARIE
void* volatile kanarieSink = nullptr;
#endif

Plug* zelf(const clap_plugin_t* p) { return static_cast<Plug*>(p->plugin_data); }

int indexVanId(clap_id id) {
  for (int i = 0; i < Ontwerp::N_PARAMS; ++i)
    if (Ontwerp::PARAMS[i].id == id) return i;
  return id == BYPASS_ID ? NB : -1;
}
// een waarde naar het Ontwerp (of de bypass) op de draad die hem bezit; geeft de waarde terug zoals hij nu staat
double zetIndex(Plug* s, int i, double v) {
  if (i == NB) { s->bypassDoel = v >= 0.5; return s->bypassDoel ? 1.0 : 0.0; }
  s->ontwerp.zetParam(i, v);
  return s->ontwerp.leesParam(i);
}

#ifdef PB_NOTEN
// noten naar de toetsen-knoop. De spiegel blijft staan: een noot is geen parameterwaarde die een host moet bewaren.
void speel(Plug* s) {
  int n = s->noten.boven();
  if (n >= 0) { s->ontwerp.zetParam(PB_NOOT_PARAM, pb::nootVerhouding(n)); s->ontwerp.zetParam(PB_POORT_PARAM, 1.0); }
  else s->ontwerp.zetParam(PB_POORT_PARAM, 0.0);
}
void noot(Plug* s, bool aan, int key) {
  if (key < 0 || key > 127) return;
  if (aan) s->noten.aan(key); else s->noten.uit(key);
  speel(s);
}
#endif
void verwerkEvent(Plug* s, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  if (h->type == CLAP_EVENT_PARAM_VALUE) {
    auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
    int i = indexVanId(ev->param_id);
    if (i >= 0) s->spiegel[i].store(zetIndex(s, i, ev->value), std::memory_order_relaxed);
  }
#ifdef PB_NOTEN
  else if (h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF) {
    auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
    noot(s, h->type == CLAP_EVENT_NOTE_ON, ev->key);
  } else if (h->type == CLAP_EVENT_NOTE_CHOKE) {
    s->noten.leeg(); speel(s);
  } else if (h->type == CLAP_EVENT_MIDI) {
    auto* ev = reinterpret_cast<const clap_event_midi_t*>(h);
    const uint8_t st = ev->data[0] & 0xF0;
    if (st == 0x90 && ev->data[2] > 0) noot(s, true, ev->data[1]);
    else if (st == 0x80 || st == 0x90) noot(s, false, ev->data[1]);
    else if (st == 0xB0 && (ev->data[1] == 123 || ev->data[1] == 120)) { s->noten.leeg(); speel(s); }   // alle noten uit
  }
#endif
}
// een geladen state (mainthread) toepassen op het Ontwerp: alleen vanaf de draad die het Ontwerp nu bezit
void pasVuilToe(Plug* s) {
  uint64_t m = s->vuil.exchange(0, std::memory_order_acquire);
  for (int i = 0; m; ++i, m >>= 1)
    if (m & 1) zetIndex(s, i, s->spiegel[i].load(std::memory_order_relaxed));
}
uint32_t staartNaarSmp(const Plug* s) {
  double t = s->ontwerp.staartSec();
  if (t < 0) return INT32_MAX;  // oneindig (CLAP: INT32_MAX of meer)
  return (uint32_t)(t * s->sr + 0.5);
}

// ---- audio-ports: stereo, of mono als de host dat kiest -------------------------------------------------------------
uint32_t poortenAantal(const clap_plugin_t*, bool isIn) { return (INSTRUMENT && isIn) ? 0 : 1; }
bool poortInfo(const clap_plugin_t* p, uint32_t index, bool isIn, clap_audio_port_info_t* info) {
  if (index != 0 || (INSTRUMENT && isIn)) return false;
  auto* s = zelf(p);
  info->id = 0;
  std::snprintf(info->name, sizeof(info->name), "%s", isIn ? "In" : "Uit");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = s->kanalen;
  info->port_type = s->kanalen == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  info->in_place_pair = 0;  // in-place is veilig: per sample per kanaal eerst lezen, dan schrijven
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {poortenAantal, poortInfo};

uint32_t configAantal(const clap_plugin_t*) { return 2; }
bool configGet(const clap_plugin_t*, uint32_t index, clap_audio_ports_config_t* c) {
  if (index > 1) return false;
  c->id = index;
  std::snprintf(c->name, sizeof(c->name), "%s", index == 0 ? "Stereo" : "Mono");
  c->input_port_count = INSTRUMENT ? 0 : 1; c->output_port_count = 1;
  c->has_main_input = !INSTRUMENT; c->has_main_output = true;
  c->main_input_channel_count = c->main_output_channel_count = index == 0 ? 2 : 1;
  c->main_input_port_type = c->main_output_port_type = index == 0 ? CLAP_PORT_STEREO : CLAP_PORT_MONO;
  return true;
}
bool configSelect(const clap_plugin_t* p, clap_id id) {
  auto* s = zelf(p);
  if (s->actief || id > 1) return false;
  s->kanalen = id == 0 ? 2 : 1;
  return true;
}
const clap_plugin_audio_ports_config_t AUDIO_PORTS_CONFIG = {configAantal, configGet, configSelect};

#ifdef PB_NOTEN
// ---- note ports: één ingang, CLAP-noten ------------------------------------------------------------------------------
// Alleen het CLAP-dialect. Met CLAP_NOTE_DIALECT_MIDI erbij maakt clap-wrapper v0.16.0 in de VST3 voor IMidiMapping
// 16 × 130 "MIDI/controller"-parameters die hij niet in de state bewaart: pluginval "Discrete parameter state
// restoration" faalde erop (18 okt). verwerkEvent leest MIDI-bytes toch: de AU-wrapper geeft CC's (alle noten uit)
// altijd als CLAP_EVENT_MIDI door, ook bij een CLAP-poort; de null-test speelt de noten daarom ook als MIDI.
uint32_t notenAantal(const clap_plugin_t*, bool isIn) { return isIn ? 1 : 0; }
bool notenInfo(const clap_plugin_t*, uint32_t index, bool isIn, clap_note_port_info_t* info) {
  if (index != 0 || !isIn) return false;
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  std::snprintf(info->name, sizeof(info->name), "%s", "Noten");
  return true;
}
const clap_plugin_note_ports_t NOTE_PORTS = {notenAantal, notenInfo};
#endif

// ---- params -------------------------------------------------------------------------------------------------
uint32_t paramAantal(const clap_plugin_t*) { return N_ALLE; }
bool paramInfo(const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
  if (i == (uint32_t)NB) {
    std::memset(info, 0, sizeof(*info));
    info->id = BYPASS_ID;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS;
    std::snprintf(info->name, sizeof(info->name), "Bypass");
    info->min_value = 0; info->max_value = 1; info->default_value = 0;
    return true;
  }
  if (i >= (uint32_t)Ontwerp::N_PARAMS) return false;
  const auto& P = Ontwerp::PARAMS[i];
  std::memset(info, 0, sizeof(*info));
  info->id = P.id;
  info->flags = ((P.vlaggen & pb::AUTOMATEERBAAR) ? CLAP_PARAM_IS_AUTOMATABLE : 0) |
                ((P.vlaggen & pb::STAPPEN) ? CLAP_PARAM_IS_STEPPED : 0);
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof(info->name), "%s", P.naam);
  info->module[0] = 0;
  info->min_value = P.min; info->max_value = P.max; info->default_value = P.standaard;
  return true;
}
bool paramWaarde(const clap_plugin_t* p, clap_id id, double* v) {
  int i = indexVanId(id);
  if (i < 0) return false;
  *v = zelf(p)->spiegel[i].load(std::memory_order_relaxed);
  return true;
}
bool paramNaarTekst(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t n) {
  int i = indexVanId(id);
  if (i < 0) return false;
  if (i == NB) { std::snprintf(uit, n, "%s", v >= 0.5 ? "aan" : "uit"); return true; }
  const auto& P = Ontwerp::PARAMS[i];
  if (P.vlaggen & pb::STAPPEN) std::snprintf(uit, n, "%d", (int)v);
  else if (v >= 1000 && std::strcmp(P.eenheid, "Hz") == 0) std::snprintf(uit, n, "%.2f kHz", v / 1000.0);
  else std::snprintf(uit, n, "%.3g %s", v, P.eenheid);
  return true;
}
bool tekstNaarParam(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  int i = indexVanId(id);
  if (i < 0) return false;
  if (i == NB) { *v = (!std::strcmp(tekst, "aan") || !std::strcmp(tekst, "1") || !std::strcmp(tekst, "on")) ? 1.0 : 0.0; return true; }
  char* eind = nullptr;
  double x = std::strtod(tekst, &eind);
  if (eind == tekst) return false;
  while (*eind == ' ') ++eind;
  if (*eind == 'k') x *= 1000.0;
  const auto& P = Ontwerp::PARAMS[i];
  *v = x < P.min ? P.min : (x > P.max ? P.max : x);
  return true;
}
void paramFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
  auto* s = zelf(p);
  pasVuilToe(s);
  for (uint32_t k = 0, n = in->size(in); k < n; ++k) verwerkEvent(s, in->get(in, k));
}
const clap_plugin_params_t PARAMS_EXT = {paramAantal, paramInfo, paramWaarde, paramNaarTekst, tekstNaarParam, paramFlush};

// ---- state: magic, versie, aantal, dan (id, waarde) ---------------------------------------------------------
bool schrijfAlles(const clap_ostream_t* st, const void* d, int64_t n) {
  auto* c = static_cast<const char*>(d);
  while (n > 0) { int64_t w = st->write(st, c, (uint64_t)n); if (w <= 0) return false; c += w; n -= w; }
  return true;
}
bool leesAlles(const clap_istream_t* st, void* d, int64_t n) {
  auto* c = static_cast<char*>(d);
  while (n > 0) { int64_t r = st->read(st, c, (uint64_t)n); if (r <= 0) return false; c += r; n -= r; }
  return true;
}
bool stateSave(const clap_plugin_t* p, const clap_ostream_t* st) {
  auto* s = zelf(p);
  uint32_t kop[3] = {STATE_MAGIC, STATE_VERSIE, (uint32_t)N_ALLE};
  if (!schrijfAlles(st, kop, sizeof(kop))) return false;
  for (int i = 0; i < N_ALLE; ++i) {
    uint32_t id = i == NB ? BYPASS_ID : Ontwerp::PARAMS[i].id;
    double v = s->spiegel[i].load(std::memory_order_relaxed);
    if (!schrijfAlles(st, &id, 4) || !schrijfAlles(st, &v, 8)) return false;
  }
  return true;
}
bool stateLoad(const clap_plugin_t* p, const clap_istream_t* st) {
  auto* s = zelf(p);
  uint32_t kop[3];
  if (!leesAlles(st, kop, sizeof(kop)) || kop[0] != STATE_MAGIC || kop[1] < 1) return false;
  if (kop[2] > 4096) return false;
  for (uint32_t k = 0; k < kop[2]; ++k) {
    uint32_t id; double v;
    if (!leesAlles(st, &id, 4) || !leesAlles(st, &v, 8)) return false;
    int i = indexVanId(id);
    if (i < 0 || v != v) continue;  // onbekend id = van een nieuwere versie: overslaan
    const double lo = i == NB ? 0.0 : Ontwerp::PARAMS[i].min, hi = i == NB ? 1.0 : Ontwerp::PARAMS[i].max;
    v = v < lo ? lo : (v > hi ? hi : v);
    s->spiegel[i].store(v, std::memory_order_relaxed);
    if (s->actief) s->vuil.fetch_or(1ull << i, std::memory_order_release);
    else zetIndex(s, i, v);
  }
  if (s->hostParams) {
    s->hostParams->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
    if (s->actief) s->hostParams->request_flush(s->host);   // niet aan het verwerken: de host roept dan flush
  }
  return true;
}
const clap_plugin_state_t STATE_EXT = {stateSave, stateLoad};

// ---- latency en tail ---------------------------------------------------------------------------------------
uint32_t latentie(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY_EXT = {latentie};
uint32_t staart(const clap_plugin_t* p) {
  auto* s = zelf(p);
  return s->actief ? s->staartSmp.load(std::memory_order_relaxed) : staartNaarSmp(s);
}
const clap_plugin_tail_t TAIL_EXT = {staart};

// ---- plugin ---------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t* p) {
  auto* s = zelf(p);
  s->hostParams = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
  for (int i = 0; i < Ontwerp::N_PARAMS; ++i) { s->ontwerp.zetParam(i, Ontwerp::PARAMS[i].standaard); s->spiegel[i].store(Ontwerp::PARAMS[i].standaard); }
  s->spiegel[NB].store(0.0);
  return true;
}
void destroy(const clap_plugin_t* p) { auto* s = zelf(p); std::free(s->geheugen); std::free(s->droog[0]); s->~Plug(); std::free(s); }
bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t maxFrames) {
  auto* s = zelf(p);
  s->sr = sr;
  unsigned long n = Ontwerp::geheugenNodig(sr);
  if (n > s->geheugenGrootte) {
    void* nieuw = std::realloc(s->geheugen, n);
    if (!nieuw) return false;
    s->geheugen = static_cast<unsigned char*>(nieuw); s->geheugenGrootte = n;
  }
  if (s->geheugen) std::memset(s->geheugen, 0, s->geheugenGrootte);
  pb::Geheugen g; g.p = s->geheugen; g.over = s->geheugenGrootte;
  if (maxFrames > s->droogMax) {   // ruimte voor de droge kopie (bypass), hier en niet in process
    float* d = static_cast<float*>(std::realloc(s->droog[0], 2ul * maxFrames * sizeof(float)));
    if (!d) return false;
    s->droog[0] = d; s->droog[1] = d + maxFrames; s->droogMax = maxFrames;
  }
  s->bypassStap = 1.0 / (0.010 * sr);
  s->bypassG = s->bypassDoel ? 1.0 : 0.0;
  pasVuilToe(s);
  s->ontwerp.activeer(sr, maxFrames, g);
  s->staartSmp.store(staartNaarSmp(s), std::memory_order_relaxed);
  s->actief = true;
  return true;
}
void deactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { auto* s = zelf(p); s->ontwerp.reset(); s->noten.leeg(); }

clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
  auto* s = zelf(p);
#ifdef PB_RTSAN_KANARIE
  // Alleen in de controlebouw van CI: een opzettelijke allocatie, om te bewijzen dat RTSan hem vangt.
  // De pointer gaat naar een globale variabele: een malloc/free-paar in één functie haalt clang bij -O2 weg,
  // ook met een volatile lokale pointer (gezien: de eerste kanarie werd daardoor nooit gevangen).
  kanarieSink = std::malloc(16);
  std::free(kanarieSink);
#endif
  pasVuilToe(s);   // een state die de mainthread laadde, vóór de events van dit blok
#ifdef PB_HEEFT_TRANSPORT
  if (pr->transport && (pr->transport->flags & CLAP_TRANSPORT_HAS_TEMPO))
    s->ontwerp.zetTransport(pr->transport->tempo,
                            (pr->transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) ? (double)pr->transport->song_pos_beats / (double)CLAP_BEATTIME_FACTOR : 0.0,
                            (pr->transport->flags & CLAP_TRANSPORT_IS_PLAYING) && (pr->transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE));
  else s->ontwerp.zetTransport(0.0, 0.0, false);
#endif
  const uint32_t n = pr->frames_count;
  const uint32_t nEv = pr->in_events->size(pr->in_events);
  uint32_t ev = 0, pos = 0;

  int kIn = pr->audio_inputs_count ? (int)pr->audio_inputs[0].channel_count : 0;
  int kUit = pr->audio_outputs_count ? (int)pr->audio_outputs[0].channel_count : 0;
  int k = kUit < 2 ? kUit : 2;
  if (k == 0) return CLAP_PROCESS_CONTINUE;
  const float* in[2]; float* uit[2];
  static const float stilte[1] = {0};
  for (int c = 0; c < k; ++c) {
    uit[c] = pr->audio_outputs[0].data32[c];
    in[c] = kIn > 0 ? pr->audio_inputs[0].data32[c < kIn ? c : kIn - 1] : nullptr;
  }
  (void)stilte;

  while (pos < n) {
    while (ev < nEv) {
      const auto* h = pr->in_events->get(pr->in_events, ev);
      if (h->time > pos) break;
      verwerkEvent(s, h);
      ++ev;
    }
    uint32_t tot = n;
    if (ev < nEv) { uint32_t t = pr->in_events->get(pr->in_events, ev)->time; if (t < tot) tot = t; }
    if (tot <= pos) tot = pos + 1;
    if (tot > n) tot = n;
    const float* inDeel[2]; float* uitDeel[2];
    for (int c = 0; c < k; ++c) { uitDeel[c] = uit[c] + pos; inDeel[c] = in[c] ? in[c] + pos : nullptr; }
    if (!in[0]) {  // geen ingang: stilte erin (in de uitgang, die we toch overschrijven)
      for (int c = 0; c < k; ++c) { std::memset(uitDeel[c], 0, (tot - pos) * sizeof(float)); inDeel[c] = uitDeel[c]; }
    }
    const uint32_t len = tot - pos;
    const bool bypass = s->bypassDoel || s->bypassG > 0;   // uit en niet aan het vloeien: precies het oude pad
    if (bypass) for (int c = 0; c < k; ++c) std::memcpy(s->droog[c], inDeel[c], len * sizeof(float));
    s->ontwerp.verwerk(inDeel, uitDeel, k, len);
    if (bypass) {
      const double doel = s->bypassDoel ? 1.0 : 0.0;
      for (uint32_t i = 0; i < len; ++i) {
        double& g = s->bypassG;
        g = g < doel ? (g + s->bypassStap > doel ? doel : g + s->bypassStap) : (g - s->bypassStap < doel ? doel : g - s->bypassStap);
        for (int c = 0; c < k; ++c) uitDeel[c][i] = pb::uitvoer((1.0 - g) * uitDeel[c][i] + g * s->droog[c][i]);   // ook de droge invoer: geen subnormale float naar de host
      }
    }
    pos = tot;
  }
  for (int c = k; c < kUit; ++c) std::memcpy(pr->audio_outputs[0].data32[c], uit[k - 1], n * sizeof(float));
  s->staartSmp.store(staartNaarSmp(s), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

const void* extensie(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG)) return &AUDIO_PORTS_CONFIG;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE_EXT;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY_EXT;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL_EXT;
#ifdef PB_NOTEN
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
#endif
  return nullptr;
}
void opMainThread(const clap_plugin_t*) {}

const clap_plugin_t* maakPlugin(const clap_host_t* host) {
  void* mem = std::malloc(sizeof(Plug));
  if (!mem) return nullptr;
  auto* s = new (mem) Plug();
  s->host = host;
  s->plugin = {&DESCRIPTOR, s, init, destroy, activate, deactivate, startProcessing, stopProcessing,
               reset, process, extensie, opMainThread};
  return &s->plugin;
}

// ---- factory ----------------------------------------------------------------------------------------------
uint32_t fabAantal(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* fabDescriptor(const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &DESCRIPTOR : nullptr; }
const clap_plugin_t* fabMaak(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id)) return nullptr;
  return maakPlugin(host);
}
const clap_plugin_factory_t FACTORY = {fabAantal, fabDescriptor, fabMaak};

}  // namespace

bool pb_entry_init(const char*) { return true; }
void pb_entry_deinit() {}
const void* pb_entry_get_factory(const char* id) {
  return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &FACTORY : nullptr;
}
