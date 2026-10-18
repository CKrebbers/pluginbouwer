// paneeltoets — de toets van fase 3, door het gebouwde .clap (dlopen, zoals pb-statetoets).
//
//   pb-paneeltoets <plugin.clap> <uitmap>
//
// Het paneel zonder venster, via de eigen extensie PB_EXT_PANEELTOETS (paneel_toets.h): de plugin tekent zijn paneel in
// een buffer (dezelfde code als in de Cocoa-view) en krijgt muisacties. Getoetst (design.md):
//   - tekening: kleuren uit design.md op de plekken uit frontpaneel.svg (achtergrond, schaduw, tape, rand, bogen), de
//     boog van de gekozen knop is tape; schermafbeeldingen paneel.png (640 × 250), paneel@2x.png en paneel-560.png
//     (de mockupmaat 560 × 220) in <uitmap>.
//   - knoppen sturen via de host (valkuil 14): na slepen vraagt de plugin request_flush; get_value verandert pas in
//     flush of process; flush/process geeft GESTURE_BEGIN, PARAM_VALUE…, GESTURE_END als uitvoer-events.
//   - slepen: 200 px omhoog = het hele bereik, lineair in de CLAP-waarde; Shift = 10× fijner; klemmen aan de grens;
//     dubbelklik = de standaard (exact).
//   - actief: dezelfde waarde via het paneel (in process) of als event van de host geeft bit voor bit dezelfde render.
//   - een waarde van de host (flush) verschijnt in de tekening.
// Uitvoer: één regel per toets ("ok …" of "FOUT …"), aan het eind een samenvatting; exit 0 alleen als alles ok is.
#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "paneel.h"
#include "paneel_toets.h"

namespace {

int fouten = 0, goed = 0;

void toets(bool ok, const std::string& wat) {
  std::printf("%s %s\n", ok ? "ok  " : "FOUT", wat.c_str());
  ok ? ++goed : ++fouten;
}

[[noreturn]] void stop(const std::string& t) {
  std::fprintf(stderr, "pb-paneeltoets: %s\n", t.c_str());
  std::exit(2);
}

std::string getal(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.17g", v);
  return b;
}

// --- host: telt request_flush ----------------------------------------------------------------------------------------
int flushVragen = 0;
void hostRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostRequestFlush(const clap_host_t*) { ++flushVragen; }
const clap_host_params_t HOST_PARAMS = {hostRescan, hostClear, hostRequestFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  return !std::strcmp(id, CLAP_EXT_PARAMS) ? &HOST_PARAMS : nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-paneeltoets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

// --- events ----------------------------------------------------------------------------------------------------------
union Ev {
  clap_event_header_t h;
  clap_event_param_value_t p;
  clap_event_param_gesture_t g;
  clap_event_note_t n;
};
struct Events {
  std::vector<Ev> ev;
};
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t) static_cast<const Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<const Events*>(l->ctx)->ev[i].h;
}
bool evPush(const clap_output_events_t* l, const clap_event_header_t* h) {
  if (!l->ctx || h->size > sizeof(Ev)) return true;
  Ev e{};
  std::memcpy(&e, h, h->size);
  static_cast<Events*>(l->ctx)->ev.push_back(e);
  return true;
}

Ev paramEv(uint32_t tijd, clap_id id, double v) {
  Ev e{};
  e.p.header = {sizeof(e.p), tijd, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.p.param_id = id;
  e.p.note_id = -1;
  e.p.port_index = -1;
  e.p.channel = -1;
  e.p.key = -1;
  e.p.value = v;
  return e;
}
Ev nootEv(uint32_t tijd, bool aan, int key) {
  Ev e{};
  e.n.header = {sizeof(e.n), tijd, CLAP_CORE_EVENT_SPACE_ID, (uint16_t)(aan ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
  e.n.note_id = -1;
  e.n.port_index = 0;
  e.n.channel = 0;
  e.n.key = (int16_t)key;
  e.n.velocity = aan ? 0.8 : 0.0;
  return e;
}

// de uitvoer van flush/process als tekst: B<id> V<id>=<waarde> E<id>
std::string beschrijf(const Events& e) {
  std::string s;
  for (auto& x : e.ev) {
    if (!s.empty()) s += ' ';
    if (x.h.type == CLAP_EVENT_PARAM_GESTURE_BEGIN) s += "B" + std::to_string(x.g.param_id);
    else if (x.h.type == CLAP_EVENT_PARAM_GESTURE_END) s += "E" + std::to_string(x.g.param_id);
    else if (x.h.type == CLAP_EVENT_PARAM_VALUE) s += "V" + std::to_string(x.p.param_id) + "=" + getal(x.p.value);
    else s += "?" + std::to_string(x.h.type);
  }
  return s.empty() ? "(geen)" : s;
}

// --- de plugin -------------------------------------------------------------------------------------------------------
const clap_plugin_factory_t* fab = nullptr;
const char* pluginId = nullptr;
constexpr double SR = 48000;
constexpr uint32_t BLOK = 256;

struct Param {
  clap_id id;
  std::string naam;
  double min, max, standaard;
};
std::vector<Param> ps;

struct Instantie {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* params = nullptr;
  const pb_paneeltoets_t* paneel = nullptr;
  bool actief = false;

  Instantie() {
    pl = fab->create_plugin(fab, &HOST, pluginId);
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    params = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    paneel = static_cast<const pb_paneeltoets_t*>(pl->get_extension(pl, PB_EXT_PANEELTOETS));
    if (!params || !paneel) stop("geen params- of paneeltoets-extensie");
  }
  ~Instantie() {
    paneel->weg(pl);
    if (actief) {
      pl->stop_processing(pl);
      pl->deactivate(pl);
    }
    pl->destroy(pl);
  }
  void activeer() {
    if (!pl->activate(pl, SR, 1, BLOK) || !pl->start_processing(pl)) stop("activate mislukt");
    actief = true;
  }
  Events flush(const std::vector<std::pair<clap_id, double>>& w = {}) {
    Events e, uit;
    for (auto& [id, v] : w) e.ev.push_back(paramEv(0, id, v));
    clap_input_events_t in = {&e, evSize, evGet};
    clap_output_events_t o = {&uit, evPush};
    params->flush(pl, &in, &o);
    return uit;
  }
  double waarde(clap_id id) {
    double v = NAN;
    if (!params->get_value(pl, id, &v)) stop("get_value mislukt voor id " + std::to_string(id));
    return v;
  }
  void punt(int k, double* x, double* y) {
    if (!paneel->knop(pl, k, x, y)) stop("geen knop " + std::to_string(k));
  }
  // sleep knop k `px` punten omhoog in stappen van `stap`
  void sleep(int k, double px, double stap, bool shift) {
    double x, y;
    punt(k, &x, &y);
    paneel->muis(pl, pb_paneel::NEER, x, y, shift);
    double gedaan = 0;
    while (std::fabs(gedaan) < std::fabs(px)) {
      gedaan += px > 0 ? stap : -stap;
      paneel->muis(pl, pb_paneel::SLEEP, x, y - gedaan, shift);
    }
    paneel->muis(pl, pb_paneel::OP, x, y - gedaan, shift);
  }
  std::vector<uint8_t> teken(double b, double h, double schaal) {
    const uint32_t bw = (uint32_t)std::ceil(b * schaal), bh = (uint32_t)std::ceil(h * schaal);
    std::vector<uint8_t> rgba((size_t)bw * bh * 4, 0);
    if (!paneel->teken(pl, rgba.data(), bw, bh, schaal)) stop("teken mislukt");
    return rgba;
  }
  // blokken van BLOK samples met een noot aan in blok 0; `eerste` gaat als host-events in het eerste blok
  std::vector<float> render(int blokken, const std::vector<std::pair<clap_id, double>>& eerste, Events* uitvoer) {
    std::vector<float> l(BLOK), r(BLOK), alles;
    float* uit[2] = {l.data(), r.data()};
    clap_audio_buffer_t au = {uit, nullptr, 2, 0, 0};
    for (int k = 0; k < blokken; ++k) {
      Events e, u;
      if (k == 0)
        for (auto& [id, v] : eerste) e.ev.push_back(paramEv(0, id, v));
      if (k == 0) e.ev.push_back(nootEv(9, true, 60));
      clap_input_events_t in = {&e, evSize, evGet};
      clap_output_events_t uitEv = {&u, evPush};
      clap_process_t pr = {(int64_t)k * BLOK, BLOK, nullptr, nullptr, &au, 0, 1, &in, &uitEv};
      if (pl->process(pl, &pr) == CLAP_PROCESS_ERROR) stop("process gaf een fout");
      if (uitvoer) uitvoer->ev.insert(uitvoer->ev.end(), u.ev.begin(), u.ev.end());
      alles.insert(alles.end(), l.begin(), l.end());
      alles.insert(alles.end(), r.begin(), r.end());
    }
    return alles;
  }
};

// --- pixels ----------------------------------------------------------------------------------------------------------
struct Rgb {
  int r, g, b;
};
Rgb pixel(const std::vector<uint8_t>& p, uint32_t bw, double x, double y) {
  const size_t i = 4 * ((size_t)std::floor(y) * bw + (size_t)std::floor(x));
  return {p[i], p[i + 1], p[i + 2]};
}
std::string hex(Rgb c) {
  char b[16];
  std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
  return b;
}
bool zelfde(Rgb a, Rgb b) { return std::abs(a.r - b.r) <= 2 && std::abs(a.g - b.g) <= 2 && std::abs(a.b - b.b) <= 2; }
constexpr Rgb PANEEL{0x23, 0x26, 0x29}, INKT{0xe7, 0xe5, 0xdd}, TAPE{0xf2, 0xc2, 0x00}, SPOOR{0x40, 0xcb, 0xda},
    GROEF{0x3a, 0x3e, 0x42}, SCHADUW{0x0d, 0x0e, 0x0f};

void kleurToets(const std::vector<uint8_t>& p, uint32_t bw, double s, double x, double y, Rgb verwacht,
                const std::string& wat) {
  const Rgb c = pixel(p, bw, x * s, y * s);
  toets(zelfde(c, verwacht), wat + ": " + hex(c) + " (verwacht " + hex(verwacht) + ")");
}

// een punt op de boog (straal 22) van knop k onder hoek a (graden vanaf boven, met de klok mee), in mockup-eenheden
void boogPunt(int k, double a, double* x, double* y) {
  const double r = a * 3.14159265358979323846 / 180;
  *x = 62 + 76 * k + 22 * std::sin(r);
  *y = 130 - 22 * std::cos(r);
}

void schrijf(const std::string& pad, const std::vector<uint8_t>& rgba, uint32_t b, uint32_t h) {
  const auto png = pb_paneel::maakPng(rgba.data(), (int)b, (int)h);
  FILE* f = std::fopen(pad.c_str(), "wb");
  if (!f || std::fwrite(png.data(), 1, png.size(), f) != png.size()) stop("kan " + pad + " niet schrijven");
  std::fclose(f);
  std::printf("     schermafbeelding %s (%u × %u)\n", pad.c_str(), b, h);
}

void tekenToetsen(const std::string& map) {
  Instantie in;
  toets(in.paneel->maak(in.pl, 640, 250), "paneel zonder venster gemaakt (beginmaat 640 × 250)");

  // beginmaat 640 × 250: s = min(640/560, 250/220)
  const double s = std::fmin(640.0 / 560, 250.0 / 220);
  auto p = in.teken(640, 250, 1);
  schrijf(map + "/paneel.png", p, 640, 250);
  kleurToets(p, 640, s, 300, 70, PANEEL, "achtergrond is paneel");
  kleurToets(p, 640, 1, 640 - 3 * s - 1, 250 - 3 * s - 1, SCHADUW, "harde schaduw rechtsonder");
  kleurToets(p, 640, s, 0.2, 100, INKT, "rand links is inkt");
  kleurToets(p, 640, s, 26, 40, TAPE, "naamlabel op tape");
  double x, y;
  boogPunt(0, -120, &x, &y);
  kleurToets(p, 640, s, x, y, SPOOR, "Noot (standaard 1 = 20 %): boog bij −120° is spoor");
  boogPunt(0, 0, &x, &y);
  kleurToets(p, 640, s, x, y, GROEF, "Noot: boog bij 0° is groef (voorbij de waarde)");
  boogPunt(1, -120, &x, &y);
  kleurToets(p, 640, s, x, y, GROEF, "Poort (standaard 0): de boog is helemaal groef");
  kleurToets(p, 640, s, 62 + 76 * 2 + 16, 130, INKT, "Filter: de rand van de knop is inkt");
  kleurToets(p, 640, s, 62 + 76 * 3 - 8, 130 + 8, PANEEL, "Loslaten: binnen de knop is paneel");
  toets(in.paneel->gekozen(in.pl) == -1, "geen gekozen knop zolang er niets is aangeraakt");

  // retina: dezelfde tekening op 2×
  auto p2 = in.teken(640, 250, 2);
  schrijf(map + "/paneel@2x.png", p2, 1280, 500);
  boogPunt(0, -120, &x, &y);
  kleurToets(p2, 1280, 2 * s, x, y, SPOOR, "2×: Noot-boog bij −120° is spoor");

  // de mockupmaat (frontpaneel.svg, 560 × 220): s = 1, dus de coördinaten van de svg zelf
  Instantie m;
  m.paneel->maak(m.pl, 560, 220);
  auto q = m.teken(560, 220, 1);
  schrijf(map + "/paneel-560.png", q, 560, 220);
  boogPunt(2, -120, &x, &y);
  kleurToets(q, 560, 1, x, y, SPOOR, "560 × 220: Filter (900 Hz) boog bij −120° is spoor");
  kleurToets(q, 560, 1, 556, 216, SCHADUW, "560 × 220: schaduw op (556, 216) zoals de svg");
  kleurToets(q, 560, 1, 100, 25, TAPE, "560 × 220: tape op (100, 25)");

  // de gekozen knop (laatst aangeraakt): zijn boog is tape
  double kx, ky;
  m.punt(2, &kx, &ky);
  m.paneel->muis(m.pl, pb_paneel::NEER, kx, ky, false);
  m.paneel->muis(m.pl, pb_paneel::OP, kx, ky, false);
  toets(m.paneel->gekozen(m.pl) == 2, "aanraken kiest de knop (Filter)");
  auto q2 = m.teken(560, 220, 1);
  boogPunt(2, -120, &x, &y);
  kleurToets(q2, 560, 1, x, y, TAPE, "de gekozen knop (Filter): boog is tape");
  boogPunt(0, -120, &x, &y);
  kleurToets(q2, 560, 1, x, y, SPOOR, "een andere knop (Noot) blijft spoor");
  toets(m.flush().ev.empty() && flushVragen == 0, "aanraken zonder slepen stuurt niets");
  schrijf(map + "/paneel-gekozen.png", q2, 560, 220);
}

void sleepToetsen() {
  Instantie in;
  in.paneel->maak(in.pl, 640, 250);
  const Param& filter = ps[2];
  const Param& los = ps[3];
  const Param& noot = ps[0];

  // Filter 100 px omhoog in stappen van 10: half het bereik
  flushVragen = 0;
  in.sleep(2, 100, 10, false);
  toets(flushVragen > 0, "na slepen vraagt de plugin request_flush (" + std::to_string(flushVragen) + "×)");
  toets(in.waarde(filter.id) == filter.standaard,
        "vóór flush ziet get_value nog de oude waarde (" + getal(in.waarde(filter.id)) + "): het paneel zet niets zelf");
  Events u = in.flush();
  const double verwacht = filter.standaard + 0.5 * (filter.max - filter.min);
  const bool vorm = u.ev.size() >= 3 && u.ev.front().h.type == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
                    u.ev.back().h.type == CLAP_EVENT_PARAM_GESTURE_END;
  toets(vorm && u.ev.size() == 12, "flush: GESTURE_BEGIN, 10 × PARAM_VALUE, GESTURE_END: " + beschrijf(u));
  bool stijgt = vorm;
  for (size_t i = 1; vorm && i + 1 < u.ev.size(); ++i)
    stijgt = stijgt && u.ev[i].h.type == CLAP_EVENT_PARAM_VALUE && u.ev[i].p.param_id == filter.id &&
             (i == 1 || u.ev[i].p.value > u.ev[i - 1].p.value);
  toets(stijgt, "alle waarden voor Filter (id " + std::to_string(filter.id) + "), stijgend");
  const double na = in.waarde(filter.id);
  toets(std::fabs(na - verwacht) < 1e-9, "100 px omhoog = half het bereik: " + getal(na) + " (verwacht " + getal(verwacht) + ")");
  toets(vorm && u.ev[u.ev.size() - 2].p.value == na, "de laatste PARAM_VALUE is wat get_value geeft");
  toets(in.flush().ev.empty(), "een tweede flush geeft niets meer");

  // Shift: 10× fijner
  in.sleep(3, 100, 5, true);
  in.flush();
  const double fijn = los.standaard + 0.05 * (los.max - los.min);
  toets(std::fabs(in.waarde(los.id) - fijn) < 1e-9,
        "Shift + 100 px = 5 % van het bereik: Loslaten " + getal(in.waarde(los.id)) + " (verwacht " + getal(fijn) + ")");

  // omlaag en klemmen
  in.sleep(3, -1000, 25, false);
  u = in.flush();
  toets(in.waarde(los.id) == los.min, "1000 px omlaag klemt op het minimum: " + getal(in.waarde(los.id)));
  in.sleep(0, 1000, 50, false);
  u = in.flush();
  toets(in.waarde(noot.id) == noot.max && u.ev.size() >= 2 && u.ev[u.ev.size() - 2].p.value == noot.max,
        "1000 px omhoog klemt op het maximum: Noot " + getal(in.waarde(noot.id)));

  // dubbelklik = standaard, als één gebaar
  double x, y;
  in.punt(2, &x, &y);
  in.paneel->muis(in.pl, pb_paneel::DUBBEL, x, y, false);
  in.paneel->muis(in.pl, pb_paneel::OP, x, y, false);
  u = in.flush();
  toets(beschrijf(u) == "B" + std::to_string(filter.id) + " V" + std::to_string(filter.id) + "=" + getal(filter.standaard) +
                            " E" + std::to_string(filter.id),
        "dubbelklik op Filter: " + beschrijf(u));
  toets(in.waarde(filter.id) == filter.standaard, "dubbelklik zet exact de standaard: " + getal(in.waarde(filter.id)));

  // naast de knoppen doet slepen niets
  in.paneel->muis(in.pl, pb_paneel::NEER, 500, 60, false);
  in.paneel->muis(in.pl, pb_paneel::SLEEP, 500, 10, false);
  in.paneel->muis(in.pl, pb_paneel::OP, 500, 10, false);
  toets(in.flush().ev.empty(), "slepen naast de knoppen stuurt niets");

  // een waarde van de host verschijnt in de tekening (de view tekent dan opnieuw)
  auto voor = in.teken(640, 250, 1);
  in.flush({{filter.id, 3000.0}});
  auto na2 = in.teken(640, 250, 1);
  toets(voor != na2, "Filter = 3000 van de host: de tekening verandert");
}

void actiefToetsen() {
  const Param& filter = ps[2];
  Instantie a, b;
  a.paneel->maak(a.pl, 640, 250);
  a.activeer();
  b.activeer();
  a.render(2, {}, nullptr);
  b.render(2, {}, nullptr);
  // a: het paneel sleept Filter 60 px omhoog (vóór het volgende blok); b: dezelfde eindwaarde als host-event
  a.sleep(2, 60, 60, false);
  const double doel = filter.standaard + 0.3 * (filter.max - filter.min);
  Events uitA;
  auto ra = a.render(60, {}, &uitA);
  auto rb = b.render(60, {{filter.id, a.waarde(filter.id)}}, nullptr);
  toets(beschrijf(uitA) == "B" + std::to_string(filter.id) + " V" + std::to_string(filter.id) + "=" +
                               getal(a.waarde(filter.id)) + " E" + std::to_string(filter.id),
        "actief: process stuurt het gebaar naar de host: " + beschrijf(uitA));
  toets(std::fabs(a.waarde(filter.id) - doel) < 1e-9, "actief: get_value na process " + getal(a.waarde(filter.id)));
  bool stil = true;
  for (float v : ra) stil = stil && v == 0;
  toets(!stil && ra.size() == rb.size() && !std::memcmp(ra.data(), rb.data(), ra.size() * sizeof(float)),
        "actief: paneel-waarde in process klinkt bit voor bit als dezelfde waarde van de host");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) stop("gebruik: pb-paneeltoets <plugin.clap> <uitmap>");
  std::string pad = argv[1], lib = pad, map = argv[2];
#ifdef __APPLE__
  {  // een .clap is op macOS een bundle: de code staat in Contents/MacOS/<naam zonder .clap>
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
    lib = pad + "/Contents/MacOS/" + naam;
  }
#endif
  void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) stop(std::string("dlopen: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
  if (!entry || !entry->init(pad.c_str())) stop("geen clap_entry");
  fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!fab || fab->get_plugin_count(fab) < 1) stop("geen plugin in de factory");
  pluginId = fab->get_plugin_descriptor(fab, 0)->id;

  {
    Instantie in;
    const uint32_t n = in.params->count(in.pl);
    for (uint32_t i = 0; i < n; ++i) {
      clap_param_info_t info;
      if (!in.params->get_info(in.pl, i, &info)) stop("get_info mislukt");
      if (info.flags & CLAP_PARAM_IS_BYPASS) continue;  // de bypass (fase 5) is geen knop op het paneel
      ps.push_back({info.id, info.name, info.min_value, info.max_value, info.default_value});
    }
    if (ps.size() != 4) stop("verwacht 4 parameters (Noot, Poort, Filter, Loslaten)");
    const auto* gui = static_cast<const clap_plugin_gui_t*>(in.pl->get_extension(in.pl, CLAP_EXT_GUI));
#ifdef __APPLE__
    toets(gui && gui->is_api_supported(in.pl, CLAP_WINDOW_API_COCOA, false) &&
              !gui->is_api_supported(in.pl, CLAP_WINDOW_API_COCOA, true),
          "gui-extensie: Cocoa, ingebed (niet zwevend)");
#else
    toets(gui == nullptr, "gui-extensie alleen op macOS (Cocoa); hier geen (pluginval opent dan geen editor)");
#endif
  }

  tekenToetsen(map);
  sleepToetsen();
  actiefToetsen();

  entry->deinit();
  std::printf("\npb-paneeltoets: %d toetsen, %d ok, %d fout\n", goed + fouten, goed, fouten);
  return fouten == 0 ? 0 : 1;
}
