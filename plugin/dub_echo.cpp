// dub_echo.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
// Fase 1: de DSP is aangesloten. Parameters zijn CLAP-params (id = nummer in SPEC.md, waarden in hun eigen eenheid),
// events gaan op hun sample (het blok wordt bij elk event gesplitst), state zoals ARCHITECTUUR.md.
// Fase 2: tekst <-> waarde in gewone eenheden (ms, Hz/kHz, dB, Q, %), stapjes voor parameters met de vlag STAPPEN,
// params.flush buiten process. Met -DPB_ZONDER_PARAM=<id> bouw je een "oudere" versie zonder die parameter
// (alleen voor de state-toets: state van zo'n build moet in de volledige build laden).
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "entry.h"
#include "ontwerpen/keten_dub_echo.h"

namespace {

using pb::Ontwerp;

#ifdef PB_KANARIE
// De kanarie voor RTSan: een allocatie in process, met de pointer in een globale variabele zodat de compiler hem
// niet weg kan laten. Een RTSan-run die deze build niet afbreekt, bewaakt niets.
void* volatile g_kanarie = nullptr;
#endif

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO,
                                nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT,       Ontwerp::META.clapId, Ontwerp::META.naam, Ontwerp::META.maker, Ontwerp::META.url, "", "",
    Ontwerp::META.versie,    Ontwerp::META.uitleg, FEATURES};

constexpr uint32_t KANALEN = 2;
constexpr int N = Ontwerp::N_PARAMS;
constexpr uint32_t STATE_VERSIE = 1;
#ifdef PB_ZONDER_PARAM
constexpr clap_id ZONDER = PB_ZONDER_PARAM;  // deze build kent dit id niet (de DSP houdt hem op zijn standaard)
#else
constexpr clap_id ZONDER = 0;                // id's beginnen bij 1: 0 = alles zichtbaar
#endif
constexpr bool bestaat(int i) { return Ontwerp::PARAMS[i].id != ZONDER; }
constexpr int telZichtbaar() {
  int n = 0;
  for (int i = 0; i < N; ++i) n += bestaat(i) ? 1 : 0;
  return n;
}
constexpr int N_ZICHTBAAR = telZichtbaar();

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp ontwerp;                        // groot: Plugin wordt op de heap gemaakt (main thread)
  std::vector<unsigned char> geheugen;    // vooraf gereserveerd in activate, nooit in process
  std::vector<float> kopie;               // invoerkopie (vooraf gereserveerd) als in- en uitgang buffers delen
  double sr = 48000;
  bool actief = false;                    // alleen de main thread leest en schrijft dit
  std::atomic<double> staart{0.0};        // uitgerekend op de audiothread / in activate, gelezen door tail.get
  // De waarde per parameter zoals de host hem ziet (get_value, state save). De DSP krijgt zijn waarden alleen via
  // events (process/flush) of, bij een state load terwijl hij draait, via `teLaden` aan het begin van process.
  std::atomic<double> waarde[N];
  std::atomic<bool> teLaden{false};
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id && bestaat(i)) return i;
  return -1;
}
// de index in PARAMS van de k-de parameter die de host ziet
int zichtbaar(uint32_t k) {
  for (int i = 0; i < N; ++i)
    if (bestaat(i) && k-- == 0) return i;
  return -1;
}
bool getrapt(int i) { return (Ontwerp::PARAMS[i].vlaggen & pb::STAPPEN) != 0; }
double klem(int i, double v) {
  const auto& p = Ontwerp::PARAMS[i];
  if (!(v == v)) return p.standaard;  // NaN
  v = v < p.min ? p.min : (v > p.max ? p.max : v);
  if (getrapt(i)) v = std::floor(v + 0.5);  // stapjes: alleen hele getallen
  return v;
}

// zet één parameter in de DSP en in de host-kopie (audiothread, of main thread als de plugin niet draait)
void zet(Plugin* s, int i, double v) {
  v = klem(i, v);
  s->ontwerp.zetParam(i, v);
  s->waarde[i].store(v, std::memory_order_relaxed);
}

void doeEvent(Plugin* s, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
  const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
  const int i = indexVan(e->param_id);
  if (i >= 0) zet(s, i, e->value);
}

// Een subnormale uitvoersample wordt (getekende) 0. De DSP poetst zijn invoer en zijn interne toestand zelf, maar een
// heel zachte maar normale invoer (rond 1e-38, clap-validator fuzz) komt via het droge pad × mix subnormaal uit de
// uitgang. Alleen de bits worden bekeken, en het gebeurt NA de DSP: de DSP rekent precies wat hij altijd rekende
// (geen FTZ ervoor, valkuil 6). De verwachte uitvoer in testsignalen/ bevat geen subnormale samples, dus de null-test
// blijft bit voor bit.
void poetsUitvoer(float* x, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t u;
    std::memcpy(&u, &x[i], 4);
    if ((u & 0x7f800000u) == 0 && (u & 0x007fffffu) != 0) {
      u &= 0x80000000u;
      std::memcpy(&x[i], &u, 4);
    }
  }
}

void laadUitHostKopie(Plugin* s) {
  for (int i = 0; i < N; ++i) s->ontwerp.zetParam(i, s->waarde[i].load(std::memory_order_relaxed));
}

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* p) { delete zelf(p); }

bool activate(const clap_plugin_t* p, double sr, uint32_t minFrames, uint32_t maxFrames) {
  Plugin* s = zelf(p);
  s->sr = sr;
  s->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);
  s->kopie.assign((size_t)KANALEN * (maxFrames > 0 ? maxFrames : 1), 0.0f);
  pb::Geheugen g;
  g.p = s->geheugen.data();
  g.over = (unsigned long)s->geheugen.size();
  s->ontwerp.activeer(sr, minFrames, g);
  if (s->teLaden.exchange(false)) laadUitHostKopie(s);
  s->staart.store(s->ontwerp.staartSec());
  s->actief = true;
  return true;
}
void deactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { zelf(p)->ontwerp.reset(); }

clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
#ifdef PB_KANARIE
  g_kanarie = std::malloc(64);
#endif
  Plugin* s = zelf(p);
  if (s->teLaden.exchange(false, std::memory_order_acquire)) laadUitHostKopie(s);

  const uint32_t n = pr->frames_count;
  const clap_input_events_t* ev = pr->in_events;
  const uint32_t nEv = ev ? ev->size(ev) : 0;
  uint32_t e = 0;

  float* uit[KANALEN] = {nullptr, nullptr};
  const float* in[KANALEN] = {nullptr, nullptr};
  bool audio = pr->audio_outputs_count >= 1 && pr->audio_outputs[0].data32 && pr->audio_outputs[0].channel_count > 0;
  if (audio) {
    const clap_audio_buffer_t& u = pr->audio_outputs[0];
    for (uint32_t c = 0; c < KANALEN; ++c) uit[c] = u.data32[c < u.channel_count ? c : u.channel_count - 1];
    const clap_audio_buffer_t* ib = pr->audio_inputs_count > 0 ? &pr->audio_inputs[0] : nullptr;
    for (uint32_t c = 0; c < KANALEN; ++c)
      in[c] = (ib && ib->data32 && ib->channel_count > 0) ? ib->data32[c < ib->channel_count ? c : ib->channel_count - 1]
                                                         : nullptr;
    for (uint32_t c = 0; c < KANALEN; ++c) if (!uit[c]) audio = false;
  }
  // De DSP schrijft per sample kanaal 0 vóór hij kanaal 1 leest. Deelt een invoerbuffer zijn geheugen met een
  // uitgang van een ander kanaal (of ontbreekt hij), dan rekenen we op een vooraf gereserveerde kopie.
  bool viaKopie = false;
  if (audio) {
    for (uint32_t c = 0; c < KANALEN; ++c) {
      if (!in[c]) viaKopie = true;
      for (uint32_t d = 0; d < KANALEN; ++d) if (d != c && in[c] == uit[d]) viaKopie = true;
    }
  }
  const uint32_t stuk = viaKopie ? (uint32_t)(s->kopie.size() / KANALEN) : n;

  uint32_t pos = 0;
  while (pos < n || e < nEv) {
    // alle events op of vóór deze sample toepassen
    while (e < nEv) {
      const clap_event_header_t* h = ev->get(ev, e);
      if (h->time > pos && pos < n) break;
      doeEvent(s, h);
      ++e;
    }
    if (pos >= n) break;
    uint32_t tot = n;
    if (e < nEv) {
      const uint32_t t = ev->get(ev, e)->time;
      if (t < tot) tot = t;
    }
    if (tot - pos > stuk) tot = pos + stuk;
    const uint32_t m = tot - pos;
    if (audio) {
      if (viaKopie) {
        float* k[KANALEN];
        for (uint32_t c = 0; c < KANALEN; ++c) {
          k[c] = s->kopie.data() + (size_t)c * stuk;
          if (in[c]) std::memcpy(k[c], in[c] + pos, m * sizeof(float));
          else std::memset(k[c], 0, m * sizeof(float));
        }
        const float* ki[KANALEN] = {k[0], k[1]};
        float* ku[KANALEN] = {uit[0] + pos, uit[1] + pos};
        s->ontwerp.verwerk(ki, ku, (int)KANALEN, m);
      } else {
        const float* ki[KANALEN] = {in[0] + pos, in[1] + pos};
        float* ku[KANALEN] = {uit[0] + pos, uit[1] + pos};
        s->ontwerp.verwerk(ki, ku, (int)KANALEN, m);
      }
      poetsUitvoer(uit[0] + pos, m);
      if (uit[1] != uit[0]) poetsUitvoer(uit[1] + pos, m);
    }
    pos = tot;
  }
  s->staart.store(s->ontwerp.staartSec(), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

// --- params ---------------------------------------------------------------------------------------------------
uint32_t paramsCount(const clap_plugin_t*) { return (uint32_t)N_ZICHTBAAR; }
bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  const int i = zichtbaar(index);
  if (i < 0) return false;
  const auto& p = Ontwerp::PARAMS[i];
  std::memset(info, 0, sizeof *info);
  info->id = p.id;
  info->flags = (p.vlaggen & pb::AUTOMATEERBAAR) ? CLAP_PARAM_IS_AUTOMATABLE : 0;
  if (getrapt(i)) info->flags |= CLAP_PARAM_IS_STEPPED;
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof info->name, "%s", p.naam);
  info->module[0] = 0;
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}
bool paramsValue(const clap_plugin_t* pl, clap_id id, double* v) {
  const int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(pl)->waarde[i].load(std::memory_order_relaxed);
  return true;
}

// Tekst <-> waarde. Zonder printf/strtod: die volgen de locale van de host (een komma in plaats van een punt), en
// dan zou "37,5 %" de ene keer wel en de andere keer niet terugkomen. Elke tekst die value_to_text maakt, geeft via
// text_to_value een waarde die weer precies dezelfde tekst geeft (clap-validator param-conversions en de fuzzer).
enum class Soort { Ms, Hz, Db, Q, Procent, Geheel, Kaal };
Soort soortVan(int i) {
  const char* e = Ontwerp::PARAMS[i].eenheid;
  if (getrapt(i)) return Soort::Geheel;
  if (!std::strcmp(e, "ms")) return Soort::Ms;
  if (!std::strcmp(e, "Hz")) return Soort::Hz;
  if (!std::strcmp(e, "dB")) return Soort::Db;
  if (!std::strcmp(e, "Q")) return Soort::Q;
  if (!*e) return Soort::Procent;  // 0..1 en 0..1.25 zonder eenheid: als percentage
  return Soort::Kaal;
}
// n / 10^dec als tekst met een punt: (375, 1) -> "37.5", (71, 2) -> "0.71"
void tekst(char* t, uint32_t cap, long long n, int dec, const char* eenheid) {
  long long schaal = 1;
  for (int k = 0; k < dec; ++k) schaal *= 10;
  const bool neg = n < 0;
  if (neg) n = -n;
  char getal[48];
  if (dec > 0)
    std::snprintf(getal, sizeof getal, "%s%lld.%0*lld", neg ? "-" : "", n / schaal, dec, n % schaal);
  else
    std::snprintf(getal, sizeof getal, "%s%lld", neg ? "-" : "", n);
  std::snprintf(t, cap, *eenheid ? "%s %s" : "%s", getal, eenheid);
}
long long rond(double x) { return std::llround(x); }
long long minstens(long long n, long long m) { return n < m ? m : n; }
// Per tak wordt één keer afgerond, en op dat gehele getal wordt beslist welke vorm het wordt; zo komt een waarde op
// een grens (999,6 Hz) altijd in dezelfde vorm terug ("1.00 kHz").
bool paramsToText(const clap_plugin_t*, clap_id id, double v, char* t, uint32_t cap) {
  const int i = indexVan(id);
  if (i < 0 || !t || cap == 0) return false;
  if (!std::isfinite(v) || std::fabs(v) > 1e12) return false;
  switch (soortVan(i)) {
    case Soort::Ms: {  // 20.5 ms, 375 ms
      const long long tiendes = rond(v * 10);
      if (tiendes < 1000) tekst(t, cap, tiendes, 1, "ms");
      else tekst(t, cap, minstens(rond(v), 100), 0, "ms");
      break;
    }
    case Soort::Hz: {  // 100 Hz, 3.00 kHz, 12.0 kHz
      const long long hz = rond(v);
      const long long honderdsten = minstens(rond(v / 10), 100);  // in honderdsten kHz
      if (hz < 1000) tekst(t, cap, hz, 0, "Hz");
      else if (honderdsten < 1000) tekst(t, cap, honderdsten, 2, "kHz");
      else tekst(t, cap, minstens(rond(v / 100), 100), 1, "kHz");
      break;
    }
    case Soort::Db: tekst(t, cap, rond(v * 10), 1, "dB"); break;
    case Soort::Q: tekst(t, cap, rond(v * 100), 2, ""); break;
    case Soort::Procent: tekst(t, cap, rond(v * 1000), 1, "%"); break;
    case Soort::Geheel: tekst(t, cap, rond(v), 0, Ontwerp::PARAMS[i].eenheid); break;
    case Soort::Kaal: tekst(t, cap, rond(v * 1000), 3, Ontwerp::PARAMS[i].eenheid); break;
  }
  return true;
}
// "375", "375ms", "0,4 s", "3k", "3.2 kHz", "-6 dB", "37,5 %", "0.71"; hoofdletters doen er niet toe
bool leesGetal(const char*& c, double& x) {
  bool neg = false;
  if (*c == '-' || *c == '+') neg = *c++ == '-';
  double geheel = 0, deel = 0, schaal = 1;
  int cijfers = 0;
  for (; *c >= '0' && *c <= '9'; ++c, ++cijfers) geheel = geheel * 10 + (*c - '0');
  if (*c == '.' || *c == ',') {
    ++c;
    for (; *c >= '0' && *c <= '9'; ++c, ++cijfers) { deel = deel * 10 + (*c - '0'); schaal *= 10; }
  }
  if (cijfers == 0) return false;
  x = geheel + deel / schaal;
  if (neg) x = -x;
  return std::isfinite(x);
}
bool gelijk(const char* a, const char* b) {  // hoofdletterongevoelig
  for (; *a && *b; ++a, ++b) {
    const char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a, y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
    if (x != y) return false;
  }
  return *a == *b;
}
bool paramsFromText(const clap_plugin_t*, clap_id id, const char* t, double* v) {
  const int i = indexVan(id);
  if (i < 0 || !t || !v) return false;
  const char* c = t;
  while (*c == ' ' || *c == '\t') ++c;
  double x;
  if (!leesGetal(c, x)) return false;
  while (*c == ' ' || *c == '\t') ++c;
  char eenheid[16];
  int n = 0;
  while (*c && *c != ' ' && *c != '\t' && n < (int)sizeof eenheid - 1) eenheid[n++] = *c++;
  eenheid[n] = 0;
  while (*c == ' ' || *c == '\t') ++c;
  if (*c) return false;  // er staat nog iets achter
  const bool geen = n == 0;
  switch (soortVan(i)) {
    case Soort::Ms:
      if (gelijk(eenheid, "s") || gelijk(eenheid, "sec")) x *= 1000;
      else if (!geen && !gelijk(eenheid, "ms")) return false;
      break;
    case Soort::Hz:
      if (gelijk(eenheid, "khz") || gelijk(eenheid, "k")) x *= 1000;
      else if (!geen && !gelijk(eenheid, "hz")) return false;
      break;
    case Soort::Db: if (!geen && !gelijk(eenheid, "db")) return false; break;
    case Soort::Q: if (!geen && !gelijk(eenheid, "q")) return false; break;
    case Soort::Procent:  // ook zonder %-teken is het getal een percentage (zo staat het op het scherm)
      if (!geen && !gelijk(eenheid, "%")) return false;
      x /= 100;
      break;
    case Soort::Geheel:
    case Soort::Kaal:
      if (!geen && !gelijk(eenheid, Ontwerp::PARAMS[i].eenheid)) return false;
      break;
  }
  *v = klem(i, x);
  return true;
}

// Buiten process: als de plugin niet actief is op de main thread, als hij actief is op de audiothread tussen twee
// process-aanroepen (nooit tegelijk met process). De waarden gaan meteen naar het Ontwerp; de glijders lopen vanaf
// de volgende sample, net als bij een event in process.
void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
  Plugin* s = zelf(p);
  if (s->teLaden.exchange(false)) laadUitHostKopie(s);
  const uint32_t n = in ? in->size(in) : 0;
  for (uint32_t i = 0; i < n; ++i) doeEvent(s, in->get(in, i));
  if (s->actief) s->staart.store(s->ontwerp.staartSec(), std::memory_order_relaxed);
}
const clap_plugin_params_t PARAMS_EXT = {paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush};

// --- state ----------------------------------------------------------------------------------------------------
// "PBST", versie (u32), aantal (u32), dan per parameter (id u32, waarde f64); little-endian.
void zet32(unsigned char* b, uint32_t v) { for (int k = 0; k < 4; ++k) b[k] = (unsigned char)(v >> (8 * k)); }
uint32_t lees32(const unsigned char* b) {
  uint32_t v = 0;
  for (int k = 0; k < 4; ++k) v |= (uint32_t)b[k] << (8 * k);
  return v;
}
bool schrijfAlles(const clap_ostream_t* os, const unsigned char* b, size_t n) {
  while (n > 0) {
    const int64_t w = os->write(os, b, n);
    if (w <= 0) return false;
    b += w; n -= (size_t)w;
  }
  return true;
}
bool leesAlles(const clap_istream_t* is, unsigned char* b, size_t n) {
  while (n > 0) {
    const int64_t r = is->read(is, b, n);
    if (r <= 0) return false;
    b += r; n -= (size_t)r;
  }
  return true;
}

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* os) {
  const Plugin* s = zelf(p);
  unsigned char b[12 + 12 * N_ZICHTBAAR];
  std::memcpy(b, "PBST", 4);
  zet32(b + 4, STATE_VERSIE);
  zet32(b + 8, (uint32_t)N_ZICHTBAAR);
  for (int k = 0; k < N_ZICHTBAAR; ++k) {
    const int i = zichtbaar((uint32_t)k);
    unsigned char* q = b + 12 + 12 * k;
    zet32(q, Ontwerp::PARAMS[i].id);
    const double v = s->waarde[i].load(std::memory_order_relaxed);
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    zet32(q + 4, (uint32_t)bits);
    zet32(q + 8, (uint32_t)(bits >> 32));
  }
  return schrijfAlles(os, b, sizeof b);
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* is) {
  Plugin* s = zelf(p);
  unsigned char kop[12];
  if (!leesAlles(is, kop, sizeof kop) || std::memcmp(kop, "PBST", 4)) return false;
  const uint32_t versie = lees32(kop + 4), aantal = lees32(kop + 8);
  if (versie < 1 || aantal > 100000) return false;
  double nieuw[N];
  for (int i = 0; i < N; ++i) nieuw[i] = Ontwerp::PARAMS[i].standaard;  // een ontbrekend id houdt zijn standaard
  for (uint32_t k = 0; k < aantal; ++k) {
    unsigned char q[12];
    if (!leesAlles(is, q, sizeof q)) return false;
    const uint64_t bits = (uint64_t)lees32(q + 4) | ((uint64_t)lees32(q + 8) << 32);
    double v;
    std::memcpy(&v, &bits, 8);
    const int i = indexVan(lees32(q));
    if (i >= 0) nieuw[i] = klem(i, v);  // een onbekend id (nieuwere versie) wordt overgeslagen
  }
  for (int i = 0; i < N; ++i) s->waarde[i].store(nieuw[i], std::memory_order_relaxed);
  if (s->actief) {
    s->teLaden.store(true, std::memory_order_release);  // de audiothread neemt ze over aan het begin van process
  } else {
    laadUitHostKopie(s);
  }
  if (s->host) {
    auto* hp = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
    if (hp && hp->rescan) hp->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
  }
  return true;
}
const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- audio-ports ----------------------------------------------------------------------------------------------
uint32_t portsCount(const clap_plugin_t*, bool) { return 1; }
bool portsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (index != 0) return false;
  info->id = 0;
  std::snprintf(info->name, sizeof info->name, "%s", isInput ? "Ingang" : "Uitgang");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = KANALEN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = 0;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {portsCount, portsGet};

// --- latency en tail ------------------------------------------------------------------------------------------
uint32_t latencyGet(const clap_plugin_t* p) { return zelf(p)->ontwerp.latentie(); }
const clap_plugin_latency_t LATENCY = {latencyGet};

uint32_t tailGet(const clap_plugin_t* p) {
  const Plugin* s = zelf(p);
  const double sec = s->staart.load();
  if (sec < 0) return UINT32_MAX;  // oneindig (lusversterking >= 1)
  const double samples = sec * s->sr;
  if (!(samples < 4.0e9)) return UINT32_MAX;
  return samples > 0 ? (uint32_t)(samples + 0.5) : 0;
}
const clap_plugin_tail_t TAIL = {tailGet};

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  return nullptr;
}
void onMainThread(const clap_plugin_t*) {}

// --- factory --------------------------------------------------------------------------------------------------
uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id)) return nullptr;
  auto* s = new (std::nothrow) Plugin();
  if (!s) return nullptr;
  s->host = host;
  for (int i = 0; i < N; ++i) s->waarde[i].store(Ontwerp::PARAMS[i].standaard);
  s->clap = {&DESCRIPTOR, s, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset,
             process, getExtension, onMainThread};
  return &s->clap;
}
const clap_plugin_factory_t FACTORY = {factoryCount, factoryDescriptor, factoryCreate};

}  // namespace

bool pb_entry_init(const char*) { return true; }
void pb_entry_deinit(void) {}
const void* pb_entry_get_factory(const char* id) {
  return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &FACTORY : nullptr;
}
