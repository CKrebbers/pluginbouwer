// toetsenpad.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_toetsenpad.h.
//
// Een instrument zonder audio-ingang, één stereo-uitgang en één note port (alleen het CLAP-dialect, valkuil 19).
// Fase 1: het Ontwerp draait in process. Parameters zijn CLAP-params (id = nummer in SPEC.md, bereik en standaard uit
// PARAMS); events (parameters en noten) gaan op hun sample: het blok wordt bij elk event gesplitst. Noten volgen
// ARCHITECTUUR.md "Noten" (pb::NootStapel uit dsp/pb_noten.h). State zoals ARCHITECTUUR.md "State".
// Fase 2: tekst ↔ waarde volgens SPEC.md (komma, geen locale), params.flush buiten process. Getoetst door
// plugin/toets/statetoets.cpp (pb-statetoets) door het gebouwde .clap, en door clap-validator.
//
// Draden (ARCHITECTUUR.md "Threads", valkuil 11): init, activate, state en tekst draaien op de mainthread; process
// alleen op de audiothread. Zolang de plugin actief is raakt alleen de audiothread het Ontwerp aan; de mainthread leest
// een atomaire spiegel per parameter (get_value, state.save) en state.load zet waarden klaar die process of flush
// overneemt. In process geen allocatie, locks of syscalls (RTSan bewaakt het in CI).
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ontwerpen/keten_toetsenpad.h"
#include "pb_noten.h"
#include "toetsenpad_entry.h"

#ifdef PB_KANARIE
// Alleen in de kanarie-build van de RTSan-job: een malloc in process die RTSan MOET vangen (valkuil 4). De pointer
// gaat naar een globale variabele, zodat de compiler de malloc niet weg kan laten.
void* volatile pbKanarieGeheugen = nullptr;
#endif

namespace {

using pb::Ontwerp;

constexpr uint32_t KANALEN = 2;
constexpr int N = Ontwerp::N_PARAMS;
constexpr int NOOT = PB_NOOT_PARAM;    // index in PARAMS, geen id
constexpr int POORT = PB_POORT_PARAM;  // index in PARAMS, geen id

// state (ARCHITECTUUR.md "State"): "PBST", versie, aantal, dan per parameter (id u32, waarde f64), little-endian
constexpr uint32_t STATE_VERSIE = 1;

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT, Ontwerp::META.clapId, Ontwerp::META.naam, Ontwerp::META.maker, Ontwerp::META.url, "", "",
    Ontwerp::META.versie, Ontwerp::META.uitleg, FEATURES};

struct Toetsenpad {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;
  Ontwerp ontwerp;
  pb::NootStapel stapel;                // alleen de audiothread (of de mainthread als de plugin niet actief is)
  std::vector<unsigned char> geheugen;  // het blok voor het Ontwerp, gereserveerd in activate (mainthread)
  std::vector<float> stil[KANALEN];     // de stille invoer voor verwerk (een instrument leest hem niet)
  double sr = 48000;
  bool actief = false;                  // alleen de mainthread leest en schrijft dit

  std::atomic<double> spiegel[N];       // wat de host ziet (get_value, state.save); de audiothread werkt hem bij
  std::atomic<bool> stateKlaar{false};  // state.load terwijl actief: process/flush neemt de spiegel over
  std::atomic<uint32_t> staart{0};      // in samples, uitgerekend door de audiothread (tail.get leest het)

  Toetsenpad() {
    for (int i = 0; i < N; ++i) spiegel[i].store(Ontwerp::PARAMS[i].standaard);
  }
};

Toetsenpad* zelf(const clap_plugin_t* p) { return static_cast<Toetsenpad*>(p->plugin_data); }

int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id) return i;
  return -1;
}

double klem(int i, double v) {
  const auto& p = Ontwerp::PARAMS[i];
  if (!(v == v)) return p.standaard;  // NaN
  return v < p.min ? p.min : (v > p.max ? p.max : v);
}

// staart van het Ontwerp in samples (−1 = oneindig → INT32_MAX, CLAP: oneindig)
uint32_t staartSamples(const Toetsenpad* t) {
  const double s = t->ontwerp.staartSec();
  if (s < 0) return (uint32_t)INT32_MAX;
  const double n = s * t->sr;
  if (n >= (double)INT32_MAX) return (uint32_t)INT32_MAX;
  const uint32_t g = (uint32_t)n;
  return (double)g < n ? g + 1 : g;
}

// --- noten (ARCHITECTUUR.md "Noten") -----------------------------------------------------------------------------
// stap 2: de bovenste noot wordt Noot (verhouding) en Poort 1; geen noot: Poort 0. De spiegel blijft wat hij was.
void naNoot(Toetsenpad* t) {
  const int n = t->stapel.boven();
  if (n >= 0) {
    t->ontwerp.zetParam(NOOT, pb::nootVerhouding(n));
    t->ontwerp.zetParam(POORT, 1.0);
  } else {
    t->ontwerp.zetParam(POORT, 0.0);
  }
}

void nootAan(Toetsenpad* t, int key) {
  if (key < 0 || key > 127) return;  // buiten 0..127: negeren, geen stap 2
  t->stapel.aan(key);
  naNoot(t);
}

void nootUit(Toetsenpad* t, int key) {
  if (key < 0 || key > 127) return;
  t->stapel.uit(key);
  naNoot(t);
}

void parameter(Toetsenpad* t, clap_id id, double v) {
  const int i = indexVan(id);
  if (i < 0) return;
  v = klem(i, v);
  t->ontwerp.zetParam(i, v);
  t->spiegel[i].store(v, std::memory_order_relaxed);
}

// één event uit process of flush (audiothread, of mainthread als de plugin niet actief is)
void doeEvent(Toetsenpad* t, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  switch (h->type) {
    case CLAP_EVENT_PARAM_VALUE: {
      const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
      parameter(t, e->param_id, e->value);
      break;
    }
    case CLAP_EVENT_NOTE_ON:
      nootAan(t, reinterpret_cast<const clap_event_note_t*>(h)->key);
      break;
    case CLAP_EVENT_NOTE_OFF:
      nootUit(t, reinterpret_cast<const clap_event_note_t*>(h)->key);
      break;
    case CLAP_EVENT_NOTE_CHOKE:
      t->stapel.leeg();
      naNoot(t);
      break;
    case CLAP_EVENT_MIDI: {
      const auto* e = reinterpret_cast<const clap_event_midi_t*>(h);
      const uint8_t status = e->data[0] & 0xF0, d1 = e->data[1], d2 = e->data[2];
      if (status == 0x90 && d2 > 0) nootAan(t, d1);
      else if (status == 0x80 || status == 0x90) nootUit(t, d1);
      else if (status == 0xB0 && (d1 == 120 || d1 == 123)) {
        t->stapel.leeg();
        naNoot(t);
      }
      break;
    }
    default:
      break;
  }
}

// state.load terwijl actief: de klaargezette waarden gaan vóór de events van dit blok het Ontwerp in
void neemStateOver(Toetsenpad* t) {
  if (!t->stateKlaar.exchange(false, std::memory_order_acquire)) return;
  for (int i = 0; i < N; ++i) t->ontwerp.zetParam(i, t->spiegel[i].load(std::memory_order_relaxed));
}

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

// --- latency en tail -----------------------------------------------------------------------------------------------
uint32_t latencyGet(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY = {latencyGet};

uint32_t tailGet(const clap_plugin_t* p) { return zelf(p)->staart.load(std::memory_order_relaxed); }
const clap_plugin_tail_t TAIL = {tailGet};

// --- params --------------------------------------------------------------------------------------------------------
uint32_t paramsCount(const clap_plugin_t*) { return (uint32_t)N; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)N) return false;
  const auto& p = Ontwerp::PARAMS[index];
  std::memset(info, 0, sizeof(*info));
  info->id = p.id;
  info->flags = (p.vlaggen & pb::AUTOMATEERBAAR) ? CLAP_PARAM_IS_AUTOMATABLE : 0;
  if (p.vlaggen & pb::STAPPEN) info->flags |= CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::strncpy(info->name, p.naam, sizeof(info->name) - 1);
  info->module[0] = 0;
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}

bool paramsGetValue(const clap_plugin_t* pl, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(pl)->spiegel[i].load(std::memory_order_relaxed);
  return true;
}

// Tekst volgens SPEC.md "Tekst ↔ waarde": komma als decimaalteken, spatie voor de eenheid, geen locale.
// Eerst afronden op wat getoond wordt, dan pas de grens kiezen: 999,7 Hz is "1,00 kHz", niet "1000 Hz".
double rond(double v, double stap) { return std::floor(v / stap + 0.5) * stap; }

void metKomma(char* s) {
  for (; *s; ++s)
    if (*s == '.') *s = ',';
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t grootte) {
  const int i = indexVan(id);
  if (i < 0 || !uit || grootte == 0 || !(v == v)) return false;
  char b[64];
  const char* eenheid = Ontwerp::PARAMS[i].eenheid;
  if (!std::strcmp(eenheid, "Hz")) {  // onder 1000 hele Hz; vanaf 1000 kHz met 2 decimalen, vanaf 10 000 met 1
    if (rond(v, 1) < 1000) std::snprintf(b, sizeof b, "%.0f Hz", rond(v, 1) + 0.0);
    else if (rond(v, 10) < 10000) std::snprintf(b, sizeof b, "%.2f kHz", rond(v, 10) / 1000.0);
    else std::snprintf(b, sizeof b, "%.1f kHz", rond(v, 100) / 1000.0);
  } else if (!std::strcmp(eenheid, "ms")) {  // onder 100 één decimaal, vanaf 100 hele ms
    if (rond(v, 0.1) < 100) std::snprintf(b, sizeof b, "%.1f ms", rond(v * 10, 1) / 10.0 + 0.0);
    else std::snprintf(b, sizeof b, "%.0f ms", rond(v, 1));
  } else {  // eenheidloos: × 100, hele procenten
    std::snprintf(b, sizeof b, "%.0f %%", rond(v * 100.0, 1) + 0.0);
  }
  metKomma(b);
  std::snprintf(uit, grootte, "%s", b);
  return true;
}

// Een getal met komma of punt, zonder locale (een host kan de C-locale veranderd hebben). De cijfers worden als geheel
// getal gelezen (mant) met zijn deler (een macht van 10); de aanroeper deelt één keer, ook voor procenten, zodat "38 %"
// precies 0.38 wordt (zoals strtod het zou doen).
bool leesGetal(const char* s, double* mant, double* deler, const char** rest) {
  while (*s == ' ') ++s;
  bool neg = false;
  if (*s == '-' || *s == '+') neg = *s++ == '-';
  double x = 0, d = 1;
  int cijfers = 0;
  while (*s >= '0' && *s <= '9') { x = x * 10 + (*s++ - '0'); ++cijfers; }
  if (*s == ',' || *s == '.') {
    ++s;
    while (*s >= '0' && *s <= '9') { x = x * 10 + (*s++ - '0'); d *= 10; ++cijfers; }
  }
  if (!cijfers || cijfers > 15) return false;  // meer dan 15 cijfers is niet exact; zo lang is geen waarde van ons
  *mant = neg ? -x : x;
  *deler = d;
  *rest = s;
  return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !tekst) return false;
  double mant, deler, x;
  const char* r;
  if (!leesGetal(tekst, &mant, &deler, &r)) return false;
  while (*r == ' ') ++r;
  const char* eenheid = Ontwerp::PARAMS[i].eenheid;
  if (!std::strcmp(eenheid, "Hz")) {  // "900", "900 Hz", "3k", "3,00 kHz"
    const bool kilo = *r == 'k' || *r == 'K';
    if (kilo) ++r;
    if ((r[0] == 'H' || r[0] == 'h') && (r[1] == 'z' || r[1] == 'Z')) r += 2;
    x = kilo ? mant * 1000.0 / deler : mant / deler;
  } else if (!std::strcmp(eenheid, "ms")) {  // "375", "375 ms", "20,0 ms"
    if (r[0] == 'm' && r[1] == 's') r += 2;
    x = mant / deler;
  } else {  // eenheidloos: procenten, met of zonder "%" ("38" en "38 %" zijn 0,38)
    if (*r == '%') ++r;
    x = mant / (deler * 100.0);
  }
  while (*r == ' ') ++r;
  if (*r) return false;
  *v = klem(i, x);
  return true;
}

// flush: buiten process. Actief: op de audiothread; niet actief: op de mainthread (dan draait er geen audio).
void paramsFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t*) {
  auto* t = zelf(pl);
  neemStateOver(t);
  const uint32_t n = in ? in->size(in) : 0;
  for (uint32_t k = 0; k < n; ++k) doeEvent(t, in->get(in, k));
}

const clap_plugin_params_t PARAMS_EXT = {paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
                                         paramsTextToValue, paramsFlush};

// --- state ---------------------------------------------------------------------------------------------------------
void zetU32(unsigned char* b, uint32_t v) {
  for (int k = 0; k < 4; ++k) b[k] = (unsigned char)(v >> (8 * k));
}
uint32_t leesU32(const unsigned char* b) {
  uint32_t v = 0;
  for (int k = 0; k < 4; ++k) v |= (uint32_t)b[k] << (8 * k);
  return v;
}

bool stateSave(const clap_plugin_t* pl, const clap_ostream_t* os) {
  auto* t = zelf(pl);
  std::vector<unsigned char> b(12 + 12 * N);
  std::memcpy(b.data(), "PBST", 4);
  zetU32(&b[4], STATE_VERSIE);
  zetU32(&b[8], (uint32_t)N);
  for (int i = 0; i < N; ++i) {
    unsigned char* q = &b[12 + 12 * i];
    zetU32(q, Ontwerp::PARAMS[i].id);
    const double v = t->spiegel[i].load(std::memory_order_relaxed);
    uint64_t u;
    std::memcpy(&u, &v, 8);
    for (int k = 0; k < 8; ++k) q[4 + k] = (unsigned char)(u >> (8 * k));
  }
  size_t gedaan = 0;
  while (gedaan < b.size()) {
    const int64_t r = os->write(os, b.data() + gedaan, b.size() - gedaan);
    if (r <= 0) return false;
    gedaan += (size_t)r;
  }
  return true;
}

bool stateLoad(const clap_plugin_t* pl, const clap_istream_t* is) {
  auto* t = zelf(pl);
  std::vector<unsigned char> b;
  unsigned char buf[4096];
  for (;;) {
    const int64_t r = is->read(is, buf, sizeof buf);
    if (r < 0) return false;
    if (r == 0) break;
    b.insert(b.end(), buf, buf + r);
    if (b.size() > (1u << 20)) return false;  // geen state van ons is zo groot
  }
  if (b.size() < 12 || std::memcmp(b.data(), "PBST", 4)) return false;
  const uint32_t versie = leesU32(&b[4]), aantal = leesU32(&b[8]);
  if (versie == 0) return false;
  if ((uint64_t)(b.size() - 12) < (uint64_t)aantal * 12) return false;

  // per id; een ontbrekend id houdt zijn standaard, een onbekend id wordt overgeslagen
  double w[N];
  for (int i = 0; i < N; ++i) w[i] = Ontwerp::PARAMS[i].standaard;
  for (uint32_t k = 0; k < aantal; ++k) {
    const unsigned char* q = &b[12 + 12 * (size_t)k];
    const int i = indexVan(leesU32(q));
    if (i < 0) continue;
    uint64_t u = 0;
    for (int j = 0; j < 8; ++j) u |= (uint64_t)q[4 + j] << (8 * j);
    double v;
    std::memcpy(&v, &u, 8);
    w[i] = klem(i, v);
  }

  for (int i = 0; i < N; ++i) t->spiegel[i].store(w[i], std::memory_order_relaxed);
  if (t->actief) {
    t->stateKlaar.store(true, std::memory_order_release);  // process of flush neemt ze over
  } else {
    for (int i = 0; i < N; ++i) t->ontwerp.zetParam(i, w[i]);
  }
  if (t->hostParams) t->hostParams->rescan(t->host, CLAP_PARAM_RESCAN_VALUES);
  if (t->actief && t->hostParams) t->hostParams->request_flush(t->host);
  return true;
}

const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- plugin --------------------------------------------------------------------------------------------------------
bool pluginInit(const clap_plugin_t* p) {
  auto* t = zelf(p);
  t->hostParams = static_cast<const clap_host_params_t*>(t->host->get_extension(t->host, CLAP_EXT_PARAMS));
  return true;
}

void pluginDestroy(const clap_plugin_t* p) { delete zelf(p); }

bool pluginActivate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t maxFrames) {
  (void)minFrames;
  auto* t = zelf(p);
  t->sr = sr;
  t->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);
  for (auto& s : t->stil) s.assign(maxFrames > 0 ? maxFrames : 1, 0.0f);
  pb::Geheugen g;
  g.p = t->geheugen.data();
  g.over = (unsigned long)t->geheugen.size();
  t->ontwerp.activeer(sr, maxFrames, g);
  // wat state.load klaarzette terwijl we niet actief waren, staat al in het Ontwerp; dit is voor de zekerheid
  if (t->stateKlaar.exchange(false))
    for (int i = 0; i < N; ++i) t->ontwerp.zetParam(i, t->spiegel[i].load());
  t->staart.store(staartSamples(t));
  t->actief = true;
  return true;
}

void pluginDeactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }

bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* p) {
  auto* t = zelf(p);
  t->ontwerp.reset();
  // de stapel leeg (ARCHITECTUUR.md "Noten" 4). Klonk er een noot, dan sluit de poort ook (zoals stap 2 na leeg),
  // anders blijft hij na een reset hangen; zonder noten blijft Poort wat de knop zegt.
  const bool hadNoot = t->stapel.boven() >= 0;
  t->stapel.leeg();
  if (hadNoot) naNoot(t);
}

// verwerk van pos tot eind (een stuk tussen twee events)
void render(Toetsenpad* t, const clap_audio_buffer_t& uit, uint32_t pos, uint32_t eind) {
  if (eind <= pos) return;
  const uint32_t n = eind - pos;
  const int kan = uit.channel_count < KANALEN ? (int)uit.channel_count : (int)KANALEN;
  const float* in[KANALEN];
  float* u[KANALEN];
  for (int c = 0; c < (int)KANALEN; ++c) in[c] = t->stil[c].data() + pos;
  for (int c = 0; c < kan; ++c) u[c] = uit.data32[c] + pos;
  t->ontwerp.verwerk(in, u, kan, n);
}

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* pr) {
#ifdef PB_KANARIE
  pbKanarieGeheugen = std::malloc(64);
#endif
  auto* t = zelf(p);
  const uint32_t frames = pr->frames_count;

  // transport van de host (tempo en plek in de maat; deze keten heeft geen sequencer, maar het contract vraagt het)
  if (const clap_event_transport_t* tr = pr->transport) {
    const double bpm = (tr->flags & CLAP_TRANSPORT_HAS_TEMPO) ? tr->tempo : 0.0;
    const double beat = (tr->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
                            ? (double)tr->song_pos_beats / (double)CLAP_BEATTIME_FACTOR : 0.0;
    t->ontwerp.zetTransport(bpm, beat, (tr->flags & CLAP_TRANSPORT_IS_PLAYING) != 0);
  } else {
    t->ontwerp.zetTransport(0.0, 0.0, false);
  }

  neemStateOver(t);

  const bool heeftUit = pr->audio_outputs_count > 0 && pr->audio_outputs[0].data32 &&
                        pr->audio_outputs[0].channel_count > 0;
  const uint32_t nEv = pr->in_events ? pr->in_events->size(pr->in_events) : 0;
  uint32_t pos = 0;
  for (uint32_t k = 0; k < nEv; ++k) {
    const clap_event_header_t* h = pr->in_events->get(pr->in_events, k);
    uint32_t tijd = h->time < frames ? h->time : frames;
    if (tijd > pos) {  // het blok splitsen: tot het event rekenen, dan het event toepassen
      if (heeftUit) render(t, pr->audio_outputs[0], pos, tijd);
      pos = tijd;
    }
    doeEvent(t, h);
  }
  if (heeftUit) render(t, pr->audio_outputs[0], pos, frames);

  // andere uitgangen en kanalen boven de stereo: stilte
  for (uint32_t b = 0; b < pr->audio_outputs_count; ++b) {
    const clap_audio_buffer_t& uit = pr->audio_outputs[b];
    for (uint32_t c = (b == 0 && heeftUit) ? KANALEN : 0; c < uit.channel_count; ++c) {
      if (uit.data32 && uit.data32[c]) std::memset(uit.data32[c], 0, sizeof(float) * frames);
      if (uit.data64 && uit.data64[c]) std::memset(uit.data64[c], 0, sizeof(double) * frames);
    }
  }

  t->staart.store(staartSamples(t), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
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
