// dub_echo.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
// Fase 1: de DSP is aangesloten. Parameters zijn CLAP-params (id = nummer in SPEC.md, waarden in hun eigen eenheid),
// events gaan op hun sample (het blok wordt bij elk event gesplitst), state zoals ARCHITECTUUR.md.
// Fase 2: tekst <-> waarde in gewone eenheden (ms, Hz/kHz, dB, Q, %), stapjes voor parameters met de vlag STAPPEN,
// params.flush buiten process. Met -DPB_ZONDER_PARAM=<id> bouw je een "oudere" versie zonder die parameter
// (alleen voor de state-toets: state van zo'n build moet in de volledige build laden).
// Fase 3: het frontpaneel (design.md) via de CLAP gui-extensie, api "cocoa" (macOS; met -DPB_MET_UI). De UI stuurt
// gebaren en waarden door een lock-free wachtrij naar de audiothread; die past ze in process of params.flush toe zoals
// een host-event en meldt ze de host als CLAP_EVENT_PARAM_GESTURE_BEGIN / PARAM_VALUE / GESTURE_END (out_events).
// De UI raakt de DSP nooit aan.
// Fase 4: de fabriekspresets uit presets/*.json (presets.h, door CMake in een tabel gezet) zitten in de plugin zelf:
// de preset-discovery-factory meldt per preset een provider met een locatie van soort PLUGIN (location = nullptr) en
// de preset als load_key (de bestandsnaam), en de preset-load-extensie laadt ze zoals een state: een parameter die de preset niet noemt
// krijgt zijn standaard. clap-wrapper (patch) maakt er AU factory presets van.
// Fase 5: mono en stereo. Drie poortindelingen (stereo, mono, mono -> stereo) via audio-ports-config (en
// configurable-audio-ports, dat clap-wrapper voor VST3 gebruikt; de AU kiest via een patch op clap-wrapper). De DSP
// rekent met zoveel kanalen als de uitgang heeft (1 of 2). Bypass is een eigen parameter (id 9, CLAP_PARAM_IS_BYPASS):
// clap-wrapper maakt er de VST3-bypass en kAudioUnitProperty_BypassEffect van. De DSP blijft doorrekenen; de uitgang
// schuift in 10 ms naar de droge invoer en terug (geen tik), en is in rust bit voor bit de DSP of bit voor bit de invoer.
#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "entry.h"
#include "ontwerpen/keten_dub_echo.h"
#include "presets.h"
#include "ui/paneel.h"
#ifdef PB_MET_UI
#include "ui/paneel_mac.h"
#endif

namespace {

using pb::Ontwerp;

#ifdef PB_KANARIE
// De kanarie voor RTSan: een allocatie in process, met de pointer in een globale variabele zodat de compiler hem
// niet weg kan laten. Een RTSan-run die deze build niet afbreekt, bewaakt niets.
void* volatile g_kanarie = nullptr;
#endif

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO,
                                CLAP_PLUGIN_FEATURE_MONO,
                                nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT,       Ontwerp::META.clapId, Ontwerp::META.naam, Ontwerp::META.maker, Ontwerp::META.url, "", "",
    Ontwerp::META.versie,    Ontwerp::META.uitleg, FEATURES};

constexpr uint32_t KANALEN = 2;  // de DSP rekent met hoogstens 2 kanalen

// fase 5: de poortindelingen (id = index; nooit hernummeren). Stereo is de standaard.
struct Indeling { clap_id id; const char* naam; uint32_t in, uit; };
constexpr Indeling INDELINGEN[] = {{0, "Stereo", 2, 2}, {1, "Mono", 1, 1}, {2, "Mono naar stereo", 1, 2}};
constexpr uint32_t N_INDELINGEN = sizeof INDELINGEN / sizeof INDELINGEN[0];

// fase 5: bypass, een parameter van de schil (niet van de DSP). Id 9: het eerste vrije nummer na SPEC.md.
constexpr clap_id BYPASS_ID = 9;
constexpr double BYPASS_SEC = 0.010;  // overgang nat <-> droog
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

// Van de UI (main thread, één schrijver) naar de audiothread (process of flush, één lezer tegelijk). Vaste grootte,
// geen locks: de lezer alloceert en wacht nooit.
struct UiBericht {
  enum Soort : uint8_t { BEGIN, WAARDE, EINDE } soort;
  uint8_t index;  // in PARAMS
  double waarde;
};
struct UiWachtrij {
  static constexpr uint32_t GROOTTE = 4096;  // macht van 2
  UiBericht ring[GROOTTE];
  std::atomic<uint32_t> kop{0};     // alleen de UI schrijft
  std::atomic<uint32_t> staart{0};  // alleen de audiothread schrijft
  // `reserve`: zoveel plekken moeten daarna nog vrij blijven (zodat een gebaar altijd nog kan eindigen)
  bool zet(const UiBericht& b, uint32_t reserve) {
    const uint32_t k = kop.load(std::memory_order_relaxed);
    const uint32_t st = staart.load(std::memory_order_acquire);
    if (GROOTTE - (k - st) < 1 + reserve) return false;
    ring[k & (GROOTTE - 1)] = b;
    kop.store(k + 1, std::memory_order_release);
    return true;
  }
  bool haal(UiBericht& b) {
    const uint32_t st = staart.load(std::memory_order_relaxed);
    if (st == kop.load(std::memory_order_acquire)) return false;
    b = ring[st & (GROOTTE - 1)];
    staart.store(st + 1, std::memory_order_release);
    return true;
  }
};

struct Plugin;
struct PluginBediening final : pb_ui::Bediening {
  Plugin* s = nullptr;
  int aantal() const override;
  pb_ui::Knop knop(int k) const override;
  double waarde(int k) const override;
  bool wachtend(int k) const override;
  void alsTekst(int k, double v, char* t, uint32_t cap) const override;
  void gebaarBegin(int k) override;
  void zet(int k, double v) override;
  void gebaarEinde(int k) override;
  bool stuur(const UiBericht& b, uint32_t reserve);
};

struct Plugin {
  clap_plugin_t clap{};
  const clap_host_t* host = nullptr;
  Ontwerp ontwerp;                        // groot: Plugin wordt op de heap gemaakt (main thread)
  std::vector<unsigned char> geheugen;    // vooraf gereserveerd in activate, nooit in process
  std::vector<float> kopie;               // invoerkopie (vooraf gereserveerd); de DSP leest altijd hieruit
  double sr = 48000;
  bool actief = false;                    // alleen de main thread leest en schrijft dit
  uint32_t indeling = 0;                  // index in INDELINGEN; main thread, alleen te wisselen als niet actief
  // bypass: de hostwaarde (atomisch, zoals `waarde`), het doel en de stand van de overgang (audiothread, of main
  // thread als de plugin niet actief is). bypassMix 0 = nat (de DSP), 1 = droog (de invoer).
  std::atomic<double> bypass{0.0};
  bool bypassDoel = false;
  double bypassMix = 0.0;
  bool bypassVers = true;  // nog geen audio sinds activate/reset: de bypass springt (zoals de glijders van de DSP)
  std::atomic<double> staart{0.0};        // uitgerekend op de audiothread / in activate, gelezen door tail.get
  // De waarde per parameter zoals de host hem ziet (get_value, state save). De DSP krijgt zijn waarden alleen via
  // events (process/flush) of, bij een state load terwijl hij draait, via `teLaden` aan het begin van process.
  std::atomic<double> waarde[N];
  std::atomic<bool> teLaden{false};
  // fase 3: de UI
  const clap_host_params_t* hostParams = nullptr;  // opgehaald in init (main thread)
  UiWachtrij ui;
  std::atomic<uint32_t> uiGezet[N];     // waarden die de UI stuurde (main thread telt op)
  std::atomic<uint32_t> uiVerwerkt[N];  // waarden die de audiothread toepaste (audiothread telt op)
  bool uiGebaar[N] = {};                // main thread: een gebaar is bij de wachtrij begonnen
  bool uiVerloren[N] = {};              // main thread: het begin paste niet meer; tot het einde vervalt alles
  PluginBediening bediening;
  void* view = nullptr;                 // de NSView (main thread)
  uint32_t guiB = (uint32_t)pb_ui::BASIS_B, guiH = (uint32_t)pb_ui::BASIS_H;
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
  if (e->param_id == BYPASS_ID) {
    const double v = e->value >= 0.5 ? 1.0 : 0.0;  // NaN telt als uit
    s->bypass.store(v, std::memory_order_relaxed);
    s->bypassDoel = v != 0;
    return;
  }
  const int i = indexVan(e->param_id);
  if (i >= 0) zet(s, i, e->value);
}

// Alles wat de UI stuurde: toepassen zoals een host-event (op sample 0 van dit blok, vóór de host-events) en de host
// melden via out_events, zodat hij het opneemt (automatisering, undo) en zijn eigen kopie bijwerkt. Audiothread (of
// main thread via flush als de plugin niet actief is); de gebeurtenissen staan op de stack, er wordt niets gealloceerd.
void verwerkUi(Plugin* s, const clap_output_events_t* uit) {
  UiBericht b;
  while (s->ui.haal(b)) {
    const int i = b.index;
    if (i >= N || !bestaat(i)) continue;
    const clap_id id = Ontwerp::PARAMS[i].id;
    if (b.soort == UiBericht::WAARDE) {
      zet(s, i, b.waarde);
      if (uit) {
        clap_event_param_value_t e;
        std::memset(&e, 0, sizeof e);
        e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id;
        e.cookie = nullptr;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = s->waarde[i].load(std::memory_order_relaxed);
        uit->try_push(uit, &e.header);
      }
      s->uiVerwerkt[i].fetch_add(1, std::memory_order_release);
    } else if (uit) {
      clap_event_param_gesture_t g;
      std::memset(&g, 0, sizeof g);
      g.header = {sizeof g, 0, CLAP_CORE_EVENT_SPACE_ID,
                  (uint16_t)(b.soort == UiBericht::BEGIN ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END), 0};
      g.param_id = id;
      uit->try_push(uit, &g.header);
    }
  }
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
  s->bypassDoel = s->bypass.load(std::memory_order_relaxed) != 0;
}

// --- plugin -------------------------------------------------------------------------------------------------
bool init(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  if (s->host && s->host->get_extension)
    s->hostParams = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
  return true;
}
void destroy(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
#ifdef PB_MET_UI
  if (s->view) pb_ui::sluit(s->view);  // een host hoort gui.destroy eerst te doen; voor de zekerheid
  s->view = nullptr;
#endif
  delete s;
}

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
  s->bypassMix = s->bypassDoel ? 1.0 : 0.0;  // na activate geen overgang: meteen nat of droog
  s->bypassVers = true;
  s->staart.store(s->bypassDoel ? 0.0 : s->ontwerp.staartSec());
  s->actief = true;
  return true;
}
void deactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  s->ontwerp.reset();
  s->bypassMix = s->bypassDoel ? 1.0 : 0.0;
  s->bypassVers = true;
}

clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
#ifdef PB_KANARIE
  g_kanarie = std::malloc(64);
#endif
  Plugin* s = zelf(p);
  if (s->teLaden.exchange(false, std::memory_order_acquire)) laadUitHostKopie(s);
  verwerkUi(s, pr->out_events);

  const uint32_t n = pr->frames_count;
  const clap_input_events_t* ev = pr->in_events;
  const uint32_t nEv = ev ? ev->size(ev) : 0;
  uint32_t e = 0;

  // Kanalen: de DSP rekent met zoveel kanalen als de uitgang heeft (stereo of mono). Bij mono -> stereo krijgen beide
  // DSP-ingangen het ene invoerkanaal. Een ontbrekende invoer is stilte.
  float* uit[KANALEN] = {nullptr, nullptr};
  const float* in[KANALEN] = {nullptr, nullptr};
  uint32_t kan = 0;
  if (pr->audio_outputs_count >= 1 && pr->audio_outputs[0].data32 && pr->audio_outputs[0].channel_count > 0) {
    const clap_audio_buffer_t& u = pr->audio_outputs[0];
    kan = u.channel_count >= KANALEN ? KANALEN : u.channel_count;
    const clap_audio_buffer_t* ib = pr->audio_inputs_count > 0 ? &pr->audio_inputs[0] : nullptr;
    const bool metIn = ib && ib->data32 && ib->channel_count > 0;
    for (uint32_t c = 0; c < kan; ++c) {
      uit[c] = u.data32[c];
      if (metIn) in[c] = ib->data32[c < ib->channel_count ? c : ib->channel_count - 1];
      if (!uit[c]) kan = 0;
    }
  }
  // De DSP leest altijd uit een vooraf gereserveerde kopie van de invoer. Hij schrijft per sample kanaal 0 vóór hij
  // kanaal 1 leest, dus een hostbuffer die invoer én uitgang is (in-place, of bij mono -> stereo één buffer voor beide
  // DSP-ingangen die ook uitgang 0 is) zou anders halverwege overschreven zijn. De kopie is ook het droge signaal voor
  // de bypass.
  const uint32_t stuk = (uint32_t)(s->kopie.size() / KANALEN);
  const double bypassStap = 1.0 / (BYPASS_SEC * s->sr);

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
    // vóór de eerste sample na activate/reset springt de bypass (een host die hem pas in het eerste blok stuurt,
    // krijgt hetzelfde als een host die hem vóór activate zette)
    if (s->bypassVers) s->bypassMix = s->bypassDoel ? 1.0 : 0.0;
    uint32_t tot = n;
    if (e < nEv) {
      const uint32_t t = ev->get(ev, e)->time;
      if (t < tot) tot = t;
    }
    if (tot - pos > stuk) tot = pos + stuk;
    const uint32_t m = tot - pos;
    if (kan > 0) {
      float* k[KANALEN];
      float* ku[KANALEN] = {nullptr, nullptr};
      for (uint32_t c = 0; c < kan; ++c) {
        k[c] = s->kopie.data() + (size_t)c * stuk;
        if (in[c]) std::memcpy(k[c], in[c] + pos, m * sizeof(float));
        else std::memset(k[c], 0, m * sizeof(float));
        ku[c] = uit[c] + pos;
      }
      const float* ki[KANALEN] = {k[0], kan > 1 ? k[1] : nullptr};
      s->ontwerp.verwerk(ki, ku, (int)kan, m);
      // bypass: in rust bit voor bit nat (mix 0) of bit voor bit droog (mix 1); daartussen een lineaire overgang
      if (s->bypassDoel || s->bypassMix > 0.0) {
        double mix = s->bypassMix;
        for (uint32_t i = 0; i < m; ++i) {
          mix = s->bypassDoel ? (mix + bypassStap < 1.0 ? mix + bypassStap : 1.0)
                              : (mix - bypassStap > 0.0 ? mix - bypassStap : 0.0);
          for (uint32_t c = 0; c < kan; ++c) {
            if (mix >= 1.0) ku[c][i] = k[c][i];
            else if (mix > 0.0) ku[c][i] = (float)((1.0 - mix) * (double)ku[c][i] + mix * (double)k[c][i]);
          }
        }
        s->bypassMix = mix;
      }
      // pas na de bypass: ook een subnormale invoer (droog) of een overgang mag niet subnormaal naar buiten
      for (uint32_t c = 0; c < kan; ++c) poetsUitvoer(ku[c], m);
      s->bypassVers = false;
    }
    pos = tot;
  }
  // in volle bypass is de uitgang de invoer: geen staart
  s->staart.store(s->bypassDoel && s->bypassMix >= 1.0 ? 0.0 : s->ontwerp.staartSec(), std::memory_order_relaxed);
  return CLAP_PROCESS_CONTINUE;
}

// --- params ---------------------------------------------------------------------------------------------------
// De parameters van de DSP (in de volgorde van SPEC.md), dan als laatste Bypass.
uint32_t paramsCount(const clap_plugin_t*) { return (uint32_t)N_ZICHTBAAR + 1; }
bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (index == (uint32_t)N_ZICHTBAAR) {
    std::memset(info, 0, sizeof *info);
    info->id = BYPASS_ID;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS;
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof info->name, "Bypass");
    info->module[0] = 0;
    info->min_value = 0;
    info->max_value = 1;
    info->default_value = 0;
    return true;
  }
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
  if (id == BYPASS_ID) {
    *v = zelf(pl)->bypass.load(std::memory_order_relaxed);
    return true;
  }
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
  if ((i < 0 && id != BYPASS_ID) || !t || cap == 0) return false;
  if (!std::isfinite(v) || std::fabs(v) > 1e12) return false;
  if (id == BYPASS_ID) {  // Aan / Uit
    std::snprintf(t, cap, "%s", v >= 0.5 ? "Aan" : "Uit");
    return true;
  }
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
  if ((i < 0 && id != BYPASS_ID) || !t || !v) return false;
  const char* c = t;
  while (*c == ' ' || *c == '\t') ++c;
  if (id == BYPASS_ID) {  // aan/uit, on/off, of een getal (>= 0,5 = aan)
    char w[16];
    int n = 0;
    while (*c && *c != ' ' && *c != '\t' && n < (int)sizeof w - 1) w[n++] = *c++;
    w[n] = 0;
    while (*c == ' ' || *c == '\t') ++c;
    if (*c) return false;
    if (gelijk(w, "aan") || gelijk(w, "on")) { *v = 1; return true; }
    if (gelijk(w, "uit") || gelijk(w, "off")) { *v = 0; return true; }
    const char* q = w;
    double x;
    if (!leesGetal(q, x) || *q) return false;
    *v = x >= 0.5 ? 1 : 0;
    return true;
  }
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
void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* uit) {
  Plugin* s = zelf(p);
  if (s->teLaden.exchange(false)) laadUitHostKopie(s);
  verwerkUi(s, uit);
  const uint32_t n = in ? in->size(in) : 0;
  for (uint32_t i = 0; i < n; ++i) doeEvent(s, in->get(in, i));
  if (s->actief) s->staart.store(s->bypassDoel && s->bypassMix >= 1.0 ? 0.0 : s->ontwerp.staartSec(), std::memory_order_relaxed);
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
  unsigned char b[12 + 12 * (N_ZICHTBAAR + 1)];  // de parameters van de DSP en Bypass
  std::memcpy(b, "PBST", 4);
  zet32(b + 4, STATE_VERSIE);
  zet32(b + 8, (uint32_t)N_ZICHTBAAR + 1);
  for (int k = 0; k <= N_ZICHTBAAR; ++k) {
    const int i = k < N_ZICHTBAAR ? zichtbaar((uint32_t)k) : -1;
    unsigned char* q = b + 12 + 12 * k;
    zet32(q, i >= 0 ? Ontwerp::PARAMS[i].id : BYPASS_ID);
    const double v = i >= 0 ? s->waarde[i].load(std::memory_order_relaxed) : s->bypass.load(std::memory_order_relaxed);
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    zet32(q + 4, (uint32_t)bits);
    zet32(q + 8, (uint32_t)(bits >> 32));
  }
  return schrijfAlles(os, b, sizeof b);
}

void pasToe(Plugin* s, const double* nieuw, double bypass);
bool stateLoad(const clap_plugin_t* p, const clap_istream_t* is) {
  Plugin* s = zelf(p);
  unsigned char kop[12];
  if (!leesAlles(is, kop, sizeof kop) || std::memcmp(kop, "PBST", 4)) return false;
  const uint32_t versie = lees32(kop + 4), aantal = lees32(kop + 8);
  if (versie < 1 || aantal > 100000) return false;
  double nieuw[N];
  for (int i = 0; i < N; ++i) nieuw[i] = Ontwerp::PARAMS[i].standaard;  // een ontbrekend id houdt zijn standaard
  double bypass = 0;
  for (uint32_t k = 0; k < aantal; ++k) {
    unsigned char q[12];
    if (!leesAlles(is, q, sizeof q)) return false;
    const uint64_t bits = (uint64_t)lees32(q + 4) | ((uint64_t)lees32(q + 8) << 32);
    double v;
    std::memcpy(&v, &bits, 8);
    const int i = indexVan(lees32(q));
    if (i >= 0) nieuw[i] = klem(i, v);  // een onbekend id (nieuwere versie) wordt overgeslagen
    else if (lees32(q) == BYPASS_ID) bypass = v >= 0.5 ? 1 : 0;
  }
  pasToe(s, nieuw, bypass);
  return true;
}
const clap_plugin_state_t STATE = {stateSave, stateLoad};

// Nieuwe waarden voor alle parameters (state of preset; main thread). Draait de plugin, dan neemt de audiothread ze
// over aan het begin van de volgende process (of flush); anders gaan ze meteen naar het Ontwerp.
void pasToe(Plugin* s, const double* nieuw, double bypass) {
  for (int i = 0; i < N; ++i) s->waarde[i].store(nieuw[i], std::memory_order_relaxed);
  s->bypass.store(bypass, std::memory_order_relaxed);
  if (s->actief) {
    s->teLaden.store(true, std::memory_order_release);  // de audiothread neemt ze over aan het begin van process
  } else {
    laadUitHostKopie(s);
  }
  if (s->host) {
    auto* hp = static_cast<const clap_host_params_t*>(s->host->get_extension(s->host, CLAP_EXT_PARAMS));
    if (hp && hp->rescan) hp->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
  }
}

// --- presets (fase 4) -----------------------------------------------------------------------------------------
int presetVan(const char* sleutel) {
  for (unsigned k = 0; sleutel && k < pb_presets::AANTAL; ++k)
    if (!std::strcmp(pb_presets::LIJST[k].sleutel, sleutel)) return (int)k;
  return -1;
}
int indexVanNaam(const char* naam) {
  for (int i = 0; i < N; ++i)
    if (!std::strcmp(Ontwerp::PARAMS[i].naam, naam) && bestaat(i)) return i;
  return -1;
}
const clap_host_preset_load_t* hostPresetLoad(const Plugin* s) {
  if (!s->host || !s->host->get_extension) return nullptr;
  auto* h = static_cast<const clap_host_preset_load_t*>(s->host->get_extension(s->host, CLAP_EXT_PRESET_LOAD));
  if (!h) h = static_cast<const clap_host_preset_load_t*>(s->host->get_extension(s->host, CLAP_EXT_PRESET_LOAD_COMPAT));
  return h;
}
// [main thread] Alleen de eigen locatie (soort PLUGIN, location nullptr) met een bekende load_key.
bool presetLaden(const clap_plugin_t* p, uint32_t soort, const char* plek, const char* sleutel) {
  Plugin* s = zelf(p);
  const clap_host_preset_load_t* h = hostPresetLoad(s);
  const int k = (soort == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && !plek) ? presetVan(sleutel) : -1;
  if (k < 0) {
    if (h && h->on_error) h->on_error(s->host, soort, plek, sleutel, 0, "geen fabriekspreset van Dub-echo");
    return false;
  }
  const pb_presets::Preset& pr = pb_presets::LIJST[k];
  double nieuw[N];
  for (int i = 0; i < N; ++i) nieuw[i] = Ontwerp::PARAMS[i].standaard;  // wat de preset niet noemt: standaard
  for (unsigned j = 0; j < pr.aantal; ++j) {
    const int i = indexVanNaam(pr.waarden[j].param);
    if (i >= 0) nieuw[i] = klem(i, pr.waarden[j].waarde);
  }
  pasToe(s, nieuw, s->bypass.load(std::memory_order_relaxed));  // een preset laat de bypass zoals hij staat
  if (h && h->loaded) h->loaded(s->host, soort, plek, sleutel);
  return true;
}
const clap_plugin_preset_load_t PRESET_LOAD = {presetLaden};

// De preset-discovery-factory: per fabriekspreset één provider, met één locatie in de plugin (soort PLUGIN, location
// nullptr) en daarin die ene preset (load_key = de bestandsnaam). Eén provider met alle presets als container zou de
// gewone vorm zijn, maar clap-validator 0.4.1 loopt daar vast: begin_preset houdt een mutex vast terwijl het de vorige
// preset wegschrijft (flush_preset pakt dezelfde mutex), dus vanaf de tweede preset in een container blijft hij hangen.
// Zo is elke container één preset, en klopt het nog steeds met CLAP.
struct ProviderInfo {
  std::string id, naam;
  clap_preset_discovery_provider_descriptor_t desc;
};
const std::vector<ProviderInfo>& providers() {  // main thread; één keer opgebouwd
  static const std::vector<ProviderInfo> lijst = [] {
    std::vector<ProviderInfo> v(pb_presets::AANTAL);
    for (unsigned k = 0; k < pb_presets::AANTAL; ++k) {
      v[k].id = std::string(PB_CLAP_ID ".preset.") + pb_presets::LIJST[k].sleutel;
      v[k].naam = std::string(PB_NAAM ": ") + pb_presets::LIJST[k].naam;
      v[k].desc = {CLAP_VERSION_INIT, v[k].id.c_str(), v[k].naam.c_str(), PB_MAKER};
    }
    return v;
  }();
  return lijst;
}
const clap_preset_discovery_location_t LOCATIE = {CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Fabriekspresets",
                                                  CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
struct Provider {
  clap_preset_discovery_provider_t clap;
  const clap_preset_discovery_indexer_t* indexer;
  unsigned preset;  // in pb_presets::LIJST
};
bool providerInit(const clap_preset_discovery_provider_t* p) {
  const auto* ix = static_cast<const Provider*>(p->provider_data)->indexer;
  return ix && ix->declare_location && ix->declare_location(ix, &LOCATIE);
}
void providerWeg(const clap_preset_discovery_provider_t* p) { delete static_cast<Provider*>(p->provider_data); }
bool providerMetadata(const clap_preset_discovery_provider_t* p, uint32_t soort, const char* plek,
                      const clap_preset_discovery_metadata_receiver_t* r) {
  if (soort != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || plek || !r) return false;
  const pb_presets::Preset& pr = pb_presets::LIJST[static_cast<const Provider*>(p->provider_data)->preset];
  if (!r->begin_preset(r, pr.naam, pr.sleutel)) return true;  // de host wil hem niet
  const clap_universal_plugin_id_t id = {"clap", DESCRIPTOR.id};
  r->add_plugin_id(r, &id);
  r->set_flags(r, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
  r->add_creator(r, Ontwerp::META.maker);
  if (*pr.uitleg) r->set_description(r, pr.uitleg);
  return true;
}
const void* providerExt(const clap_preset_discovery_provider_t*, const char*) { return nullptr; }

uint32_t pdCount(const clap_preset_discovery_factory_t*) { return (uint32_t)providers().size(); }
const clap_preset_discovery_provider_descriptor_t* pdDescriptor(const clap_preset_discovery_factory_t*, uint32_t i) {
  return i < providers().size() ? &providers()[i].desc : nullptr;
}
const clap_preset_discovery_provider_t* pdCreate(const clap_preset_discovery_factory_t*,
                                                 const clap_preset_discovery_indexer_t* ix, const char* id) {
  for (unsigned k = 0; id && k < providers().size(); ++k) {
    if (providers()[k].id != id) continue;
    auto* pv = new (std::nothrow) Provider();
    if (!pv) return nullptr;
    pv->indexer = ix;
    pv->preset = k;
    pv->clap = {&providers()[k].desc, pv, providerInit, providerWeg, providerMetadata, providerExt};
    return &pv->clap;
  }
  return nullptr;
}
const clap_preset_discovery_factory_t PRESET_FACTORY = {pdCount, pdDescriptor, pdCreate};

// --- audio-ports, audio-ports-config, configurable-audio-ports (fase 5) ---------------------------------------
// Eén hoofdingang en één hoofduitgang; hoeveel kanalen hangt af van de gekozen indeling. De host kiest alleen als de
// plugin niet actief is (CLAP: [main-thread && !active]).
void poortInfo(const Indeling& d, bool isInput, clap_audio_port_info_t* info) {
  const uint32_t k = isInput ? d.in : d.uit;
  info->id = 0;
  std::snprintf(info->name, sizeof info->name, "%s", isInput ? "Ingang" : "Uitgang");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = k;
  info->port_type = k == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  info->in_place_pair = d.in == d.uit ? 0 : CLAP_INVALID_ID;  // in-place alleen bij evenveel kanalen
}
uint32_t portsCount(const clap_plugin_t*, bool) { return 1; }
bool portsGet(const clap_plugin_t* p, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (index != 0) return false;
  poortInfo(INDELINGEN[zelf(p)->indeling], isInput, info);
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {portsCount, portsGet};

uint32_t configCount(const clap_plugin_t*) { return N_INDELINGEN; }
bool configGet(const clap_plugin_t*, uint32_t index, clap_audio_ports_config_t* c) {
  if (index >= N_INDELINGEN) return false;
  const Indeling& d = INDELINGEN[index];
  std::memset(c, 0, sizeof *c);
  c->id = d.id;
  std::snprintf(c->name, sizeof c->name, "%s", d.naam);
  c->input_port_count = 1;
  c->output_port_count = 1;
  c->has_main_input = true;
  c->main_input_channel_count = d.in;
  c->main_input_port_type = d.in == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  c->has_main_output = true;
  c->main_output_channel_count = d.uit;
  c->main_output_port_type = d.uit == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
  return true;
}
bool configSelect(const clap_plugin_t* p, clap_id id) {
  Plugin* s = zelf(p);
  if (s->actief || id >= N_INDELINGEN) return false;
  s->indeling = id;
  return true;
}
const clap_plugin_audio_ports_config_t PORTS_CONFIG = {configCount, configGet, configSelect};

clap_id configHuidig(const clap_plugin_t* p) { return INDELINGEN[zelf(p)->indeling].id; }
bool configInfo(const clap_plugin_t*, clap_id id, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (id >= N_INDELINGEN || index != 0) return false;
  poortInfo(INDELINGEN[id], isInput, info);
  return true;
}
const clap_plugin_audio_ports_config_info_t PORTS_CONFIG_INFO = {configHuidig, configInfo};

// configurable-audio-ports: de gevraagde kanalen per poort, uitgaande van de huidige indeling. Alleen poort 0, 1 of 2
// kanalen (een opgegeven port_type moet erbij passen), en het resultaat moet een van de INDELINGEN zijn.
int indelingVoor(const Plugin* s, const clap_audio_port_configuration_request_t* r, uint32_t n) {
  uint32_t in = INDELINGEN[s->indeling].in, uit = INDELINGEN[s->indeling].uit;
  for (uint32_t k = 0; k < n; ++k) {
    if (r[k].port_index != 0 || r[k].channel_count < 1 || r[k].channel_count > 2) return -1;
    if (r[k].port_type && std::strcmp(r[k].port_type, r[k].channel_count == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO)) return -1;
    (r[k].is_input ? in : uit) = r[k].channel_count;
  }
  for (uint32_t d = 0; d < N_INDELINGEN; ++d)
    if (INDELINGEN[d].in == in && INDELINGEN[d].uit == uit) return (int)d;
  return -1;
}
bool kanToepassen(const clap_plugin_t* p, const clap_audio_port_configuration_request_t* r, uint32_t n) {
  return !zelf(p)->actief && indelingVoor(zelf(p), r, n) >= 0;
}
bool pasIndelingToe(const clap_plugin_t* p, const clap_audio_port_configuration_request_t* r, uint32_t n) {
  Plugin* s = zelf(p);
  const int d = s->actief ? -1 : indelingVoor(s, r, n);
  if (d < 0) return false;
  s->indeling = (uint32_t)d;
  return true;
}
const clap_plugin_configurable_audio_ports_t CONFIGURABLE_PORTS = {kanToepassen, pasIndelingToe};

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

// --- de UI (main thread) ---------------------------------------------------------------------------------------
int PluginBediening::aantal() const { return N_ZICHTBAAR; }
pb_ui::Knop PluginBediening::knop(int k) const {
  const int i = zichtbaar((uint32_t)k);
  const auto& p = Ontwerp::PARAMS[i < 0 ? 0 : i];
  const Soort so = soortVan(i < 0 ? 0 : i);
  return {p.id, p.naam, p.uitleg, p.min, p.max, p.standaard, so == Soort::Hz || so == Soort::Ms};
}
double PluginBediening::waarde(int k) const {
  const int i = zichtbaar((uint32_t)k);
  return i < 0 ? 0 : s->waarde[i].load(std::memory_order_relaxed);
}
bool PluginBediening::wachtend(int k) const {
  const int i = zichtbaar((uint32_t)k);
  return i >= 0 && s->uiGezet[i].load(std::memory_order_relaxed) != s->uiVerwerkt[i].load(std::memory_order_acquire);
}
void PluginBediening::alsTekst(int k, double v, char* t, uint32_t cap) const {
  const int i = zichtbaar((uint32_t)k);
  if (i < 0 || !paramsToText(&s->clap, Ontwerp::PARAMS[i].id, v, t, cap)) std::snprintf(t, cap, "-");
}
// De wachtrij raakt alleen vol als de host lang geen process of flush aanroept. Dan vervalt wat niet past, maar een
// gebaar blijft altijd heel: een begin houdt 16 plekken vrij, een waarde 8, een einde past dus altijd; en past een
// begin niet, dan vervalt alles tot het bijbehorende einde.
bool PluginBediening::stuur(const UiBericht& b, uint32_t reserve) {
  if (b.soort == UiBericht::WAARDE) s->uiGezet[b.index].fetch_add(1, std::memory_order_relaxed);
  if (!s->ui.zet(b, reserve)) {
    if (b.soort == UiBericht::WAARDE) s->uiGezet[b.index].fetch_sub(1, std::memory_order_relaxed);
    return false;
  }
  // de host plant process of params.flush; daar gaat het naar de DSP en naar de host
  if (s->hostParams && s->hostParams->request_flush) s->hostParams->request_flush(s->host);
  return true;
}
void PluginBediening::gebaarBegin(int k) {
  const int i = zichtbaar((uint32_t)k);
  if (i < 0) return;
  if (s->uiGebaar[i]) gebaarEinde(k);  // nooit twee gebaren tegelijk op één parameter
  const bool gelukt = stuur({UiBericht::BEGIN, (uint8_t)i, 0}, 16);
  s->uiGebaar[i] = gelukt;
  s->uiVerloren[i] = !gelukt;
}
void PluginBediening::zet(int k, double v) {
  const int i = zichtbaar((uint32_t)k);
  if (i >= 0 && !s->uiVerloren[i]) stuur({UiBericht::WAARDE, (uint8_t)i, klem(i, v)}, 8);
}
void PluginBediening::gebaarEinde(int k) {
  const int i = zichtbaar((uint32_t)k);
  if (i < 0) return;
  if (s->uiGebaar[i]) stuur({UiBericht::EINDE, (uint8_t)i, 0}, 0);
  s->uiGebaar[i] = false;
  s->uiVerloren[i] = false;
}

#ifdef PB_MET_UI
// --- gui (CLAP gui-extensie; clap-wrapper geeft hem door aan de AU en de VST3) ---------------------------------
// Alleen "cocoa", ingebed. Maten in logische punten (op macOS doet set_scale niets).
bool guiApi(const clap_plugin_t*, const char* api, bool zwevend) {
  return !zwevend && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA);
}
bool guiVoorkeur(const clap_plugin_t*, const char** api, bool* zwevend) {
  *api = CLAP_WINDOW_API_COCOA;
  *zwevend = false;
  return true;
}
bool guiMaak(const clap_plugin_t* p, const char* api, bool zwevend) {
  Plugin* s = zelf(p);
  if (!guiApi(p, api, zwevend) || s->view) return false;
  s->view = pb_ui::maakView(&s->bediening, s->guiB, s->guiH);
  return s->view != nullptr;
}
void guiWeg(const clap_plugin_t* p) {
  Plugin* s = zelf(p);
  if (s->view) pb_ui::sluit(s->view);
  s->view = nullptr;
}
bool guiSchaal(const clap_plugin_t*, double) { return false; }
bool guiMaat(const clap_plugin_t* p, uint32_t* b, uint32_t* h) {
  *b = zelf(p)->guiB;
  *h = zelf(p)->guiH;
  return true;
}
bool guiKanSchalen(const clap_plugin_t*) { return true; }
bool guiHints(const clap_plugin_t*, clap_gui_resize_hints_t* h) {
  h->can_resize_horizontally = true;
  h->can_resize_vertically = true;
  h->preserve_aspect_ratio = false;  // het paneel schaalt gelijkmatig mee en blijft in het midden
  h->aspect_ratio_width = 0;
  h->aspect_ratio_height = 0;
  return true;
}
bool guiPasMaat(const clap_plugin_t*, uint32_t* b, uint32_t* h) {
  pb_ui::klemMaat(b, h);
  return true;
}
bool guiZetMaat(const clap_plugin_t* p, uint32_t b, uint32_t h) {
  Plugin* s = zelf(p);
  s->guiB = b;
  s->guiH = h;
  if (s->view) pb_ui::zetMaat(s->view, b, h);
  return true;
}
bool guiOuder(const clap_plugin_t* p, const clap_window_t* w) {
  Plugin* s = zelf(p);
  if (!s->view || !w || !w->api || std::strcmp(w->api, CLAP_WINDOW_API_COCOA)) return false;
  return pb_ui::zetOuder(s->view, w->cocoa);
}
bool guiTijdelijk(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiTitel(const clap_plugin_t*, const char*) {}
bool guiToon(const clap_plugin_t* p) {
  if (!zelf(p)->view) return false;
  pb_ui::toon(zelf(p)->view, true);
  return true;
}
bool guiVerberg(const clap_plugin_t* p) {
  if (!zelf(p)->view) return false;
  pb_ui::toon(zelf(p)->view, false);
  return true;
}
const clap_plugin_gui_t GUI = {guiApi,      guiVoorkeur, guiMaak,     guiWeg,       guiSchaal,   guiMaat,
                               guiKanSchalen, guiHints,  guiPasMaat,  guiZetMaat,   guiOuder,    guiTijdelijk,
                               guiTitel,    guiToon,     guiVerberg};
#endif

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG)) return &PORTS_CONFIG;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) || !std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT))
    return &PORTS_CONFIG_INFO;
  if (!std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS) || !std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS_COMPAT))
    return &CONFIGURABLE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS_EXT;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD) || !std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &PRESET_LOAD;
#ifdef PB_MET_UI
  if (!std::strcmp(id, CLAP_EXT_GUI)) return &GUI;
#endif
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
  s->bediening.s = s;
  for (int i = 0; i < N; ++i) {
    s->waarde[i].store(Ontwerp::PARAMS[i].standaard);
    s->uiGezet[i].store(0);
    s->uiVerwerkt[i].store(0);
  }
  s->clap = {&DESCRIPTOR, s, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset,
             process, getExtension, onMainThread};
  return &s->clap;
}
const clap_plugin_factory_t FACTORY = {factoryCount, factoryDescriptor, factoryCreate};

}  // namespace

bool pb_entry_init(const char*) { return true; }
void pb_entry_deinit(void) {}
const void* pb_entry_get_factory(const char* id) {
  if (!std::strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return &FACTORY;
  if (!std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) || !std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT))
    return &PRESET_FACTORY;
  return nullptr;
}
