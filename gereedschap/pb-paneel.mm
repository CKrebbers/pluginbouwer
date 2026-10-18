// pb-paneel — toetst het frontpaneel (fase 3) door het gebouwde .clap en door de geïnstalleerde AU, en maakt de
// schermafbeeldingen voor CI. Alleen macOS.
//
//   pb-paneel <PB Toetsenpad.clap> <uitmap>
//
// Wat hij toetst:
//   - de gui-extensie: cocoa (niet zwevend), beginmaat 640 × 250, minimaal 560 × 220, schaalbaar, twee keer openen;
//   - het beeld (op 640 × 250, op 560 × 220 = de maat van frontpaneel.svg, en op 2×): kleuren en plekken uit de mockup;
//   - knoppen via de host (valkuil 14): slepen met nagebootste muisevents verandert niets in de plugin zelf; pas
//     process (of flush als hij niet actief is) geeft de host GESTURE_BEGIN/PARAM_VALUE/GESTURE_END en past de waarde
//     toe. 50 px omhoog = een kwart van het bereik, Shift = 10× fijner, dubbelklik = standaard;
//   - het geluid: een knop via het paneel klinkt bit voor bit als dezelfde waarde als host-event;
//   - de AU (clap-wrapper): de Cocoa-view via kAudioUnitProperty_CocoaUI, slepen + één Render = de AU-parameter
//     verandert en de host hoort begin en eind van het gebaar (AUEventListener).
// Uitvoer: <uitmap>/*.png en "N controles, M fout"; exitcode 1 bij een fout.
#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>

#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int gControles = 0, gFouten = 0;
void controle(bool ok, const std::string& wat) {
  ++gControles;
  if (!ok) ++gFouten;
  std::printf("%s %s\n", ok ? "ok  " : "FOUT", wat.c_str());
}
std::string getal(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.10g", v);
  return b;
}

// --- een kleine CLAP-host ------------------------------------------------------------------------------------
int gFlushVragen = 0;
void hRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hFlush(const clap_host_t*) { ++gFlushVragen; }
const clap_host_params_t HOST_PARAMS = {hRescan, hClear, hFlush};
const void* hostExt(const clap_host_t*, const char* id) { return std::strcmp(id, CLAP_EXT_PARAMS) ? nullptr : &HOST_PARAMS; }
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-paneel", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

union Ev {
  clap_event_header_t h;
  clap_event_param_value_t p;
  clap_event_param_gesture_t g;
  clap_event_note_t n;
};
struct Lijst {
  std::vector<Ev> ev;
};
uint32_t lSize(const clap_input_events_t* l) { return (uint32_t)static_cast<Lijst*>(l->ctx)->ev.size(); }
const clap_event_header_t* lGet(const clap_input_events_t* l, uint32_t i) { return &static_cast<Lijst*>(l->ctx)->ev[i].h; }
bool lPush(const clap_output_events_t* l, const clap_event_header_t* h) {
  Ev e;
  std::memset(&e, 0, sizeof e);
  std::memcpy(&e, h, h->size < sizeof e ? h->size : sizeof e);
  static_cast<Lijst*>(l->ctx)->ev.push_back(e);
  return true;
}

Ev paramEvent(clap_id id, double v, uint32_t t = 0) {
  Ev e;
  std::memset(&e, 0, sizeof e);
  e.p.header = {sizeof(e.p), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.p.param_id = id;
  e.p.note_id = -1;
  e.p.port_index = -1;
  e.p.channel = -1;
  e.p.key = -1;
  e.p.value = v;
  return e;
}
Ev nootAan(int16_t key, uint32_t t = 0) {
  Ev e;
  std::memset(&e, 0, sizeof e);
  e.n.header = {sizeof(e.n), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0};
  e.n.note_id = -1;
  e.n.port_index = 0;
  e.n.channel = 0;
  e.n.key = key;
  e.n.velocity = 1;
  return e;
}

struct Clap {
  const clap_plugin_entry_t* entry = nullptr;
  const clap_plugin_factory_t* fab = nullptr;
  const char* id = nullptr;
};

const clap_plugin_t* maak(const Clap& c) {
  const clap_plugin_t* pl = c.fab->create_plugin(c.fab, &HOST, c.id);
  if (!pl || !pl->init(pl)) {
    std::printf("FOUT plugin start niet\n");
    std::exit(1);
  }
  return pl;
}

constexpr uint32_t BLOK = 512;
uint64_t gTijd = 0;
// één blok; geeft de uitvoer (L dan R) en vult `uit` met de events die de plugin de host gaf
std::vector<float> blok(const clap_plugin_t* pl, Lijst& in, Lijst& uit) {
  std::vector<float> l(BLOK), r(BLOK);
  float* kan[2] = {l.data(), r.data()};
  clap_audio_buffer_t ob;
  std::memset(&ob, 0, sizeof ob);
  ob.data32 = kan;
  ob.channel_count = 2;
  clap_input_events_t ie = {&in, lSize, lGet};
  clap_output_events_t oe = {&uit, lPush};
  clap_process_t pr;
  std::memset(&pr, 0, sizeof pr);
  pr.steady_time = (int64_t)gTijd;
  pr.frames_count = BLOK;
  pr.audio_outputs = &ob;
  pr.audio_outputs_count = 1;
  pr.in_events = &ie;
  pr.out_events = &oe;
  pl->process(pl, &pr);
  gTijd += BLOK;
  l.insert(l.end(), r.begin(), r.end());
  return l;
}

double waarde(const clap_plugin_t* pl, clap_id id) {
  auto* pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
  double v = NAN;
  if (!pp || !pp->get_value(pl, id, &v)) return NAN;
  return v;
}

// verwacht: BEGIN, één of meer VALUE, END, alles voor `id`; de laatste VALUE = v
void toetsGebaar(const Lijst& uit, clap_id id, double v, double tol, const std::string& wat) {
  bool ok = uit.ev.size() >= 3;
  double laatste = NAN;
  for (size_t k = 0; ok && k < uit.ev.size(); ++k) {
    const Ev& e = uit.ev[k];
    const uint16_t t = e.h.type;
    const uint16_t moet = k == 0 ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                          : k + 1 == uit.ev.size() ? CLAP_EVENT_PARAM_GESTURE_END
                                                   : CLAP_EVENT_PARAM_VALUE;
    if (t != moet || e.h.space_id != CLAP_CORE_EVENT_SPACE_ID) ok = false;
    else if (t == CLAP_EVENT_PARAM_VALUE) {
      if (e.p.param_id != id) ok = false;
      laatste = e.p.value;
    } else if (e.g.param_id != id) ok = false;
  }
  controle(ok, wat + ": de host krijgt GESTURE_BEGIN, PARAM_VALUE…, GESTURE_END voor id " + std::to_string(id) + " (" +
                   std::to_string(uit.ev.size()) + " events)");
  controle(std::fabs(laatste - v) <= tol, wat + ": laatste PARAM_VALUE " + getal(laatste) + " = " + getal(v));
}

// --- beeld --------------------------------------------------------------------------------------------------
NSWindow* venster(double b, double h) {
  NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, b, h)
                                            styleMask:NSWindowStyleMaskBorderless
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
  w.releasedWhenClosed = NO;
  return w;
}

NSBitmapImageRep* beeld(NSView* v) {
  [v.window displayIfNeeded];
  NSBitmapImageRep* r = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
  [v cacheDisplayInRect:v.bounds toBitmapImageRep:r];
  return r;
}

void bewaar(NSBitmapImageRep* r, NSString* map, NSString* naam) {
  NSData* png = [r representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  NSString* pad = [map stringByAppendingPathComponent:naam];
  controle(png && [png writeToFile:pad atomically:YES], std::string("schermafbeelding ") + pad.UTF8String);
}

struct Rgb {
  int r = -1, g = -1, b = -1;
};
Rgb pixel(NSBitmapImageRep* r, NSView* v, double x, double y) {  // x, y in punten van de view (y omlaag)
  const double f = r.pixelsWide / v.bounds.size.width;
  const NSInteger px = (NSInteger)std::floor(x * f), py = (NSInteger)std::floor(y * f);
  Rgb u;
  if (px < 0 || py < 0 || px >= r.pixelsWide || py >= r.pixelsHigh) return u;
  NSColor* c = [[r colorAtX:px y:py] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
  if (!c) return u;
  u.r = (int)std::lround(c.redComponent * 255);
  u.g = (int)std::lround(c.greenComponent * 255);
  u.b = (int)std::lround(c.blueComponent * 255);
  return u;
}
bool lijkt(Rgb p, uint32_t hex, int tol) {
  return std::abs(p.r - (int)((hex >> 16) & 0xff)) <= tol && std::abs(p.g - (int)((hex >> 8) & 0xff)) <= tol &&
         std::abs(p.b - (int)(hex & 0xff)) <= tol;
}
std::string hex(Rgb p) {
  char b[16];
  std::snprintf(b, sizeof b, "#%02x%02x%02x", p.r & 0xff, p.g & 0xff, p.b & 0xff);
  return b;
}
// is er in een vierkantje van ±straal punten rond (x, y) een pixel met deze kleur?
bool ergens(NSBitmapImageRep* r, NSView* v, double x, double y, double straal, uint32_t kl, int tol, Rgb* midden) {
  if (midden) *midden = pixel(r, v, x, y);
  const double f = r.pixelsWide / v.bounds.size.width;
  for (double dy = -straal; dy <= straal + 1e-9; dy += 0.5 / f)
    for (double dx = -straal; dx <= straal + 1e-9; dx += 0.5 / f)
      if (lijkt(pixel(r, v, x + dx, y + dy), kl, tol)) return true;
  return false;
}
// een pixel in het vak waarvoor `past` waar is
template <class F>
bool inVak(NSBitmapImageRep* r, NSView* v, double x0, double y0, double x1, double y1, F past) {
  const double f = r.pixelsWide / v.bounds.size.width;
  for (double y = y0; y <= y1; y += 0.5 / f)
    for (double x = x0; x <= x1; x += 0.5 / f)
      if (past(pixel(r, v, x, y))) return true;
  return false;
}

constexpr uint32_t PANEEL = 0x232629, INKT = 0xe7e5dd, TAPE = 0xf2c200, SPOOR = 0x40cbda, GROEF = 0x3a3e42,
                   SCHADUW = 0x0d0e0f;
const double MIN_[4] = {0.25, 0, 100, 10}, MAX_[4] = {4, 1, 8000, 4000};
const char* NAAM[4] = {"Noot", "Poort", "Filter", "Loslaten"};

// het beeld tegen frontpaneel.svg: s = schaal (1 = de mockup), waarden per knop, gekozen = knop met de tape-boog
void toetsBeeld(NSBitmapImageRep* r, NSView* v, double s, const double* w, int gekozen, const std::string& wat) {
  auto in = [&](double x, double y, uint32_t kl, int tol, double straal, const std::string& wat2) {
    Rgb m;
    const bool ok = ergens(r, v, x * s, y * s, straal * s, kl, tol, &m);
    controle(ok, wat + ": " + wat2 + " (bij " + getal(x) + "," + getal(y) + " in de mockup: " + hex(m) + ")");
  };
  in(400, 100, PANEEL, 6, 0, "paneel");
  in(150, 21, TAPE, 6, 0, "naamlabel tape");
  in(556, 216, SCHADUW, 8, 0.3, "harde schaduw rechtsonder");
  in(300, 0.5, INKT, 10, 0.3, "rand inkt");
  controle(inVak(r, v, 30 * s, 24 * s, 150 * s, 35 * s, [](Rgb p) { return p.r >= 0 && p.r < 140; }),
           wat + ": tekst PB TOETSENPAD op de tape");
  for (int i = 0; i < 4; ++i) {
    const double cx = 62 + 76 * i, cy = 130;
    const double n = (w[i] - MIN_[i]) / (MAX_[i] - MIN_[i]);
    const std::string k = std::string(NAAM[i]) + " ";
    auto opBoog = [&](double norm, double straal) {
      const double a = (-135 + 270 * norm) * M_PI / 180;
      return std::pair<double, double>(cx + straal * std::sin(a), cy - straal * std::cos(a));
    };
    if (n > 0.04) {
      auto [x, y] = opBoog(n / 2, 22);
      in(x, y, i == gekozen ? TAPE : SPOOR, 24, 1.0, k + (i == gekozen ? "boog tape (gekozen)" : "boog spoor"));
    }
    if (n < 0.96) {
      auto [x, y] = opBoog(n + (1 - n) / 2, 22);
      in(x, y, GROEF, 12, 1.0, k + "groef");
    }
    {  // de wijzer: inkt op 2/3 van de straal, paneel aan de overkant
      auto [x, y] = opBoog(n, 9);
      in(x, y, INKT, 40, 1.0, k + "wijzer");
      auto [x2, y2] = opBoog(n + 0.5, 8);
      in(x2, y2, PANEEL, 8, 0.5, k + "knop");
    }
    auto licht = [](Rgb p) { return p.r > 110 && p.g > 110 && p.b > 110; };
    controle(inVak(r, v, (cx - 20) * s, 86 * s, (cx + 20) * s, 97 * s, licht), wat + ": " + k + "label");
    controle(inVak(r, v, (cx - 28) * s, 165 * s, (cx + 28) * s, 175 * s, licht), wat + ": " + k + "waarde");
  }
}

// --- muis ---------------------------------------------------------------------------------------------------
NSEvent* muis(NSEventType t, NSView* v, double x, double y, NSInteger klikken, NSEventModifierFlags mods) {
  const NSPoint p = [v convertPoint:NSMakePoint(x, y) toView:nil];
  return [NSEvent mouseEventWithType:t
                            location:p
                       modifierFlags:mods
                           timestamp:0
                        windowNumber:v.window.windowNumber
                             context:nil
                         eventNumber:0
                          clickCount:klikken
                            pressure:1];
}
void sleepKnop(NSView* v, double x, double y0, double y1, int stappen, bool fijn) {
  const NSEventModifierFlags m = fijn ? NSEventModifierFlagShift : (NSEventModifierFlags)0;
  [v mouseDown:muis(NSEventTypeLeftMouseDown, v, x, y0, 1, m)];
  for (int k = 1; k <= stappen; ++k)
    [v mouseDragged:muis(NSEventTypeLeftMouseDragged, v, x, y0 + (y1 - y0) * k / stappen, 1, m)];
  [v mouseUp:muis(NSEventTypeLeftMouseUp, v, x, y1, 1, m)];
}
void dubbelklik(NSView* v, double x, double y) {
  [v mouseDown:muis(NSEventTypeLeftMouseDown, v, x, y, 1, 0)];
  [v mouseUp:muis(NSEventTypeLeftMouseUp, v, x, y, 1, 0)];
  [v mouseDown:muis(NSEventTypeLeftMouseDown, v, x, y, 2, 0)];
  [v mouseUp:muis(NSEventTypeLeftMouseUp, v, x, y, 2, 0)];
}

// --- AU -----------------------------------------------------------------------------------------------------
int gAuBegin = 0, gAuEind = 0, gAuWaarde = 0;
void auLuister(void*, void*, const AudioUnitEvent* e, UInt64, AudioUnitParameterValue) {
  if (e->mEventType == kAudioUnitEvent_BeginParameterChangeGesture) ++gAuBegin;
  else if (e->mEventType == kAudioUnitEvent_EndParameterChangeGesture) ++gAuEind;
  else if (e->mEventType == kAudioUnitEvent_ParameterValueChange) ++gAuWaarde;
}

void toetsAu(NSString* map) {
  AudioComponentDescription d = {kAudioUnitType_MusicDevice, 'PTgg', 'Vrve', 0, 0};
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  controle(comp != nullptr, "AU: aumu PTgg Vrve gevonden (geïnstalleerd)");
  if (!comp) return;
  AudioUnit au = nullptr;
  controle(AudioComponentInstanceNew(comp, &au) == noErr && au, "AU: instantie");
  if (!au) return;
  UInt32 maxF = BLOK;
  AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF);
  OSStatus st = AudioUnitInitialize(au);
  controle(st == noErr, "AU: Initialize (" + std::to_string((int)st) + ")");

  UInt32 grootte = 0;
  Boolean schrijf = false;
  st = AudioUnitGetPropertyInfo(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &grootte, &schrijf);
  controle(st == noErr && grootte >= sizeof(AudioUnitCocoaViewInfo), "AU: kAudioUnitProperty_CocoaUI bestaat");
  if (st != noErr || grootte < sizeof(AudioUnitCocoaViewInfo)) return;
  std::vector<unsigned char> buf(grootte);
  auto* info = reinterpret_cast<AudioUnitCocoaViewInfo*>(buf.data());
  st = AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &grootte);
  controle(st == noErr, "AU: CocoaUI gelezen");
  if (st != noErr) return;
  NSURL* url = (__bridge_transfer NSURL*)info->mCocoaAUViewBundleLocation;
  NSString* klasNaam = (__bridge_transfer NSString*)info->mCocoaAUViewClass[0];
  NSBundle* bundel = [NSBundle bundleWithURL:url];
  Class klas = [bundel classNamed:klasNaam];
  controle(klas != nil, std::string("AU: view-fabriek ") + (klasNaam ? klasNaam.UTF8String : "?"));
  if (!klas) return;
  id<AUCocoaUIBase> fabriek = [[klas alloc] init];
  NSView* auv = [fabriek uiViewForAudioUnit:au withSize:NSMakeSize(0, 0)];
  controle(auv != nil, "AU: view van de wrapper");
  if (!auv) return;
  NSWindow* w = venster(auv.frame.size.width, auv.frame.size.height);
  [w.contentView addSubview:auv];
  controle(auv.frame.size.width == 640 && auv.frame.size.height == 250,
           "AU: beginmaat 640 × 250 (" + getal(auv.frame.size.width) + " × " + getal(auv.frame.size.height) + ")");
  NSView* pv = auv.subviews.firstObject;
  controle(pv != nil, "AU: het paneel zit in de view van de wrapper");
  if (!pv) return;
  NSBitmapImageRep* r = beeld(auv);
  bewaar(r, map, @"au-paneel-640x250.png");
  const double std_[4] = {1, 0, 900, 450};
  toetsBeeld(r, auv, std::fmin(640.0 / 560, 250.0 / 220), std_, -1, "AU 640×250");

  // slepen op Filter, één Render: de AU-parameter verandert en de host hoort het gebaar
  AUEventListenerRef luister = nullptr;
  AUEventListenerCreate(auLuister, nullptr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 0, 0, &luister);
  AudioUnitEvent ev;
  std::memset(&ev, 0, sizeof ev);
  ev.mArgument.mParameter.mAudioUnit = au;
  ev.mArgument.mParameter.mParameterID = 3;
  ev.mArgument.mParameter.mScope = kAudioUnitScope_Global;
  ev.mArgument.mParameter.mElement = 0;
  for (AudioUnitEventType t : {kAudioUnitEvent_BeginParameterChangeGesture, kAudioUnitEvent_EndParameterChangeGesture,
                               kAudioUnitEvent_ParameterValueChange}) {
    ev.mEventType = t;
    if (luister) AUEventListenerAddEventType(luister, nullptr, &ev);
  }
  AudioUnitParameterInfo pi;
  UInt32 piGrootte = sizeof pi;
  std::memset(&pi, 0, sizeof pi);
  AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, 3, &pi, &piGrootte);
  AudioUnitParameterValue v0 = 0, v1 = 0;
  AudioUnitGetParameter(au, 3, kAudioUnitScope_Global, 0, &v0);
  const double s = std::fmin(640.0 / 560, 250.0 / 220);
  sleepKnop(pv, 214 * s, 130 * s, 130 * s - 50, 2, false);  // 50 punten omhoog = een kwart van het bereik

  AudioStreamBasicDescription fmt;
  UInt32 fg = sizeof fmt;
  std::memset(&fmt, 0, sizeof fmt);
  AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, &fg);
  const bool los = (fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
  const UInt32 kan = fmt.mChannelsPerFrame ? fmt.mChannelsPerFrame : 2;
  std::vector<float> data(BLOK * kan);
  std::vector<unsigned char> ablBuf(sizeof(AudioBufferList) + kan * sizeof(AudioBuffer));
  auto* abl = reinterpret_cast<AudioBufferList*>(ablBuf.data());
  abl->mNumberBuffers = los ? kan : 1;
  for (UInt32 c = 0; c < abl->mNumberBuffers; ++c) {
    abl->mBuffers[c].mNumberChannels = los ? 1 : kan;
    abl->mBuffers[c].mDataByteSize = (UInt32)(BLOK * sizeof(float) * (los ? 1 : kan));
    abl->mBuffers[c].mData = data.data() + (los ? c * BLOK : 0);
  }
  AudioUnitRenderActionFlags vl = 0;
  AudioTimeStamp ts;
  std::memset(&ts, 0, sizeof ts);
  ts.mSampleTime = 0;
  ts.mFlags = kAudioTimeStampSampleTimeValid;
  st = AudioUnitRender(au, &vl, &ts, 0, BLOK, abl);
  controle(st == noErr, "AU: Render (" + std::to_string((int)st) + ")");
  for (int k = 0; k < 20; ++k) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
  AudioUnitGetParameter(au, 3, kAudioUnitScope_Global, 0, &v1);
  const double clapWaarde = 900 + 0.25 * 7900;  // 2875 Hz
  const double verwacht = pi.maxValue > 1.5 ? clapWaarde : (clapWaarde - 100) / 7900;
  controle(std::fabs(v1 - verwacht) <= 1e-3 * std::fmax(1.0, std::fabs(verwacht)),
           "AU: Filter na slepen + Render = " + getal(v1) + " (was " + getal(v0) + ", verwacht " + getal(verwacht) +
               ", AU-bereik " + getal(pi.minValue) + ".." + getal(pi.maxValue) + ")");
  controle(gAuBegin >= 1 && gAuEind >= 1, "AU: de host hoort begin (" + std::to_string(gAuBegin) + ") en eind (" +
                                              std::to_string(gAuEind) + ") van het gebaar, waarde " +
                                              std::to_string(gAuWaarde) + "×");
  bewaar(beeld(auv), map, @"au-paneel-na-slepen.png");

  [auv removeFromSuperview];  // de wrapper ruimt het paneel op (gui.destroy)
  if (luister) AUEventListenerDispose(luister);
  AudioUnitUninitialize(au);
  AudioComponentInstanceDispose(au);
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc < 3) {
      std::fprintf(stderr, "gebruik: pb-paneel <PB Toetsenpad.clap> <uitmap>\n");
      return 2;
    }
    const std::string pad = argv[1];
    NSString* map = [NSString stringWithUTF8String:argv[2]];
    [[NSFileManager defaultManager] createDirectoryAtPath:map withIntermediateDirectories:YES attributes:nil error:nil];
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
    const std::string lib = pad + "/Contents/MacOS/" + naam;
    void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
      std::printf("FOUT dlopen: %s\n", dlerror());
      return 1;
    }
    Clap c;
    c.entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
    if (!c.entry || !c.entry->init(pad.c_str())) return 1;
    c.fab = static_cast<const clap_plugin_factory_t*>(c.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!c.fab) return 1;
    c.id = c.fab->get_plugin_descriptor(c.fab, 0)->id;

    const clap_plugin_t* pl = maak(c);
    auto* gui = static_cast<const clap_plugin_gui_t*>(pl->get_extension(pl, CLAP_EXT_GUI));
    auto* pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    controle(gui != nullptr, "CLAP: gui-extensie");
    if (!gui || !pp) return 1;
    controle(gui->is_api_supported(pl, CLAP_WINDOW_API_COCOA, false), "gui: cocoa ingebed");
    controle(!gui->is_api_supported(pl, CLAP_WINDOW_API_COCOA, true), "gui: niet zwevend");
    controle(!gui->is_api_supported(pl, CLAP_WINDOW_API_X11, false), "gui: geen x11 op macOS");
    const char* api = nullptr;
    bool zwevend = true;
    controle(gui->get_preferred_api(pl, &api, &zwevend) && api && !std::strcmp(api, CLAP_WINDOW_API_COCOA) && !zwevend,
             "gui: voorkeur cocoa");
    controle(gui->create(pl, CLAP_WINDOW_API_COCOA, false), "gui: create");
    uint32_t b = 0, hh = 0;
    controle(gui->get_size(pl, &b, &hh) && b == 640 && hh == 250,
             "gui: beginmaat 640 × 250 (" + std::to_string(b) + " × " + std::to_string(hh) + ")");
    controle(gui->can_resize(pl), "gui: schaalbaar");
    b = 100, hh = 100;
    controle(gui->adjust_size(pl, &b, &hh) && b == 560 && hh == 220, "gui: minimaal 560 × 220");
    b = 900, hh = 300;
    controle(gui->adjust_size(pl, &b, &hh) && b == 900 && hh == 300, "gui: groter mag");

    NSWindow* w = venster(640, 250);
    clap_window_t cw;
    cw.api = CLAP_WINDOW_API_COCOA;
    cw.cocoa = (__bridge void*)w.contentView;
    controle(gui->set_parent(pl, &cw), "gui: set_parent");
    controle(gui->show(pl), "gui: show");
    NSView* pv = w.contentView.subviews.firstObject;
    controle(pv != nil && pv.frame.size.width == 640 && pv.frame.size.height == 250, "gui: view 640 × 250 in de ouder");
    if (!pv) return 1;

    const double std_[4] = {1, 0, 900, 450};
    NSBitmapImageRep* r = beeld(pv);
    bewaar(r, map, @"paneel-640x250.png");
    toetsBeeld(r, pv, std::fmin(640.0 / 560, 250.0 / 220), std_, -1, "640×250");
    controle(gui->set_size(pl, 1120, 440), "gui: set_size 1120 × 440");
    [w setContentSize:NSMakeSize(1120, 440)];
    r = beeld(pv);
    bewaar(r, map, @"paneel-1120x440.png");
    toetsBeeld(r, pv, 2, std_, -1, "1120×440");
    controle(gui->set_size(pl, 560, 220), "gui: set_size 560 × 220 (de maat van frontpaneel.svg)");
    [w setContentSize:NSMakeSize(560, 220)];
    r = beeld(pv);
    bewaar(r, map, @"paneel-560x220.png");
    toetsBeeld(r, pv, 1, std_, -1, "560×220");

    // --- knoppen via de host (actief: process) ---
    controle(pl->activate(pl, 48000, 1, BLOK) && pl->start_processing(pl), "activate");
    const int f0 = gFlushVragen;
    sleepKnop(pv, 214, 130, 80, 2, false);  // Filter 50 px omhoog = een kwart van het bereik: 900 + 1975 = 2875 Hz
    controle(waarde(pl, 3) == 900, "het paneel verandert de parameter niet zelf: get_value blijft 900 tot process");
    controle(gFlushVragen > f0, "het paneel vraagt request_flush (" + std::to_string(gFlushVragen - f0) + "×)");
    Lijst in, uit;
    in.ev.push_back(nootAan(57));
    std::vector<float> a = blok(pl, in, uit);
    toetsGebaar(uit, 3, 2875, 0, "slepen Filter");
    controle(waarde(pl, 3) == 2875, "get_value(Filter) = " + getal(waarde(pl, 3)) + " na process");
    for (int k = 0; k < 3; ++k) {
      Lijst i2, u2;
      std::vector<float> x = blok(pl, i2, u2);
      a.insert(a.end(), x.begin(), x.end());
      controle(u2.ev.empty(), "daarna geen events meer naar de host (blok " + std::to_string(k + 2) + ")");
    }
    {  // hetzelfde als host-event in een verse instantie: bit voor bit hetzelfde geluid
      const clap_plugin_t* p2 = maak(c);
      p2->activate(p2, 48000, 1, BLOK);
      p2->start_processing(p2);
      Lijst i2, u2;
      i2.ev.push_back(paramEvent(3, 2875));
      i2.ev.push_back(nootAan(57));
      std::vector<float> bb = blok(p2, i2, u2);
      for (int k = 0; k < 3; ++k) {
        Lijst i3, u3;
        std::vector<float> x = blok(p2, i3, u3);
        bb.insert(bb.end(), x.begin(), x.end());
      }
      bool gelijk = a.size() == bb.size() && !std::memcmp(a.data(), bb.data(), a.size() * sizeof(float));
      double piek = 0;
      for (float x : a) piek = std::fmax(piek, std::fabs(x));
      controle(gelijk && piek > 1e-4, "geluid: Filter via het paneel = Filter als host-event, bit voor bit (" +
                                          std::to_string(a.size()) + " samples, piek " + getal(piek) + ")");
      p2->stop_processing(p2);
      p2->deactivate(p2);
      p2->destroy(p2);
    }

    sleepKnop(pv, 290, 130, 110, 4, true);  // Loslaten 20 px omhoog met Shift: 3990 × 0,1 / 10 = 39,9 ms
    {
      Lijst i2, u2;
      blok(pl, i2, u2);
      toetsGebaar(u2, 4, 489.9, 1e-9, "Shift-slepen Loslaten (10× fijner)");
    }
    dubbelklik(pv, 214, 130);  // Filter terug naar 900
    {
      Lijst i2, u2;
      blok(pl, i2, u2);
      bool eind = !u2.ev.empty() && u2.ev.back().h.type == CLAP_EVENT_PARAM_GESTURE_END;
      double laatste = NAN;
      for (auto& e : u2.ev)
        if (e.h.type == CLAP_EVENT_PARAM_VALUE) laatste = e.p.value;
      controle(eind && laatste == 900 && waarde(pl, 3) == 900, "dubbelklik Filter = standaard 900 Hz");
    }
    {  // de gekozen knop (Filter, laatst aangeraakt) heeft een tape-boog; Loslaten staat op 489,9 ms
      const double w2[4] = {1, 0, 900, 489.9};
      r = beeld(pv);
      bewaar(r, map, @"paneel-na-slepen.png");
      toetsBeeld(r, pv, 1, w2, 2, "na slepen");
    }
    // de host verandert een knop: het paneel volgt (zonder zelf iets te sturen)
    {
      Lijst i2, u2;
      i2.ev.push_back(paramEvent(1, 2.0));
      blok(pl, i2, u2);
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
      const double w2[4] = {2.0, 0, 900, 489.9};
      r = beeld(pv);
      bewaar(r, map, @"paneel-host-zet-noot.png");
      toetsBeeld(r, pv, 1, w2, 2, "host zet Noot op 200 %");
      controle(u2.ev.empty(), "een host-event geeft geen events terug naar de host");
    }

    // --- niet actief: params.flush brengt het naar de host ---
    pl->stop_processing(pl);
    pl->deactivate(pl);
    sleepKnop(pv, 138, 130, 30, 5, false);  // Poort 100 px omhoog = 0,5
    controle(waarde(pl, 2) == 0, "niet actief: het paneel verandert Poort niet zelf");
    {
      Lijst i2, u2;
      clap_input_events_t ie = {&i2, lSize, lGet};
      clap_output_events_t oe = {&u2, lPush};
      pp->flush(pl, &ie, &oe);
      toetsGebaar(u2, 2, 0.5, 1e-12, "niet actief, params.flush");
      controle(std::fabs(waarde(pl, 2) - 0.5) <= 1e-12, "niet actief: get_value(Poort) = 0,5 na flush");
    }

    // --- verbergen, opruimen, opnieuw openen ---
    controle(gui->hide(pl) && pv.hidden, "gui: hide");
    controle(gui->show(pl) && !pv.hidden, "gui: show na hide");
    gui->destroy(pl);
    controle(w.contentView.subviews.count == 0, "gui: destroy haalt de view weg");
    controle(gui->create(pl, CLAP_WINDOW_API_COCOA, false) && gui->set_parent(pl, &cw) &&
                 w.contentView.subviews.count == 1,
             "gui: opnieuw openen");
    controle(gui->get_size(pl, &b, &hh) && b == 560 && hh == 220, "gui: onthoudt de laatste maat");
    gui->destroy(pl);
    pl->destroy(pl);  // ook met een open paneel mag dit; hier al dicht
    c.entry->deinit();

    // --- dezelfde view door de AU van clap-wrapper ---
    toetsAu(map);
  }
  std::printf("%d controles, %d fout\n", gControles, gFouten);
  return gFouten ? 1 : 0;
}
