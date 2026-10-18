// plugin.cpp — de CLAP-schil van PB Toetsenpad (fase 2: parameters met tekst ↔ waarde, state, params.flush).
//
// Een instrument: geen audio-ingang, één stereo-uitgang, één note port (alleen het CLAP-dialect, valkuil 19).
// Het Ontwerp uit dsp/ontwerpen/keten_toetsenpad.h levert naam, id, maker, parameters, latency en staart; process
// geeft `verwerk` een stille invoer en past parameter- en noot-events op hun sample toe (het blok wordt bij elk event
// gesplitst; de DSP rekent per sample, dus de uitvoer hangt niet af van het splitsen).
//
// Draden (ARCHITECTUUR.md "Threads", valkuil 11): zolang de plugin actief is, raakt alleen de audiothread (process,
// en params.flush) het Ontwerp aan. params.get_value en state.save lezen een atomaire kopie per parameter die de
// audiothread bijwerkt; state.load terwijl de plugin actief is zet waarden klaar plus een atomaire vlag, en
// process/flush neemt ze over vóór de events van dat blok. tail.get leest een staart die process uitrekende.
// activate reserveert al het geheugen op de mainthread; process alloceert niet, lockt niet en doet geen syscalls
// (RTSan in CI). Met PB_KANARIE alloceert process wel: die bouw MOET falen in RTSan.
#include <clap/clap.h>

#include <atomic>
#include <cmath>
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
// de kanarie: een allocatie in process, bewaard in een globale variabele zodat de compiler hem niet wegoptimaliseert
void* volatile pbKanarie = nullptr;
#endif

namespace {

using pb::Ontwerp;

constexpr uint32_t KANALEN = 2;
constexpr int N = Ontwerp::N_PARAMS;
constexpr int NOOT = PB_NOOT_PARAM;    // index in PARAMS, geen id
constexpr int POORT = PB_POORT_PARAM;  // index in PARAMS, geen id

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

// de index in PARAMS bij een parameter-id (de id's staan vast in SPEC.md), of −1
int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id) return i;
  return -1;
}

double klem(double v, int i) {
  const auto& p = Ontwerp::PARAMS[i];
  return v < p.min ? p.min : (v > p.max ? p.max : v);
}

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;
  Ontwerp ontwerp;
  pb::NootStapel stapel;
  std::vector<unsigned char> geheugen;   // het blok van Ontwerp::geheugenNodig, gereserveerd in activate
  std::vector<float> stil;               // de stille invoer voor verwerk (gereserveerd in activate)
  uint32_t stilLengte = 0;
  double sr = 48000;
  std::atomic<bool> actief{false};

  std::atomic<double> spiegel[N];        // wat de host ziet (get_value, state.save); de audiothread werkt hem bij
  std::atomic<double> klaar[N];          // state.load terwijl actief: overnemen in process/flush
  std::atomic<bool> klaarVlag{false};
  std::atomic<double> staart{0.0};       // seconden, −1 = oneindig; uitgerekend op de audiothread

  Plugin() {
    for (int i = 0; i < N; ++i) {
      spiegel[i].store(Ontwerp::PARAMS[i].standaard);
      klaar[i].store(Ontwerp::PARAMS[i].standaard);
    }
  }
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// --- events (audiothread, of de mainthread als de plugin niet actief is) -----------------------------------------
void neemKlaarOver(Plugin* s) {
  if (!s->klaarVlag.exchange(false, std::memory_order_acq_rel)) return;
  for (int i = 0; i < N; ++i) {
    const double v = s->klaar[i].load(std::memory_order_relaxed);
    s->ontwerp.zetParam(i, v);
    s->spiegel[i].store(v, std::memory_order_relaxed);
  }
}

// ARCHITECTUUR.md "Noten" stap 2: na elk noot-event
void nootNaarParams(Plugin* s) {
  const int n = s->stapel.boven();
  if (n >= 0) {
    s->ontwerp.zetParam(NOOT, pb::nootVerhouding(n));
    s->ontwerp.zetParam(POORT, 1.0);
  } else {
    s->ontwerp.zetParam(POORT, 0.0);
  }
}

void pasToe(Plugin* s, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  switch (h->type) {
    case CLAP_EVENT_PARAM_VALUE: {
      const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
      const int i = indexVan(e->param_id);
      if (i < 0 || !std::isfinite(e->value)) return;
      const double v = klem(e->value, i);
      s->ontwerp.zetParam(i, v);
      s->spiegel[i].store(v, std::memory_order_relaxed);
      return;
    }
    case CLAP_EVENT_NOTE_ON:
    case CLAP_EVENT_NOTE_OFF: {
      const auto* e = reinterpret_cast<const clap_event_note_t*>(h);
      if (e->key < 0 || e->key > 127) return;  // buiten 0..127: negeren, ook geen stap 2
      if (h->type == CLAP_EVENT_NOTE_ON) s->stapel.aan(e->key);
      else s->stapel.uit(e->key);
      nootNaarParams(s);
      return;
    }
    case CLAP_EVENT_NOTE_CHOKE:
      s->stapel.leeg();
      nootNaarParams(s);
      return;
    case CLAP_EVENT_MIDI: {
      const auto* e = reinterpret_cast<const clap_event_midi_t*>(h);
      const uint8_t st = e->data[0] & 0xF0, d1 = e->data[1], d2 = e->data[2];
      if (st == 0x90 || st == 0x80) {
        if (d1 > 127) return;
        if (st == 0x90 && d2 > 0) s->stapel.aan(d1);
        else s->stapel.uit(d1);
        nootNaarParams(s);
      } else if (st == 0xB0 && (d1 == 120 || d1 == 123)) {
        s->stapel.leeg();
        nootNaarParams(s);
      }
      return;
    }
    default:
      return;
  }
}

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

// --- params: id = het nummer in SPEC.md, waarden in hun eigen eenheid --------------------------------------------
uint32_t prCount(const clap_plugin_t*) { return N; }
bool prInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index >= (uint32_t)N) return false;
  const auto& p = Ontwerp::PARAMS[index];
  std::memset(info, 0, sizeof *info);
  info->id = p.id;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof info->name, "%s", p.naam);
  info->module[0] = 0;
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}
bool prValue(const clap_plugin_t* pl, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(pl)->spiegel[i].load(std::memory_order_relaxed);
  return true;
}

bool eenheidloos(int i) {
  const char* e = Ontwerp::PARAMS[i].eenheid;
  return !std::strcmp(e, "") || !std::strcmp(e, "×") || !std::strcmp(e, "%");
}

// v met `dec` decimalen, de komma als decimaalteken. Zelf gedaan in plaats van printf/strtod: die volgen de locale van
// de host (een Nederlandse Logic kan een komma verwachten, een andere een punt), en de tekst moet overal gelijk zijn.
void metDecimalen(double v, int dec, char* t, size_t grootte) {
  std::snprintf(t, grootte, "%.*f", dec, v);
  for (char* c = t; *c; ++c)
    if (*c == '.') *c = ',';
  if (t[0] == '-') {  // "-0" en "-0,0" na afronden zijn gewoon 0
    bool nul = true;
    for (const char* c = t + 1; *c; ++c)
      if (*c != '0' && *c != ',') nul = false;
    if (nul) std::memmove(t, t + 1, std::strlen(t));
  }
}

// de waarde van een tekst die metDecimalen schreef (komma of punt): wat afronden ervan overlaat
double alsGetal(const char* t) {
  double x = 0, schaal = 1;
  bool neg = *t == '-', na = false;
  for (const char* c = t + (neg ? 1 : 0); *c; ++c) {
    if (*c == ',' || *c == '.') na = true;
    else if (*c >= '0' && *c <= '9') {
      x = x * 10 + (*c - '0');
      if (na) schaal *= 10;
    }
  }
  return (neg ? -x : x) / schaal;
}

// SPEC.md "Tekst ↔ waarde": komma als decimaalteken, spatie voor de eenheid. De grens kiest op de afgeronde waarde,
// zodat 999,7 Hz "1,00 kHz" wordt en geen "1000 Hz".
bool prToText(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t grootte) {
  const int i = indexVan(id);
  if (i < 0 || !uit || grootte == 0 || !std::isfinite(v)) return false;
  const char* e = Ontwerp::PARAMS[i].eenheid;
  char g[48];
  const char* eenheid = e;
  if (eenheidloos(i)) {
    metDecimalen(v * 100.0, 0, g, sizeof g);
    eenheid = "%";
  } else if (!std::strcmp(e, "Hz")) {
    metDecimalen(v, 0, g, sizeof g);
    if (std::fabs(alsGetal(g)) >= 1000.0) {
      metDecimalen(v / 1000.0, 2, g, sizeof g);
      if (std::fabs(alsGetal(g)) >= 10.0) metDecimalen(v / 1000.0, 1, g, sizeof g);
      eenheid = "kHz";
    }
  } else if (!std::strcmp(e, "ms")) {
    metDecimalen(v, 1, g, sizeof g);
    if (std::fabs(alsGetal(g)) >= 100.0) metDecimalen(v, 0, g, sizeof g);
  } else metDecimalen(v, 2, g, sizeof g);
  std::snprintf(uit, grootte, "%s %s", g, eenheid);
  return true;
}

// Een getal met komma of punt, zonder locale (zie metDecimalen). Exact afgerond: mantisse (geheel, < 2^53) en een
// macht van tien zijn exact, en één deling of vermenigvuldiging rondt correct af, dus "0,38" geeft dezelfde double als
// 0.38 in de broncode. `macht` schuift de komma (−2 voor procenten, +3 voor kHz).
bool leesGetal(const char*& c, int macht, double* uit) {
  bool neg = false;
  if (*c == '+' || *c == '-') neg = *c++ == '-';
  uint64_t m = 0;
  int cijfers = 0, na = 0, decimalen = 0;
  bool punt = false;
  for (;; ++c) {
    if (*c >= '0' && *c <= '9') {
      if (m < (1ull << 53) / 10) {
        m = m * 10 + (uint64_t)(*c - '0');
        if (punt) ++decimalen;
      } else if (!punt) ++na;  // te veel cijfers voor exact: de rest telt als nullen
      ++cijfers;
    } else if ((*c == ',' || *c == '.') && !punt) punt = true;
    else break;
  }
  if (cijfers == 0) return false;
  const int exp = na - decimalen + macht;
  double x = (double)m;
  static const double MACHT[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
  if (exp > 22 || exp < -22) x = exp > 0 ? HUGE_VAL : 0.0;
  else x = exp >= 0 ? x * MACHT[exp] : x / MACHT[-exp];
  *uit = neg ? -x : x;
  return true;
}

bool gelijkZonderHoofdletters(const char* a, size_t n, const char* b) {
  if (std::strlen(b) != n) return false;
  for (size_t k = 0; k < n; ++k) {
    char x = a[k], y = b[k];
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

// SPEC.md: komma én punt, met of zonder eenheid, "k" voor kHz, procenten voor eenheidloos; buiten het bereik klemmen,
// onleesbaar = false
bool prFromText(const clap_plugin_t*, clap_id id, const char* tekst, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !tekst || !v) return false;
  const char* c = tekst;
  while (*c == ' ' || *c == '\t') ++c;
  // eerst de eenheid (achteraan, na het getal), dan weten we welke macht het getal krijgt
  const char* begin = c;
  while (*c == '+' || *c == '-' || *c == ',' || *c == '.' || (*c >= '0' && *c <= '9')) ++c;
  const char* rest = c;
  while (*rest == ' ' || *rest == '\t') ++rest;
  size_t n = std::strlen(rest);
  while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) --n;
  const char* e = Ontwerp::PARAMS[i].eenheid;
  int macht = 0;
  if (eenheidloos(i)) {
    if (!(n == 0 || gelijkZonderHoofdletters(rest, n, "%"))) return false;
    macht = -2;
  } else if (!std::strcmp(e, "Hz")) {
    if (gelijkZonderHoofdletters(rest, n, "k") || gelijkZonderHoofdletters(rest, n, "khz")) macht = 3;
    else if (!(n == 0 || gelijkZonderHoofdletters(rest, n, "hz"))) return false;
  } else if (!(n == 0 || gelijkZonderHoofdletters(rest, n, e))) return false;
  double x;
  const char* g = begin;
  if (!leesGetal(g, macht, &x) || g != c || !std::isfinite(x)) return false;
  *v = klem(x, i);
  return true;
}

void prFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t*) {
  Plugin* s = zelf(pl);
  neemKlaarOver(s);
  if (!in) return;
  const uint32_t n = in->size(in);
  for (uint32_t k = 0; k < n; ++k) pasToe(s, in->get(in, k));
}
const clap_plugin_params_t PARAMS_EXT = {prCount, prInfo, prValue, prToText, prFromText, prFlush};

// --- state: "PBST", versie (u32), aantal (u32), per parameter (id u32, waarde f64), little-endian -----------------
constexpr uint32_t STATE_VERSIE = 1;

void zet32(std::vector<unsigned char>& b, uint32_t v) {
  for (int k = 0; k < 4; ++k) b.push_back((unsigned char)(v >> (8 * k)));
}
uint32_t lees32(const unsigned char* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool stSave(const clap_plugin_t* pl, const clap_ostream_t* os) {
  Plugin* s = zelf(pl);
  std::vector<unsigned char> b;
  b.insert(b.end(), {'P', 'B', 'S', 'T'});
  zet32(b, STATE_VERSIE);
  zet32(b, (uint32_t)N);
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

bool stLoad(const clap_plugin_t* pl, const clap_istream_t* is) {
  Plugin* s = zelf(pl);
  std::vector<unsigned char> b;
  unsigned char buf[4096];
  for (;;) {
    const int64_t r = is->read(is, buf, sizeof buf);
    if (r < 0) return false;
    if (r == 0) break;
    b.insert(b.end(), buf, buf + r);
    if (b.size() > (1u << 20)) return false;  // een state van een parameterlijst is nooit zo groot
  }
  if (b.size() < 12 || std::memcmp(b.data(), "PBST", 4)) return false;
  const uint32_t versie = lees32(&b[4]), aantal = lees32(&b[8]);
  if (versie == 0) return false;
  if ((uint64_t)aantal * 12 > b.size() - 12) return false;  // korter dan het aantal belooft
  // per id: een onbekend id overslaan, een ontbrekend id houdt zijn standaard
  double waarden[N];
  for (int i = 0; i < N; ++i) waarden[i] = Ontwerp::PARAMS[i].standaard;
  for (uint32_t k = 0; k < aantal; ++k) {
    const unsigned char* p = &b[12 + 12 * (size_t)k];
    const int i = indexVan(lees32(p));
    if (i < 0) continue;
    const uint64_t u = (uint64_t)lees32(p + 4) | ((uint64_t)lees32(p + 8) << 32);
    double v;
    std::memcpy(&v, &u, 8);
    if (std::isfinite(v)) waarden[i] = klem(v, i);
  }
  if (s->actief.load()) {
    for (int i = 0; i < N; ++i) {
      s->klaar[i].store(waarden[i], std::memory_order_relaxed);
      s->spiegel[i].store(waarden[i], std::memory_order_relaxed);
    }
    s->klaarVlag.store(true, std::memory_order_release);
  } else {
    // niet actief: alleen de mainthread raakt het Ontwerp aan
    for (int i = 0; i < N; ++i) {
      s->ontwerp.zetParam(i, waarden[i]);
      s->spiegel[i].store(waarden[i], std::memory_order_relaxed);
    }
  }
  if (s->hostParams) {
    s->hostParams->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
    if (s->actief.load()) s->hostParams->request_flush(s->host);
  }
  return true;
}
const clap_plugin_state_t STATE = {stSave, stLoad};

// --- latency en tail ---------------------------------------------------------------------------------------------
uint32_t latGet(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY = {latGet};

uint32_t tailGet(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  const double t = s->staart.load(std::memory_order_relaxed);
  if (t < 0) return INT32_MAX;  // oneindig (lusversterking ≥ 1)
  const double n = std::ceil(t * s->sr);
  return n >= (double)INT32_MAX ? INT32_MAX : (uint32_t)n;
}
const clap_plugin_tail_t TAIL = {tailGet};

// --- plugin ------------------------------------------------------------------------------------------------------
bool plInit(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->hostParams = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
  return true;
}

void plDestroy(const clap_plugin_t* p) { delete zelf(p); }

bool plActivate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t maxFrames) {
  Plugin* s = zelf(p);
  const unsigned long nodig = Ontwerp::geheugenNodig(sr);
  if (s->geheugen.size() < nodig) s->geheugen.assign(nodig, 0);
  pb::Geheugen g;
  g.p = s->geheugen.data();
  g.over = (unsigned long)s->geheugen.size();
  neemKlaarOver(s);
  s->ontwerp.activeer(sr, minFrames, g);
  s->stapel.leeg();
  s->stilLengte = maxFrames > 0 ? maxFrames : 4096;
  s->stil.assign((size_t)s->stilLengte * KANALEN, 0.0f);
  s->sr = sr;
  s->staart.store(s->ontwerp.staartSec());
  s->actief.store(true);
  return true;
}

void plDeactivate(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->actief.store(false);
  neemKlaarOver(s);  // een load die process niet meer zag
}

bool plStartProcessing(const clap_plugin_t*) { return true; }
void plStopProcessing(const clap_plugin_t*) {}
void plReset(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->ontwerp.reset();
  s->stapel.leeg();
}

// verwerk een stuk [van, tot) van het blok, in stukken van hooguit de stille buffer
void render(Plugin* s, float* const* uit, uint32_t kanalen, uint32_t van, uint32_t tot) {
  const float* in[KANALEN] = {s->stil.data(), s->stil.data() + s->stilLengte};
  while (van < tot) {
    uint32_t n = tot - van;
    if (n > s->stilLengte) n = s->stilLengte;
    float* u[KANALEN];
    for (uint32_t c = 0; c < kanalen; ++c) u[c] = uit[c] + van;
    s->ontwerp.verwerk(in, u, (int)kanalen, n);
    van += n;
  }
}

clap_process_status plProcess(const clap_plugin_t* p, const clap_process_t* pr) {
  Plugin* s = zelf(p);
#ifdef PB_KANARIE
  pbKanarie = std::malloc(64);
#endif
  neemKlaarOver(s);

  // transport van de host: tempo en plek in de maat (alleen een sequencer leest het)
  if (pr->transport) {
    const clap_event_transport_t* t = pr->transport;
    const double bpm = (t->flags & CLAP_TRANSPORT_HAS_TEMPO) ? t->tempo : 0.0;
    const double tel = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
                           ? (double)t->song_pos_beats / (double)CLAP_BEATTIME_FACTOR
                           : 0.0;
    s->ontwerp.zetTransport(bpm, tel, (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) &&
                                          (t->flags & CLAP_TRANSPORT_IS_PLAYING));
  }

  const uint32_t frames = pr->frames_count;
  float* leeg[KANALEN] = {nullptr, nullptr};
  float* const* uit = leeg;
  uint32_t kanalen = 0;
  if (pr->audio_outputs_count > 0 && pr->audio_outputs[0].data32) {
    uit = pr->audio_outputs[0].data32;
    kanalen = pr->audio_outputs[0].channel_count;
    if (kanalen > KANALEN) kanalen = KANALEN;
  }

  // events op hun sample: tot elk event renderen, dan het event toepassen
  const clap_input_events_t* in = pr->in_events;
  const uint32_t nEv = in ? in->size(in) : 0;
  uint32_t pos = 0;
  for (uint32_t k = 0; k < nEv; ++k) {
    const clap_event_header_t* h = in->get(in, k);
    uint32_t t = h->time < frames ? h->time : frames;
    if (t < pos) t = pos;
    if (t > pos && kanalen) render(s, uit, kanalen, pos, t);
    if (t > pos) pos = t;
    pasToe(s, h);
  }
  if (frames > pos && kanalen) render(s, uit, kanalen, pos, frames);

  // overige uitgangen (die er niet horen te zijn) stil
  for (uint32_t b = 0; b < pr->audio_outputs_count; ++b) {
    const clap_audio_buffer_t& ub = pr->audio_outputs[b];
    for (uint32_t c = (b == 0 ? kanalen : 0); c < ub.channel_count; ++c)
      if (ub.data32 && ub.data32[c]) std::memset(ub.data32[c], 0, sizeof(float) * frames);
    pr->audio_outputs[b].constant_mask = 0;
  }

  s->staart.store(s->ontwerp.staartSec(), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

const void* plGetExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
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
