// pb-paneel — een CLAP-host die het frontpaneel opent zoals een DAW, er schermafbeeldingen van maakt en het met de
// muis bedient. Toetst fase 3 (PROMPTS.md) door de CLAP-API heen, niet door de code van het paneel:
//   - het paneel heeft de kleuren uit design.md, is minimaal 560 × 220 en schaalbaar;
//   - slepen stuurt de parameter via de host: gebaar begin, waarden, gebaar eind als uitvoerevents (in params.flush
//     als de plugin niet actief is, in process op de audiothread als hij speelt), en params.get volgt;
//   - Shift + slepen is tien keer fijner, dubbelklik zet de standaard, slepen stopt bij de grens;
//   - verandert de host een parameter, dan tekent het paneel hem opnieuw.
// Schermafbeeldingen (PNG) komen in <uitmap>; CI bewaart ze als artefact.
//
//   pb-paneel <plugin.clap> <uitmap>
//
// macOS: Cocoa (een NSWindow buiten beeld; de foto via cacheDisplayInRect, geen schermrechten nodig).
// Linux: X11 (onder xvfb-run, diepte 24); de host bewaakt de X-verbinding van de plugin (posix-fd-support) en tikt
// zijn timer (timer-support), zoals clap-wrapper dat voor VST3 doet.
// Met -fsanitize=realtime loopt process in een real-time context (zie pb-render.cpp); met -fsanitize=thread vangt TSan
// gedeelde toestand tussen paneel en audiothread zonder synchronisatie.
#include <clap/clap.h>
#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "png.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <poll.h>
#endif

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
void ok(bool goed, const std::string& t) {
  std::printf("%s %s\n", goed ? "ok  " : "FOUT", t.c_str());
  std::fflush(stdout);
  if (!goed) ++g_fouten;
}
[[noreturn]] void stop(const std::string& t) { std::fprintf(stderr, "pb-paneel: %s\n", t.c_str()); std::exit(2); }
std::string getal(double v) { char b[64]; std::snprintf(b, sizeof b, "%.10g", v); return b; }

// design.md
constexpr uint32_t PANEEL = 0x232629, INKT = 0xe7e5dd, TAPE = 0xf2c200, SPOOR = 0x40cbda;

// --- uitvoerevents van de plugin: vaste ring, geen allocatie (ook vanuit process op de audiothread) -------------
struct Ev { uint16_t soort; clap_id id; double waarde; };
struct Uit {
  static constexpr uint32_t MAX = 1 << 14;
  Ev ev[MAX];
  std::atomic<uint32_t> n{0};
  clap_output_events_t clap{this, push};
  static bool push(const clap_output_events_t* o, const clap_event_header_t* h) {
    auto* u = static_cast<Uit*>(o->ctx);
    const uint32_t k = u->n.load(std::memory_order_relaxed);
    if (k >= MAX || h->space_id != CLAP_CORE_EVENT_SPACE_ID) return k < MAX;
    Ev e{h->type, CLAP_INVALID_ID, 0};
    if (h->type == CLAP_EVENT_PARAM_VALUE) {
      auto* v = reinterpret_cast<const clap_event_param_value_t*>(h);
      e.id = v->param_id; e.waarde = v->value;
    } else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END) {
      e.id = reinterpret_cast<const clap_event_param_gesture_t*>(h)->param_id;
    } else {
      return true;
    }
    u->ev[k] = e;
    u->n.store(k + 1, std::memory_order_release);
    return true;
  }
};
Uit g_uit;

uint32_t leegIn(const clap_input_events_t*) { return 0; }
const clap_event_header_t* geenIn(const clap_input_events_t*, uint32_t) { return nullptr; }
const clap_input_events_t LEEG_IN = {nullptr, leegIn, geenIn};

// --- host --------------------------------------------------------------------------------------------------
std::atomic<bool> g_flushGevraagd{false};
std::thread::id g_main = std::this_thread::get_id();
std::atomic<bool> g_audioLoopt{false};

struct Timer { clap_id id; uint32_t ms; std::chrono::steady_clock::time_point volgende; };
std::vector<Timer> g_timers;
std::vector<int> g_fds;
const clap_plugin_t* g_plugin = nullptr;

void hRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hFlush(const clap_host_t*) { g_flushGevraagd = true; }
const clap_host_params_t H_PARAMS = {hRescan, hClear, hFlush};

bool hTimerReg(const clap_host_t*, uint32_t ms, clap_id* id) {
  *id = (clap_id)(g_timers.size() + 1);
  g_timers.push_back({*id, ms ? ms : 1, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms)});
  return true;
}
bool hTimerWeg(const clap_host_t*, clap_id id) {
  for (auto it = g_timers.begin(); it != g_timers.end(); ++it)
    if (it->id == id) { g_timers.erase(it); return true; }
  return false;
}
const clap_host_timer_support_t H_TIMER = {hTimerReg, hTimerWeg};

bool hFdReg(const clap_host_t*, int fd, clap_posix_fd_flags_t) { g_fds.push_back(fd); return true; }
bool hFdMod(const clap_host_t*, int, clap_posix_fd_flags_t) { return true; }
bool hFdWeg(const clap_host_t*, int fd) {
  for (auto it = g_fds.begin(); it != g_fds.end(); ++it)
    if (*it == fd) { g_fds.erase(it); return true; }
  return false;
}
const clap_host_posix_fd_support_t H_FD = {hFdReg, hFdMod, hFdWeg};

void hGuiHints(const clap_host_t*) {}
bool hGuiMaat(const clap_host_t*, uint32_t, uint32_t) { return false; }
bool hGuiNee(const clap_host_t*) { return false; }
void hGuiDicht(const clap_host_t*, bool) {}
const clap_host_gui_t H_GUI = {hGuiHints, hGuiMaat, hGuiNee, hGuiNee, hGuiDicht};

bool hIsMain(const clap_host_t*) { return std::this_thread::get_id() == g_main; }
bool hIsAudio(const clap_host_t*) { return std::this_thread::get_id() != g_main; }
const clap_host_thread_check_t H_THREADS = {hIsMain, hIsAudio};

const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &H_PARAMS;
  if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &H_TIMER;
  if (!std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &H_FD;
  if (!std::strcmp(id, CLAP_EXT_GUI)) return &H_GUI;
  if (!std::strcmp(id, CLAP_EXT_THREAD_CHECK)) return &H_THREADS;
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-paneel", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

// --- het venster van de host, per platform ------------------------------------------------------------------
struct Foto { int b = 0, h = 0; std::vector<uint32_t> px; };

#ifdef __APPLE__
NSWindow* g_win = nil;
NSView* inhoud() { return [g_win contentView]; }
NSView* pluginView() { NSArray* s = [inhoud() subviews]; return [s count] ? [s objectAtIndex:0] : nil; }

void schermOpen(uint32_t b, uint32_t h) {
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
  g_win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, b, h) styleMask:NSWindowStyleMaskBorderless
                                        backing:NSBackingStoreBuffered defer:NO];
}
void schermMaat(uint32_t b, uint32_t h) { [g_win setContentSize:NSMakeSize(b, h)]; }
clap_window_t schermOuder() { clap_window_t w; w.api = CLAP_WINDOW_API_COCOA; w.cocoa = inhoud(); return w; }
void schermDicht() { [g_win close]; g_win = nil; }
const char* schermApi() { return CLAP_WINDOW_API_COCOA; }

void pomp(double sec) {
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:sec]];
}

Foto foto() {
  NSView* v = pluginView();
  Foto f;
  if (!v) return f;
  NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
  [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
  f.b = (int)[rep pixelsWide];
  f.h = (int)[rep pixelsHigh];
  f.px.resize((size_t)f.b * f.h);
  NSBitmapImageRep* srgb = [rep bitmapImageRepByConvertingToColorSpace:[NSColorSpace sRGBColorSpace]
                                                       renderingIntent:NSColorRenderingIntentDefault];
  if (srgb) rep = srgb;
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.b; ++x) {
      NSUInteger p[5] = {0, 0, 0, 0, 0};
      [rep getPixel:p atX:x y:y];
      f.px[(size_t)y * f.b + x] = (uint32_t)((p[0] & 0xff) << 16 | (p[1] & 0xff) << 8 | (p[2] & 0xff));
    }
  return f;
}

// muis: soort 0 neer, 1 slepen, 2 op; x, y in punten vanaf linksboven van het paneel
void muis(int soort, double x, double y, bool shift, int kliks) {
  NSView* v = pluginView();
  if (!v) return;
  const NSPoint p = [v convertPoint:NSMakePoint(x, y) toView:nil];
  const NSEventType t = soort == 0 ? NSEventTypeLeftMouseDown : soort == 1 ? NSEventTypeLeftMouseDragged
                                                                          : NSEventTypeLeftMouseUp;
  NSEvent* e = [NSEvent mouseEventWithType:t location:p modifierFlags:shift ? NSEventModifierFlagShift : 0
                                 timestamp:0 windowNumber:[g_win windowNumber] context:nil eventNumber:0
                                clickCount:kliks pressure:soort == 2 ? 0 : 1];
  if (soort == 0) [v mouseDown:e]; else if (soort == 1) [v mouseDragged:e]; else [v mouseUp:e];
  pomp(0.01);
}
#else
Display* g_dpy = nullptr;
Window g_win = 0;
unsigned long g_tijd = 1000;  // X-tijd van de nagemaakte muis (ms)

void schermOpen(uint32_t b, uint32_t h) {
  g_dpy = XOpenDisplay(nullptr);
  if (!g_dpy) stop("geen X-display (draai onder xvfb-run)");
  g_win = XCreateSimpleWindow(g_dpy, DefaultRootWindow(g_dpy), 0, 0, b, h, 0, 0, 0);
  XMapWindow(g_dpy, g_win);
  XSync(g_dpy, False);
}
void schermMaat(uint32_t b, uint32_t h) { XResizeWindow(g_dpy, g_win, b, h); XSync(g_dpy, False); }
clap_window_t schermOuder() { clap_window_t w; w.api = CLAP_WINDOW_API_X11; w.x11 = g_win; return w; }
void schermDicht() { XDestroyWindow(g_dpy, g_win); XCloseDisplay(g_dpy); g_dpy = nullptr; }
const char* schermApi() { return CLAP_WINDOW_API_X11; }

Window pluginWin() {
  Window wortel, ouder, *kind = nullptr;
  unsigned n = 0;
  if (!XQueryTree(g_dpy, g_win, &wortel, &ouder, &kind, &n) || n == 0) return 0;
  Window w = kind[0];
  XFree(kind);
  return w;
}

// de event-lus van een host: fd's van de plugin en zijn timers
void pomp(double sec) {
  const auto eind = std::chrono::steady_clock::now() + std::chrono::duration<double>(sec);
  auto* fdExt = static_cast<const clap_plugin_posix_fd_support_t*>(g_plugin->get_extension(g_plugin, CLAP_EXT_POSIX_FD_SUPPORT));
  auto* tExt = static_cast<const clap_plugin_timer_support_t*>(g_plugin->get_extension(g_plugin, CLAP_EXT_TIMER_SUPPORT));
  for (;;) {
    while (XPending(g_dpy)) { XEvent e; XNextEvent(g_dpy, &e); }
    const auto nu = std::chrono::steady_clock::now();
    for (size_t i = 0; i < g_timers.size(); ++i)
      if (g_timers[i].volgende <= nu) {
        g_timers[i].volgende = nu + std::chrono::milliseconds(g_timers[i].ms);
        if (tExt) tExt->on_timer(g_plugin, g_timers[i].id);
      }
    if (nu >= eind) break;
    std::vector<pollfd> p;
    for (int fd : g_fds) p.push_back({fd, POLLIN, 0});
    if (poll(p.data(), p.size(), 5) > 0)
      for (auto& q : p)
        if ((q.revents & POLLIN) && fdExt) fdExt->on_fd(g_plugin, q.fd, CLAP_POSIX_FD_READ);
  }
}

Foto foto() {
  Foto f;
  Window w = pluginWin();
  XWindowAttributes a;
  if (!w || !XGetWindowAttributes(g_dpy, w, &a)) return f;
  XImage* im = XGetImage(g_dpy, w, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
  if (!im) return f;
  f.b = a.width; f.h = a.height;
  f.px.resize((size_t)f.b * f.h);
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.b; ++x) f.px[(size_t)y * f.b + x] = (uint32_t)XGetPixel(im, x, y) & 0xffffff;
  XDestroyImage(im);
  return f;
}

void muis(int soort, double x, double y, bool shift, int kliks) {
  Window w = pluginWin();
  if (!w) return;
  (void)kliks;  // X11 kent geen klik-telling: twee keer neer binnen 400 ms is dubbel (zie dubbelklik hieronder)
  XEvent e;
  std::memset(&e, 0, sizeof e);
  const unsigned toets = shift ? ShiftMask : 0;
  g_tijd += 20;
  if (soort == 1) {
    e.type = MotionNotify;
    e.xmotion.window = w; e.xmotion.x = (int)std::lround(x); e.xmotion.y = (int)std::lround(y);
    e.xmotion.state = toets | Button1Mask; e.xmotion.time = g_tijd;
    XSendEvent(g_dpy, w, False, Button1MotionMask, &e);
  } else {
    e.type = soort == 0 ? ButtonPress : ButtonRelease;
    e.xbutton.window = w; e.xbutton.x = (int)std::lround(x); e.xbutton.y = (int)std::lround(y);
    e.xbutton.button = Button1; e.xbutton.state = toets | (soort == 2 ? Button1Mask : 0); e.xbutton.time = g_tijd;
    XSendEvent(g_dpy, w, False, soort == 0 ? ButtonPressMask : ButtonReleaseMask, &e);
  }
  XSync(g_dpy, False);
  pomp(0.01);
}
#endif

// --- hulp: de plugin -----------------------------------------------------------------------------------------
const clap_plugin_params_t* g_params = nullptr;
const clap_plugin_gui_t* g_gui = nullptr;

double paramGet(clap_id id) { double v = NAN; g_params->get_value(g_plugin, id, &v); return v; }
double standaardVan(clap_id id) {
  for (uint32_t k = 0; k < g_params->count(g_plugin); ++k) {
    clap_param_info_t i;
    if (g_params->get_info(g_plugin, k, &i) && i.id == id) return i.default_value;
  }
  return NAN;
}
double maxVan(clap_id id) {
  for (uint32_t k = 0; k < g_params->count(g_plugin); ++k) {
    clap_param_info_t i;
    if (g_params->get_info(g_plugin, k, &i) && i.id == id) return i.max_value;
  }
  return NAN;
}

// zoals een host die niet speelt: vraagt de plugin om flush, dan flush op de mainthread (alleen als niet actief)
void flushAlsGevraagd() {
  if (g_flushGevraagd.exchange(false)) g_params->flush(g_plugin, &LEEG_IN, &g_uit.clap);
}

// de events sinds `vanaf` moeten één gebaar zijn: BEGIN(id), ≥1 WAARDE(id), EIND(id); geeft de laatste waarde
bool gebaar(uint32_t vanaf, clap_id id, double* laatste, const std::string& wat) {
  const uint32_t n = g_uit.n.load(std::memory_order_acquire);
  std::string reeks;
  int waarden = 0;
  bool goed = n >= vanaf + 3 && g_uit.ev[vanaf].soort == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
              g_uit.ev[n - 1].soort == CLAP_EVENT_PARAM_GESTURE_END;
  for (uint32_t k = vanaf; k < n; ++k) {
    const Ev& e = g_uit.ev[k];
    goed &= e.id == id;
    if (e.soort == CLAP_EVENT_PARAM_VALUE) { ++waarden; *laatste = e.waarde; }
    else if (k != vanaf && k != n - 1) goed = false;
  }
  goed &= waarden >= 1;
  reeks = std::to_string(n - vanaf) + " events: begin, " + std::to_string(waarden) + " waarde(n), eind; id " +
          std::to_string(id);
  ok(goed, wat + ": " + reeks);
  return goed;
}

void sleepKnop(double x, double y0, double y1, bool shift, int stappen = 5) {
  muis(0, x, y0, shift, 1);
  for (int s = 1; s <= stappen; ++s) muis(1, x, y0 + (y1 - y0) * s / stappen, shift, 0);
  muis(2, x, y1, shift, 0);
}

int telKleur(const Foto& f, uint32_t k, int marge) {
  int n = 0;
  for (uint32_t p : f.px) {
    const int dr = (int)((p >> 16) & 0xff) - (int)((k >> 16) & 0xff), dg = (int)((p >> 8) & 0xff) - (int)((k >> 8) & 0xff),
              db = (int)(p & 0xff) - (int)(k & 0xff);
    n += std::abs(dr) <= marge && std::abs(dg) <= marge && std::abs(db) <= marge;
  }
  return n;
}

void bewaar(const Foto& f, const std::string& map, const char* naam) {
  const std::string pad = map + "/" + naam;
  ok(f.b > 0 && png::schrijf(pad.c_str(), f.px.data(), f.b, f.h), "schermafbeelding " + pad + " (" +
     std::to_string(f.b) + " × " + std::to_string(f.h) + " px)");
}

// --- de audiothread: process in blokken van 512, zoals een host die speelt ---------------------------------------
// noinline: anders vouwt -O2 hem in de lus vóór RTSan instrumenteert (zie pb-render.cpp)
__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT {
  return pl->process(pl, pr);
}

std::atomic<bool> g_audioStop{false};
void audioLus() {
  std::vector<float> l(512, 0.f), r(512, 0.f), ol(512), orr(512);
  float* in[2] = {l.data(), r.data()};
  float* uit[2] = {ol.data(), orr.data()};
  clap_audio_buffer_t bi{}, bu{};
  bi.data32 = in; bi.channel_count = 2;
  bu.data32 = uit; bu.channel_count = 2;
  clap_process_t pr{};
  pr.steady_time = 0;
  pr.frames_count = 512;
  pr.audio_inputs = &bi; pr.audio_outputs = &bu;
  pr.audio_inputs_count = pr.audio_outputs_count = 1;
  pr.in_events = &LEEG_IN;
  pr.out_events = &g_uit.clap;
  g_plugin->start_processing(g_plugin);
  g_audioLoopt = true;
  while (!g_audioStop.load()) {
    doeProcess(g_plugin, &pr);
    pr.steady_time += 512;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  g_plugin->stop_processing(g_plugin);
  g_audioLoopt = false;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) stop("gebruik: pb-paneel <plugin.clap> <uitmap>");
  const std::string map = argv[2];

  const std::string pad = argv[1];
  std::string bin = pad;
#ifdef __APPLE__
  {  // een .clap is op macOS een bundle: de code staat in Contents/MacOS/<naam zonder .clap>
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
    bin = pad + "/Contents/MacOS/" + naam;
  }
#endif
  void* lib = dlopen(bin.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!lib) stop(std::string("kan niet laden: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!entry || !entry->init(pad.c_str())) stop("geen clap_entry");
  auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  const clap_plugin_t* pl = fab->create_plugin(fab, &HOST, fab->get_plugin_descriptor(fab, 0)->id);
  if (!pl || !pl->init(pl)) stop("plugin start niet");
  g_plugin = pl;
  g_params = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
  g_gui = static_cast<const clap_plugin_gui_t*>(pl->get_extension(pl, CLAP_EXT_GUI));
  if (!g_params) stop("geen params-extensie");

  // --- 1. openen, maat, kleuren (niet actief: flush op de mainthread) ---
  ok(g_gui != nullptr, "gui-extensie aanwezig");
  if (!g_gui) return 1;
  const char* api = nullptr;
  bool zwevend = true;
  ok(g_gui->is_api_supported(pl, schermApi(), false) && g_gui->get_preferred_api(pl, &api, &zwevend) &&
         !std::strcmp(api, schermApi()) && !zwevend, std::string("venster-API ") + schermApi() + ", ingebed");
  ok(!g_gui->is_api_supported(pl, schermApi(), true), "zwevend venster niet aangeboden");
  ok(g_gui->create(pl, schermApi(), false), "gui.create");
#ifndef __APPLE__
  ok(g_gui->set_scale(pl, 1.0), "gui.set_scale(1)");
#endif
  uint32_t b = 0, h = 0;
  // eerst aanroepen, dan de tekst maken: de volgorde van functie-argumenten ligt in C++ niet vast
  bool gelukt = g_gui->get_size(pl, &b, &h);
  ok(gelukt && b >= 560 && h >= 220, "gui.get_size " + std::to_string(b) + " × " +
     std::to_string(h) + " (minimaal 560 × 220)");
  ok(g_gui->can_resize(pl), "gui.can_resize");
  uint32_t kb = 100, kh = 100;
  gelukt = g_gui->adjust_size(pl, &kb, &kh);
  ok(gelukt && kb == 560 && kh == 220, "gui.adjust_size(100, 100) → " +
     std::to_string(kb) + " × " + std::to_string(kh));
  schermOpen(b, h);
  clap_window_t ouder = schermOuder();
  ok(g_gui->set_parent(pl, &ouder), "gui.set_parent");
  ok(g_gui->show(pl), "gui.show");
  pomp(0.3);

  const Foto f1 = foto();
  bewaar(f1, map, "paneel-standaard.png");
  const int n = (int)f1.px.size();
  ok(n > 0 && telKleur({1, 1, {f1.px[(size_t)(f1.b * 3 + 3)]}}, PANEEL, 2) == 1, "achtergrond is paneel #232629");
  ok(telKleur(f1, PANEEL, 2) > n / 2, "paneel #232629 is de meeste pixels (" + std::to_string(telKleur(f1, PANEEL, 2)) + ")");
  ok(telKleur(f1, TAPE, 2) > n / 200, "tape #f2c200 (naamlabel): " + std::to_string(telKleur(f1, TAPE, 2)) + " px");
  ok(telKleur(f1, SPOOR, 2) > n / 400, "spoor #40cbda (bogen): " + std::to_string(telKleur(f1, SPOOR, 2)) + " px");
  ok(telKleur(f1, INKT, 2) > n / 100, "inkt #e7e5dd (tekst, randen): " + std::to_string(telKleur(f1, INKT, 2)) + " px");

  // knop i: de rij van 8 vult de breedte; de knoppen staan in het midden van de hoogte
  auto knopX = [&](int i) { return (i + 0.5) * b / 8.0; };
  const double midY = h / 2.0;

  // --- 2. slepen: gebaar via de host, params.get volgt ---
  const double tijd0 = paramGet(1);
  ok(tijd0 == standaardVan(1), "Tijd begint op de standaard " + getal(tijd0));
  uint32_t vanaf = g_uit.n.load();
  sleepKnop(knopX(0), midY, midY - 50, false);
  ok(g_flushGevraagd.load(), "het paneel vraagt de host om flush (host_params.request_flush)");
  ok(paramGet(1) == tijd0, "vóór de flush is params.get nog onveranderd (het paneel zet de DSP niet zelf)");
  flushAlsGevraagd();
  double grof = NAN;
  gebaar(vanaf, 1, &grof, "slepen Tijd 50 punten omhoog");
  ok(grof > tijd0 && paramGet(1) == grof, "Tijd " + getal(tijd0) + " → " + getal(grof) + "; params.get = " +
     getal(paramGet(1)));
  pomp(0.1);
  const Foto f2 = foto();
  bewaar(f2, map, "paneel-tijd-gesleept.png");
  ok(f2.px != f1.px, "het paneel toont de nieuwe waarde");

  // --- 3. Shift + slepen = tien keer fijner ---
  vanaf = g_uit.n.load();
  sleepKnop(knopX(0), midY, midY + 50, true);
  flushAlsGevraagd();
  double fijn = NAN;
  gebaar(vanaf, 1, &fijn, "Shift + slepen Tijd 50 punten omlaag");
  const double verhouding = (grof - tijd0) / (grof - fijn);
  ok(std::fabs(verhouding - 10) < 1e-6, "Shift is 10× fijner: " + getal(grof - tijd0) + " tegen " +
     getal(grof - fijn) + " (verhouding " + getal(verhouding) + ")");

  // --- 4. dubbelklik = standaard ---
  vanaf = g_uit.n.load();
  muis(0, knopX(0), midY, false, 1);
  muis(2, knopX(0), midY, false, 1);
  flushAlsGevraagd();
  vanaf = g_uit.n.load();  // de eerste klik is een (leeg) gebaar zonder waarde
  muis(0, knopX(0), midY, false, 2);
  muis(2, knopX(0), midY, false, 2);
  flushAlsGevraagd();
  double dubbel = NAN;
  gebaar(vanaf, 1, &dubbel, "dubbelklik op Tijd");
  ok(dubbel == standaardVan(1) && paramGet(1) == standaardVan(1), "dubbelklik zet de standaard " + getal(dubbel));

  // --- 5. slepen stopt bij de grens ---
  vanaf = g_uit.n.load();
  sleepKnop(knopX(7), midY, midY - 3000, false, 20);
  flushAlsGevraagd();
  double grens = NAN;
  gebaar(vanaf, 8, &grens, "Mix ver omhoog slepen");
  ok(grens == maxVan(8) && paramGet(8) == maxVan(8), "Mix stopt op het maximum " + getal(grens));

  // --- 6. de host verandert een parameter: het paneel volgt ---
  pomp(0.1);
  const Foto f3 = foto();
  clap_event_param_value_t e{};
  e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.param_id = 3; e.note_id = e.port_index = e.channel = e.key = -1; e.value = 12000;
  struct EenIn { const clap_event_header_t* h; } een{&e.header};
  clap_input_events_t in = {&een, [](const clap_input_events_t*) -> uint32_t { return 1; },
                            [](const clap_input_events_t* l, uint32_t) { return static_cast<const EenIn*>(l->ctx)->h; }};
  g_params->flush(pl, &in, &g_uit.clap);
  ok(paramGet(3) == 12000, "host zet Toon op 12000 Hz (flush)");
  pomp(0.2);
  const Foto f4 = foto();
  bewaar(f4, map, "paneel-host-zet-toon.png");
  ok(f4.px != f3.px, "het paneel tekent de waarde van de host opnieuw (timer)");

  // --- 7. schalen ---
  uint32_t gb = 900, gh = 330;
  ok(g_gui->adjust_size(pl, &gb, &gh) && gb == 900 && gh == 330, "gui.adjust_size(900, 330) laat het zo");
  schermMaat(gb, gh);
  ok(g_gui->set_size(pl, gb, gh), "gui.set_size(900, 330)");
  gelukt = g_gui->get_size(pl, &b, &h);
  ok(gelukt && b == 900 && h == 330, "gui.get_size → " + std::to_string(b) + " × " + std::to_string(h));
  pomp(0.2);
  const Foto f5 = foto();
  bewaar(f5, map, "paneel-groot.png");
  ok(f5.b >= 900 && f5.h >= 330, "het paneel vult de nieuwe maat");
  g_gui->destroy(pl);
  ok(true, "gui.destroy");

  // --- 8. spelend: het gebaar gaat door process op de audiothread ---
  ok(pl->activate(pl, 48000, 1, 512), "activate (48 kHz, blok 512)");
  std::thread audio(audioLus);
  while (!g_audioLoopt.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ok(g_gui->create(pl, schermApi(), false), "gui.create terwijl de plugin speelt");
#ifndef __APPLE__
  g_gui->set_scale(pl, 1.0);
#endif
  g_gui->set_size(pl, 640, 250);
  g_gui->get_size(pl, &b, &h);
  schermMaat(b, h);
  ouder = schermOuder();
  ok(g_gui->set_parent(pl, &ouder) && g_gui->show(pl), "gui.set_parent + show");
  pomp(0.2);
  const double toon0 = paramGet(3);
  vanaf = g_uit.n.load();
  g_flushGevraagd = false;
  sleepKnop(knopX(2), midY, midY + 40, false);
  for (int k = 0; k < 400 && g_uit.n.load() - vanaf < 3; ++k) pomp(0.01);  // wacht op process
  for (int k = 0; k < 50; ++k) pomp(0.01);                                  // en op de rest van het gebaar
  double toon = NAN;
  gebaar(vanaf, 3, &toon, "spelend: slepen Toon 40 punten omlaag");
  ok(toon < toon0 && paramGet(3) == toon, "Toon " + getal(toon0) + " → " + getal(toon) + " via process; params.get = " +
     getal(paramGet(3)));
  pomp(0.1);
  bewaar(foto(), map, "paneel-spelend.png");
  g_gui->destroy(pl);

  // --- 9. vaak open en dicht terwijl hij speelt (zoals pluginval) ---
  for (int k = 0; k < 5; ++k) {
    g_gui->create(pl, schermApi(), false);
    ouder = schermOuder();
    g_gui->set_parent(pl, &ouder);
    g_gui->show(pl);
    pomp(0.02);
    g_gui->hide(pl);
    g_gui->destroy(pl);
  }
  ok(true, "5× open en dicht terwijl hij speelt");
  g_audioStop = true;
  audio.join();
  pl->deactivate(pl);
  schermDicht();
  pl->destroy(pl);
  entry->deinit();

  if (g_fouten) { std::printf("pb-paneel: %d fout(en)\n", g_fouten); return 1; }
  std::printf("pb-paneel: geslaagd\n");
  return 0;
}
