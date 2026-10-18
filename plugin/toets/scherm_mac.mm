// scherm_mac.mm — pb-scherm: het echte Cocoa-paneel via de CLAP gui-extensie, door het gebouwde .clap (macOS).
//
//   pb-scherm <plugin.clap> <uitmap>
//
// Doet wat een host doet: gui.create(Cocoa), get_size, set_parent op de contentView van een NSWindow, show. Dan:
//   - schermafbeeldingen van de view (cacheDisplayInRect, dus de drawRect van de plugin): paneel-mac.png, na slepen
//     paneel-mac-gekozen.png en na set_size 800 × 320 paneel-mac-800.png; plus een best-effort `screencapture` van het
//     venster (paneel-mac-venster.png);
//   - kleuren uit design.md op de plekken uit frontpaneel.svg;
//   - muis met echte NSEvents naar de view (hitTest, mouseDown/Dragged/Up, Shift, dubbelklik): de plugin vraagt
//     request_flush, flush geeft GESTURE_BEGIN/PARAM_VALUE/GESTURE_END, get_value volgt pas na flush;
//   - adjust_size klemt op de minimummaat 560 × 220; destroy twee keer (zoals clap-wrapper's AUv2 Cleanup) mag.
// Uitvoer: één regel per toets ("ok …" of "FOUT …"), aan het eind een samenvatting; exit 0 alleen als alles ok is.
#import <Cocoa/Cocoa.h>
#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "paneel_toets.h"

namespace {

int fouten = 0, goed = 0;

void toets(bool ok, const std::string& wat) {
  std::printf("%s %s\n", ok ? "ok  " : "FOUT", wat.c_str());
  std::fflush(stdout);
  ok ? ++goed : ++fouten;
}

[[noreturn]] void stop(const std::string& t) {
  std::fprintf(stderr, "pb-scherm: %s\n", t.c_str());
  std::exit(2);
}

std::string getal(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.17g", v);
  return b;
}

int flushVragen = 0;
void hostRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostRequestFlush(const clap_host_t*) { ++flushVragen; }
const clap_host_params_t HOST_PARAMS = {hostRescan, hostClear, hostRequestFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  return !std::strcmp(id, CLAP_EXT_PARAMS) ? &HOST_PARAMS : nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-scherm", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

union Ev {
  clap_event_header_t h;
  clap_event_param_value_t p;
  clap_event_param_gesture_t g;
};
struct Events {
  std::vector<Ev> ev;
};
uint32_t evSize(const clap_input_events_t*) { return 0; }
const clap_event_header_t* evGet(const clap_input_events_t*, uint32_t) { return nullptr; }
bool evPush(const clap_output_events_t* l, const clap_event_header_t* h) {
  if (h->size > sizeof(Ev)) return true;
  Ev e{};
  std::memcpy(&e, h, h->size);
  static_cast<Events*>(l->ctx)->ev.push_back(e);
  return true;
}
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

void draai(double sec) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:sec]]; }

struct Rgb {
  int r, g, b;
};
std::string hex(Rgb c) {
  char b[16];
  std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
  return b;
}
constexpr Rgb PANEEL{0x23, 0x26, 0x29}, INKT{0xe7, 0xe5, 0xdd}, TAPE{0xf2, 0xc2, 0x00}, SPOOR{0x40, 0xcb, 0xda},
    GROEF{0x3a, 0x3e, 0x42}, SCHADUW{0x0d, 0x0e, 0x0f};

// een schermafbeelding van de view (zijn eigen drawRect) als bitmap, en als PNG naar pad. De bitmap is sRGB, zoals de
// kleuren in design.md: dan is het tekenen van het (sRGB-)beeld van de plugin een identiteit en lezen we ruwe bytes.
// (bitmapImageRepForCachingDisplayInRect geeft een bitmap in de kleurruimte van het scherm; de omzetting heen en terug
// verschoof daar #232629 naar #2f3336, eerste CI-run van fase 3.)
NSBitmapImageRep* foto(NSView* v, const std::string& pad) {
  const NSRect r = [v bounds];
  const double schaal = [v window] ? [[v window] backingScaleFactor] : 1.0;
  NSBitmapImageRep* leeg = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                   pixelsWide:(NSInteger)std::ceil(r.size.width * schaal)
                                                                   pixelsHigh:(NSInteger)std::ceil(r.size.height * schaal)
                                                                bitsPerSample:8
                                                              samplesPerPixel:4
                                                                     hasAlpha:YES
                                                                     isPlanar:NO
                                                               colorSpaceName:NSCalibratedRGBColorSpace
                                                                  bytesPerRow:0
                                                                 bitsPerPixel:32] autorelease];
  NSBitmapImageRep* rep = [leeg bitmapImageRepByRetaggingWithColorSpace:[NSColorSpace sRGBColorSpace]];
  [rep setSize:r.size];
  [v cacheDisplayInRect:r toBitmapImageRep:rep];
  NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  if (![png writeToFile:[NSString stringWithUTF8String:pad.c_str()] atomically:YES]) stop("kan " + pad + " niet schrijven");
  std::printf("     schermafbeelding %s (%ld × %ld pixels, %s)\n", pad.c_str(), (long)[rep pixelsWide],
              (long)[rep pixelsHigh], [[[rep colorSpace] localizedName] UTF8String]);
  return rep;
}

// kleur op (x, y) in mockup-eenheden (s = punten per eenheid): de ruwe sRGB-bytes van de bitmap
void kleur(NSBitmapImageRep* rep, NSView* v, double s, double x, double y, Rgb verwacht, const std::string& wat) {
  const double px = [rep pixelsWide] / [v bounds].size.width;
  const NSInteger i = (NSInteger)std::floor(x * s * px), j = (NSInteger)std::floor(y * s * px);
  const unsigned char* q = [rep bitmapData] + j * [rep bytesPerRow] + i * ([rep bitsPerPixel] / 8);
  const Rgb k{q[0], q[1], q[2]};
  const bool ok = std::abs(k.r - verwacht.r) <= 2 && std::abs(k.g - verwacht.g) <= 2 && std::abs(k.b - verwacht.b) <= 2;
  toets(ok, wat + ": " + hex(k) + " (verwacht " + hex(verwacht) + ")");
}

void boogPunt(int k, double a, double* x, double* y) {
  const double r = a * 3.14159265358979323846 / 180;
  *x = 62 + 76 * k + 22 * std::sin(r);
  *y = 130 - 22 * std::cos(r);
}

NSEvent* muisEvent(NSEventType soort, NSView* v, double x, double y, NSEventModifierFlags mod, NSInteger klikken) {
  const NSPoint w = [v convertPoint:NSMakePoint(x, y) toView:nil];
  return [NSEvent mouseEventWithType:soort
                            location:w
                       modifierFlags:mod
                           timestamp:[[NSProcessInfo processInfo] systemUptime]
                        windowNumber:[[v window] windowNumber]
                             context:nil
                         eventNumber:0
                          clickCount:klikken
                            pressure:1];
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 3) stop("gebruik: pb-scherm <plugin.clap> <uitmap>");
    std::string pad = argv[1], map = argv[2];
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
    const std::string lib = pad + "/Contents/MacOS/" + naam;

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) stop(std::string("dlopen: ") + dlerror());
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
    if (!entry || !entry->init(pad.c_str())) stop("geen clap_entry");
    auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!fab || fab->get_plugin_count(fab) < 1) stop("geen plugin in de factory");
    const clap_plugin_t* pl = fab->create_plugin(fab, &HOST, fab->get_plugin_descriptor(fab, 0)->id);
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    auto* params = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    auto* gui = static_cast<const clap_plugin_gui_t*>(pl->get_extension(pl, CLAP_EXT_GUI));
    auto* pt = static_cast<const pb_paneeltoets_t*>(pl->get_extension(pl, PB_EXT_PANEELTOETS));
    if (!params || !gui || !pt) stop("geen params-, gui- of paneeltoets-extensie");

    clap_param_info_t filter, los;
    params->get_info(pl, 2, &filter);
    params->get_info(pl, 3, &los);

    // --- zoals een host: create, get_size, set_parent, show --------------------------------------------------------
    const char* api = nullptr;
    bool zwevend = true;
    toets(gui->get_preferred_api(pl, &api, &zwevend) && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA) && !zwevend,
          "voorkeur: Cocoa, ingebed");
    toets(!gui->is_api_supported(pl, CLAP_WINDOW_API_COCOA, true), "zwevend venster: niet ondersteund");
    toets(gui->create(pl, CLAP_WINDOW_API_COCOA, false), "gui.create(Cocoa)");
    uint32_t b = 0, hh = 0;
    toets(gui->get_size(pl, &b, &hh) && b == 640 && hh == 250, "beginmaat " + std::to_string(b) + " × " + std::to_string(hh) + " (design.md: 640 × 250)");
    uint32_t ab = 100, ah = 50;
    toets(gui->can_resize(pl) && gui->adjust_size(pl, &ab, &ah) && ab == 560 && ah == 220,
          "schaalbaar; adjust_size(100, 50) klemt op het minimum " + std::to_string(ab) + " × " + std::to_string(ah));

    NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, b, hh)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    [w setTitle:@"PB Toetsenpad"];
    clap_window_t cw;
    cw.api = CLAP_WINDOW_API_COCOA;
    cw.cocoa = [w contentView];
    toets(gui->set_parent(pl, &cw), "gui.set_parent op de contentView");
    toets(gui->show(pl), "gui.show");
    [w makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    draai(0.5);

    NSView* v = [[[w contentView] subviews] firstObject];
    toets(v != nil, "de plugin hing een view in het venster");
    if (!v) stop("geen view");
    toets(NSEqualSizes([v frame].size, NSMakeSize(640, 250)), "de view is 640 × 250");
    NSString* klasse = NSStringFromClass([v class]);
    toets([klasse hasPrefix:@"PBToetsenpadPaneel_"], std::string("klasse ") + [klasse UTF8String]);
    const NSPoint midden = NSMakePoint(320, 125);
    toets([[w contentView] hitTest:[[w contentView] convertPoint:midden fromView:v]] == v, "hitTest vindt de view van de plugin");

    // --- de schermafbeelding ---------------------------------------------------------------------------------------
    const double s = std::fmin(640.0 / 560, 250.0 / 220);
    NSBitmapImageRep* rep = foto(v, map + "/paneel-mac.png");
    kleur(rep, v, s, 300, 70, PANEEL, "achtergrond is paneel");
    kleur(rep, v, 1, 640 - 3 * s - 1, 250 - 3 * s - 1, SCHADUW, "harde schaduw rechtsonder");
    kleur(rep, v, s, 26, 40, TAPE, "naamlabel op tape");
    double x, y;
    boogPunt(0, -120, &x, &y);
    kleur(rep, v, s, x, y, SPOOR, "Noot: boog bij −120° is spoor");
    boogPunt(1, -120, &x, &y);
    kleur(rep, v, s, x, y, GROEF, "Poort (0 %): boog is groef");
    kleur(rep, v, s, 62 + 76 * 2 + 16, 130, INKT, "Filter: rand van de knop is inkt");
    {
      // best-effort: het venster zoals het op het scherm staat (kan zonder schermopname-toestemming leeg zijn)
      const std::string cmd = "screencapture -x -o -l" + std::to_string((long)[w windowNumber]) + " '" + map +
                              "/paneel-mac-venster.png'";
      const int r = std::system(cmd.c_str());
      std::printf("     screencapture van het venster: exit %d (alleen ter informatie)\n", r);
    }

    // --- muis: echte NSEvents naar de view -------------------------------------------------------------------------
    double kx, ky;
    pt->knop(pl, 2, &kx, &ky);  // het midden van Filter, in punten van de view
    flushVragen = 0;
    [v mouseDown:muisEvent(NSEventTypeLeftMouseDown, v, kx, ky, 0, 1)];
    for (int k = 1; k <= 10; ++k) [v mouseDragged:muisEvent(NSEventTypeLeftMouseDragged, v, kx, ky - 10 * k, 0, 1)];
    [v mouseUp:muisEvent(NSEventTypeLeftMouseUp, v, kx, ky - 100, 0, 1)];
    double vw = 0;
    params->get_value(pl, filter.id, &vw);
    toets(flushVragen > 0 && vw == filter.default_value,
          "na slepen: request_flush (" + std::to_string(flushVragen) + "×), get_value nog " + getal(vw) + " (het paneel zet niets zelf)");
    Events uit;
    clap_input_events_t in = {nullptr, evSize, evGet};
    clap_output_events_t o = {&uit, evPush};
    params->flush(pl, &in, &o);
    params->get_value(pl, filter.id, &vw);
    const double verwacht = filter.default_value + 0.5 * (filter.max_value - filter.min_value);
    toets(uit.ev.size() == 12 && uit.ev.front().h.type == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
              uit.ev.back().h.type == CLAP_EVENT_PARAM_GESTURE_END,
          "flush na NSEvents: " + beschrijf(uit));
    toets(std::fabs(vw - verwacht) < 1e-9, "100 pt omhoog = half het bereik: Filter " + getal(vw) + " (verwacht " + getal(verwacht) + ")");

    draai(0.3);  // de timer/needsDisplay tekent opnieuw
    NSBitmapImageRep* rep2 = foto(v, map + "/paneel-mac-gekozen.png");
    boogPunt(2, -120, &x, &y);
    kleur(rep2, v, s, x, y, TAPE, "de gekozen knop (Filter): boog is tape");
    boogPunt(2, 0, &x, &y);
    kleur(rep2, v, s, x - 1.5, y, TAPE, "Filter op 4850 Hz: de boog loopt voorbij boven (0°)");

    // Shift: 10× fijner
    pt->knop(pl, 3, &kx, &ky);
    [v mouseDown:muisEvent(NSEventTypeLeftMouseDown, v, kx, ky, NSEventModifierFlagShift, 1)];
    [v mouseDragged:muisEvent(NSEventTypeLeftMouseDragged, v, kx, ky - 100, NSEventModifierFlagShift, 1)];
    [v mouseUp:muisEvent(NSEventTypeLeftMouseUp, v, kx, ky - 100, NSEventModifierFlagShift, 1)];
    uit.ev.clear();
    params->flush(pl, &in, &o);
    params->get_value(pl, los.id, &vw);
    const double fijn = los.default_value + 0.05 * (los.max_value - los.min_value);
    toets(std::fabs(vw - fijn) < 1e-9, "Shift + 100 pt = 5 % van het bereik: Loslaten " + getal(vw) + " (verwacht " + getal(fijn) + ")");

    // dubbelklik = standaard
    pt->knop(pl, 2, &kx, &ky);
    [v mouseDown:muisEvent(NSEventTypeLeftMouseDown, v, kx, ky, 0, 1)];
    [v mouseUp:muisEvent(NSEventTypeLeftMouseUp, v, kx, ky, 0, 1)];
    [v mouseDown:muisEvent(NSEventTypeLeftMouseDown, v, kx, ky, 0, 2)];
    [v mouseUp:muisEvent(NSEventTypeLeftMouseUp, v, kx, ky, 0, 2)];
    uit.ev.clear();
    params->flush(pl, &in, &o);
    params->get_value(pl, filter.id, &vw);
    toets(vw == filter.default_value && beschrijf(uit) == "B3 V3=900 E3", "dubbelklik op Filter: " + beschrijf(uit));

    // --- schalen ---------------------------------------------------------------------------------------------------
    [w setContentSize:NSMakeSize(800, 320)];
    toets(gui->set_size(pl, 800, 320), "gui.set_size(800, 320)");
    draai(0.3);
    toets(NSEqualSizes([v frame].size, NSMakeSize(800, 320)), "de view volgt: 800 × 320");
    NSBitmapImageRep* rep3 = foto(v, map + "/paneel-mac-800.png");
    const double s3 = std::fmin(800.0 / 560, 320.0 / 220);
    boogPunt(0, -120, &x, &y);
    kleur(rep3, v, s3, x, y, SPOOR, "800 × 320: Noot-boog bij −120° is spoor");
    kleur(rep3, v, 1, 800 - 3 * s3 - 1, 320 - 3 * s3 - 1, SCHADUW, "800 × 320: schaduw rechtsonder");

    // --- opruimen zoals clap-wrapper (AUv2 Cleanup roept destroy twee keer) -----------------------------------------
    gui->hide(pl);
    gui->destroy(pl);
    gui->destroy(pl);
    toets([[[w contentView] subviews] count] == 0, "na gui.destroy is de view weg");
    draai(0.2);
    toets(gui->create(pl, CLAP_WINDOW_API_COCOA, false), "gui.create opnieuw na destroy");
    gui->set_parent(pl, &cw);
    draai(0.2);
    gui->destroy(pl);
    pl->destroy(pl);
    [w close];
    entry->deinit();

    std::printf("\npb-scherm: %d toetsen, %d ok, %d fout\n", goed + fouten, goed, fouten);
    return fouten == 0 ? 0 : 1;
  }
}
