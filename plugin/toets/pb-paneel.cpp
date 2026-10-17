// pb-paneel — de eigen toets van fase 3: een kleine CLAP-host die het frontpaneel echt opent (Cocoa op macOS, X11
// op Linux), er met de muis aan draait en schermafbeeldingen maakt (PNG).
//
//   pb-paneel <plugin.clap> <uitmap> [--audio]
//
// Wat hij toetst:
//  - gui-extensie: cocoa/x11, niet zwevend; standaardgrootte, minimum 560 × 220 (adjust_size), schaalbaar (set_size);
//  - slepen aan Tijd: de plugin meldt de host gebaar-begin, waarde(n), gebaar-eind, en de waarde klopt met de knop
//    (200 punten = het hele bereik, Tijd logaritmisch); Shift + slepen aan Mix is 10× fijner; dubbelklik = standaard;
//  - de waarden komen alleen via de host binnen: zonder --audio vraagt de plugin om een flush (request_flush) en de
//    waarden staan pas in get_value na die flush; met --audio draait process op een eigen audiothread (zoals in een
//    DAW, met RTSan als die aan staat) en komen de events uit process;
//  - een waarde die de host zet (Toon = 12 kHz) verschijnt op het paneel; de andere knoppen blijven gelijk.
// Schermafbeeldingen: <uitmap>/paneel-standaard.png, paneel-bediend.png, paneel-groot.png.
#include <clap/clap.h>

#include <dlfcn.h>
#include <unistd.h>

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

[[noreturn]] void fout(const std::string& t) {
  std::fprintf(stderr, "pb-paneel: FOUT: %s\n", t.c_str());
  std::exit(1);
}
void eis(bool ok, const std::string& t) {
  if (!ok) fout(t);
  std::printf("  ok: %s\n", t.c_str());
}

// --- PNG (ongecomprimeerd: deflate met 'stored' blokken) -----------------------------------------------------
uint32_t crc(const unsigned char* p, size_t n, uint32_t c = 0xffffffffu) {
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return c;
}
void schrijfPng(const std::string& pad, const std::vector<uint32_t>& pix, int b, int h) {
  std::vector<unsigned char> ruw;
  for (int y = 0; y < h; ++y) {
    ruw.push_back(0);
    for (int x = 0; x < b; ++x) {
      const uint32_t p = pix[(size_t)y * b + x];
      ruw.push_back((p >> 16) & 0xff); ruw.push_back((p >> 8) & 0xff); ruw.push_back(p & 0xff);
    }
  }
  std::vector<unsigned char> z = {0x78, 0x01};
  uint32_t a = 1, s2 = 0;
  for (unsigned char c : ruw) { a = (a + c) % 65521; s2 = (s2 + a) % 65521; }
  for (size_t o = 0; o < ruw.size() || o == 0; o += 65535) {
    const size_t n = std::min<size_t>(65535, ruw.size() - o);
    z.push_back(o + n >= ruw.size() ? 1 : 0);
    z.push_back(n & 0xff); z.push_back(n >> 8); z.push_back(~n & 0xff); z.push_back((~n >> 8) & 0xff);
    z.insert(z.end(), ruw.begin() + (long)o, ruw.begin() + (long)(o + n));
    if (n == 0) break;
  }
  const uint32_t adler = (s2 << 16) | a;
  for (int k = 3; k >= 0; --k) z.push_back((adler >> (8 * k)) & 0xff);
  FILE* f = std::fopen(pad.c_str(), "wb");
  if (!f) fout("kan niet schrijven: " + pad);
  auto u32 = [&](uint32_t v) { unsigned char q[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v}; std::fwrite(q, 1, 4, f); };
  auto stuk = [&](const char* type, const std::vector<unsigned char>& d) {
    u32((uint32_t)d.size());
    std::vector<unsigned char> t(type, type + 4);
    t.insert(t.end(), d.begin(), d.end());
    std::fwrite(t.data(), 1, t.size(), f);
    u32(crc(t.data(), t.size()) ^ 0xffffffffu);
  };
  std::fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  std::vector<unsigned char> ihdr = {(unsigned char)(b >> 24), (unsigned char)(b >> 16), (unsigned char)(b >> 8), (unsigned char)b,
                                     (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h,
                                     8, 2, 0, 0, 0};
  stuk("IHDR", ihdr);
  stuk("IDAT", z);
  stuk("IEND", {});
  std::fclose(f);
}

// --- de host --------------------------------------------------------------------------------------------------
std::atomic<bool> flushGevraagd{false};
std::atomic<int> rescans{0};
struct Timer { clap_id id; uint32_t ms; std::chrono::steady_clock::time_point volgende; };
std::vector<Timer> timers;
std::vector<int> fds;

void hRescan(const clap_host_t*, clap_param_rescan_flags) { rescans++; }
void hClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hFlush(const clap_host_t*) { flushGevraagd = true; }
const clap_host_params_t HOST_PARAMS = {hRescan, hClear, hFlush};
bool hTimer(const clap_host_t*, uint32_t ms, clap_id* id) {
  *id = (clap_id)(timers.size() + 1);
  timers.push_back({*id, ms, std::chrono::steady_clock::now()});
  return true;
}
bool hTimerWeg(const clap_host_t*, clap_id id) {
  for (auto& t : timers) if (t.id == id) t.ms = 0;
  return true;
}
const clap_host_timer_support_t HOST_TIMER = {hTimer, hTimerWeg};
bool hFd(const clap_host_t*, int fd, clap_posix_fd_flags_t) { fds.push_back(fd); return true; }
bool hFdAnders(const clap_host_t*, int, clap_posix_fd_flags_t) { return true; }
bool hFdWeg(const clap_host_t*, int fd) {
  for (auto& f : fds) if (f == fd) f = -1;
  return true;
}
const clap_host_posix_fd_support_t HOST_FD = {hFd, hFdAnders, hFdWeg};

const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
#ifndef __APPLE__
  if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &HOST_TIMER;
  if (!std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &HOST_FD;
#endif
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-paneel", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

// events die de plugin aan de host meldt (uit flush of uit process): vooraf gereserveerd, geen allocatie
struct Gemeld { uint16_t type; clap_id id; double waarde; };
constexpr int MAX_GEMELD = 65536;
Gemeld gemeld[MAX_GEMELD];
std::atomic<int> nGemeld{0};
bool uitPush(const clap_output_events_t*, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return true;
  const int n = nGemeld.load(std::memory_order_relaxed);
  if (n >= MAX_GEMELD) return false;
  if (h->type == CLAP_EVENT_PARAM_VALUE) {
    auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
    gemeld[n] = {h->type, e->param_id, e->value};
  } else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END) {
    gemeld[n] = {h->type, reinterpret_cast<const clap_event_param_gesture_t*>(h)->param_id, 0};
  } else {
    return true;
  }
  nGemeld.store(n + 1, std::memory_order_release);
  return true;
}
const clap_output_events_t UIT = {nullptr, uitPush};

// invoer-events van de host (één parameterwaarde)
struct Invoer { clap_event_param_value_t e{}; uint32_t n = 0; };
uint32_t inSize(const clap_input_events_t* l) { return static_cast<const Invoer*>(l->ctx)->n; }
const clap_event_header_t* inGet(const clap_input_events_t* l, uint32_t) { return &static_cast<const Invoer*>(l->ctx)->e.header; }
Invoer hostWaarde(clap_id id, double v) {
  Invoer in;
  in.e.header = {sizeof in.e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  in.e.param_id = id; in.e.note_id = -1; in.e.port_index = -1; in.e.channel = -1; in.e.key = -1; in.e.value = v;
  in.n = 1;
  return in;
}

const clap_plugin_t* plugin = nullptr;
const clap_plugin_gui_t* gui = nullptr;
const clap_plugin_params_t* params = nullptr;
const clap_plugin_timer_support_t* pTimer = nullptr;
const clap_plugin_posix_fd_support_t* pFd = nullptr;

// --- de audiothread (met --audio) -----------------------------------------------------------------------------
std::atomic<bool> audioStop{false};
std::atomic<int> audioBlokken{0};
std::atomic<int> hostZet{0};  // 1 = de host zet in het volgende blok een waarde (Toon)
Invoer hostZetEvent;
__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT {
  return pl->process(pl, pr);
}
void audioLus() {
  plugin->start_processing(plugin);
  constexpr uint32_t B = 256;
  static float l[B], r[B], ul[B], ur[B];
  float* in[2] = {l, r};
  float* uit[2] = {ul, ur};
  Invoer leeg;
  int64_t tijd = 0;
  while (!audioStop.load()) {
    const bool zet = hostZet.load(std::memory_order_acquire) == 1;
    clap_input_events_t ev = {zet ? &hostZetEvent : &leeg, inSize, inGet};
    clap_audio_buffer_t ai = {in, nullptr, 2, 0, 0}, au = {uit, nullptr, 2, 0, 0};
    clap_process_t pr = {tijd, B, nullptr, &ai, &au, 1, 1, &ev, &UIT};
    doeProcess(plugin, &pr);
    if (zet) hostZet.store(2, std::memory_order_release);
    tijd += B;
    audioBlokken++;
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
  plugin->stop_processing(plugin);
}

// --- het venster van de host, per platform --------------------------------------------------------------------
#ifdef __APPLE__
const char* API = CLAP_WINDOW_API_COCOA;
NSWindow* venster = nil;
void schermOpen(uint32_t b, uint32_t h) {
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  venster = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, b, h)
                                        styleMask:NSWindowStyleMaskTitled
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  [venster setReleasedWhenClosed:NO];
  [venster orderFrontRegardless];
}
clap_window_t ouder() {
  clap_window_t w{};
  w.api = API;
  w.cocoa = (void*)[venster contentView];
  return w;
}
void schermGrootte(uint32_t b, uint32_t h) { [venster setContentSize:NSMakeSize(b, h)]; }
void pomp(double sec) {
  const auto eind = std::chrono::steady_clock::now() + std::chrono::duration<double>(sec);
  while (std::chrono::steady_clock::now() < eind) {
    @autoreleasepool {
      NSEvent* e;
      while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES]))
        [NSApp sendEvent:e];
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
  }
}
// de inhoud van het venster zoals hij getekend wordt (cacheDisplayInRect: tekent de views opnieuw in een bitmap)
std::vector<uint32_t> foto(int* b, int* h) {
  NSView* v = [venster contentView];
  [v displayIfNeeded];
  NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
  [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
  *b = (int)[rep pixelsWide];
  *h = (int)[rep pixelsHigh];
  std::vector<uint32_t> pix((size_t)*b * *h);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef cg = CGBitmapContextCreate(pix.data(), *b, *h, 8, (size_t)*b * 4, cs,
                                          kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst);
  CGContextDrawImage(cg, CGRectMake(0, 0, *b, *h), [rep CGImage]);
  CGContextRelease(cg);
  CGColorSpaceRelease(cs);
  for (auto& p : pix) p &= 0xffffff;
  return pix;
}
NSView* paneelView() {
  NSArray* kinderen = [[venster contentView] subviews];
  if ([kinderen count] < 1) fout("het paneel zette geen view in het venster");
  return [kinderen objectAtIndex:0];
}
NSEvent* muisEvent(NSEventType t, double x, double y, bool shift, int klikken) {
  NSView* v = paneelView();
  const NSPoint inView = NSMakePoint(x, [v bounds].size.height - y);
  const NSPoint inVenster = [v convertPoint:inView toView:nil];
  return [NSEvent mouseEventWithType:t location:inVenster modifierFlags:shift ? NSEventModifierFlagShift : 0
                           timestamp:0 windowNumber:[venster windowNumber] context:nil eventNumber:0
                          clickCount:klikken pressure:1];
}
void muisNeer(double x, double y, bool shift, int klikken) { [paneelView() mouseDown:muisEvent(NSEventTypeLeftMouseDown, x, y, shift, klikken)]; }
void muisSleep(double x, double y, bool shift) { [paneelView() mouseDragged:muisEvent(NSEventTypeLeftMouseDragged, x, y, shift, 1)]; }
void muisOp(double x, double y, int klikken) { [paneelView() mouseUp:muisEvent(NSEventTypeLeftMouseUp, x, y, false, klikken)]; }
void schermSluit() { [venster orderOut:nil]; }
#else
const char* API = CLAP_WINDOW_API_X11;
Display* dpy = nullptr;
Window venster = 0;
Time klok = 1000;
void schermOpen(uint32_t b, uint32_t h) {
  dpy = XOpenDisplay(nullptr);
  if (!dpy) fout("geen X-server (draai onder xvfb-run)");
  venster = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, b, h, 0, 0, 0);
  XMapWindow(dpy, venster);
  XSync(dpy, False);
}
clap_window_t ouder() {
  clap_window_t w{};
  w.api = API;
  w.x11 = venster;
  return w;
}
void schermGrootte(uint32_t b, uint32_t h) {
  XResizeWindow(dpy, venster, b, h);
  XSync(dpy, False);
}
void pomp(double sec) {  // de timers en fd's van de plugin, zoals een host ze bedient
  const auto eind = std::chrono::steady_clock::now() + std::chrono::duration<double>(sec);
  while (std::chrono::steady_clock::now() < eind) {
    for (int fd : fds) {
      if (fd < 0) continue;
      pollfd p = {fd, POLLIN, 0};
      if (poll(&p, 1, 0) > 0 && pFd) pFd->on_fd(plugin, fd, CLAP_POSIX_FD_READ);
    }
    const auto nu = std::chrono::steady_clock::now();
    for (auto& t : timers)
      if (t.ms && nu >= t.volgende) {
        t.volgende = nu + std::chrono::milliseconds(t.ms);
        if (pTimer) pTimer->on_timer(plugin, t.id);
      }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
std::vector<uint32_t> foto(int* b, int* h) {
  XSync(dpy, False);
  XWindowAttributes wa;
  XGetWindowAttributes(dpy, venster, &wa);
  *b = wa.width; *h = wa.height;
  XImage* im = XGetImage(dpy, venster, 0, 0, (unsigned)*b, (unsigned)*h, AllPlanes, ZPixmap);
  if (!im) fout("XGetImage mislukt");
  std::vector<uint32_t> pix((size_t)*b * *h);
  for (int y = 0; y < *h; ++y)
    for (int x = 0; x < *b; ++x) pix[(size_t)y * *b + x] = (uint32_t)XGetPixel(im, x, y) & 0xffffff;
  XDestroyImage(im);
  return pix;
}
Window paneelVenster() {
  Window wortel, ouderW, *kinderen = nullptr;
  unsigned n = 0;
  XQueryTree(dpy, venster, &wortel, &ouderW, &kinderen, &n);
  if (n < 1) fout("het paneel zette geen venster in het venster van de host");
  Window w = kinderen[0];
  XFree(kinderen);
  return w;
}
void stuur(int type, double x, double y, unsigned toestand) {
  XEvent e{};
  const Window w = paneelVenster();
  if (type == MotionNotify) {
    e.xmotion = {MotionNotify, 0, True, dpy, w, DefaultRootWindow(dpy), None, klok, (int)x, (int)y, (int)x, (int)y, toestand, NotifyNormal, True};
  } else {
    e.xbutton = {type, 0, True, dpy, w, DefaultRootWindow(dpy), None, klok, (int)x, (int)y, (int)x, (int)y, toestand, Button1, True};
  }
  klok += 20;
  XSendEvent(dpy, w, False, 0, &e);  // masker 0: naar de client die het venster maakte (de plugin)
  XFlush(dpy);
}
void muisNeer(double x, double y, bool shift, int) { stuur(ButtonPress, x, y, shift ? ShiftMask : 0); }
void muisSleep(double x, double y, bool shift) { stuur(MotionNotify, x, y, Button1Mask | (shift ? ShiftMask : 0)); }
void muisOp(double x, double y, int) { stuur(ButtonRelease, x, y, Button1Mask); }
void schermSluit() {
  XDestroyWindow(dpy, venster);
  XCloseDisplay(dpy);
}
#endif

// --- de toets -------------------------------------------------------------------------------------------------
bool metAudio = false;
int gelezen = 0;
std::thread audioThread;

// alles wat de plugin sinds de vorige keer meldde; zonder audio via flush (zoals een host na request_flush)
std::vector<Gemeld> haalGemeld() {
  pomp(0.1);
  if (metAudio) {
    const int start = audioBlokken.load();
    while (audioBlokken.load() < start + 3) pomp(0.01);
  } else {
    eis(flushGevraagd.exchange(false), "de plugin vroeg de host om een flush (request_flush)");
    Invoer leeg;
    clap_input_events_t in = {&leeg, inSize, inGet};
    params->flush(plugin, &in, &UIT);
  }
  std::vector<Gemeld> uit;
  const int n = nGemeld.load(std::memory_order_acquire);
  for (int i = gelezen; i < n; ++i) uit.push_back(gemeld[i]);
  gelezen = n;
  return uit;
}
double waardeVan(clap_id id) {
  double v = NAN;
  if (!params->get_value(plugin, id, &v)) fout("get_value mislukt");
  return v;
}
// de gemelde events van één gebaar op één parameter: begin, waarde(n), eind; geeft de laatste waarde
double eisGebaar(const std::vector<Gemeld>& ev, clap_id id, bool metWaarde) {
  int begin = 0, eind = 0, waarden = 0;
  double laatste = NAN;
  bool volgorde = true;
  for (const auto& e : ev) {
    if (e.id != id) fout("event voor een parameter die niet bediend werd: " + std::to_string(e.id));
    if (e.type == CLAP_EVENT_PARAM_GESTURE_BEGIN) { ++begin; volgorde &= eind == 0 && waarden == 0; }
    if (e.type == CLAP_EVENT_PARAM_VALUE) { ++waarden; laatste = e.waarde; volgorde &= begin == 1 && eind == 0; }
    if (e.type == CLAP_EVENT_PARAM_GESTURE_END) { ++eind; volgorde &= begin == 1; }
  }
  char t[160];
  std::snprintf(t, sizeof t, "parameter %u: gebaar-begin %d, waarden %d, gebaar-eind %d, in die volgorde", id, begin, waarden, eind);
  eis(begin == 1 && eind == 1 && volgorde && (metWaarde ? waarden >= 1 : waarden == 0), t);
  return laatste;
}

// de indeling van het paneel (paneel.cpp): 560 × 220, gelijkmatig geschaald en in het midden
struct Indeling { double k, ox, oy; };
Indeling indeling(double b, double h) {
  const double k = std::min(b / 560, h / 220);
  return {k, (b - 560 * k) / 2, (h - 220 * k) / 2};
}
void knop(const Indeling& d, int i, double* x, double* y) {
  *x = d.ox + (12 + 67.0 * (i + 0.5)) * d.k;
  *y = d.oy + 122 * d.k;
}
// verschil tussen twee foto's in de kolom van knop i
int verschilInKolom(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int pb, int ph, double schaal, const Indeling& d, int i) {
  int n = 0;
  const int x0 = (int)((d.ox + (12 + 67.0 * i + 3) * d.k) * schaal), x1 = (int)((d.ox + (12 + 67.0 * (i + 1) - 3) * d.k) * schaal);
  const int y0 = (int)((d.oy + 52 * d.k) * schaal), y1 = (int)((d.oy + 202 * d.k) * schaal);
  for (int y = std::max(0, y0); y < std::min(ph, y1); ++y)
    for (int x = std::max(0, x0); x < std::min(pb, x1); ++x) n += a[(size_t)y * pb + x] != b[(size_t)y * pb + x];
  return n;
}
int telKleur(const std::vector<uint32_t>& pix, uint32_t kleur) {
  int n = 0;
  for (uint32_t p : pix) {
    const int dr = (int)((p >> 16) & 0xff) - (int)((kleur >> 16) & 0xff);
    const int dg = (int)((p >> 8) & 0xff) - (int)((kleur >> 8) & 0xff);
    const int db = (int)(p & 0xff) - (int)(kleur & 0xff);
    n += std::abs(dr) <= 8 && std::abs(dg) <= 8 && std::abs(db) <= 8;
  }
  return n;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) fout("gebruik: pb-paneel <plugin.clap> <uitmap> [--audio]");
  const std::string pad = argv[1], uitmap = argv[2];
  metAudio = argc > 3 && !std::strcmp(argv[3], "--audio");
  std::string lib = pad;
#ifdef __APPLE__
  {
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
    lib = pad + "/Contents/MacOS/" + naam;
  }
#endif
  void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) fout(std::string("dlopen: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
  if (!entry || !entry->init(pad.c_str())) fout("geen clap_entry");
  auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!fab) fout("geen factory");
  plugin = fab->create_plugin(fab, &HOST, fab->get_plugin_descriptor(fab, 0)->id);
  if (!plugin || !plugin->init(plugin)) fout("plugin start niet");
  gui = static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin, CLAP_EXT_GUI));
  params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  pTimer = static_cast<const clap_plugin_timer_support_t*>(plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT));
  pFd = static_cast<const clap_plugin_posix_fd_support_t*>(plugin->get_extension(plugin, CLAP_EXT_POSIX_FD_SUPPORT));
  std::printf("pb-paneel: %s (%s%s)\n", pad.c_str(), API, metAudio ? ", process op een audiothread" : ", flush op de mainthread");

  eis(gui != nullptr && params != nullptr, "de plugin heeft de gui- en params-extensie");
  eis(gui->is_api_supported(plugin, API, false), std::string("gui-API ") + API + " (ingebed)");
  eis(!gui->is_api_supported(plugin, API, true), "geen zwevend venster (alleen ingebed)");
  eis(gui->create(plugin, API, false), "gui.create");
  uint32_t b = 0, hh = 0;
  eis(gui->get_size(plugin, &b, &hh) && b >= 560 && hh >= 220, "standaardgrootte " + std::to_string(b) + " × " + std::to_string(hh) + " ≥ 560 × 220");
  uint32_t kb = 100, kh = 100;
  eis(gui->can_resize(plugin) && gui->adjust_size(plugin, &kb, &kh) && kb == 560 && kh == 220, "schaalbaar, minimaal 560 × 220 (adjust_size 100 × 100)");
  schermOpen(b, hh);
  const clap_window_t w = ouder();
  eis(gui->set_parent(plugin, &w), "gui.set_parent");
  eis(gui->show(plugin), "gui.show");
  pomp(0.5);

  if (metAudio) {
    eis(plugin->activate(plugin, 48000, 1, 256), "activate (48 kHz)");
    audioThread = std::thread(audioLus);
    pomp(0.1);
  }

  int fb, fh;
  const std::vector<uint32_t> standaard = foto(&fb, &fh);
  schrijfPng(uitmap + "/paneel-standaard.png", standaard, fb, fh);
  const double schaal = (double)fb / b;
  std::printf("  schermafbeelding %s/paneel-standaard.png (%d × %d pixels, %.0f× )\n", uitmap.c_str(), fb, fh, schaal);
  eis(telKleur(standaard, 0x232629) > fb * fh / 3, "achtergrond in 'paneel' #232629");
  eis(telKleur(standaard, 0xf2c200) > (int)(500 * schaal * schaal), "naamlabel in 'tape' #f2c200");
  eis(telKleur(standaard, 0x40cbda) > (int)(8 * 100 * schaal * schaal), "bogen in 'spoor' #40cbda");
  eis(telKleur(standaard, 0xe7e5dd) > (int)(2000 * schaal * schaal), "tekst en randen in 'inkt' #e7e5dd");
  pomp(0.1);
  eis(!flushGevraagd.load() && nGemeld.load() == 0, "openen en tekenen melden niets aan de host");

  const Indeling d = indeling(b, hh);
  double x, y;
  // 1. Tijd (id 1, logaritmisch 20..1200 ms) 60 punten omlaag slepen: 0,3 van het bereik eraf
  knop(d, 0, &x, &y);
  muisNeer(x, y, false, 1);
  for (int s = 1; s <= 6; ++s) { muisSleep(x, y + 10 * s, false); pomp(0.02); }
  muisOp(x, y + 60, 1);
  if (!metAudio) eis(waardeVan(1) == 375, "vóór de flush staat Tijd nog op 375 ms: het paneel zet geen waarde zelf");
  double v = eisGebaar(haalGemeld(), 1, true);
  const double verwacht = 20 * std::exp((std::log(375.0 / 20) / std::log(60.0) - 0.3) * std::log(60.0));
  char t[200];
  std::snprintf(t, sizeof t, "Tijd na 60 punten omlaag: %.6f ms (verwacht %.6f; 200 punten = het bereik)", v, verwacht);
  eis(std::fabs(v - verwacht) < 1e-6 * verwacht, t);
  eis(waardeVan(1) == v, "get_value(Tijd) volgt pas via process/flush");
  // 2. Mix (id 8, lineair 0..1) met Shift 40 punten omhoog: 10× fijner = 0,02
  knop(d, 7, &x, &y);
  muisNeer(x, y, true, 1);
  for (int s = 1; s <= 4; ++s) { muisSleep(x, y - 10 * s, true); pomp(0.02); }
  muisOp(x, y - 40, 1);
  v = eisGebaar(haalGemeld(), 8, true);
  std::snprintf(t, sizeof t, "Shift + slepen = fijn: Mix %.9f (verwacht 0.395)", v);
  eis(std::fabs(v - 0.395) < 1e-9, t);
  // 3. dubbelklik op Tijd: terug naar de standaard (375 ms)
  knop(d, 0, &x, &y);
  muisNeer(x, y, false, 1);
  muisOp(x, y, 1);
  eisGebaar(haalGemeld(), 1, false);  // een gewone klik: gebaar zonder waarde
  muisNeer(x, y, false, 2);
  muisOp(x, y, 2);
  v = eisGebaar(haalGemeld(), 1, true);
  eis(v == 375 && waardeVan(1) == 375, "dubbelklik = standaard (Tijd 375 ms)");
  pomp(0.2);
  const std::vector<uint32_t> naBediening = foto(&fb, &fh);

  // 4. de host zet Toon (id 3) op 12 kHz: het paneel volgt; Terugkoppeling (id 2) bleef ongemoeid
  if (metAudio) {
    hostZetEvent = hostWaarde(3, 12000);
    hostZet.store(1, std::memory_order_release);
    while (hostZet.load(std::memory_order_acquire) != 2) pomp(0.01);
  } else {
    Invoer in = hostWaarde(3, 12000);
    clap_input_events_t ev = {&in, inSize, inGet};
    params->flush(plugin, &ev, &UIT);
  }
  pomp(0.3);
  const std::vector<uint32_t> naHost = foto(&fb, &fh);
  schrijfPng(uitmap + "/paneel-bediend.png", naHost, fb, fh);
  eis(waardeVan(3) == 12000, "de host zette Toon op 12 kHz");
  eis(verschilInKolom(naBediening, naHost, fb, fh, schaal, d, 2) > 50, "het paneel toont de nieuwe Toon");
  eis(verschilInKolom(naBediening, naHost, fb, fh, schaal, d, 1) == 0, "Terugkoppeling op het paneel onveranderd");
  eis(verschilInKolom(standaard, naHost, fb, fh, schaal, d, 7) > 0, "Mix op het paneel veranderd (0.395)");
  eis(nGemeld.load() == gelezen, "een waarde van de host wordt niet teruggemeld");

  // 5. groter: twee keer het minimum
  uint32_t gb = 1120, gh = 440;
  eis(gui->adjust_size(plugin, &gb, &gh) && gb == 1120 && gh == 440, "adjust_size 1120 × 440 blijft");
  schermGrootte(gb, gh);
  eis(gui->set_size(plugin, gb, gh), "gui.set_size 1120 × 440");
  pomp(0.4);
  int gfb, gfh;
  const std::vector<uint32_t> groot = foto(&gfb, &gfh);
  schrijfPng(uitmap + "/paneel-groot.png", groot, gfb, gfh);
  // van 700 × 275 naar 1120 × 440 is 1,6× zo groot: 2,56× zoveel pixels in de bogen
  eis(gfb == (int)std::lround(1120 * schaal) && telKleur(groot, 0x40cbda) > 2 * telKleur(naHost, 0x40cbda),
      "het paneel schaalt mee (bogen ±2,5× zoveel pixels)");

  if (metAudio) {
    audioStop = true;
    audioThread.join();
  }
  gui->hide(plugin);
  gui->destroy(plugin);
  pomp(0.05);
  schermSluit();
  if (metAudio) plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  std::printf("paneel: geslaagd (%s%s)\n", API, metAudio ? ", met audiothread" : "");
  return 0;
}
