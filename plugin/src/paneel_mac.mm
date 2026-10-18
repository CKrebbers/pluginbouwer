// paneel_mac.mm — het frontpaneel op macOS: een NSView die de mockup (frontpaneel.svg) tekent met CoreGraphics en
// CoreText, en de muis doorgeeft aan het PaneelModel. Alles hier loopt op de mainthread.
//
// De NSView-klasse wordt bij het laden gemaakt met een naam die per geladen binary verschilt: een host die de CLAP
// en de VST3 (of twee versies) in één proces laadt, krijgt dan geen botsende Objective-C-klassen.
// Lettertypen (IBM Plex Mono, Silkscreen; OFL) zitten in het binary (lettertypen.c, gemaakt door CMake).
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#include <objc/runtime.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "paneel.h"
#include "tekst.h"

extern "C" {
extern const unsigned char pb_lettertype_plexmono[];
extern const unsigned long pb_lettertype_plexmono_n;
extern const unsigned char pb_lettertype_silkscreen[];
extern const unsigned long pb_lettertype_silkscreen_n;
}

namespace dub {
namespace {

using pb::Ontwerp;

// design.md
enum KleurNaam { PANEEL, INKT, TAPE, SPOOR, GROEF, SCHADUW, N_KLEUREN };
const uint32_t KLEUR_HEX[N_KLEUREN] = {0x232629, 0xe7e5dd, 0xf2c200, 0x40cbda, 0x3a3e42, 0x0d0e0f};

struct Paneel {
  PaneelPlek* plek = nullptr;
  NSView* view = nil;
  CFRunLoopTimerRef timer = nullptr;
  PaneelModel model;
  CTFontRef mono = nullptr, kop = nullptr;
  CGColorRef kleur[N_KLEUREN] = {};
  NSString* tip[N_KNOPPEN] = {};
};

ptrdiff_t ivarPlek = 0;
Paneel* paneelVan(id self) { return *reinterpret_cast<Paneel**>(reinterpret_cast<char*>(self) + ivarPlek); }
void zetPaneel(id self, Paneel* p) { *reinterpret_cast<Paneel**>(reinterpret_cast<char*>(self) + ivarPlek) = p; }

CTFontRef maakLettertype(const unsigned char* d, unsigned long n, double grootte) {
  CFDataRef data = CFDataCreate(nullptr, d, (CFIndex)n);
  CTFontDescriptorRef desc = data ? CTFontManagerCreateFontDescriptorFromData(data) : nullptr;
  if (data) CFRelease(data);
  CTFontRef f = desc ? CTFontCreateWithFontDescriptor(desc, grootte, nullptr)
                     : CTFontCreateWithName(CFSTR("Menlo"), grootte, nullptr);
  if (desc) CFRelease(desc);
  return f;
}

// tekst op de basislijn y; midden = text-anchor middle
void tekst(CGContextRef c, CTFontRef f, CGColorRef kleur, const std::string& s, double x, double y, bool midden) {
  CFStringRef str = CFStringCreateWithCString(nullptr, s.c_str(), kCFStringEncodingUTF8);
  if (!str) return;
  const void* sleutels[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
  const void* waarden[] = {f, kleur};
  CFDictionaryRef attr = CFDictionaryCreate(nullptr, sleutels, waarden, 2, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef as = CFAttributedStringCreate(nullptr, str, attr);
  CTLineRef regel = CTLineCreateWithAttributedString(as);
  const double b = CTLineGetTypographicBounds(regel, nullptr, nullptr, nullptr);
  CGContextSaveGState(c);
  CGContextSetTextMatrix(c, CGAffineTransformMakeScale(1, -1));  // de view is flipped (y omlaag)
  CGContextSetTextPosition(c, midden ? x - b / 2 : x, y);
  CTLineDraw(regel, c);
  CGContextRestoreGState(c);
  CFRelease(regel);
  CFRelease(as);
  CFRelease(attr);
  CFRelease(str);
}

void vlak(CGContextRef c, CGColorRef k, double x, double y, double b, double h) {
  CGContextSetFillColorWithColor(c, k);
  CGContextFillRect(c, CGRectMake(x, y, b, h));
}

// boog van −135° tot de hoek van t (0..1), straal BOOG_R, rond (cx, cy)
void boog(CGContextRef c, CGColorRef k, double cx, double cy, double t) {
  if (t <= 0) return;
  CGContextSetStrokeColorWithColor(c, k);
  CGContextSetLineWidth(c, 3);
  CGContextBeginPath(c);
  // hoekVoor telt vanaf boven met de klok mee; CG vanaf +x, en met y omlaag is oplopend ook met de klok mee
  CGContextAddArc(c, cx, cy, BOOG_R, hoekVoor(0) - M_PI / 2, hoekVoor(t) - M_PI / 2, 0);
  CGContextStrokePath(c);
}

void teken(Paneel* p, CGContextRef c, double breedte, double hoogte) {
  CGColorRef* k = p->kleur;
  vlak(c, k[PANEEL], 0, 0, breedte, hoogte);
  const Schaal sc = schaalVoor(breedte, hoogte);
  CGContextSaveGState(c);
  CGContextTranslateCTM(c, sc.ox, sc.oy);
  CGContextScaleCTM(c, sc.s, sc.s);
  CGContextClipToRect(c, CGRectMake(0, 0, ONTWERP_B, ONTWERP_H));
  CGContextSetLineCap(c, kCGLineCapButt);

  // harde schaduw, paneel met rand van 2 px, naamlabel op tape
  vlak(c, k[SCHADUW], 3, 3, PANEEL_B, PANEEL_H);
  vlak(c, k[PANEEL], 0, 0, PANEEL_B, PANEEL_H);
  CGContextSetStrokeColorWithColor(c, k[INKT]);
  CGContextSetLineWidth(c, 2);
  CGContextStrokeRect(c, CGRectMake(0, 0, PANEEL_B, PANEEL_H));
  vlak(c, k[TAPE], 24, 18, 111, 24);
  tekst(c, p->kop, k[PANEEL], "PB DUB-ECHO", 30, 35, false);

  for (int i = 0; i < N_KNOPPEN; ++i) {
    const double cx = KNOP_X0 + KNOP_DX * i, cy = KNOP_Y;
    const double v = p->model.getoond[i];
    const double t = genormaliseerd(i, v);
    tekst(c, p->mono, k[INKT], LABELS[i], cx, cy + LABEL_DY, true);
    boog(c, k[GROEF], cx, cy, 1.0);
    boog(c, k[i == p->model.gekozen ? TAPE : SPOOR], cx, cy, t);
    CGContextSetFillColorWithColor(c, k[PANEEL]);
    CGContextSetStrokeColorWithColor(c, k[INKT]);
    CGContextSetLineWidth(c, 2);
    const CGRect knop = CGRectMake(cx - KNOP_R, cy - KNOP_R, 2 * KNOP_R, 2 * KNOP_R);
    CGContextFillEllipseInRect(c, knop);
    CGContextStrokeEllipseInRect(c, knop);
    const double a = hoekVoor(t);
    CGContextBeginPath(c);
    CGContextMoveToPoint(c, cx, cy);
    CGContextAddLineToPoint(c, cx + WIJZER_R * std::sin(a), cy - WIJZER_R * std::cos(a));
    CGContextStrokePath(c);
    tekst(c, p->mono, k[INKT], waardeNaarTekst(Ontwerp::PARAMS[i].eenheid, v), cx, cy + WAARDE_DY, true);
  }
  CGContextRestoreGState(c);
}

void zetTips(Paneel* p) {
  [p->view removeAllToolTips];
  const NSRect b = [p->view bounds];
  const Schaal sc = schaalVoor(b.size.width, b.size.height);
  for (int i = 0; i < N_KNOPPEN; ++i) {
    const double x = KNOP_X0 + KNOP_DX * i - KNOP_DX / 2, y = KNOP_Y - 50;
    const NSRect r = NSMakeRect(sc.ox + x * sc.s, sc.oy + y * sc.s, KNOP_DX * sc.s, 110 * sc.s);
    [p->view addToolTipRect:r owner:p->tip[i] userData:nullptr];
  }
}

// --- de NSView-methoden -------------------------------------------------------------------------------------
BOOL isFlippedImp(id, SEL) { return YES; }
BOOL acceptsFirstMouseImp(id, SEL, NSEvent*) { return YES; }

void drawRectImp(id self, SEL, NSRect) {
  Paneel* p = paneelVan(self);
  if (!p) return;
  NSGraphicsContext* gc = [NSGraphicsContext currentContext];
  if (!gc) return;
  const NSRect b = [(NSView*)self bounds];
  teken(p, [gc CGContext], b.size.width, b.size.height);
}

void muis(id self, NSEvent* e, int soort) {
  Paneel* p = paneelVan(self);
  if (!p) return;
  NSView* v = (NSView*)self;
  const NSPoint q = [v convertPoint:[e locationInWindow] fromView:nil];  // flipped: y omlaag
  bool anders = false;
  if (soort == 0) {
    const NSRect b = [v bounds];
    const Schaal sc = schaalVoor(b.size.width, b.size.height);
    anders = p->model.muisNeer((q.x - sc.ox) / sc.s, (q.y - sc.oy) / sc.s, q.y, (int)[e clickCount]);
  } else if (soort == 1) {
    anders = p->model.muisSleep(q.y, ([e modifierFlags] & NSEventModifierFlagShift) != 0);
  } else {
    anders = p->model.muisOp();
  }
  if (anders) [v setNeedsDisplay:YES];
}
void mouseDownImp(id self, SEL, NSEvent* e) { muis(self, e, 0); }
void mouseDraggedImp(id self, SEL, NSEvent* e) { muis(self, e, 1); }
void mouseUpImp(id self, SEL, NSEvent* e) { muis(self, e, 2); }

Class viewKlasse() {
  static Class klasse = [] {
    Class basis = [NSView class];
    Class k = nil;
    for (int n = 0; !k && n < 1000; ++n) {
      char naam[96];
      std::snprintf(naam, sizeof naam, "PBDubEchoPaneel_%llx_%d", (unsigned long long)(uintptr_t)&ivarPlek, n);
      k = objc_allocateClassPair(basis, naam, 0);
    }
    if (!k) return (Class)nil;
    static_assert(sizeof(void*) == 8, "64 bit");
    class_addIvar(k, "pbPaneel", sizeof(void*), 3, "^v");
    auto typen = [&](SEL s) { return method_getTypeEncoding(class_getInstanceMethod(basis, s)); };
    class_addMethod(k, @selector(isFlipped), (IMP)isFlippedImp, typen(@selector(isFlipped)));
    class_addMethod(k, @selector(acceptsFirstMouse:), (IMP)acceptsFirstMouseImp, typen(@selector(acceptsFirstMouse:)));
    class_addMethod(k, @selector(drawRect:), (IMP)drawRectImp, typen(@selector(drawRect:)));
    class_addMethod(k, @selector(mouseDown:), (IMP)mouseDownImp, typen(@selector(mouseDown:)));
    class_addMethod(k, @selector(mouseDragged:), (IMP)mouseDraggedImp, typen(@selector(mouseDragged:)));
    class_addMethod(k, @selector(mouseUp:), (IMP)mouseUpImp, typen(@selector(mouseUp:)));
    objc_registerClassPair(k);
    ivarPlek = ivar_getOffset(class_getInstanceVariable(k, "pbPaneel"));
    return k;
  }();
  return klasse;
}

void tik(CFRunLoopTimerRef, void* info) {
  auto* p = static_cast<Paneel*>(info);
  if (p->model.ververs()) [p->view setNeedsDisplay:YES];
}

void vraagFlush(void* ctx) {
  auto* plek = static_cast<PaneelPlek*>(ctx);
  if (plek->hostParams && plek->hostParams->request_flush) plek->hostParams->request_flush(plek->host);
}

void sluit(PaneelPlek* plek) {
  auto* p = static_cast<Paneel*>(plek->paneel);
  if (!p) return;
  plek->paneel = nullptr;
  if (p->timer) {
    CFRunLoopTimerInvalidate(p->timer);
    CFRelease(p->timer);
  }
  if (p->view) {
    zetPaneel(p->view, nullptr);  // een host die de view nog vasthoudt, raakt het paneel niet meer
    [p->view removeAllToolTips];
    [p->view removeFromSuperview];
    [p->view release];
  }
  for (auto& t : p->tip) [t release];
  for (auto& k : p->kleur) if (k) CGColorRelease(k);
  if (p->mono) CFRelease(p->mono);
  if (p->kop) CFRelease(p->kop);
  delete p;
}

Paneel* geopend(const clap_plugin_t* pl) {
  PaneelPlek* plek = paneelPlek(pl);
  return plek ? static_cast<Paneel*>(plek->paneel) : nullptr;
}

// --- de gui-extensie ------------------------------------------------------------------------------------------
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool zwevend) {
  return !zwevend && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* zwevend) {
  *api = CLAP_WINDOW_API_COCOA;
  *zwevend = false;
  return true;
}
bool guiCreate(const clap_plugin_t* pl, const char* api, bool zwevend) {
  if (!guiIsApiSupported(pl, api, zwevend)) return false;
  PaneelPlek* plek = paneelPlek(pl);
  Class klasse = viewKlasse();
  if (!plek || !klasse) return false;
  sluit(plek);  // de AU vraagt soms een tweede view zonder de eerste te sluiten
  auto* p = new Paneel;
  p->plek = plek;
  p->model.kopie = plek->kopie;
  p->model.bus = plek->bus;
  p->model.vraagFlush = vraagFlush;
  p->model.ctx = plek;
  p->model.begin();
  p->mono = maakLettertype(pb_lettertype_plexmono, pb_lettertype_plexmono_n, 11);
  p->kop = maakLettertype(pb_lettertype_silkscreen, pb_lettertype_silkscreen_n, 14);
  for (int i = 0; i < N_KLEUREN; ++i) {
    const uint32_t h = KLEUR_HEX[i];
    p->kleur[i] = CGColorCreateSRGB(((h >> 16) & 255) / 255.0, ((h >> 8) & 255) / 255.0, (h & 255) / 255.0, 1.0);
  }
  for (int i = 0; i < N_KNOPPEN; ++i) {
    const std::string s = std::string(Ontwerp::PARAMS[i].naam) + "\n" + Ontwerp::PARAMS[i].uitleg;
    p->tip[i] = [[NSString alloc] initWithUTF8String:s.c_str()];
  }
  p->view = [(NSView*)[klasse alloc] initWithFrame:NSMakeRect(0, 0, plek->breedte, plek->hoogte)];
  zetPaneel(p->view, p);
  [p->view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  zetTips(p);
  CFRunLoopTimerContext ctx = {0, p, nullptr, nullptr, nullptr};
  p->timer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.03, 1.0 / 30, 0, 0, tik, &ctx);
  if (p->timer) CFRunLoopAddTimer(CFRunLoopGetMain(), p->timer, kCFRunLoopCommonModes);
  plek->paneel = p;
  return true;
}
void guiDestroy(const clap_plugin_t* pl) {
  if (PaneelPlek* plek = paneelPlek(pl)) sluit(plek);  // mag twee keer (de AU van clap-wrapper doet dat)
}
bool guiSetScale(const clap_plugin_t*, double) { return false; }  // Cocoa rekent in punten
bool guiGetSize(const clap_plugin_t* pl, uint32_t* b, uint32_t* h) {
  PaneelPlek* plek = paneelPlek(pl);
  if (!plek) return false;
  *b = plek->breedte;
  *h = plek->hoogte;
  return true;
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* r) {
  r->can_resize_horizontally = true;
  r->can_resize_vertically = true;
  r->preserve_aspect_ratio = false;
  r->aspect_ratio_width = BEGIN_B;
  r->aspect_ratio_height = BEGIN_H;
  return true;
}
bool guiAdjustSize(const clap_plugin_t*, uint32_t* b, uint32_t* h) {
  if (*b < (uint32_t)MIN_B) *b = MIN_B;
  if (*h < (uint32_t)MIN_H) *h = MIN_H;
  return true;
}
bool guiSetSize(const clap_plugin_t* pl, uint32_t b, uint32_t h) {
  PaneelPlek* plek = paneelPlek(pl);
  if (!plek) return false;
  guiAdjustSize(pl, &b, &h);
  plek->breedte = b;
  plek->hoogte = h;
  if (Paneel* p = geopend(pl)) {
    [p->view setFrameSize:NSMakeSize(b, h)];
    zetTips(p);
    [p->view setNeedsDisplay:YES];
  }
  return true;
}
bool guiSetParent(const clap_plugin_t* pl, const clap_window_t* w) {
  Paneel* p = geopend(pl);
  if (!p || !w || !w->api || std::strcmp(w->api, CLAP_WINDOW_API_COCOA) != 0 || !w->cocoa) return false;
  NSView* ouder = (NSView*)w->cocoa;
  [ouder addSubview:p->view];
  return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* pl) {
  Paneel* p = geopend(pl);
  if (!p) return false;
  [p->view setHidden:NO];
  return true;
}
bool guiHide(const clap_plugin_t* pl) {
  Paneel* p = geopend(pl);
  if (!p) return false;
  [p->view setHidden:YES];
  return true;
}

const clap_plugin_gui_t GUI = {guiIsApiSupported, guiGetPreferredApi, guiCreate,     guiDestroy,      guiSetScale,
                               guiGetSize,        guiCanResize,       guiGetResizeHints, guiAdjustSize, guiSetSize,
                               guiSetParent,      guiSetTransient,    guiSuggestTitle, guiShow,         guiHide};

}  // namespace

const clap_plugin_gui_t* paneelGui() { return &GUI; }

}  // namespace dub
