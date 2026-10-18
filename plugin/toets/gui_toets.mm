// gui_toets — de eigen toets van fase 3, deel 2 (macOS): het paneel door het gebouwde .clap, zoals een host.
//
//   gui_toets <plugin.clap> <map voor de schermafbeeldingen>
//
// Opent de gui-extensie (Cocoa) in een NSWindow, maakt schermafbeeldingen (PNG, de view zoals AppKit hem tekent) en
// controleert een paar pixels tegen de kleuren van design.md. Dan met muisevents (NSEvent naar de view) aan de knoppen:
// - slepen, Shift + slepen en dubbelklik veranderen de waarde van de plugin NIET zelf: get_value blijft gelijk tot de
//   host flusht of process aanroept (knoppen sturen via de host, valkuil 14);
// - de host krijgt request_flush, en uit flush (inactief) en process (actief) komen GESTURE_BEGIN, PARAM_VALUE en
//   GESTURE_END als uitvoerevents, met de waarde van design.md (200 px = het hele bereik, Shift 10× fijner);
// - een waarde van de host (automatisering) komt via de timer op het paneel;
// - schalen (adjust_size houdt 560 × 220 als kleinste maat), verbergen/tonen, en twee keer destroy (de AU doet dat).
// Uitvoer: per controle "ok" of "FOUT"; exitcode 0 alleen als alles ok is.
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

int fouten = 0, controles = 0;
void controle(bool goed, const std::string& wat) {
  ++controles;
  if (!goed) ++fouten;
  std::printf("%s  %s\n", goed ? "ok  " : "FOUT", wat.c_str());
  std::fflush(stdout);
}
std::string getal(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.6g", v);
  return b;
}

// --- host ---------------------------------------------------------------------------------------------------
int flushVerzoeken = 0, resizeVerzoeken = 0;
void hRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hRequestFlush(const clap_host_t*) { ++flushVerzoeken; }
const clap_host_params_t HOST_PARAMS = {hRescan, hClear, hRequestFlush};
void hResizeHints(const clap_host_t*) {}
bool hRequestResize(const clap_host_t*, uint32_t, uint32_t) { ++resizeVerzoeken; return true; }
bool hRequestShow(const clap_host_t*) { return true; }
bool hRequestHide(const clap_host_t*) { return true; }
void hClosed(const clap_host_t*, bool) {}
const clap_host_gui_t HOST_GUI = {hResizeHints, hRequestResize, hRequestShow, hRequestHide, hClosed};
const void* hExt(const clap_host_t*, const char* id) {
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &HOST_PARAMS;
  if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &HOST_GUI;
  return nullptr;
}
void hNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-guitoets", "Pluginbouwer", "", "1", hExt, hNiets, hNiets, hNiets};

// --- events -------------------------------------------------------------------------------------------------
struct Event {
  uint16_t type;
  clap_id id;
  double waarde;
};
std::vector<Event> uitvoer;
bool pushFn(const clap_output_events_t*, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return true;
  if (h->type == CLAP_EVENT_PARAM_VALUE) {
    auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
    uitvoer.push_back({h->type, e->param_id, e->value});
  } else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END) {
    auto* e = reinterpret_cast<const clap_event_param_gesture_t*>(h);
    uitvoer.push_back({h->type, e->param_id, 0});
  }
  return true;
}
const clap_output_events_t UIT = {nullptr, pushFn};
std::string beschrijf(const std::vector<Event>& v) {
  std::string s;
  for (auto& e : v) {
    s += e.type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? "B" : (e.type == CLAP_EVENT_PARAM_GESTURE_END ? "E" : "W");
    s += std::to_string(e.id);
    if (e.type == CLAP_EVENT_PARAM_VALUE) s += "=" + getal(e.waarde);
    s += " ";
  }
  return s;
}

// invoerevents: hooguit één parameterwaarde
clap_event_param_value_t invoerEvent;
bool heeftInvoer = false;
uint32_t inSize(const clap_input_events_t*) { return heeftInvoer ? 1 : 0; }
const clap_event_header_t* inGet(const clap_input_events_t*, uint32_t i) {
  return heeftInvoer && i == 0 ? &invoerEvent.header : nullptr;
}
const clap_input_events_t IN = {nullptr, inSize, inGet};

void zetInvoer(clap_id id, double v) {
  std::memset(&invoerEvent, 0, sizeof invoerEvent);
  invoerEvent.header = {sizeof invoerEvent, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  invoerEvent.param_id = id;
  invoerEvent.note_id = -1;
  invoerEvent.port_index = -1;
  invoerEvent.channel = -1;
  invoerEvent.key = -1;
  invoerEvent.value = v;
  heeftInvoer = true;
}

void draai(double sec) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:sec]]; }

// --- schermafbeelding en pixels -------------------------------------------------------------------------------
// De view tekent zichzelf in een eigen bitmap met de sRGB-kleurruimte (1 pixel per punt): de bytes zijn dan precies de
// kleuren van design.md, zonder de kleurconversie van het scherm. Ernaast de view zoals AppKit hem voor het venster
// cachet (op de schaal van het scherm).
struct Beeld {
  int b = 0, h = 0;
  std::vector<unsigned char> rgba;  // rij 0 = boven
};
Beeld foto(NSView* v, const std::string& pad) {
  Beeld beeld;
  beeld.b = (int)[v bounds].size.width;
  beeld.h = (int)[v bounds].size.height;
  beeld.rgba.assign((size_t)beeld.b * beeld.h * 4, 0);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef ctx = CGBitmapContextCreate(beeld.rgba.data(), beeld.b, beeld.h, 8, beeld.b * 4, cs,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  NSGraphicsContext* gc = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
  [v displayRectIgnoringOpacity:[v bounds] inContext:gc];
  CGContextFlush(ctx);
  CGImageRef img = CGBitmapContextCreateImage(ctx);
  NSBitmapImageRep* rep = [[[NSBitmapImageRep alloc] initWithCGImage:img] autorelease];
  NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  const bool ok = png && [png writeToFile:[NSString stringWithUTF8String:pad.c_str()] atomically:YES];
  CGImageRelease(img);
  CGContextRelease(ctx);
  CGColorSpaceRelease(cs);
  controle(ok, "schermafbeelding " + pad + " (" + std::to_string(beeld.b) + " × " + std::to_string(beeld.h) + " pixels, sRGB)");
  // zoals AppKit hem voor het scherm tekent
  NSBitmapImageRep* scherm = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
  [v cacheDisplayInRect:[v bounds] toBitmapImageRep:scherm];
  NSData* png2 = [scherm representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  std::string pad2 = pad.substr(0, pad.size() - 4) + "-scherm.png";
  [png2 writeToFile:[NSString stringWithUTF8String:pad2.c_str()] atomically:YES];
  return beeld;
}
const uint32_t PALET[] = {0x232629, 0xe7e5dd, 0xf2c200, 0x40cbda, 0x3a3e42, 0x0d0e0f};
const char PALET_TEKEN[] = {'.', '#', 'T', 's', 'g', ' '};
uint32_t kleurOp(const Beeld& beeld, double x, double y) {
  const int px = (int)std::floor(x), py = (int)std::floor(y);
  if (px < 0 || py < 0 || px >= beeld.b || py >= beeld.h) return 0xff00ff;
  const unsigned char* q = &beeld.rgba[((size_t)py * beeld.b + px) * 4];
  return ((uint32_t)q[0] << 16) | ((uint32_t)q[1] << 8) | q[2];
}
int afstand(uint32_t a, uint32_t b) {
  return std::abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255)) + std::abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255)) +
         std::abs((int)(a & 255) - (int)(b & 255));
}
// kleur van het punt (x, y) in view-punten, y vanaf boven; tegen een hexkleur uit design.md
void pixel(const Beeld& beeld, double x, double y, uint32_t hex, const std::string& wat) {
  const uint32_t k = kleurOp(beeld, x, y);
  char t[160];
  std::snprintf(t, sizeof t, "pixel (%.1f, %.1f) %s: #%06x (verwacht #%06x)", x, y, wat.c_str(), k, hex);
  controle(afstand(k, hex) <= 12, t);
}
// punt op de boog van knop i, op de hoek (graden vanaf boven, met de klok mee)
double boogX(int i, double graden) { return 62 + 76 * i + 22 * std::sin(graden * M_PI / 180); }
double boogY(double graden) { return 130 - 22 * std::cos(graden * M_PI / 180); }
// een kaartje van een stuk van het beeld in het log: elke pixel als de dichtstbijzijnde kleur van design.md
// (. paneel, # inkt, T tape, s spoor, g groef, spatie schaduw), 1 teken per pixel, elke tweede rij
void kaart(const Beeld& beeld, int x0, int y0, int b, int h, const std::string& wat) {
  std::printf("kaart %s (x %d..%d, y %d..%d, elke tweede rij):\n", wat.c_str(), x0, x0 + b - 1, y0, y0 + h - 1);
  for (int y = y0; y < y0 + h; y += 2) {
    std::string r = "  |";
    for (int x = x0; x < x0 + b; ++x) {
      const uint32_t k = kleurOp(beeld, x + 0.5, y + 0.5);
      int best = 0;
      for (int j = 1; j < 6; ++j) if (afstand(k, PALET[j]) < afstand(k, PALET[best])) best = j;
      r += PALET_TEKEN[best];
    }
    std::printf("%s|\n", r.c_str());
  }
}

// --- de muis --------------------------------------------------------------------------------------------------
struct Muis {
  NSWindow* venster;
  NSView* view;
  double hoogte;
  NSEvent* event(NSEventType type, double x, double y, int klikken, bool shift) {
    // view-punten (y vanaf boven, de view is flipped en zit op (0, 0) in een niet-flipped contentView)
    return [NSEvent mouseEventWithType:type
                              location:NSMakePoint(x, hoogte - y)
                         modifierFlags:(NSEventModifierFlags)(shift ? NSEventModifierFlagShift : 0)
                             timestamp:0
                          windowNumber:[venster windowNumber]
                               context:nil
                           eventNumber:0
                            clickCount:klikken
                              pressure:1.0];
  }
  void neer(double x, double y, int klikken = 1) { [view mouseDown:event(NSEventTypeLeftMouseDown, x, y, klikken, false)]; }
  void sleep(double x, double y, bool shift = false) {
    [view mouseDragged:event(NSEventTypeLeftMouseDragged, x, y, 1, shift)];
  }
  void op(double x, double y, int klikken = 1) { [view mouseUp:event(NSEventTypeLeftMouseUp, x, y, klikken, false)]; }
  // van (x, y) dy punten omhoog, in stapjes van 1, zoals een echte muis
  void sleepOmhoog(double x, double y, int dy, bool shift = false) {
    for (int k = 1; k <= dy; ++k) sleep(x, y - k, shift);
  }
};

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc < 3) { std::fprintf(stderr, "gebruik: gui_toets <plugin.clap> <map>\n"); return 2; }
    std::string pad = argv[1];
    const std::string map = argv[2];
    if (pad.size() > 5 && pad.compare(pad.size() - 5, 5, ".clap") == 0) {  // een .clap op macOS is een bundel
      std::string naam = pad.substr(pad.find_last_of('/') + 1);
      naam = naam.substr(0, naam.size() - 5);
      pad += "/Contents/MacOS/" + naam;
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    void* lib = dlopen(pad.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    if (!entry || !entry->init(argv[1])) { std::fprintf(stderr, "geen clap_entry\n"); return 2; }
    auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_descriptor_t* d = fab ? fab->get_plugin_descriptor(fab, 0) : nullptr;
    if (!d) { std::fprintf(stderr, "geen plugin\n"); return 2; }
    const clap_plugin_t* pl = fab->create_plugin(fab, &HOST, d->id);
    if (!pl || !pl->init(pl)) { std::fprintf(stderr, "init mislukt\n"); return 2; }
    auto* pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    auto* gui = static_cast<const clap_plugin_gui_t*>(pl->get_extension(pl, CLAP_EXT_GUI));
    controle(gui != nullptr, "de plugin heeft de gui-extensie");
    if (!gui || !pp) return 1;
    auto waarde = [&](clap_id id) { double v = NAN; pp->get_value(pl, id, &v); return v; };
    auto flush = [&] { uitvoer.clear(); heeftInvoer = false; pp->flush(pl, &IN, &UIT); return beschrijf(uitvoer); };

    controle(gui->is_api_supported(pl, CLAP_WINDOW_API_COCOA, false), "Cocoa (ingebed) wordt ondersteund");
    controle(!gui->is_api_supported(pl, CLAP_WINDOW_API_COCOA, true) && !gui->is_api_supported(pl, CLAP_WINDOW_API_X11, false),
             "zwevend en X11 niet");
    const char* api = nullptr;
    bool zwevend = true;
    controle(gui->get_preferred_api(pl, &api, &zwevend) && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA) && !zwevend,
             "voorkeur: Cocoa, ingebed");
    controle(gui->create(pl, CLAP_WINDOW_API_COCOA, false), "create(cocoa)");
    uint32_t b = 0, h = 0;
    controle(gui->get_size(pl, &b, &h) && b == 732 && h == 220, "beginmaat " + std::to_string(b) + " × " + std::to_string(h) +
                                                                     " (de mockup op ware grootte)");
    controle(gui->can_resize(pl), "schaalbaar");

    NSWindow* venster = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, b, h)
                                                    styleMask:NSWindowStyleMaskTitled
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
    [venster setTitle:@"PB Dub-echo (gui-toets)"];
    clap_window_t w{};
    w.api = CLAP_WINDOW_API_COCOA;
    w.cocoa = (void*)[venster contentView];
    controle(gui->set_parent(pl, &w), "set_parent(contentView)");
    controle(gui->show(pl), "show");
    [venster makeKeyAndOrderFront:nil];
    draai(0.3);
    NSView* v = [[[venster contentView] subviews] firstObject];
    controle(v != nil && [v bounds].size.width == b && [v bounds].size.height == h, "de view zit in het venster, " +
                                                                                       std::to_string(b) + " × " + std::to_string(h));
    if (!v) return 1;
    Muis muis{venster, v, (double)h};

    // 1. zoals de mockup: alle knoppen op hun standaard
    Beeld rep = foto(v, map + "/paneel.png");
    kaart(rep, 0, 0, 160, 50, "naamlabel");
    kaart(rep, 28, 88, 68, 94, "knop Tijd (375 ms)");
    pixel(rep, 10, 100, 0x232629, "paneel");
    pixel(rep, 26, 20, 0xf2c200, "naamlabel (tape)");
    pixel(rep, 728.5, 100, 0x0d0e0f, "harde schaduw rechts");
    pixel(rep, 300, 0.5, 0xe7e5dd, "rand (inkt)");
    pixel(rep, 670, 108, 0x3a3e42, "groef van Ruis (op 0)");
    pixel(rep, boogX(0, -100), boogY(-100), 0x40cbda, "boog van Tijd (spoor)");
    pixel(rep, 62, 130 + 8, 0x232629, "knop van Tijd (paneel)");

    // 2. inactief: slepen op Tijd, 100 punten omhoog = half het bereik (20..1200 ms)
    int f0 = flushVerzoeken;
    muis.neer(62, 130);
    muis.sleepOmhoog(62, 130, 100);
    muis.op(62, 30);
    controle(flushVerzoeken > f0, "slepen vraagt de host om een flush (" + std::to_string(flushVerzoeken - f0) + " keer)");
    controle(waarde(1) == 375, "vóór de flush staat Tijd nog op 375: het paneel zet de plugin niet zelf");
    std::string ev = flush();
    controle(ev == "B1 W1=965 E1 ", "flush geeft het gebaar aan de host: " + ev);
    controle(std::fabs(waarde(1) - 965) < 1e-9, "daarna Tijd = " + getal(waarde(1)) + " ms (verwacht 965)");
    draai(0.2);
    rep = foto(v, map + "/paneel-na-slepen.png");
    pixel(rep, boogX(0, -100), boogY(-100), 0xf2c200, "boog van Tijd na aanraken (gekozen knop: tape)");
    pixel(rep, boogX(0, 60), boogY(60), 0xf2c200, "boog van Tijd op 965 ms loopt door tot rechts (tape)");
    pixel(rep, boogX(1, -100), boogY(-100), 0x40cbda, "boog van Terug (spoor)");
    pixel(rep, boogX(1, 100), boogY(100), 0x3a3e42, "rechts op Terug alleen de groef");

    // 3. dubbelklik = standaard
    muis.neer(62, 130, 1);
    muis.op(62, 130, 1);
    muis.neer(62, 130, 2);
    muis.op(62, 130, 2);
    controle(std::fabs(waarde(1) - 965) < 1e-9, "vóór de flush verandert een dubbelklik niets aan de plugin");
    ev = flush();
    int begins = 0, eindes = 0;
    for (auto& e : uitvoer) { begins += e.type == CLAP_EVENT_PARAM_GESTURE_BEGIN; eindes += e.type == CLAP_EVENT_PARAM_GESTURE_END; }
    controle(begins == eindes && begins >= 1 && !uitvoer.empty() && uitvoer[uitvoer.size() - 2].type == CLAP_EVENT_PARAM_VALUE &&
                 uitvoer[uitvoer.size() - 2].waarde == 375,
             "dubbelklik: gebaar met de standaard (" + ev + ")");
    controle(waarde(1) == 375, "Tijd terug op 375");

    // 4. Shift + slepen op Mix: 100 punten = 5 % van het bereik
    muis.neer(594, 130);
    muis.sleepOmhoog(594, 130, 100, true);
    muis.op(594, 30);
    ev = flush();
    controle(ev == "B8 W8=0.425 E8 ", "Shift + slepen op Mix (10× fijner): " + ev);

    // 5. actief: process brengt het gebaar naar de host, ook midden in een gebaar
    controle(pl->activate(pl, 48000, 1, 512), "activate(48000, 1, 512)");
    pl->start_processing(pl);
    std::vector<float> l(512), r(512), ul(512), ur(512);
    auto blok = [&] {
      float* in[2] = {l.data(), r.data()};
      float* uit[2] = {ul.data(), ur.data()};
      clap_audio_buffer_t bi{in, nullptr, 2, 0, 0}, bu{uit, nullptr, 2, 0, 0};
      clap_process_t pr{};
      pr.steady_time = -1;
      pr.frames_count = 512;
      pr.audio_inputs = &bi; pr.audio_inputs_count = 1;
      pr.audio_outputs = &bu; pr.audio_outputs_count = 1;
      pr.in_events = &IN; pr.out_events = &UIT;
      uitvoer.clear();
      pl->process(pl, &pr);
      heeftInvoer = false;
      return beschrijf(uitvoer);
    };
    heeftInvoer = false;
    blok();
    muis.neer(138, 130);
    muis.sleepOmhoog(138, 130, 40);  // Terugkoppeling 0..1,25: 40 punten = +0,25
    controle(waarde(2) == 0.5, "actief, tijdens het slepen: de plugin staat nog op 0,5 tot process");
    ev = blok();
    controle(ev == "B2 W2=0.75 ", "process geeft BEGIN en de waarde: " + ev);
    controle(std::fabs(waarde(2) - 0.75) < 1e-12, "Terugkoppeling = " + getal(waarde(2)));
    muis.op(138, 90);
    ev = blok();
    controle(ev == "E2 ", "loslaten: process geeft EIND: " + ev);
    controle(blok().empty(), "daarna geen events meer");

    // 6. een waarde van de host (automatisering) komt op het paneel
    zetInvoer(9, 1.0);
    blok();
    controle(waarde(9) == 1.0, "host zet Ruis op 1");
    draai(0.3);
    rep = foto(v, map + "/paneel-host.png");
    pixel(rep, 670, 108, 0x40cbda, "boog van Ruis na automatisering (spoor)");
    pl->stop_processing(pl);
    pl->deactivate(pl);

    // 7. schalen, verbergen en tonen
    uint32_t ab = 100, ah = 100;
    controle(gui->adjust_size(pl, &ab, &ah) && ab == 560 && ah == 220, "adjust_size(100, 100) → " + std::to_string(ab) + " × " +
                                                                           std::to_string(ah) + " (kleinste maat 560 × 220)");
    clap_gui_resize_hints_t hints{};
    controle(gui->get_resize_hints(pl, &hints) && hints.can_resize_horizontally && hints.can_resize_vertically,
             "resize-hints: horizontaal en verticaal");
    [venster setContentSize:NSMakeSize(1098, 330)];
    controle(gui->set_size(pl, 1098, 330), "set_size(1098, 330)");
    draai(0.2);
    controle([v bounds].size.width == 1098 && [v bounds].size.height == 330, "de view is 1098 × 330");
    rep = foto(v, map + "/paneel-groot.png");
    pixel(rep, 26 * 1.5 + 1, 20 * 1.5 + 1, 0xf2c200, "naamlabel op 1,5×");
    controle(gui->get_size(pl, &b, &h) && b == 1098 && h == 330, "get_size volgt");
    controle(gui->hide(pl) && [v isHidden], "hide");
    controle(gui->show(pl) && ![v isHidden], "show");

    // 8. destroy (twee keer, zoals de AU van clap-wrapper), dan opnieuw maken
    gui->destroy(pl);
    controle([[[venster contentView] subviews] count] == 0, "destroy haalt de view uit het venster");
    gui->destroy(pl);
    controle(true, "tweede destroy zonder gevolgen");
    controle(gui->create(pl, CLAP_WINDOW_API_COCOA, false) && gui->set_parent(pl, &w), "opnieuw create + set_parent");
    draai(0.1);
    gui->destroy(pl);
    [venster close];
    pl->destroy(pl);
    entry->deinit();
  }
  std::printf("gui-toets: %d/%d controles ok, %d fout\n", controles - fouten, controles, fouten);
  return fouten == 0 ? 0 : 1;
}
