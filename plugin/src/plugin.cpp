// plugin.cpp — de CLAP-schil van Toetsenpad. Een instrument zonder audio-ingang: één stereo-uitgang en één note port
// (alleen het CLAP-dialect, valkuil 19). Het Ontwerp uit dsp/ rekent; deze schil zet events op hun sample (het blok
// wordt bij elk event gesplitst), maakt van noten de parameters Noot en Poort (ARCHITECTUUR.md "Noten"), en doet
// parameters, state, latency en tail.
//
// Draden (valkuil 11): zolang de plugin actief is, raakt alleen de audiothread (process, flush, reset) het Ontwerp aan.
// De mainthread leest een atomaire spiegel per parameter (get_value, state.save); state.load zet waarden klaar met een
// atomaire vlag, en process of flush neemt ze over vóór de events van dat blok.
#include <clap/clap.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "ontwerpen/keten_toetsenpad.h"
#include "pb_noten.h"
#include "entry.h"

#ifdef PB_KANARIE
// RTSan-kanarie: een malloc in process die RTSan MOET vangen (valkuil 4). De pointer gaat naar een globale variabele,
// zodat de compiler de malloc niet wegoptimaliseert. Alleen in het CI-doel toetsenpad_kanarie.
void* volatile pbKanariePointer = nullptr;
#endif

namespace {

using pb::Ontwerp;
constexpr int N = Ontwerp::N_PARAMS;
static_assert(std::atomic<double>::is_always_lock_free, "de parameterspiegel moet lock-vrij zijn");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "de staart moet lock-vrij zijn");

// state: "PBST", versie (u32), aantal (u32), per parameter (id u32, waarde f64), little-endian (ARCHITECTUUR.md "State")
constexpr uint32_t STATE_VERSIE = 1;
constexpr size_t STATE_MAX = 1 << 20;

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;
  Ontwerp* ontwerp = nullptr;
  unsigned char* geheugen = nullptr;
  float* stil = nullptr;               // de stille invoer voor verwerk (ARCHITECTUUR.md "Instrument")
  uint32_t stilLengte = 0;
  double sr = 48000;
  bool actief = false;                 // alleen de mainthread (activate/deactivate/state.load)
  pb::NootStapel stapel;               // alleen de audiothread (of de mainthread als niet actief)

  std::atomic<double> spiegel[N];      // wat de host ziet: de knopwaarde, nooit de laatste noot
  std::atomic<double> klaar[N];        // door state.load klaargezet terwijl actief
  std::atomic<bool> klaarVlag{false};
  std::atomic<uint32_t> staart{0};     // in samples, uitgerekend door process en activate
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id) return i;
  return -1;
}

double klem(int i, double v) { return pb::klem(v, Ontwerp::PARAMS[i].min, Ontwerp::PARAMS[i].max); }

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

// --- de audiothread: state overnemen, events toepassen, rekenen -------------------------------------------------
void neemStateOver(Plugin* s) {
  if (!s->klaarVlag.exchange(false, std::memory_order_acquire)) return;
  for (int i = 0; i < N; ++i) s->ontwerp->zetParam(i, s->klaar[i].load(std::memory_order_relaxed));
}

// ARCHITECTUUR.md "Noten" stap 2: na elk noot-event de bovenste noot naar Noot (index 0) en Poort (index 1)
void nootStap2(Plugin* s) {
  const int n = s->stapel.boven();
  if (n >= 0) {
    s->ontwerp->zetParam(PB_NOOT_PARAM, pb::nootVerhouding(n));
    s->ontwerp->zetParam(PB_POORT_PARAM, 1.0);
  } else {
    s->ontwerp->zetParam(PB_POORT_PARAM, 0.0);
  }
}

void pasToe(Plugin* s, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  switch (h->type) {
    case CLAP_EVENT_PARAM_VALUE: {
      const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
      const int i = indexVan(e->param_id);
      if (i < 0 || !(e->value == e->value)) return;
      const double v = klem(i, e->value);
      s->ontwerp->zetParam(i, v);
      s->spiegel[i].store(v, std::memory_order_relaxed);
      return;
    }
    case CLAP_EVENT_NOTE_ON:
    case CLAP_EVENT_NOTE_OFF: {
      const auto* e = reinterpret_cast<const clap_event_note_t*>(h);
      if (e->key < 0 || e->key > 127) return;  // buiten 0..127: negeren, ook geen stap 2
      if (h->type == CLAP_EVENT_NOTE_ON) s->stapel.aan(e->key);
      else s->stapel.uit(e->key);
      nootStap2(s);
      return;
    }
    case CLAP_EVENT_NOTE_CHOKE:
      s->stapel.leeg();
      nootStap2(s);
      return;
    case CLAP_EVENT_MIDI: {
      const auto* e = reinterpret_cast<const clap_event_midi_t*>(h);
      const uint8_t status = e->data[0] & 0xF0, d1 = e->data[1], d2 = e->data[2];
      if (status == 0x90 || status == 0x80) {
        if (d1 > 127) return;
        if (status == 0x90 && d2 > 0) s->stapel.aan(d1);
        else s->stapel.uit(d1);
        nootStap2(s);
      } else if (status == 0xB0 && (d1 == 120 || d1 == 123)) {
        s->stapel.leeg();
        nootStap2(s);
      }
      return;
    }
    default:
      return;
  }
}

// rekent [van, tot) van het blok; de stille invoer is hooguit stilLengte lang, dus in stukken
void reken(Plugin* s, float* const* uit, uint32_t kanalen, uint32_t van, uint32_t tot) {
  const float* in[2] = {s->stil, s->stil};
  while (van < tot) {
    uint32_t m = tot - van;
    if (m > s->stilLengte) m = s->stilLengte;
    float* u[2] = {uit[0] + van, kanalen > 1 ? uit[1] + van : nullptr};
    s->ontwerp->verwerk(in, u, (int)kanalen, m);
    van += m;
  }
}

void werkStaartBij(Plugin* s) {
  const double sec = s->ontwerp->staartSec();
  uint32_t t;
  if (sec < 0) t = UINT32_MAX;  // oneindig (CLAP: ≥ INT32_MAX)
  else {
    const double smp = sec * s->sr;
    t = smp >= 4294967295.0 ? UINT32_MAX : (uint32_t)smp + ((double)(uint32_t)smp < smp ? 1u : 0u);
  }
  s->staart.store(t, std::memory_order_relaxed);
}

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

// --- latency en tail ----------------------------------------------------------------------------------------
uint32_t latGet(const clap_plugin_t* p) { return zelf(p)->ontwerp->latentie(); }
const clap_plugin_latency_t LATENCY = {latGet};

uint32_t tailGet(const clap_plugin_t* p) { return zelf(p)->staart.load(std::memory_order_relaxed); }
const clap_plugin_tail_t TAIL = {tailGet};

// --- params -------------------------------------------------------------------------------------------------
uint32_t parCount(const clap_plugin_t*) { return N; }
bool parInfo(const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
  if (i >= (uint32_t)N) return false;
  const pb::ParamInfo& pi = Ontwerp::PARAMS[i];
  std::memset(info, 0, sizeof(*info));
  info->id = pi.id;
  info->flags = (pi.vlaggen & pb::AUTOMATEERBAAR) ? CLAP_PARAM_IS_AUTOMATABLE : 0;
  if (pi.vlaggen & pb::STAPPEN) info->flags |= CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof(info->name), "%s", pi.naam);
  info->module[0] = 0;
  info->min_value = pi.min;
  info->max_value = pi.max;
  info->default_value = pi.standaard;
  return true;
}
bool parValue(const clap_plugin_t* p, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(p)->spiegel[i].load(std::memory_order_relaxed);
  return true;
}

// Tekst ↔ waarde (SPEC.md): decimaalteken een komma, spatie voor de eenheid. Zonder locale (een host kan hem zetten).
void metKomma(char* t) { for (; *t; ++t) if (*t == '.') *t = ','; }

bool parToText(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t grootte) {
  const int i = indexVan(id);
  if (i < 0 || !uit || grootte == 0) return false;
  if (!(v == v)) return false;
  const char* eenheid = Ontwerp::PARAMS[i].eenheid;
  if (!std::strcmp(eenheid, "Hz")) {
    if (v < 999.5) std::snprintf(uit, grootte, "%.0f Hz", v);
    else if (v < 9995.0) std::snprintf(uit, grootte, "%.2f kHz", v / 1000.0);
    else std::snprintf(uit, grootte, "%.1f kHz", v / 1000.0);
  } else if (!std::strcmp(eenheid, "ms")) {
    if (v < 99.95) std::snprintf(uit, grootte, "%.1f ms", v);
    else std::snprintf(uit, grootte, "%.0f ms", v);
  } else {
    double pct = v * 100.0;
    if (pct > -0.5 && pct < 0.5) pct = 0.0;  // geen "-0 %"
    std::snprintf(uit, grootte, "%.0f %%", pct);  // eenheidloos (× of leeg): hele procenten
  }
  metKomma(uit);
  return true;
}

// leest een getal met komma of punt; geeft het aantal gelezen tekens, 0 = geen getal. De cijfers worden een geheel
// getal en dan één keer gedeeld door 10^k: zo is "0,38" precies de double die het dichtst bij 0,38 ligt (zonder locale)
size_t leesGetal(const char* t, double* v) {
  size_t i = 0;
  double teken = 1;
  if (t[i] == '-' || t[i] == '+') { if (t[i] == '-') teken = -1; ++i; }
  uint64_t m = 0;
  int cijfers = 0, decimalen = 0, schaal = 0;
  auto cijfer = [&](int c, bool naKomma) {
    if (cijfers < 18) { m = m * 10 + (uint64_t)c; ++cijfers; if (naKomma) ++decimalen; }
    else if (!naKomma) ++schaal;  // te veel cijfers: de rest telt alleen als grootte mee
  };
  bool gezien = false;
  while (t[i] >= '0' && t[i] <= '9') { cijfer(t[i] - '0', false); ++i; gezien = true; }
  if (t[i] == ',' || t[i] == '.') {
    ++i;
    while (t[i] >= '0' && t[i] <= '9') { cijfer(t[i] - '0', true); ++i; gezien = true; }
  }
  if (!gezien) return 0;
  double x = (double)m;
  for (; schaal > 0; --schaal) x *= 10.0;
  double tienMacht = 1.0;
  for (int k = 0; k < decimalen; ++k) tienMacht *= 10.0;  // exact tot 10^22; decimalen ≤ 18
  *v = teken * (x / tienMacht);
  return i;
}

bool gelijkLetters(const char* a, const char* b) {  // ASCII, hoofdletterongevoelig
  for (; *a && *b; ++a, ++b) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
  }
  return *a == *b;
}

bool parFromText(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !tekst || !v) return false;
  while (*tekst == ' ' || *tekst == '\t') ++tekst;
  double x;
  const size_t n = leesGetal(tekst, &x);
  if (!n) return false;
  const char* r = tekst + n;
  while (*r == ' ' || *r == '\t') ++r;
  char rest[16];
  size_t len = std::strlen(r);
  while (len > 0 && (r[len - 1] == ' ' || r[len - 1] == '\t')) --len;
  if (len >= sizeof rest) return false;
  std::memcpy(rest, r, len);
  rest[len] = 0;
  const char* eenheid = Ontwerp::PARAMS[i].eenheid;
  auto is = [&](const char* w) { return gelijkLetters(rest, w); };
  if (!std::strcmp(eenheid, "Hz")) {
    if (is("k") || is("kHz")) x *= 1000.0;
    else if (!(is("") || is("Hz"))) return false;
  } else if (!std::strcmp(eenheid, "ms")) {
    if (!(is("") || is("ms"))) return false;
  } else {
    if (!(is("") || is("%"))) return false;  // eenheidloos: procenten ("38" of "38 %")
    x /= 100.0;
  }
  *v = klem(i, x);
  return true;
}

void parFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
  Plugin* s = zelf(p);
  neemStateOver(s);
  if (!in) return;
  const uint32_t n = in->size(in);
  for (uint32_t e = 0; e < n; ++e) pasToe(s, in->get(in, e));
}
const clap_plugin_params_t PARAMS_EXT = {parCount, parInfo, parValue, parToText, parFromText, parFlush};

// --- state --------------------------------------------------------------------------------------------------
void zet32(std::vector<unsigned char>& b, uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back((unsigned char)(v >> (8 * k))); }
uint32_t lees32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

bool stSave(const clap_plugin_t* p, const clap_ostream_t* os) {
  Plugin* s = zelf(p);
  std::vector<unsigned char> b;
  b.insert(b.end(), {'P', 'B', 'S', 'T'});
  zet32(b, STATE_VERSIE);
  zet32(b, N);
  for (int i = 0; i < N; ++i) {
    zet32(b, Ontwerp::PARAMS[i].id);
    const double v = s->spiegel[i].load(std::memory_order_relaxed);
    uint64_t u;
    std::memcpy(&u, &v, 8);
    zet32(b, (uint32_t)u);
    zet32(b, (uint32_t)(u >> 32));
  }
  size_t pos = 0;
  while (pos < b.size()) {
    const int64_t w = os->write(os, b.data() + pos, b.size() - pos);
    if (w <= 0) return false;
    pos += (size_t)w;
  }
  return true;
}

bool stLoad(const clap_plugin_t* p, const clap_istream_t* is) {
  Plugin* s = zelf(p);
  std::vector<unsigned char> b;
  unsigned char buf[4096];
  for (;;) {
    const int64_t r = is->read(is, buf, sizeof buf);
    if (r < 0) return false;
    if (r == 0) break;
    b.insert(b.end(), buf, buf + r);
    if (b.size() > STATE_MAX) return false;
  }
  if (b.size() < 12 || std::memcmp(b.data(), "PBST", 4)) return false;
  const uint32_t versie = lees32(&b[4]), aantal = lees32(&b[8]);
  if (versie == 0) return false;
  if ((uint64_t)b.size() < 12 + (uint64_t)aantal * 12) return false;
  // per id: een onbekend id overslaan, een ontbrekend id houdt zijn standaard
  double w[N];
  for (int i = 0; i < N; ++i) w[i] = Ontwerp::PARAMS[i].standaard;
  for (uint32_t k = 0; k < aantal; ++k) {
    const unsigned char* q = &b[12 + (size_t)k * 12];
    const int i = indexVan(lees32(q));
    if (i < 0) continue;
    const uint64_t u = (uint64_t)lees32(q + 4) | ((uint64_t)lees32(q + 8) << 32);
    double v;
    std::memcpy(&v, &u, 8);
    if (v == v) w[i] = klem(i, v);
  }
  for (int i = 0; i < N; ++i) s->spiegel[i].store(w[i], std::memory_order_relaxed);
  if (s->actief) {
    for (int i = 0; i < N; ++i) s->klaar[i].store(w[i], std::memory_order_relaxed);
    s->klaarVlag.store(true, std::memory_order_release);
  } else {
    for (int i = 0; i < N; ++i) s->ontwerp->zetParam(i, w[i]);
  }
  if (s->hostParams) {
    s->hostParams->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
    if (s->actief) s->hostParams->request_flush(s->host);
  }
  return true;
}
const clap_plugin_state_t STATE = {stSave, stLoad};

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->ontwerp = new (std::nothrow) Ontwerp();
  if (!s->ontwerp) return false;
  for (int i = 0; i < N; ++i) {
    s->spiegel[i].store(Ontwerp::PARAMS[i].standaard);
    s->klaar[i].store(Ontwerp::PARAMS[i].standaard);
  }
  s->hostParams = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
  return true;
}

void destroy(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  delete s->ontwerp;
  std::free(s->geheugen);
  std::free(s->stil);
  delete s;
}

bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t maxFrames) {
  Plugin* s = zelf(p);
  if (maxFrames == 0) maxFrames = 4096;
  const unsigned long n = Ontwerp::geheugenNodig(sr);
  std::free(s->geheugen);
  std::free(s->stil);
  s->geheugen = static_cast<unsigned char*>(std::calloc(n ? n : 1, 1));
  s->stil = static_cast<float*>(std::calloc(maxFrames, sizeof(float)));
  if (!s->geheugen || !s->stil) return false;
  s->stilLengte = maxFrames;
  s->sr = sr;
  pb::Geheugen g;
  g.p = s->geheugen;
  g.over = n;
  s->ontwerp->activeer(sr, maxFrames, g);
  neemStateOver(s);
  s->stapel.leeg();
  werkStaartBij(s);
  s->actief = true;
  return true;
}

void deactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->ontwerp->reset();
  s->stapel.leeg();
}

clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
#ifdef PB_KANARIE
  pbKanariePointer = std::malloc(16);
#endif
  Plugin* s = zelf(p);
  neemStateOver(s);
  if (pr->transport && (pr->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) {
    const clap_event_transport_t* t = pr->transport;
    const double tel = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) ? (double)t->song_pos_beats / CLAP_BEATTIME_FACTOR : 0.0;
    s->ontwerp->zetTransport(t->tempo, tel, (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0);
  }

  const uint32_t frames = pr->frames_count;
  float* leeg[2] = {nullptr, nullptr};
  float* const* uit = leeg;
  uint32_t kanalen = 0;
  if (pr->audio_outputs_count > 0 && pr->audio_outputs[0].data32 && pr->audio_outputs[0].channel_count > 0) {
    uit = pr->audio_outputs[0].data32;
    kanalen = pr->audio_outputs[0].channel_count > 2 ? 2 : pr->audio_outputs[0].channel_count;
  }

  // het blok splitsen bij elk event: rekenen tot zijn sample, dan het event toepassen
  uint32_t pos = 0;
  const clap_input_events_t* in = pr->in_events;
  const uint32_t nEv = in ? in->size(in) : 0;
  for (uint32_t e = 0; e < nEv; ++e) {
    const clap_event_header_t* h = in->get(in, e);
    uint32_t t = h->time < frames ? h->time : frames;
    if (t > pos) {
      if (kanalen) reken(s, uit, kanalen, pos, t);
      pos = t;
    }
    pasToe(s, h);
  }
  if (frames > pos && kanalen) reken(s, uit, kanalen, pos, frames);

  // overige uitgangen (of kanalen boven 2) stil
  for (uint32_t b = 0; b < pr->audio_outputs_count; ++b) {
    const clap_audio_buffer_t& o = pr->audio_outputs[b];
    for (uint32_t c = (b == 0 ? kanalen : 0); c < o.channel_count; ++c) {
      if (o.data32 && o.data32[c]) std::memset(o.data32[c], 0, sizeof(float) * frames);
      if (o.data64 && o.data64[c]) std::memset(o.data64[c], 0, sizeof(double) * frames);
    }
    pr->audio_outputs[b].constant_mask = 0;
  }
  werkStaartBij(s);
  return CLAP_PROCESS_CONTINUE;
}

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
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
