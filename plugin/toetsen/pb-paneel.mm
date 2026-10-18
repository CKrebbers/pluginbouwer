// pb-paneel — toets van fase 3 op macOS, door het gebouwde .clap: het paneel in een echt venster.
//
//   pb-paneel <plugin.clap> <uitmap>
//
// Een kleine CLAP-host met de gui-extensie: laadt het .clap (dlopen), maakt het paneel (Cocoa, ingebed), zet het in
// een NSWindow en
//   - controleert de gui-extensie: alleen Cocoa en ingebed, beginmaat 640 × 250, minimaal 560 × 220, schaalbaar;
//   - schrijft schermafbeeldingen (PNG) in <uitmap>: frontpaneel.png (beginmaat), frontpaneel-560x220.png (de maat
//     van de mockup) en frontpaneel-bediend.png (na bedienen en een verandering door de host);
//   - leest pixels terug: tape, paneel, harde schaduw, groef en spoor waar de mockup ze heeft, tekst in label en
//     waarde, de breedte van het naamlabel (Silkscreen geladen), een andere boog na een verandering door de host en
//     de tape-boog van de gekozen knop;
//   - bedient het met gesimuleerde muisevents (mouseDown/Dragged/Up naar de view, zoals AppKit ze geeft): slepen,
//     Shift, dubbelklik; de wijzigingen moeten via de host lopen: request_flush, dan GESTURE_BEGIN/PARAM_VALUE/
//     GESTURE_END uit params.flush (niet actief) en uit process (actief), en get_value pas daarna;
//   - sluit het paneel midden in een gebaar (GESTURE_END volgt) en maakt het een paar keer opnieuw.
// Exitcode 0 = alles groen; anders 1, met per mislukte controle een regel "MISLUKT".
#import <Cocoa/Cocoa.h>
#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int fouten = 0, goed = 0;
void controle(bool ok, const std::string& wat) {
  if (ok) ++goed;
  else ++fouten;
  std::printf("%s %s\n", ok ? "ok      " : "MISLUKT ", wat.c_str());
  std::fflush(stdout);
}
[[noreturn]] void fout(const std::string& t) {
  std::fprintf(stderr, "pb-paneel: %s\n", t.c_str());
  std::exit(2);
}
std::string g(double v) {
  char t[64];
  std::snprintf(t, sizeof t, "%.6g", v);
  return t;
}

// --- host --------------------------------------------------------------------------------------------------------
int flushVragen = 0;
void hpRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpFlush(const clap_host_t*) { ++flushVragen; }
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpFlush};
void hgHints(const clap_host_t*) {}
bool hgResize(const clap_host_t*, uint32_t, uint32_t) { return true; }
bool hgShow(const clap_host_t*) { return true; }
void hgClosed(const clap_host_t*, bool) {}
const clap_host_gui_t HOST_GUI = {hgHints, hgResize, hgShow, hgShow, hgClosed};
const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
  if (!std::strcmp(id, CLAP_EXT_GUI)) return &HOST_GUI;
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-paneel", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

union Ev {
  clap_event_header_t h;
  clap_event_param_value_t p;
  clap_event_param_gesture_t g;
};
struct Lijst { std::vector<Ev> ev; };
uint32_t lSize(const clap_input_events_t* l) { return (uint32_t)static_cast<Lijst*>(l->ctx)->ev.size(); }
const clap_event_header_t* lGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<Lijst*>(l->ctx)->ev[i].h;
}
bool lPush(const clap_output_events_t* l, const clap_event_header_t* h) {
  Ev e{};
  if (h->size > sizeof e) return false;
  std::memcpy(&e, h, h->size);
  static_cast<Lijst*>(l->ctx)->ev.push_back(e);
  return true;
}
Ev paramEv(clap_id id, double v) {
  Ev e{};
  e.p.header = {sizeof(e.p), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.p.param_id = id; e.p.note_id = -1; e.p.port_index = -1; e.p.channel = -1; e.p.key = -1; e.p.value = v;
  return e;
}

std::string beschrijf(const std::vector<Ev>& ev) {
  std::string s;
  for (const Ev& e : ev) {
    if (e.h.type == CLAP_EVENT_PARAM_GESTURE_BEGIN) s += "B" + std::to_string(e.g.param_id) + " ";
    else if (e.h.type == CLAP_EVENT_PARAM_GESTURE_END) s += "E" + std::to_string(e.g.param_id) + " ";
    else if (e.h.type == CLAP_EVENT_PARAM_VALUE) s += "V" + std::to_string(e.p.param_id) + "=" + g(e.p.value) + " ";
    else s += "?" + std::to_string(e.h.type) + " ";
  }
  return s.empty() ? "(geen)" : s;
}

// --- de plugin ---------------------------------------------------------------------------------------------------
const clap_plugin_t* pl = nullptr;
const clap_plugin_params_t* pp = nullptr;
const clap_plugin_gui_t* gui = nullptr;

std::vector<Ev> flush(const std::vector<Ev>& in = {}) {
  Lijst li{in}, uit;
  clap_input_events_t i = {&li, lSize, lGet};
  clap_output_events_t o = {&uit, lPush};
  pp->flush(pl, &i, &o);
  return uit.ev;
}
double waarde(clap_id id) {
  double v = NAN;
  pp->get_value(pl, id, &v);
  return v;
}
std::vector<Ev> blok(int64_t steady) {
  std::vector<float> l(512), r(512);
  float* uitK[2] = {l.data(), r.data()};
  Lijst li, uit;
  clap_input_events_t i = {&li, lSize, lGet};
  clap_output_events_t o = {&uit, lPush};
  clap_audio_buffer_t au = {uitK, nullptr, 2, 0, 0};
  clap_process_t pr = {steady, 512, nullptr, nullptr, &au, 0, 1, &i, &o};
  if (pl->process(pl, &pr) == CLAP_PROCESS_ERROR) fout("process gaf een fout");
  return uit.ev;
}

// --- venster, muis en pixels -------------------------------------------------------------------------------------
NSWindow* venster = nil;
NSView* ouder = nil;

NSView* paneelView() { return [[ouder subviews] count] ? [[ouder subviews] objectAtIndex:0] : nil; }

void zetMaat(uint32_t b, uint32_t h) {
  [venster setContentSize:NSMakeSize(b, h)];
  [ouder setFrame:NSMakeRect(0, 0, b, h)];
  gui->set_size(pl, b, h);
}

// een muisevent op (x, y) in de coördinaten van het paneel (punten, y omlaag)
void muis(NSEventType t, double x, double y, int klikken = 1, bool shift = false) {
  NSView* v = paneelView();
  if (!v) return;
  const NSPoint p = [v convertPoint:NSMakePoint(x, y) toView:nil];
  NSEvent* e = [NSEvent mouseEventWithType:t
                                  location:p
                             modifierFlags:(shift ? NSEventModifierFlagShift : 0)
                                 timestamp:[[NSProcessInfo processInfo] systemUptime]
                              windowNumber:[venster windowNumber]
                                   context:nil
                               eventNumber:0
                                clickCount:klikken
                                  pressure:1.0];
  if (t == NSEventTypeLeftMouseDown) [v mouseDown:e];
  else if (t == NSEventTypeLeftMouseDragged) [v mouseDragged:e];
  else [v mouseUp:e];
}

NSBitmapImageRep* foto() {
  NSView* v = ouder;
  NSBitmapImageRep* r = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
  [v cacheDisplayInRect:[v bounds] toBitmapImageRep:r];
  return r;
}

bool schrijf(NSBitmapImageRep* r, const std::string& pad) {
  NSData* d = [r representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  return d && [d writeToFile:[NSString stringWithUTF8String:pad.c_str()] atomically:YES];
}

struct Rgb { int r, g, b; };
// de kleur onder punt (x, y) van het venster (punten, y omlaag), in sRGB
Rgb pixel(NSBitmapImageRep* r, double x, double y) {
  const double s = (double)[r pixelsWide] / [ouder bounds].size.width;
  NSColor* c = [[r colorAtX:(NSInteger)std::floor(x * s) y:(NSInteger)std::floor(y * s)]
      colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
  if (!c) return {-1, -1, -1};
  return {(int)std::lround([c redComponent] * 255), (int)std::lround([c greenComponent] * 255),
          (int)std::lround([c blueComponent] * 255)};
}
bool lijkt(Rgb a, Rgb b, int tol = 10) {
  return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol;
}
std::string hex(Rgb c) {
  char t[16];
  std::snprintf(t, sizeof t, "#%02x%02x%02x", c.r & 255, c.g & 255, c.b & 255);
  return t;
}
// hoeveel pixels in een rechthoek (punten) licht (> 50 %) of donker (< 40 %) zijn: tekst, ook kleine en
// half-bedekte pixels (anti-aliasing) tellen mee
int tel(NSBitmapImageRep* r, double x, double y, double b, double h, bool licht) {
  int n = 0;
  for (double yy = y + 0.5; yy < y + h; yy += 1)
    for (double xx = x + 0.5; xx < x + b; xx += 1) {
      const Rgb p = pixel(r, xx, yy);
      const int som = p.r + p.g + p.b;
      if (licht ? som > 3 * 128 : (som >= 0 && som < 3 * 100)) ++n;
    }
  return n;
}

const Rgb PANEEL{0x23, 0x26, 0x29}, INKT{0xe7, 0xe5, 0xdd}, TAPE{0xf2, 0xc2, 0x00}, SPOOR{0x40, 0xcb, 0xda},
    GROEF{0x3a, 0x3e, 0x42}, SCHADUW{0x0d, 0x0e, 0x0f};

// een punt op de boog (r = 22) van knop i bij hoek a (graden, 0 = boven), op de maat van de mockup
NSPoint opBoog(int i, double a) {
  const double rad = a * M_PI / 180.0;
  return NSMakePoint(62 + 76 * i + 22 * std::sin(rad), 130 - 22 * std::cos(rad));
}
void controleKleur(NSBitmapImageRep* r, NSPoint p, Rgb c, const std::string& wat) {
  const Rgb z = pixel(r, p.x, p.y);
  controle(lijkt(z, c), wat + ": " + hex(z) + " bij (" + g(p.x) + ", " + g(p.y) + "), verwacht " + hex(c));
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc < 3) fout("gebruik: pb-paneel <plugin.clap> <uitmap>");
    std::string pad = argv[1];
    const std::string uitmap = argv[2];
    std::string lib = pad;
    {  // een .clap is op macOS een bundle: de code staat in Contents/MacOS/<naam zonder .clap>
      std::string naam = pad.substr(pad.find_last_of('/') + 1);
      if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam = naam.substr(0, naam.size() - 5);
      lib = pad + "/Contents/MacOS/" + naam;
    }
    void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) fout(std::string("dlopen: ") + dlerror());
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
    if (!entry || !entry->init(pad.c_str())) fout("geen clap_entry");
    auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!fab || fab->get_plugin_count(fab) < 1) fout("geen factory");
    pl = fab->create_plugin(fab, &HOST, fab->get_plugin_descriptor(fab, 0)->id);
    if (!pl || !pl->init(pl)) fout("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    gui = static_cast<const clap_plugin_gui_t*>(pl->get_extension(pl, CLAP_EXT_GUI));
    if (!pp) fout("geen params-extensie");
    controle(gui != nullptr, "de plugin heeft de gui-extensie");
    if (!gui) fout("zonder gui-extensie valt er niets te toetsen");

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    // --- de gui-extensie ---------------------------------------------------------------------------------------
    controle(gui->is_api_supported(pl, CLAP_WINDOW_API_COCOA, false), "Cocoa ingebed wordt ondersteund");
    controle(!gui->is_api_supported(pl, CLAP_WINDOW_API_COCOA, true), "zwevend niet");
    controle(!gui->is_api_supported(pl, CLAP_WINDOW_API_X11, false), "X11 niet (macOS)");
    const char* api = nullptr;
    bool zwevend = true;
    controle(gui->get_preferred_api(pl, &api, &zwevend) && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA) && !zwevend,
             "voorkeur: Cocoa, ingebed");
    controle(!gui->create(pl, CLAP_WINDOW_API_X11, false), "create met X11 weigert");
    controle(gui->create(pl, CLAP_WINDOW_API_COCOA, false), "create met Cocoa");
    uint32_t b = 0, hh = 0;
    controle(gui->get_size(pl, &b, &hh) && b == 640 && hh == 250, "beginmaat " + std::to_string(b) + " × " +
                                                                      std::to_string(hh) + " (design.md: 640 × 250)");
    controle(gui->can_resize(pl), "schaalbaar");
    clap_gui_resize_hints_t hints{};
    controle(gui->get_resize_hints(pl, &hints) && hints.can_resize_horizontally && hints.can_resize_vertically,
             "breed en hoog schaalbaar");
    uint32_t ab = 100, ah = 100;
    controle(gui->adjust_size(pl, &ab, &ah) && ab == 560 && ah == 220, "adjust_size(100, 100) = " +
                                                                           std::to_string(ab) + " × " +
                                                                           std::to_string(ah) + " (minimaal 560 × 220)");
    ab = 800; ah = 300;
    controle(gui->adjust_size(pl, &ab, &ah) && ab == 800 && ah == 300, "adjust_size(800, 300) blijft 800 × 300");

    venster = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, b, hh)
                                          styleMask:NSWindowStyleMaskTitled
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [venster setReleasedWhenClosed:NO];
    [venster setTitle:@"PB Toetsenpad"];
    ouder = [venster contentView];
    clap_window_t w{};
    w.api = CLAP_WINDOW_API_COCOA;
    w.cocoa = (void*)ouder;
    controle(gui->set_parent(pl, &w), "set_parent in een NSWindow");
    controle(gui->show(pl), "show");
    [venster orderFrontRegardless];
    NSView* v = paneelView();
    controle(v != nil, "het paneel is een subview van de view van de host");
    if (!v) fout("geen view");
    controle([v frame].size.width == 640 && [v frame].size.height == 250, "de view is 640 × 250");

    NSBitmapImageRep* r = foto();
    controle(schrijf(r, uitmap + "/frontpaneel.png"), "schermafbeelding frontpaneel.png (" +
                                                          std::to_string([r pixelsWide]) + " × " +
                                                          std::to_string([r pixelsHigh]) + " pixels)");
    // op de beginmaat: geschaald (250/220) en in het midden
    {
      const double s = 250.0 / 220.0, ox = (640 - 560 * s) / 2;
      controleKleur(r, NSMakePoint(ox + 26 * s, 20 * s), TAPE, "640 × 250: tape linksboven in het naamlabel");
      controleKleur(r, NSMakePoint(ox + 400 * s, 100 * s), PANEEL, "640 × 250: paneel");
    }

    // --- op de maat van de mockup: pixels waar de mockup ze heeft ------------------------------------------------
    zetMaat(560, 220);
    controle([v frame].size.width == 560 && [v frame].size.height == 220, "set_size(560, 220): de view volgt");
    r = foto();
    controle(schrijf(r, uitmap + "/frontpaneel-560x220.png"), "schermafbeelding frontpaneel-560x220.png");
    controleKleur(r, NSMakePoint(26, 20), TAPE, "tape (naamlabel)");
    controleKleur(r, NSMakePoint(400, 100), PANEEL, "paneel");
    controleKleur(r, NSMakePoint(556, 100), SCHADUW, "harde schaduw rechts");
    controleKleur(r, NSMakePoint(300, 216), SCHADUW, "harde schaduw onder");
    controleKleur(r, NSMakePoint(554, 100), INKT, "rand van 2 px rechts");
    controleKleur(r, opBoog(0, 90), GROEF, "Noot: groef rechts van de knop");
    controleKleur(r, opBoog(0, -110), SPOOR, "Noot: spoor tussen −135° en de wijzer (−81°)");
    controleKleur(r, opBoog(2, -120), SPOOR, "Filter: spoor");
    controleKleur(r, opBoog(1, -100), GROEF, "Poort: op 0 % alleen groef");
    controleKleur(r, opBoog(0, -30), GROEF, "Noot: groef voorbij de wijzer");
    controleKleur(r, NSMakePoint(62 + 8, 130), PANEEL, "Noot: binnen in de knop");
    controleKleur(r, NSMakePoint(62 + 16, 130), INKT, "Noot: rand van de knop (r = 16)");
    for (int i = 0; i < 4; ++i) {
      const double cx = 62 + 76 * i;
      const int label = tel(r, cx - 30, 130 - 34 - 10, 60, 13, true);
      const int waard = tel(r, cx - 30, 130 + 44 - 10, 60, 13, true);
      controle(label >= 5 && waard >= 5, "knop " + std::to_string(i + 1) + ": tekst in label (" +
                                             std::to_string(label) + " px) en waarde (" + std::to_string(waard) +
                                             " px)");
    }
    {  // het naamlabel is zo breed als de tekst + 12 px; met Silkscreen 14 px is dat ±129 (de mockup)
      int rechts = -1;
      for (int x = 100; x < 250; ++x)
        if (!lijkt(pixel(r, x + 0.5, 20.5), TAPE)) {
          rechts = x;
          break;
        }
      controle(rechts >= 24 + 122 && rechts <= 24 + 136, "naamlabel " + std::to_string(rechts - 24) +
                                                             " px breed (mockup 129; Silkscreen geladen)");
      const int tekst = tel(r, 30, 22, rechts - 34, 16, false);
      controle(tekst >= 40, "naam in het label: " + std::to_string(tekst) + " px in paneelkleur");
    }

    // --- bedienen: niet actief, via params.flush -------------------------------------------------------------
    flushVragen = 0;
    muis(NSEventTypeLeftMouseDown, 214, 130);
    controle(flushVragen >= 1, "muis neer op Filter: request_flush");
    muis(NSEventTypeLeftMouseDragged, 214, 80);
    muis(NSEventTypeLeftMouseDragged, 214, 30);
    controle(waarde(3) == 900, "voor flush: get_value(Filter) nog 900");
    muis(NSEventTypeLeftMouseUp, 214, 30);
    auto ev = flush();
    controle(beschrijf(ev) == "B3 V3=2875 V3=4850 E3 ", "Filter 50 en 100 px omhoog: " + beschrijf(ev));
    controle(waarde(3) == 4850, "na flush: get_value(Filter) = " + g(waarde(3)));

    muis(NSEventTypeLeftMouseDown, 290, 130);
    muis(NSEventTypeLeftMouseDragged, 290, 30, 1, true);
    muis(NSEventTypeLeftMouseUp, 290, 30, 1, true);
    ev = flush();
    controle(beschrijf(ev) == "B4 V4=649.5 E4 ", "Loslaten 100 px met Shift (10× fijner): " + beschrijf(ev));

    muis(NSEventTypeLeftMouseDown, 214, 130, 1);
    muis(NSEventTypeLeftMouseUp, 214, 130, 1);
    muis(NSEventTypeLeftMouseDown, 214, 130, 2);
    muis(NSEventTypeLeftMouseUp, 214, 130, 2);
    ev = flush();
    controle(beschrijf(ev) == "B3 E3 B3 V3=900 E3 ", "dubbelklik op Filter = standaard: " + beschrijf(ev));
    controle(waarde(3) == 900, "Filter weer 900");

    // de host verandert Noot (automatisering): het paneel laat het zien; Filter is de gekozen knop (tape)
    flush({paramEv(1, 2.0)});
    r = foto();
    controle(schrijf(r, uitmap + "/frontpaneel-bediend.png"), "schermafbeelding frontpaneel-bediend.png");
    controleKleur(r, opBoog(0, -30), SPOOR, "Noot op 200 % door de host: het spoor loopt nu tot voorbij −30°");
    controleKleur(r, opBoog(2, -120), TAPE, "de gekozen knop (Filter, laatst aangeraakt) heeft een tape-boog");
    controleKleur(r, opBoog(3, -120), SPOOR, "Loslaten (niet gekozen) heeft een spoor-boog");

    // --- actief: process geeft de events --------------------------------------------------------------------
    if (!pl->activate(pl, 48000, 1, 512) || !pl->start_processing(pl)) fout("activate mislukt");
    muis(NSEventTypeLeftMouseDown, 138, 130);
    muis(NSEventTypeLeftMouseDragged, 138, 90);
    muis(NSEventTypeLeftMouseUp, 138, 90);
    ev = blok(0);
    controle(beschrijf(ev) == "B2 V2=0.2 E2 ", "actief: process geeft " + beschrijf(ev));
    controle(waarde(2) == 0.2, "actief: get_value(Poort) = " + g(waarde(2)));
    controle(blok(512).empty(), "het volgende blok geeft niets meer");

    // het paneel gaat dicht midden in een gebaar: GESTURE_END volgt
    muis(NSEventTypeLeftMouseDown, 62, 130);
    gui->destroy(pl);
    controle(paneelView() == nil, "gui.destroy haalt de view weg");
    ev = blok(1024);
    controle(beschrijf(ev) == "B1 E1 ", "paneel dicht tijdens een gebaar: " + beschrijf(ev));
    pl->stop_processing(pl);
    pl->deactivate(pl);

    // een paar keer openen en sluiten, ook zonder set_parent
    bool steeds = true;
    for (int k = 0; k < 5; ++k) {
      steeds = steeds && gui->create(pl, CLAP_WINDOW_API_COCOA, false);
      if (k % 2 == 0) steeds = steeds && gui->set_parent(pl, &w) && gui->show(pl) && paneelView() != nil;
      if (k % 2 == 0) (void)foto();
      gui->destroy(pl);
      steeds = steeds && paneelView() == nil;
    }
    controle(steeds, "vijf keer create/destroy (drie keer in het venster, twee keer zonder)");

    pl->destroy(pl);
    entry->deinit();
    [venster close];
    [venster release];
  }
  std::printf("pb-paneel: %d controles, %d geslaagd, %d mislukt\n", goed + fouten, goed, fouten);
  return fouten ? 1 : 0;
}
