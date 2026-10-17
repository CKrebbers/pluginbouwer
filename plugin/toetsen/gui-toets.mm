// gui-toets (macOS) — het echte frontpaneel in een venster: muis-events, kleuren, maten, en schermafbeeldingen.
//
//   gui-toets <PB-Dub-echo.clap> <map-voor-png>
//
// Laadt de CLAP, maakt het paneel met de gui-extensie (api "cocoa") en hangt het in een NSWindow. Dan:
//   - slepen op Toon (100 punten omhoog = een halve slag), shift-slepen op Mix (10x fijner), dubbelklik op Toon;
//     vóór params.flush mag niets veranderd zijn; na flush moet de host GESTURE_BEGIN, PARAM_VALUE..., GESTURE_END
//     krijgen met de juiste waarde, en get_value moet volgen;
//   - de host zet Haperen (zoals automatisering): het paneel moet binnen een paar frames opnieuw tekenen;
//   - kleuren uit design.md op de afbeelding: paneel #232629, de gekozen knop met een tape-gele boog, de rest spoor;
//   - maten: 720 x 270, 560 x 220 en 1440 x 540; telkens een PNG in <map>;
//   - 20 keer openen en sluiten; na gui.destroy hangt er niets meer in het venster.
// Exitcode 0 = geslaagd; elke fout staat in de uitvoer met "FOUT".
#import <AppKit/AppKit.h>
#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ui/paneel.h"

namespace {

int g_fouten = 0;
void fout(const std::string& t) { std::printf("FOUT: %s\n", t.c_str()); ++g_fouten; }
[[noreturn]] void stop(const std::string& t) { std::printf("FOUT: %s\n", t.c_str()); std::fflush(stdout); std::exit(2); }
void ok(const std::string& t) { std::printf("ok: %s\n", t.c_str()); }
void eis(bool b, const std::string& t) { b ? ok(t) : fout(t); }

int g_flushVerzoeken = 0;
void hpRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpRequestFlush(const clap_host_t*) { ++g_flushVerzoeken; }
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpRequestFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  return !std::strcmp(id, CLAP_EXT_PARAMS) ? &HOST_PARAMS : nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "gui-toets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

struct Melding { uint16_t type; uint32_t tijd; clap_id id; double waarde; };
std::vector<Melding> g_uit;
bool uitPush(const clap_output_events_t*, const clap_event_header_t* h) {
  Melding m{h->type, h->time, 0, 0};
  if (h->type == CLAP_EVENT_PARAM_VALUE) {
    m.id = reinterpret_cast<const clap_event_param_value_t*>(h)->param_id;
    m.waarde = reinterpret_cast<const clap_event_param_value_t*>(h)->value;
  } else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END) {
    m.id = reinterpret_cast<const clap_event_param_gesture_t*>(h)->param_id;
  }
  g_uit.push_back(m);
  return true;
}
std::vector<clap_event_param_value_t> g_in;
uint32_t inSize(const clap_input_events_t*) { return (uint32_t)g_in.size(); }
const clap_event_header_t* inGet(const clap_input_events_t*, uint32_t i) { return &g_in[i].header; }

const clap_plugin_t* g_pl = nullptr;
const clap_plugin_params_t* g_pp = nullptr;
const clap_plugin_gui_t* g_gui = nullptr;

void flush() {
  g_uit.clear();
  clap_input_events_t in = {nullptr, inSize, inGet};
  clap_output_events_t uit = {nullptr, uitPush};
  g_pp->flush(g_pl, &in, &uit);
  g_in.clear();
}
double waarde(clap_id id) {
  double v = NAN;
  g_pp->get_value(g_pl, id, &v);
  return v;
}
void wacht(double sec) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:sec]]; }

// eist BEGIN, >=1 WAARDE, EINDE voor id; geeft de laatste waarde terug
double eisGebaar(clap_id id, const std::string& wat) {
  bool goed = g_uit.size() >= 3 && g_uit.front().type == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
              g_uit.back().type == CLAP_EVENT_PARAM_GESTURE_END;
  double laatste = NAN;
  for (size_t k = 0; goed && k < g_uit.size(); ++k) {
    if (g_uit[k].id != id || g_uit[k].tijd != 0) goed = false;
    if (k > 0 && k + 1 < g_uit.size()) {
      if (g_uit[k].type != CLAP_EVENT_PARAM_VALUE) goed = false;
      laatste = g_uit[k].waarde;
    }
  }
  if (goed) ok(wat + ": de host kreeg BEGIN, " + std::to_string(g_uit.size() - 2) + " x WAARDE, EINDE voor id " +
               std::to_string(id) + " (laatste " + std::to_string(laatste) + ")");
  else fout(wat + ": geen heel gebaar voor id " + std::to_string(id) + " bij de host (" + std::to_string(g_uit.size()) +
            " meldingen)");
  return laatste;
}

NSEvent* muis(NSEventType t, NSView* v, double x, double y, NSEventModifierFlags f, NSInteger klik) {
  const NSPoint p = [v convertPoint:NSMakePoint(x, y) toView:nil];
  return [NSEvent mouseEventWithType:t location:p modifierFlags:f timestamp:[[NSProcessInfo processInfo] systemUptime]
                        windowNumber:v.window.windowNumber context:nil eventNumber:0 clickCount:klik pressure:1];
}

// sleep van het midden van knop k, dy punten omhoog (in een paar stappen)
void sleepKnop(NSView* v, const pb_ui::Indeling& d, int k, double dy, bool fijn) {
  const NSEventModifierFlags f = fijn ? NSEventModifierFlagShift : 0;
  const double x = d.knopX[k], y = d.knopY[k];
  [v mouseDown:muis(NSEventTypeLeftMouseDown, v, x, y, f, 1)];
  for (int s = 1; s <= 5; ++s) [v mouseDragged:muis(NSEventTypeLeftMouseDragged, v, x, y - dy * s / 5, f, 1)];
  [v mouseUp:muis(NSEventTypeLeftMouseUp, v, x, y - dy, f, 1)];
}

NSBitmapImageRep* foto(NSView* v, const std::string& pad) {
  [v setNeedsDisplay:YES];
  [v displayIfNeeded];
  NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
  [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
  NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  if (![png writeToFile:[NSString stringWithUTF8String:pad.c_str()] atomically:YES]) fout("kan " + pad + " niet schrijven");
  else ok("schermafbeelding " + pad + " (" + std::to_string((long)rep.pixelsWide) + " x " +
          std::to_string((long)rep.pixelsHigh) + " pixels)");
  return rep;
}

bool lijkt(NSColor* c, pb_ui::Kleur k, double tol = 0.06) {
  NSColor* s = [c colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
  if (!s) return false;
  return std::fabs(s.redComponent - k.r) < tol && std::fabs(s.greenComponent - k.g) < tol &&
         std::fabs(s.blueComponent - k.b) < tol;
}
// telt pixels van kleur k in een vak (view-punten)
int tel(NSBitmapImageRep* rep, NSView* v, pb_ui::Vak r, pb_ui::Kleur k) {
  const double sx = rep.pixelsWide / v.bounds.size.width, sy = rep.pixelsHigh / v.bounds.size.height;
  int n = 0;
  for (long y = (long)(r.y * sy); y < (long)((r.y + r.h) * sy); y += 1)
    for (long x = (long)(r.x * sx); x < (long)((r.x + r.b) * sx); x += 1)
      if (lijkt([rep colorAtX:x y:y], k)) ++n;
  return n;
}
bool gelijkeFoto(NSBitmapImageRep* a, NSBitmapImageRep* b, NSView* v, pb_ui::Vak r) {
  const double sx = a.pixelsWide / v.bounds.size.width, sy = a.pixelsHigh / v.bounds.size.height;
  for (long y = (long)(r.y * sy); y < (long)((r.y + r.h) * sy); y += 2)
    for (long x = (long)(r.x * sx); x < (long)((r.x + r.b) * sx); x += 2) {
      NSColor* p = [[a colorAtX:x y:y] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
      NSColor* q = [[b colorAtX:x y:y] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
      if (std::fabs(p.redComponent - q.redComponent) > 0.02 || std::fabs(p.greenComponent - q.greenComponent) > 0.02 ||
          std::fabs(p.blueComponent - q.blueComponent) > 0.02)
        return false;
    }
  return true;
}

int toets(const std::string& pad, const std::string& map) {
  std::string naam = pad.substr(pad.find_last_of('/') + 1);
  if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
  void* h = dlopen((pad + "/Contents/MacOS/" + naam).c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) stop(std::string("dlopen: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
  if (!entry || !entry->init(pad.c_str())) stop("geen clap_entry");
  auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!fab) stop("geen factory");
  g_pl = fab->create_plugin(fab, &HOST, fab->get_plugin_descriptor(fab, 0)->id);
  if (!g_pl || !g_pl->init(g_pl)) stop("plugin start niet");
  g_pp = static_cast<const clap_plugin_params_t*>(g_pl->get_extension(g_pl, CLAP_EXT_PARAMS));
  g_gui = static_cast<const clap_plugin_gui_t*>(g_pl->get_extension(g_pl, CLAP_EXT_GUI));
  if (!g_pp || !g_gui) stop("geen params- of gui-extensie");
  eis(g_gui->is_api_supported(g_pl, CLAP_WINDOW_API_COCOA, false), "gui: cocoa ingebed");
  if (!g_gui->create(g_pl, CLAP_WINDOW_API_COCOA, false)) stop("gui.create mislukt");
  uint32_t b = 0, hg = 0;
  g_gui->get_size(g_pl, &b, &hg);
  eis(b == 720 && hg == 270, "gui: 720 x 270");

  NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, b, hg)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
  win.releasedWhenClosed = NO;
  win.title = @"PB Dub-echo (gui-toets)";
  clap_window_t w{};
  w.api = CLAP_WINDOW_API_COCOA;
  w.cocoa = (__bridge void*)win.contentView;
  if (!g_gui->set_parent(g_pl, &w)) stop("gui.set_parent mislukt");
  if (!g_gui->show(g_pl)) fout("gui.show mislukt");
  [win orderFrontRegardless];
  wacht(0.3);
  NSView* v = win.contentView.subviews.firstObject;
  if (!v) stop("het paneel hangt niet in het venster");
  eis(v.frame.size.width == 720 && v.frame.size.height == 270, "view is 720 x 270 punten");
  pb_ui::Indeling d = pb_ui::indeling(720, 270, 8);

  NSBitmapImageRep* voor = foto(v, map + "/paneel-720x270.png");
  eis(tel(voor, v, {2, 2, 10, 10}, pb_ui::PANEEL) > 0, "kleur: paneel #232629 in de hoek");
  eis(tel(voor, v, d.naamlabel, pb_ui::TAPE) > 100, "kleur: naamlabel tape #f2c200");
  eis(tel(voor, v, d.cel[3], pb_ui::SPOOR) > 30, "kleur: boog van een knop spoor #40cbda");
  eis(tel(voor, v, d.cel[3], pb_ui::TAPE) == 0, "kleur: nog geen gekozen knop (geen tape in de rij)");

  // slepen op Toon: 100 punten omhoog = een halve slag (200 punten = hele slag)
  {
    pb_ui::Knop toon = {3, "Toon", "", 300, 12000, 3000, true};
    const double verwacht = pb_ui::uitStand(toon, pb_ui::naarStand(toon, 3000) + 0.5);
    g_flushVerzoeken = 0;
    sleepKnop(v, d, 2, 100, false);
    eis(g_flushVerzoeken > 0, "slepen: de plugin vraagt de host om een flush (" + std::to_string(g_flushVerzoeken) + "x)");
    eis(waarde(3) == 3000, "slepen: vóór de flush is er niets veranderd (Toon 3000 Hz)");
    flush();
    const double laatste = eisGebaar(3, "slepen op Toon");
    eis(std::fabs(laatste - verwacht) < 1e-6 * verwacht && waarde(3) == laatste,
        "slepen: Toon " + std::to_string(laatste) + " Hz (verwacht " + std::to_string(verwacht) + "), get_value volgt");
  }
  // shift-slepen op Mix: 100 punten = 0,05 van de slag
  {
    sleepKnop(v, d, 7, 100, true);
    flush();
    const double laatste = eisGebaar(8, "shift-slepen op Mix");
    eis(std::fabs(laatste - (0.375 + 0.05)) < 1e-9, "shift-slepen: Mix " + std::to_string(laatste) +
        " (verwacht 0.425: 10x fijner)");
  }
  // dubbelklik op Toon: standaard
  {
    [v mouseDown:muis(NSEventTypeLeftMouseDown, v, d.knopX[2], d.knopY[2], 0, 1)];
    [v mouseUp:muis(NSEventTypeLeftMouseUp, v, d.knopX[2], d.knopY[2], 0, 1)];
    [v mouseDown:muis(NSEventTypeLeftMouseDown, v, d.knopX[2], d.knopY[2], 0, 2)];
    [v mouseUp:muis(NSEventTypeLeftMouseUp, v, d.knopX[2], d.knopY[2], 0, 2)];
    flush();
    bool standaard = !g_uit.empty() && g_uit[g_uit.size() - 2].type == CLAP_EVENT_PARAM_VALUE &&
                     g_uit[g_uit.size() - 2].waarde == 3000 && g_uit.back().type == CLAP_EVENT_PARAM_GESTURE_END;
    eis(standaard && waarde(3) == 3000, "dubbelklik: Toon terug op 3000 Hz (standaard), via de host");
  }
  wacht(0.2);
  NSBitmapImageRep* na = foto(v, map + "/paneel-na-slepen.png");
  eis(tel(na, v, d.cel[2], pb_ui::TAPE) > 30, "kleur: de gekozen knop (Toon, laatst aangeraakt) heeft een tape-gele boog");
  eis(tel(na, v, d.cel[7], pb_ui::TAPE) == 0 && tel(na, v, d.cel[7], pb_ui::SPOOR) > 30,
      "kleur: de andere knoppen houden een spoor-blauwe boog");

  // de host verandert Haperen (automatisering): het paneel volgt
  {
    g_in.push_back({});
    auto& e = g_in.back();
    e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    e.param_id = 7; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = 0.9;
    flush();
    eis(g_uit.empty(), "host-automatisering: de plugin stuurt niets terug naar de host");
    wacht(0.3);  // de timer van het paneel (30 Hz) ziet de nieuwe waarde
    NSBitmapImageRep* auto_ = foto(v, map + "/paneel-host-haperen-90.png");
    eis(!gelijkeFoto(na, auto_, v, d.cel[6]), "host-automatisering: de knop Haperen tekent opnieuw (90 %)");
    eis(gelijkeFoto(na, auto_, v, d.cel[0]), "host-automatisering: de andere knoppen blijven gelijk");
  }

  // maten
  uint32_t mb = 100, mh = 100;
  g_gui->adjust_size(g_pl, &mb, &mh);
  eis(mb == 560 && mh == 220, "adjust_size: minimaal 560 x 220");
  for (auto [nb, nh] : {std::pair<uint32_t, uint32_t>{560, 220}, {1440, 540}, {900, 600}}) {
    [win setContentSize:NSMakeSize(nb, nh)];
    eis(g_gui->set_size(g_pl, nb, nh), "set_size " + std::to_string(nb) + " x " + std::to_string(nh));
    wacht(0.1);
    eis(v.frame.size.width == nb && v.frame.size.height == nh, "view volgt naar " + std::to_string(nb) + " x " +
        std::to_string(nh));
    NSBitmapImageRep* r = foto(v, map + "/paneel-" + std::to_string(nb) + "x" + std::to_string(nh) + ".png");
    pb_ui::Indeling dn = pb_ui::indeling(nb, nh, 8);
    eis(tel(r, v, dn.naamlabel, pb_ui::TAPE) > 100 && tel(r, v, dn.cel[4], pb_ui::SPOOR) > 30,
        "schaal: naamlabel en bogen op hun plek bij " + std::to_string(nb) + " x " + std::to_string(nh));
    // slepen werkt ook geschaald (100 punten blijft een halve slag)
    sleepKnop(v, dn, 3, nb == 1440 ? -100 : 100, false);
    flush();
    eisGebaar(4, "slepen op Laag weg bij " + std::to_string(nb) + " x " + std::to_string(nh));
  }

  g_gui->destroy(g_pl);
  eis(win.contentView.subviews.count == 0, "gui.destroy: het paneel is uit het venster");
  for (int k = 0; k < 20; ++k) {
    if (!g_gui->create(g_pl, CLAP_WINDOW_API_COCOA, false) || !g_gui->set_parent(g_pl, &w) || !g_gui->show(g_pl))
      fout("openen nummer " + std::to_string(k));
    wacht(0.02);
    g_gui->destroy(g_pl);
  }
  eis(win.contentView.subviews.count == 0, "20 keer openen en sluiten; daarna hangt er niets meer in het venster");
  [win close];
  g_pl->destroy(g_pl);
  entry->deinit();
  return g_fouten == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::printf("gebruik: gui-toets <PB-Dub-echo.clap> <map-voor-png>\n");
    return 2;
  }
  int r;
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    r = toets(argv[1], argv[2]);
  }
  std::printf(r == 0 ? "GESLAAGD\n" : "MISLUKT (%d fouten)\n", g_fouten);
  return r;
}
