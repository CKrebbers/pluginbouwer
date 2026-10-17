// dub_echo.cpp — de CLAP-schil rond het Ontwerp uit dsp/ontwerpen/keten_dub_echo.h.
// process rekent het Ontwerp (Ontwerp::verwerk); events gaan op hun sample (het blok wordt bij elk event gesplitst).
// Draadregels (ARCHITECTUUR.md): het Ontwerp krijgt parameters alleen in process en params.flush (audiothread als de
// plugin actief is), of op de mainthread zolang hij niet actief is. `waarde` is de spiegel voor de host (params.get).
// Het frontpaneel (gui-extensie, fase 3) leest alleen die spiegel en zet zijn veranderingen in een lock-vrije wachtrij;
// process en flush maken er uitvoerevents voor de host van en passen ze toe zoals een event van de host.
// Fabriekspresets (fase 4, fabriekspresets.h) laden via preset-load zoals een state: alle waarden in één keer.
#include <clap/clap.h>

#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "fabriekspresets.h"
#include "ontwerpen/keten_dub_echo.h"
#include "param_tekst.h"
#include "ui_wachtrij.h"
#ifndef PB_GEEN_GUI
#include <clap/ext/posix-fd-support.h>
#include <clap/ext/timer-support.h>

#include <cmath>

#include "paneel.h"
#include "venster.h"
#endif

namespace {

using pb::Ontwerp;
constexpr int N = Ontwerp::N_PARAMS;
constexpr uint32_t STATE_VERSIE = 1;

#ifdef PB_ZONDER_PARAM
// alleen voor de state-toets (gereedschap/pb-state-toets.cpp): een bouw die parameter-id PB_ZONDER_PARAM niet kent,
// zoals een oudere versie van de plugin. Het Ontwerp houdt die parameter op zijn standaard.
constexpr clap_id ZONDER = PB_ZONDER_PARAM;
#else
constexpr clap_id ZONDER = 0;  // id's beginnen bij 1: niets weggelaten
#endif
bool zichtbaar(int i) { return Ontwerp::PARAMS[i].id != ZONDER; }
constexpr int telZichtbaar() {
  int n = 0;
  for (int i = 0; i < N; ++i) n += Ontwerp::PARAMS[i].id != ZONDER;
  return n;
}
constexpr int NZ = telZichtbaar();
// de k-de parameter die de host ziet → index in PARAMS
int zichtbareIndex(uint32_t k) {
  for (int i = 0; i < N; ++i)
    if (zichtbaar(i) && k-- == 0) return i;
  return -1;
}

#ifdef PB_KANARIE
// alleen in de RTSan-kanariebouw: een malloc in process, bewaard in een globale variabele zodat de compiler hem
// niet weg mag halen. RTSan MOET hierop afbreken; doet hij dat niet, dan bewaakt de RTSan-toets niets.
void* volatile g_kanarie = nullptr;
#endif

int indexVan(clap_id id) {
  for (int i = 0; i < N; ++i)
    if (Ontwerp::PARAMS[i].id == id && zichtbaar(i)) return i;
  return -1;
}

double begrens(int i, double v) {
  const auto& p = Ontwerp::PARAMS[i];
  return v < p.min ? p.min : v > p.max ? p.max : v;
}

struct Plugin {
  clap_plugin_t clap;
  const clap_host_t* host;
  const clap_host_params_t* hostParams = nullptr;
  const clap_host_preset_load_t* hostPresetLoad = nullptr;
  Ontwerp* ontwerp = nullptr;
  std::vector<unsigned char> geheugen;
  std::atomic<double> waarde[N];
  bool actief = false;                        // mainthread: tussen activate en deactivate
  std::atomic<bool> stateNieuw{false};        // state geladen terwijl actief: process geeft `waarde` aan het Ontwerp
  std::atomic<uint32_t> staart{0};            // samples; ≥ INT32_MAX = oneindig (tail.get)
  double sr = 48000;
  paneel::Wachtrij wachtrij;                  // paneel (mainthread) → process/flush
#ifndef PB_GEEN_GUI
  paneel::Paneel* paneel = nullptr;           // alleen mainthread, tussen gui.create en gui.destroy
  paneel::Venster* venster = nullptr;
  double guiB = paneel::STANDAARD_B, guiH = paneel::STANDAARD_H;  // logische maat (punten), blijft na sluiten
  double guiSchaal = 1;                       // host-eenheden per punt (X11: set_scale; Cocoa: 1)
#endif

  explicit Plugin(const clap_host_t* h);
};

Plugin* zelf(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// alleen op de thread die het Ontwerp bezit (zie boven)
void pasEventToe(Plugin* pl, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
  auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
  int i = indexVan(e->param_id);
  if (i < 0) return;
  double v = begrens(i, e->value);
  pl->waarde[i].store(v, std::memory_order_relaxed);
  pl->ontwerp->zetParam(i, v);  // zetParam neemt de INDEX in PARAMS
}

// staartSec() in samples voor tail.get; leest de smoothers, dus alleen op de thread die het Ontwerp bezit
void bewaarStaart(Plugin* pl) {
  double t = pl->ontwerp->staartSec();
  double s = t * pl->sr;
  pl->staart.store(t < 0 || s >= (double)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)(s + 0.5), std::memory_order_relaxed);
}

// state die geladen is terwijl de plugin actief was: het Ontwerp krijgt hem op de eerstvolgende plek die het Ontwerp
// bezit (process, flush, of activate na een deactivate)
void neemStateOver(Plugin* pl) {
  if (pl->stateNieuw.exchange(false, std::memory_order_acquire))
    for (int i = 0; i < N; ++i) pl->ontwerp->zetParam(i, pl->waarde[i].load(std::memory_order_relaxed));
}

// berichten van het paneel: eerst als uitvoerevent naar de host (gebaar begin/eind, waarde), dan toegepast zoals een
// event van de host. Alleen op de thread die het Ontwerp bezit (process, flush). Lukt het pushen niet, dan blijft het
// bericht in de wachtrij voor de volgende keer.
void neemPaneelOver(Plugin* pl, const clap_output_events_t* uit) {
  paneel::Bericht b;
  while (pl->wachtrij.kijk(b)) {
    const int i = indexVan(b.id);
    if (i >= 0 && uit) {
      bool gelukt;
      if (b.soort == paneel::Bericht::WAARDE) {
        clap_event_param_value_t e{};
        e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = b.id;
        e.note_id = e.port_index = e.channel = e.key = -1;
        e.value = begrens(i, b.waarde);
        gelukt = uit->try_push(uit, &e.header);
      } else {
        clap_event_param_gesture_t g{};
        g.header = {sizeof g, 0, CLAP_CORE_EVENT_SPACE_ID,
                    (uint16_t)(b.soort == paneel::Bericht::BEGIN ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                 : CLAP_EVENT_PARAM_GESTURE_END), 0};
        g.param_id = b.id;
        gelukt = uit->try_push(uit, &g.header);
      }
      if (!gelukt) return;
    }
    pl->wachtrij.sla_over();
    if (i >= 0 && b.soort == paneel::Bericht::WAARDE) {
      const double v = begrens(i, b.waarde);
      pl->waarde[i].store(v, std::memory_order_relaxed);
      pl->ontwerp->zetParam(i, v);
    }
  }
}

void pasEventsToe(Plugin* pl, const clap_input_events_t* in) {
  if (!in) return;
  const uint32_t n = in->size(in);
  for (uint32_t k = 0; k < n; ++k) pasEventToe(pl, in->get(in, k));
}

// --- audio-ports: stereo in, stereo uit ------------------------------------------------------------------------
uint32_t poortenAantal(const clap_plugin_t*, bool) { return 1; }
bool poortInfo(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (index != 0) return false;
  std::memset(info, 0, sizeof(*info));
  info->id = isInput ? 0 : 1;
  std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "In" : "Uit");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = isInput ? 1 : 0;
  return true;
}
const clap_plugin_audio_ports_t AUDIO_PORTS = {poortenAantal, poortInfo};

// --- params --------------------------------------------------------------------------------------------------
uint32_t paramsAantal(const clap_plugin_t*) { return NZ; }
bool paramInfo(const clap_plugin_t*, uint32_t k, clap_param_info_t* info) {
  const int i = zichtbareIndex(k);
  if (i < 0) return false;
  const auto& p = Ontwerp::PARAMS[i];
  std::memset(info, 0, sizeof(*info));
  info->id = p.id;
  info->flags = (p.vlaggen & pb::AUTOMATEERBAAR) ? CLAP_PARAM_IS_AUTOMATABLE : 0;
  if (p.vlaggen & pb::STAPPEN) info->flags |= CLAP_PARAM_IS_STEPPED;
  std::snprintf(info->name, sizeof(info->name), "%s", p.naam);
  info->min_value = p.min;
  info->max_value = p.max;
  info->default_value = p.standaard;
  return true;
}
bool paramWaarde(const clap_plugin_t* p, clap_id id, double* v) {
  int i = indexVan(id);
  if (i < 0) return false;
  *v = zelf(p)->waarde[i].load(std::memory_order_relaxed);
  return true;
}
bool paramNaarTekst(const clap_plugin_t*, clap_id id, double v, char* uit, uint32_t n) {
  int i = indexVan(id);
  if (i < 0 || n == 0 || !(v == v)) return false;
  return tekst::schrijf(Ontwerp::PARAMS[i], begrens(i, v), uit, n);
}
bool tekstNaarParam(const clap_plugin_t*, clap_id id, const char* t, double* v) {
  int i = indexVan(id);
  if (i < 0 || !t || !tekst::lees(Ontwerp::PARAMS[i], t, v)) return false;
  *v = begrens(i, *v);
  return true;
}
// [actief ? audiothread : mainthread]: nooit tegelijk met process, dus het Ontwerp mag hier gezet worden. Een host
// roept flush aan als hij parameters wil zetten zonder process (gestopt transport, of de plugin niet actief).
// Uitgaande events komen van het paneel (neemPaneelOver).
void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* uit) {
  neemStateOver(zelf(p));
  neemPaneelOver(zelf(p), uit);
  pasEventsToe(zelf(p), in);
  bewaarStaart(zelf(p));
}
const clap_plugin_params_t PARAMS = {paramsAantal, paramInfo, paramWaarde, paramNaarTekst, tekstNaarParam, paramsFlush};

// --- state: "PBST", versie, aantal, per parameter (id u32, waarde f64); laden per id ---------------------------
bool schrijf(const clap_ostream_t* s, const void* d, size_t n) {
  auto* c = static_cast<const unsigned char*>(d);
  while (n > 0) {
    int64_t w = s->write(s, c, n);
    if (w <= 0) return false;
    c += w; n -= (size_t)w;
  }
  return true;
}
// na het vullen van `waarde` door state.load of preset-load (mainthread): het Ontwerp krijgt de waarden en de host
// leest ze opnieuw
void waardenGeladen(Plugin* pl) {
  if (pl->actief) {
    pl->stateNieuw.store(true, std::memory_order_release);  // process neemt de waarden over
  } else {
    for (int i = 0; i < N; ++i) pl->ontwerp->zetParam(i, pl->waarde[i].load(std::memory_order_relaxed));
    bewaarStaart(pl);
  }
  if (pl->hostParams) pl->hostParams->rescan(pl->host, CLAP_PARAM_RESCAN_VALUES);  // de host moet opnieuw lezen
}

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* s) {
  std::vector<unsigned char> b;
  auto zet = [&](const void* d, size_t n) { auto* c = static_cast<const unsigned char*>(d); b.insert(b.end(), c, c + n); };
  uint32_t versie = STATE_VERSIE, aantal = NZ;
  zet("PBST", 4); zet(&versie, 4); zet(&aantal, 4);
  for (int i = 0; i < N; ++i) {
    if (!zichtbaar(i)) continue;
    uint32_t id = Ontwerp::PARAMS[i].id;
    double v = zelf(p)->waarde[i].load(std::memory_order_relaxed);
    zet(&id, 4); zet(&v, 8);
  }
  return schrijf(s, b.data(), b.size());
}
bool stateLoad(const clap_plugin_t* p, const clap_istream_t* s) {
  std::vector<unsigned char> b;
  unsigned char buf[4096];
  for (;;) {
    int64_t r = s->read(s, buf, sizeof buf);
    if (r < 0) return false;
    if (r == 0) break;
    b.insert(b.end(), buf, buf + r);
  }
  if (b.size() < 12 || std::memcmp(b.data(), "PBST", 4) != 0) return false;
  uint32_t versie, aantal;
  std::memcpy(&versie, &b[4], 4);
  std::memcpy(&aantal, &b[8], 4);
  if (versie < 1 || b.size() < 12 + (size_t)aantal * 12) return false;
  Plugin* pl = zelf(p);
  for (int i = 0; i < N; ++i) pl->waarde[i].store(Ontwerp::PARAMS[i].standaard, std::memory_order_relaxed);
  for (uint32_t k = 0; k < aantal; ++k) {
    uint32_t id; double v;
    std::memcpy(&id, &b[12 + k * 12], 4);
    std::memcpy(&v, &b[12 + k * 12 + 4], 8);
    int i = indexVan(id);
    if (i >= 0) pl->waarde[i].store(begrens(i, v), std::memory_order_relaxed);  // onbekend id: overslaan
  }
  waardenGeladen(pl);
  return true;
}
const clap_plugin_state_t STATE = {stateSave, stateLoad};

// --- preset-load: een fabriekspreset (locatie in de plugin, load_key = sleutel), op de mainthread ------------------
// Parameters die de preset niet noemt gaan naar hun standaard, zoals bij een state zonder die parameter.
bool presetLaad(const clap_plugin_t* p, uint32_t soort, const char* plek, const char* sleutel) {
  Plugin* pl = zelf(p);
  const fabriek::Preset* f =
      soort == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && !plek ? fabriek::zoek(sleutel) : nullptr;
  if (!f) {
    if (pl->hostPresetLoad)
      pl->hostPresetLoad->on_error(pl->host, soort, plek, sleutel, 0, "onbekende fabriekspreset");
    return false;
  }
  for (int i = 0; i < N; ++i) pl->waarde[i].store(Ontwerp::PARAMS[i].standaard, std::memory_order_relaxed);
  for (int k = 0; k < f->n; ++k) {
    const int i = indexVan(Ontwerp::PARAMS[fabriek::paramIndex(f->w[k].param)].id);  // verborgen (toetsbouw): overslaan
    if (i >= 0) pl->waarde[i].store(begrens(i, f->w[k].v), std::memory_order_relaxed);
  }
  waardenGeladen(pl);
  if (pl->hostPresetLoad) pl->hostPresetLoad->loaded(pl->host, soort, plek, sleutel);
  return true;
}
const clap_plugin_preset_load_t PRESET_LOAD = {presetLaad};

// --- latency ---------------------------------------------------------------------------------------------------
uint32_t latentie(const clap_plugin_t* p) { return zelf(p)->ontwerp->latentie(); }
const clap_plugin_latency_t LATENCY = {latentie};

// --- tail: uitgerekend aan het eind van process (en in activate), hier alleen gelezen ---------------------------
uint32_t staartGet(const clap_plugin_t* p) { return zelf(p)->staart.load(std::memory_order_relaxed); }
const clap_plugin_tail_t TAIL = {staartGet};

// --- de plugin -------------------------------------------------------------------------------------------------
#ifndef PB_GEEN_GUI
void guiWeg(const clap_plugin_t* p);
#endif
bool init(const clap_plugin_t* p) {
  Plugin* pl = zelf(p);
  pl->ontwerp = new Ontwerp();
  pl->hostParams = static_cast<const clap_host_params_t*>(pl->host->get_extension(pl->host, CLAP_EXT_PARAMS));
  pl->hostPresetLoad =
      static_cast<const clap_host_preset_load_t*>(pl->host->get_extension(pl->host, CLAP_EXT_PRESET_LOAD));
  if (!pl->hostPresetLoad)
    pl->hostPresetLoad =
        static_cast<const clap_host_preset_load_t*>(pl->host->get_extension(pl->host, CLAP_EXT_PRESET_LOAD_COMPAT));
  for (int i = 0; i < N; ++i) pl->waarde[i].store(Ontwerp::PARAMS[i].standaard, std::memory_order_relaxed);
  return true;
}
void destroy(const clap_plugin_t* p) {
  Plugin* pl = zelf(p);
#ifndef PB_GEEN_GUI
  guiWeg(p);  // een host die vergat gui.destroy aan te roepen
#endif
  delete pl->ontwerp;
  delete pl;
}
bool activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t maxFrames) {
  Plugin* pl = zelf(p);
  pl->geheugen.assign(Ontwerp::geheugenNodig(sr), 0);  // hier, op de mainthread: nooit in process
  pb::Geheugen g;
  g.p = pl->geheugen.data();
  g.over = (unsigned long)pl->geheugen.size();
  pl->ontwerp->activeer(sr, maxFrames, g);
  pl->sr = sr;
  neemStateOver(pl);  // state geladen vlak voor een deactivate: anders zou hij verloren gaan
  bewaarStaart(pl);
  pl->actief = true;
  return true;
}
void deactivate(const clap_plugin_t* p) { zelf(p)->actief = false; }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { zelf(p)->ontwerp->reset(); }

clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
  Plugin* pl = zelf(p);
#ifdef PB_KANARIE
  g_kanarie = std::malloc(16);
#endif
  neemStateOver(pl);
  neemPaneelOver(pl, pr->out_events);  // op sample 0; events van de host in dit blok gaan erna

  const uint32_t n = pr->frames_count;
  const clap_input_events_t* ev = pr->in_events;
  const uint32_t nEv = ev ? ev->size(ev) : 0;
  const bool audio = pr->audio_inputs_count >= 1 && pr->audio_outputs_count >= 1;
  int kanalen = 0;
  const float* in[2] = {nullptr, nullptr};
  float* uit[2] = {nullptr, nullptr};
  if (audio) {
    const clap_audio_buffer_t& bi = pr->audio_inputs[0];
    const clap_audio_buffer_t& bu = pr->audio_outputs[0];
    kanalen = (int)(bi.channel_count < bu.channel_count ? bi.channel_count : bu.channel_count);
    if (kanalen > 2) kanalen = 2;
    for (int c = 0; c < kanalen; ++c) { in[c] = bi.data32[c]; uit[c] = bu.data32[c]; }
    for (uint32_t c = (uint32_t)kanalen; c < bu.channel_count; ++c) std::memset(bu.data32[c], 0, n * sizeof(float));
  }

  // het blok splitsen bij elk event: rekenen tot zijn sample, dan het event toepassen
  uint32_t pos = 0;
  auto reken = [&](uint32_t tot) {
    if (tot <= pos) return;
    if (kanalen > 0) {
      const float* i2[2] = {in[0] ? in[0] + pos : nullptr, in[1] ? in[1] + pos : nullptr};
      float* u2[2] = {uit[0] ? uit[0] + pos : nullptr, uit[1] ? uit[1] + pos : nullptr};
      pl->ontwerp->verwerk(i2, u2, kanalen, tot - pos);
    }
    pos = tot;
  };
  for (uint32_t k = 0; k < nEv; ++k) {
    const clap_event_header_t* h = ev->get(ev, k);
    reken(h->time < n ? h->time : n);
    pasEventToe(pl, h);
  }
  reken(n);
  bewaarStaart(pl);
  return CLAP_PROCESS_CONTINUE;
}

#ifndef PB_GEEN_GUI
// --- gui: het frontpaneel (paneel.h), in een venster van de host (venster.h); alles op de mainthread -------------
double paneelLees(void* ctx, int i) { return static_cast<Plugin*>(ctx)->waarde[i].load(std::memory_order_relaxed); }
bool paneelStuur(void* ctx, const paneel::Bericht& b) {
  auto* pl = static_cast<Plugin*>(ctx);
#ifdef PB_UI_KANARIE
  // alleen in de TSan-kanariebouw: precies wat niet mag, het paneel zet de DSP zelf (mainthread, terwijl process
  // loopt). TSan MOET dit vangen; doet hij dat niet, dan bewaakt de paneeltoets niets.
  if (b.soort == paneel::Bericht::WAARDE) pl->ontwerp->zetParam(indexVan(b.id), b.waarde);
#endif
  if (!pl->wachtrij.zet(b)) return false;
  if (pl->hostParams) pl->hostParams->request_flush(pl->host);  // speelt de host niet, dan roept hij flush aan
  return true;
}
uint32_t hostMaat(double punten, double schaal) { return (uint32_t)std::lround(punten * schaal); }

bool guiApi(const clap_plugin_t*, const char* api, bool floating) {
  return !floating && api && paneel::Venster::api() && !std::strcmp(api, paneel::Venster::api());
}
bool guiVoorkeur(const clap_plugin_t*, const char** api, bool* floating) {
  *api = paneel::Venster::api();
  *floating = false;
  return *api != nullptr;
}
bool guiMaak(const clap_plugin_t* p, const char* api, bool floating) {
  Plugin* pl = zelf(p);
  if (!guiApi(p, api, floating) || pl->venster) return false;
  pl->guiSchaal = 1;
  pl->paneel = new paneel::Paneel({pl, paneelLees, paneelStuur});
  pl->venster = paneel::Venster::maak(pl->paneel, pl->host, hostMaat(pl->guiB, 1), hostMaat(pl->guiH, 1));
  if (!pl->venster) {
    delete pl->paneel;
    pl->paneel = nullptr;
    return false;
  }
  return true;
}
void guiWeg(const clap_plugin_t* p) {
  Plugin* pl = zelf(p);
  delete pl->venster;
  delete pl->paneel;
  pl->venster = nullptr;
  pl->paneel = nullptr;
}
bool guiZetSchaal(const clap_plugin_t* p, double s) {
  Plugin* pl = zelf(p);
  if (!pl->venster || !(s > 0) || !pl->venster->zetSchaal(s)) return false;
  pl->guiSchaal = s;
  pl->venster->zetMaat(hostMaat(pl->guiB, s), hostMaat(pl->guiH, s));
  return true;
}
bool guiMaatGet(const clap_plugin_t* p, uint32_t* b, uint32_t* h) {
  Plugin* pl = zelf(p);
  *b = hostMaat(pl->guiB, pl->guiSchaal);
  *h = hostMaat(pl->guiH, pl->guiSchaal);
  return true;
}
bool guiKanMaat(const clap_plugin_t*) { return true; }
bool guiHints(const clap_plugin_t*, clap_gui_resize_hints_t* h) {
  h->can_resize_horizontally = h->can_resize_vertically = true;
  h->preserve_aspect_ratio = false;
  h->aspect_ratio_width = h->aspect_ratio_height = 0;
  return true;
}
bool guiPasMaat(const clap_plugin_t* p, uint32_t* b, uint32_t* h) {  // design.md: minimaal 560 × 220
  Plugin* pl = zelf(p);
  const uint32_t mb = hostMaat(paneel::MIN_B, pl->guiSchaal), mh = hostMaat(paneel::MIN_H, pl->guiSchaal);
  if (*b < mb) *b = mb;
  if (*h < mh) *h = mh;
  return true;
}
bool guiZetMaat(const clap_plugin_t* p, uint32_t b, uint32_t h) {
  Plugin* pl = zelf(p);
  if (!pl->venster) return false;
  guiPasMaat(p, &b, &h);
  pl->guiB = b / pl->guiSchaal;
  pl->guiH = h / pl->guiSchaal;
  pl->venster->zetMaat(b, h);
  return true;
}
bool guiOuder(const clap_plugin_t* p, const clap_window_t* w) {
  Plugin* pl = zelf(p);
  return pl->venster && pl->venster->zetOuder(w);
}
bool guiTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiTitel(const clap_plugin_t*, const char*) {}
bool guiToon(const clap_plugin_t* p) {
  if (!zelf(p)->venster) return false;
  zelf(p)->venster->toon(true);
  return true;
}
bool guiVerberg(const clap_plugin_t* p) {
  if (!zelf(p)->venster) return false;
  zelf(p)->venster->toon(false);
  return true;
}
const clap_plugin_gui_t GUI = {guiApi,   guiVoorkeur, guiMaak,    guiWeg,     guiZetSchaal, guiMaatGet,
                               guiKanMaat, guiHints,  guiPasMaat, guiZetMaat, guiOuder,     guiTransient,
                               guiTitel, guiToon,     guiVerberg};

// X11: de host meldt X-events (posix-fd-support) en tikt de timer van het paneel (timer-support)
void opTimer(const clap_plugin_t* p, clap_id id) { if (zelf(p)->venster) zelf(p)->venster->opTimer(id); }
const clap_plugin_timer_support_t TIMER = {opTimer};
void opFd(const clap_plugin_t* p, int fd, clap_posix_fd_flags_t) { if (zelf(p)->venster) zelf(p)->venster->opFd(fd); }
const clap_plugin_posix_fd_support_t POSIX_FD = {opFd};
#endif

const void* extensie(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &LATENCY;
  if (!std::strcmp(id, CLAP_EXT_TAIL)) return &TAIL;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD) || !std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &PRESET_LOAD;
#ifndef PB_GEEN_GUI
  if (paneel::Venster::api() && !std::strcmp(id, CLAP_EXT_GUI)) return &GUI;
  if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &TIMER;
  if (!std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &POSIX_FD;
#endif
  return nullptr;
}
void opMainThread(const clap_plugin_t*) {}

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_STEREO,
                                nullptr};
const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT, Ontwerp::META.clapId, Ontwerp::META.naam, Ontwerp::META.maker, Ontwerp::META.url, "", "",
    Ontwerp::META.versie, Ontwerp::META.uitleg, FEATURES};

Plugin::Plugin(const clap_host_t* h) : host(h) {
  clap.desc = &DESCRIPTOR;
  clap.plugin_data = this;
  clap.init = init;
  clap.destroy = destroy;
  clap.activate = activate;
  clap.deactivate = deactivate;
  clap.start_processing = startProcessing;
  clap.stop_processing = stopProcessing;
  clap.reset = reset;
  clap.process = process;
  clap.get_extension = extensie;
  clap.on_main_thread = opMainThread;
  for (auto& w : waarde) w.store(0.0, std::memory_order_relaxed);
}

// --- factory ---------------------------------------------------------------------------------------------------
uint32_t fabAantal(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* fabDescriptor(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &DESCRIPTOR : nullptr;
}
const clap_plugin_t* fabMaak(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, DESCRIPTOR.id) != 0) return nullptr;
  return &(new Plugin(host))->clap;
}
const clap_plugin_factory_t FACTORY = {fabAantal, fabDescriptor, fabMaak};

}  // namespace

bool dub_echo_entry_init(const char*) { return true; }
void dub_echo_entry_deinit() {}
const void* dub_echo_entry_get_factory(const char* id) {
  if (!std::strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return &FACTORY;
  if (!std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) || !std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT))
    return &fabriek::FACTORY;
  return nullptr;
}
