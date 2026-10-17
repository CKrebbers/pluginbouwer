// ui-toets — toetst de plugin-kant van het frontpaneel (fase 3), als kleine CLAP-host, op elk platform.
//
//   ui-toets <PB-Dub-echo-ui-stub.clap>
//
// De stub-build heeft de echte gui-extensie en de echte Bediening, maar een "view" zonder Cocoa (toetsen/ui-stub.cpp):
// set_parent geeft de Bediening terug, en de toets doet wat de knoppen doen (gebaar, waarde, gebaar einde).
// Geëist wordt:
//   - de gui-extensie: alleen "cocoa" ingebed, 720 x 270, schaalbaar, minimaal 560 x 220;
//   - de knoppen staan in de volgorde van SPEC.md (id 1..8) met de grenzen en standaarden van de host;
//   - wat de UI stuurt, verandert niets vóór de host process of params.flush aanroept (de UI raakt de DSP niet);
//     de plugin vraagt erom met host_params.request_flush;
//   - in flush (niet actief) en in process (actief) komt bij de host precies GESTURE_BEGIN, PARAM_VALUE..., GESTURE_END
//     aan, in die volgorde, op sample 0, en get_value volgt;
//   - geluid: een waarde via de UI geeft bit voor bit dezelfde uitvoer als dezelfde waarde als host-event op sample 0;
//   - een volle wachtrij (de host roept lang niets aan) laat de gebaren heel en crasht niet;
//   - de tekst onder een knop is de tekst van de host (params.value_to_text).
// Met -fsanitize=realtime loopt process (met UI-berichten) in een real-time context, zoals in pb-render.
#include <clap/clap.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/paneel.h"

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define PB_RT [[clang::nonblocking]]
#endif
#endif
#ifndef PB_RT
#define PB_RT
#endif

namespace {

int g_fouten = 0;
void fout(const std::string& t) { std::printf("FOUT: %s\n", t.c_str()); ++g_fouten; }
[[noreturn]] void stop(const std::string& t) { std::printf("FOUT: %s\n", t.c_str()); std::exit(2); }
void ok(const std::string& t) { std::printf("ok: %s\n", t.c_str()); }
void eis(bool b, const std::string& t) { b ? ok(t) : fout(t); }

// --- host: params-extensie met request_flush (geteld) --------------------------------------------------------
int g_flushVerzoeken = 0;
void hpRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpRequestFlush(const clap_host_t*) { ++g_flushVerzoeken; }
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpRequestFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  return !std::strcmp(id, CLAP_EXT_PARAMS) ? &HOST_PARAMS : nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "ui-toets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

// wat de plugin aan de host meldt; vooraf gereserveerd, zodat try_push onder RTSan niet alloceert
struct Uit {
  struct E { uint16_t type; uint32_t tijd; clap_id id; double waarde; };
  E ev[16384];
  uint32_t n = 0;
};
bool uitPush(const clap_output_events_t* l, const clap_event_header_t* h) {
  auto* u = static_cast<Uit*>(l->ctx);
  if (u->n >= 16384 || h->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
  Uit::E e{h->type, h->time, 0, 0};
  if (h->type == CLAP_EVENT_PARAM_VALUE) {
    auto* v = reinterpret_cast<const clap_event_param_value_t*>(h);
    e.id = v->param_id;
    e.waarde = v->value;
  } else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END) {
    e.id = reinterpret_cast<const clap_event_param_gesture_t*>(h)->param_id;
  }
  u->ev[u->n++] = e;
  return true;
}
struct In { std::vector<clap_event_param_value_t> ev; };
uint32_t inSize(const clap_input_events_t* l) { return (uint32_t)static_cast<const In*>(l->ctx)->ev.size(); }
const clap_event_header_t* inGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<const In*>(l->ctx)->ev[i].header;
}
clap_event_param_value_t maakEvent(uint32_t tijd, clap_id id, double v) {
  clap_event_param_value_t e{};
  e.header = {sizeof(e), tijd, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.param_id = id; e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
  return e;
}

__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT {
  return pl->process(pl, pr);
}

struct Bibliotheek {
  void* h = nullptr;
  const clap_plugin_entry_t* entry = nullptr;
  const clap_plugin_factory_t* fab = nullptr;
  std::string id;
  explicit Bibliotheek(const std::string& pad) {
    std::string lib = pad;
    struct stat st{};
    if (stat(pad.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {  // macOS-bundle
      std::string naam = pad.substr(pad.find_last_of('/') + 1);
      if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
      lib = pad + "/Contents/MacOS/" + naam;
    }
    h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) stop(std::string("dlopen: ") + dlerror());
    entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
    if (!entry || !entry->init(pad.c_str())) stop("geen clap_entry in " + pad);
    fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!fab || fab->get_plugin_count(fab) < 1) stop("geen plugin in de factory");
    id = fab->get_plugin_descriptor(fab, 0)->id;
  }
};

struct Instantie {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  const clap_plugin_gui_t* gui = nullptr;
  bool actief = false;
  explicit Instantie(const Bibliotheek& b) {
    pl = b.fab->create_plugin(b.fab, &HOST, b.id.c_str());
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    gui = static_cast<const clap_plugin_gui_t*>(pl->get_extension(pl, CLAP_EXT_GUI));
    if (!pp) stop("geen params-extensie");
  }
  ~Instantie() {
    if (actief) { pl->stop_processing(pl); pl->deactivate(pl); }
    pl->destroy(pl);
  }
  double waarde(clap_id id) const {
    double v = NAN;
    if (!pp->get_value(pl, id, &v)) fout("get_value mislukt");
    return v;
  }
  void start() {
    if (!pl->activate(pl, 48000, 1, 512) || !pl->start_processing(pl)) stop("activate mislukt");
    actief = true;
  }
};

constexpr uint32_t BLOK = 512;
struct Stereo { std::vector<float> l, r; };
Stereo invoer(uint32_t n) {
  Stereo s{std::vector<float>(n), std::vector<float>(n)};
  uint32_t x = 12345;
  for (uint32_t i = 0; i < n; ++i) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    const float ruis = (float)((int32_t)x) / 2147483648.0f;
    s.l[i] = (i % 4800 < 400) ? 0.5f * ruis : 0.0f;  // korte stoten: de echo's moeten hoorbaar blijven
    s.r[i] = (i % 4800 < 400) ? -0.4f * ruis : 0.0f;
  }
  return s;
}
// één blok door process; `uit` krijgt wat de plugin aan de host meldt, `in` de host-events
void blok(Instantie& ins, const Stereo& x, Stereo& y, uint32_t pos, const In& in, Uit& uit) {
  const float* ip[2] = {x.l.data() + pos, x.r.data() + pos};
  float* op[2] = {y.l.data() + pos, y.r.data() + pos};
  clap_audio_buffer_t ib{}, ob{};
  ib.data32 = const_cast<float**>(ip);
  ib.channel_count = 2;
  ob.data32 = op;
  ob.channel_count = 2;
  clap_input_events_t ie = {const_cast<In*>(&in), inSize, inGet};
  clap_output_events_t oe = {&uit, uitPush};
  clap_process_t pr{};
  pr.steady_time = -1;
  pr.frames_count = BLOK;
  pr.audio_inputs = &ib;
  pr.audio_outputs = &ob;
  pr.audio_inputs_count = 1;
  pr.audio_outputs_count = 1;
  pr.in_events = &ie;
  pr.out_events = &oe;
  if (doeProcess(ins.pl, &pr) == CLAP_PROCESS_ERROR) fout("process gaf een fout");
}

const char* soortNaam(uint16_t t) {
  return t == CLAP_EVENT_PARAM_GESTURE_BEGIN ? "BEGIN" : t == CLAP_EVENT_PARAM_GESTURE_END ? "EINDE"
         : t == CLAP_EVENT_PARAM_VALUE ? "WAARDE" : "?";
}
// eist dat `uit` precies BEGIN, n x WAARDE (de laatste = `laatste`), EINDE voor `id` bevat, alles op sample 0
void eisGebaar(const Uit& uit, clap_id id, uint32_t n, double laatste, const std::string& wat) {
  bool goed = uit.n == n + 2;
  for (uint32_t k = 0; goed && k < uit.n; ++k) {
    const auto& e = uit.ev[k];
    const uint16_t moet = k == 0 ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                          : k == uit.n - 1 ? CLAP_EVENT_PARAM_GESTURE_END : CLAP_EVENT_PARAM_VALUE;
    goed = e.type == moet && e.id == id && e.tijd == 0;
  }
  if (goed && n > 0) goed = uit.ev[uit.n - 2].waarde == laatste;
  if (goed) {
    ok(wat + ": de host kreeg BEGIN, " + std::to_string(n) + " x WAARDE, EINDE voor id " + std::to_string(id) +
       " (laatste " + std::to_string(laatste) + "), op sample 0");
    return;
  }
  std::string lijst;
  for (uint32_t k = 0; k < uit.n && k < 12; ++k)
    lijst += std::string(" ") + soortNaam(uit.ev[k].type) + "(" + std::to_string(uit.ev[k].id) + "@" +
             std::to_string(uit.ev[k].tijd) + "=" + std::to_string(uit.ev[k].waarde) + ")";
  fout(wat + ": verwacht BEGIN, " + std::to_string(n) + " x WAARDE, EINDE voor id " + std::to_string(id) +
       "; kreeg " + std::to_string(uit.n) + ":" + lijst);
}

pb_ui::Bediening* openPaneel(Instantie& ins) {
  void* bediening = nullptr;
  clap_window_t w{};
  w.api = CLAP_WINDOW_API_COCOA;
  w.ptr = &bediening;
  if (!ins.gui->create(ins.pl, CLAP_WINDOW_API_COCOA, false)) stop("gui.create mislukt");
  if (!ins.gui->set_parent(ins.pl, &w) || !bediening) stop("gui.set_parent mislukt");
  if (!ins.gui->show(ins.pl)) fout("gui.show mislukt");
  return static_cast<pb_ui::Bediening*>(bediening);
}

int toets(const std::string& pad) {
  Bibliotheek bib(pad);

  // 1. de gui-extensie
  {
    Instantie ins(bib);
    if (!ins.gui) stop("geen gui-extensie in deze build");
    eis(ins.gui->is_api_supported(ins.pl, CLAP_WINDOW_API_COCOA, false) &&
            !ins.gui->is_api_supported(ins.pl, CLAP_WINDOW_API_COCOA, true) &&
            !ins.gui->is_api_supported(ins.pl, CLAP_WINDOW_API_X11, false) &&
            !ins.gui->is_api_supported(ins.pl, CLAP_WINDOW_API_WIN32, false),
        "gui: alleen \"cocoa\", ingebed");
    const char* api = nullptr;
    bool zwevend = true;
    eis(ins.gui->get_preferred_api(ins.pl, &api, &zwevend) && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA) && !zwevend,
        "gui: voorkeur cocoa, ingebed");
    eis(!ins.gui->create(ins.pl, CLAP_WINDOW_API_X11, false), "gui: create met x11 weigert");
    eis(ins.gui->create(ins.pl, CLAP_WINDOW_API_COCOA, false), "gui: create met cocoa");
    uint32_t b = 0, h = 0;
    eis(ins.gui->get_size(ins.pl, &b, &h) && b == 720 && h == 270, "gui: begint op 720 x 270 (" + std::to_string(b) +
        " x " + std::to_string(h) + ")");
    clap_gui_resize_hints_t hints{};
    eis(ins.gui->can_resize(ins.pl) && ins.gui->get_resize_hints(ins.pl, &hints) && hints.can_resize_horizontally &&
            hints.can_resize_vertically,
        "gui: schaalbaar");
    b = 100; h = 50;
    eis(ins.gui->adjust_size(ins.pl, &b, &h) && b == 560 && h == 220, "gui: adjust_size(100 x 50) -> 560 x 220");
    b = 1000; h = 400;
    eis(ins.gui->adjust_size(ins.pl, &b, &h) && b == 1000 && h == 400, "gui: adjust_size(1000 x 400) blijft");
    eis(ins.gui->set_size(ins.pl, 1000, 400) && ins.gui->get_size(ins.pl, &b, &h) && b == 1000 && h == 400,
        "gui: set_size(1000 x 400), get_size volgt");
    ins.gui->destroy(ins.pl);
  }

  // 2. de knoppen: volgorde, grenzen, standaard, tekst
  {
    Instantie ins(bib);
    pb_ui::Bediening* bd = openPaneel(ins);
    // fase 5: de host ziet de 8 parameters van de DSP en als laatste Bypass (CLAP_PARAM_IS_BYPASS); het paneel heeft
    // alleen de 8 knoppen (de bypass is van de host)
    bool goed = bd->aantal() == 8 && ins.pp->count(ins.pl) == 9;
    {
      clap_param_info_t laatste{};
      goed = goed && ins.pp->get_info(ins.pl, 8, &laatste) && laatste.id == 9 && (laatste.flags & CLAP_PARAM_IS_BYPASS);
    }
    for (int k = 0; goed && k < bd->aantal(); ++k) {
      clap_param_info_t info{};
      ins.pp->get_info(ins.pl, (uint32_t)k, &info);
      const pb_ui::Knop kn = bd->knop(k);
      goed = kn.id == (clap_id)(k + 1) && kn.id == info.id && !std::strcmp(kn.naam, info.name) &&
             kn.min == info.min_value && kn.max == info.max_value && kn.standaard == info.default_value &&
             bd->waarde(k) == info.default_value;
      char a[64], t[64];
      bd->alsTekst(k, 0.37 * info.min_value + 0.63 * info.max_value, a, sizeof a);
      ins.pp->value_to_text(ins.pl, info.id, 0.37 * info.min_value + 0.63 * info.max_value, t, sizeof t);
      if (std::strcmp(a, t)) goed = false;
    }
    eis(goed, "paneel: 8 knoppen in de volgorde van SPEC.md (id 1..8), naam/grenzen/standaard/tekst van de host; "
             "de host heeft daarna alleen Bypass (id 9)");
    // knop <-> stand: rondje, en logaritmisch voor Hz en ms
    bool rond = true;
    for (int k = 0; k < bd->aantal(); ++k) {
      const pb_ui::Knop kn = bd->knop(k);
      for (int j = 0; j <= 100; ++j) {
        const double s = j / 100.0, v = pb_ui::uitStand(kn, s);
        if (v < kn.min || v > kn.max || std::fabs(pb_ui::naarStand(kn, v) - s) > 1e-9) rond = false;
      }
    }
    eis(rond, "paneel: stand -> waarde -> stand klopt (101 standen per knop), binnen de grenzen");
    eis(bd->knop(0).logaritmisch && bd->knop(2).logaritmisch && bd->knop(3).logaritmisch && !bd->knop(1).logaritmisch &&
            !bd->knop(7).logaritmisch,
        "paneel: Tijd, Toon en Laag weg logaritmisch, de rest lineair");
    ins.gui->destroy(ins.pl);
  }

  // 3. niet actief: de UI zet Toon op 9 kHz; pas params.flush brengt het naar de DSP en naar de host
  {
    Instantie ins(bib);
    pb_ui::Bediening* bd = openPaneel(ins);
    g_flushVerzoeken = 0;
    bd->gebaarBegin(2);
    bd->zet(2, 9000);
    bd->gebaarEinde(2);
    eis(g_flushVerzoeken >= 1, "niet actief: de plugin vraagt om een flush (host_params.request_flush, " +
        std::to_string(g_flushVerzoeken) + "x)");
    eis(ins.waarde(3) == 3000 && bd->wachtend(2), "niet actief: vóór de flush is er niets veranderd (Toon nog 3000 Hz)");
    In in;
    clap_input_events_t ie = {&in, inSize, inGet};
    static Uit uit;
    uit.n = 0;
    clap_output_events_t oe = {&uit, uitPush};
    ins.pp->flush(ins.pl, &ie, &oe);
    eisGebaar(uit, 3, 1, 9000, "niet actief, params.flush");
    eis(ins.waarde(3) == 9000 && !bd->wachtend(2), "niet actief: na de flush is Toon 9000 Hz (get_value)");
    // dubbelklik: standaard
    uit.n = 0;
    bd->gebaarBegin(2);
    bd->zet(2, bd->knop(2).standaard);
    bd->gebaarEinde(2);
    ins.pp->flush(ins.pl, &ie, &oe);
    eisGebaar(uit, 3, 1, 3000, "dubbelklik (standaard)");
    // buiten het bereik wordt geklemd
    uit.n = 0;
    bd->gebaarBegin(5);
    bd->zet(5, 1e9);
    bd->gebaarEinde(5);
    ins.pp->flush(ins.pl, &ie, &oe);
    eisGebaar(uit, 6, 1, 24, "te grote waarde");
    ins.gui->destroy(ins.pl);
  }

  // 4. actief: slepen in process; geluid gelijk aan dezelfde waarde als host-event
  {
    const uint32_t blokken = 400;
    const Stereo x = invoer(BLOK * blokken);
    Stereo yUi{std::vector<float>(x.l.size()), std::vector<float>(x.l.size())}, yHost = yUi;
    static Uit uit;
    In leeg;
    // de UI: na blok 100 een sleep op Tijd (drie waarden), na blok 250 Mix
    {
      Instantie ins(bib);
      pb_ui::Bediening* bd = openPaneel(ins);
      ins.start();
      for (uint32_t k = 0; k < blokken; ++k) {
        uit.n = 0;
        if (k == 100) {
          bd->gebaarBegin(0);
          bd->zet(0, 300);
          bd->zet(0, 250);
          bd->zet(0, 211.5);
          bd->gebaarEinde(0);
          eis(ins.waarde(1) == 375, "actief: vóór process is Tijd nog 375 ms");
        }
        if (k == 250) {
          bd->gebaarBegin(7);
          bd->zet(7, 0.8);
          bd->gebaarEinde(7);
        }
        blok(ins, x, yUi, k * BLOK, leeg, uit);
        if (k == 100) {
          eisGebaar(uit, 1, 3, 211.5, "actief, process");
          eis(ins.waarde(1) == 211.5 && !bd->wachtend(0), "actief: na process is Tijd 211.5 ms (get_value)");
        }
        if (k == 250) eisGebaar(uit, 8, 1, 0.8, "actief, process (Mix)");
        if (k != 100 && k != 250 && uit.n != 0) fout("process meldde iets zonder dat de UI iets deed (blok " +
                                                     std::to_string(k) + ")");
      }
      ins.gui->destroy(ins.pl);
    }
    // dezelfde laatste waarden als host-event op sample 0 van hetzelfde blok
    {
      Instantie ins(bib);
      ins.start();
      for (uint32_t k = 0; k < blokken; ++k) {
        uit.n = 0;
        In in;
        if (k == 100) in.ev.push_back(maakEvent(0, 1, 211.5));
        if (k == 250) in.ev.push_back(maakEvent(0, 8, 0.8));
        blok(ins, x, yHost, k * BLOK, in, uit);
      }
    }
    size_t anders = 0, nietStil = 0;
    for (size_t i = 0; i < x.l.size(); ++i) {
      if (std::memcmp(&yUi.l[i], &yHost.l[i], 4) || std::memcmp(&yUi.r[i], &yHost.r[i], 4)) ++anders;
      if (yUi.l[i] != 0) ++nietStil;
    }
    eis(anders == 0 && nietStil > x.l.size() / 4,
        "geluid: knop via de UI = dezelfde waarde als host-event, bit voor bit (" + std::to_string(x.l.size()) +
            " samples, " + std::to_string(anders) + " verschillend)");
  }

  // 5. een volle wachtrij (de host roept niets aan): niets crasht, de gebaren blijven heel
  {
    Instantie ins(bib);
    pb_ui::Bediening* bd = openPaneel(ins);
    for (int g = 0; g < 40; ++g) {
      bd->gebaarBegin(g % 8);
      for (int j = 0; j < 300; ++j) bd->zet(g % 8, bd->knop(g % 8).min + j);
      bd->gebaarEinde(g % 8);
    }
    In in;
    clap_input_events_t ie = {&in, inSize, inGet};
    static Uit uit;
    uit.n = 0;
    clap_output_events_t oe = {&uit, uitPush};
    ins.pp->flush(ins.pl, &ie, &oe);
    int open[9] = {0}, fouteVolgorde = 0, begins = 0, eindes = 0;
    for (uint32_t k = 0; k < uit.n; ++k) {
      const auto& e = uit.ev[k];
      if (e.id < 1 || e.id > 8) { ++fouteVolgorde; continue; }
      if (e.type == CLAP_EVENT_PARAM_GESTURE_BEGIN) { if (open[e.id]++) ++fouteVolgorde; ++begins; }
      else if (e.type == CLAP_EVENT_PARAM_GESTURE_END) { if (!open[e.id]--) ++fouteVolgorde; ++eindes; }
      else if (!open[e.id]) ++fouteVolgorde;
    }
    bool wacht = false;
    for (int k = 0; k < 8; ++k) wacht = wacht || bd->wachtend(k);
    eis(fouteVolgorde == 0 && begins == eindes && begins > 0 && uit.n < 12040 && !wacht,
        "volle wachtrij: " + std::to_string(uit.n) + " van 12080 berichten aangekomen, " + std::to_string(begins) +
            " gebaren, elk heel (BEGIN ... EINDE), niets blijft wachten");
    // daarna werkt hij gewoon weer
    uit.n = 0;
    bd->gebaarBegin(1);
    bd->zet(1, 1.1);
    bd->gebaarEinde(1);
    ins.pp->flush(ins.pl, &ie, &oe);
    eisGebaar(uit, 2, 1, 1.1, "na een volle wachtrij");
    ins.gui->destroy(ins.pl);
  }
  return g_fouten == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::printf("gebruik: ui-toets <PB-Dub-echo-ui-stub.clap>\n");
    return 2;
  }
  const int r = toets(argv[1]);
  std::printf(r == 0 ? "GESLAAGD\n" : "MISLUKT (%d fouten)\n", g_fouten);
  return r;
}
