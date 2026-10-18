// paneel_mac.mm — het frontpaneel als NSView (macOS, CLAP gui-extensie COCOA; clap-wrapper geeft het door aan VST3
// en AUv2). Dun: het model (paneel.cpp) rekent indeling, waarden en muis; dit tekent zijn opdrachten met CoreGraphics
// en CoreText en geeft de muis door. Alles op de mainthread; het paneel leest alleen de atomaire spiegel en stuurt
// wijzigingen via de koppeling (de rij naar process/flush), nooit naar het Ontwerp.
//
// De NSView-klasse wordt bij het eerste gebruik met de objc-runtime gemaakt, met het adres van deze code in de naam:
// de CLAP, de VST3 en de AU hebben elk hun eigen kopie van deze code, en een host die er twee laadt mag geen twee
// klassen met dezelfde naam zien (de runtime kiest er dan willekeurig één).
// Lettertypen: IBM Plex Mono en Silkscreen (SIL OFL 1.1) zitten als bytes in het binary (lettertypen/insluiten.cmake)
// en worden alleen voor dit paneel geladen, niet in het systeem geregistreerd. Lukt dat niet: Menlo.
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include <cmath>
#include <cstdio>

#include "paneel_mac.h"

extern const unsigned char pbLetter_IBMPlexMonoRegular[];
extern const unsigned long pbLetter_IBMPlexMonoRegular_lengte;
extern const unsigned char pbLetter_SilkscreenRegular[];
extern const unsigned long pbLetter_SilkscreenRegular_lengte;

using namespace pb::paneel;

namespace {

CTFontRef maakLetter(const unsigned char* bytes, unsigned long n, double px) {
  CTFontRef f = nullptr;
  if (CFDataRef d = CFDataCreate(nullptr, bytes, (CFIndex)n)) {
    if (CTFontDescriptorRef desc = CTFontManagerCreateFontDescriptorFromData(d)) {
      f = CTFontCreateWithFontDescriptor(desc, px, nullptr);
      CFRelease(desc);
    }
    CFRelease(d);
  }
  if (!f) f = CTFontCreateWithName(CFSTR("Menlo"), px, nullptr);
  return f;
}

CGColorRef maakKleur(Kleur c) { return CGColorCreateSRGB(c.r / 255.0, c.g / 255.0, c.b / 255.0, 1.0); }

// een regel tekst; kleur mag nullptr zijn (alleen meten)
CTLineRef maakRegel(CTFontRef f, const std::string& t, CGColorRef kleur) {
  CFStringRef s = CFStringCreateWithBytes(nullptr, (const UInt8*)t.data(), (CFIndex)t.size(), kCFStringEncodingUTF8,
                                          false);
  if (!s) return nullptr;
  const void* sleutels[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
  const void* waarden[] = {f, kleur};
  CFDictionaryRef attr = CFDictionaryCreate(nullptr, sleutels, waarden, kleur ? 2 : 1, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef as = CFAttributedStringCreate(nullptr, s, attr);
  CTLineRef regel = as ? CTLineCreateWithAttributedString(as) : nullptr;
  if (as) CFRelease(as);
  CFRelease(attr);
  CFRelease(s);
  return regel;
}

Class paneelKlasse();
Ivar vensterIvar = nullptr;

}  // namespace

struct PbVenster {
  CTFontRef letter[2] = {nullptr, nullptr};  // Letter::Mono, Letter::Kop
  Paneel paneel;
  NSView* view = nil;
  CFRunLoopTimerRef timer = nullptr;
  std::vector<NSString*> tips;

  PbVenster(const Koppeling& k, std::vector<Knop> knoppen, std::string naam)
      : paneel(k, std::move(knoppen), std::move(naam),
               [this](Letter l, const std::string& t) { return breedte(l, t); }) {
    letter[0] = maakLetter(pbLetter_IBMPlexMonoRegular, pbLetter_IBMPlexMonoRegular_lengte, LETTER_PX[0]);
    letter[1] = maakLetter(pbLetter_SilkscreenRegular, pbLetter_SilkscreenRegular_lengte, LETTER_PX[1]);
  }
  ~PbVenster() {
    for (NSString* t : tips) [t release];
    for (CTFontRef f : letter)
      if (f) CFRelease(f);
  }

  double breedte(Letter l, const std::string& t) const {
    CTLineRef r = maakRegel(letter[(int)l], t, nullptr);
    if (!r) return 0;
    const double b = CTLineGetTypographicBounds(r, nullptr, nullptr, nullptr);
    CFRelease(r);
    return b;
  }

  void volgMaat() {
    const NSRect b = [view bounds];
    paneel.zetMaat(b.size.width, b.size.height);
  }

  // de volle naam als tooltip boven elke knop
  void zetTips() {
    volgMaat();
    [view removeAllToolTips];
    for (int i = 0; i < paneel.aantal() && i < (int)tips.size(); ++i) {
      const auto r = paneel.knopVlak(i);
      [view addToolTipRect:NSMakeRect(r.x, r.y, r.b, r.h) owner:tips[(size_t)i] userData:nullptr];
    }
  }

  void teken(CGContextRef ctx) {
    volgMaat();
    const NSRect b = [view bounds];
    CGColorRef achter = maakKleur(PANEEL);
    CGContextSetFillColorWithColor(ctx, achter);
    CGContextFillRect(ctx, NSRectToCGRect(b));
    CGColorRelease(achter);

    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, paneel.ox(), paneel.oy());
    CGContextScaleCTM(ctx, paneel.schaal(), paneel.schaal());
    CGContextClipToRect(ctx, CGRectMake(0, 0, ONTWERP_B, ONTWERP_H));  // zoals de viewBox van de mockup
    CGContextSetLineCap(ctx, kCGLineCapButt);
    CGContextSetLineJoin(ctx, kCGLineJoinRound);
    for (const Opdracht& d : paneel.teken()) {
      CGColorRef k = maakKleur(d.kleur);
      switch (d.type) {
        case Opdracht::Vlak:
          CGContextSetFillColorWithColor(ctx, k);
          CGContextFillRect(ctx, CGRectMake(d.x, d.y, d.b, d.h));
          break;
        case Opdracht::Rand:
          CGContextSetStrokeColorWithColor(ctx, k);
          CGContextSetLineWidth(ctx, d.dikte);
          CGContextStrokeRect(ctx, CGRectMake(d.x, d.y, d.b, d.h));
          break;
        case Opdracht::Pad:
          if (d.punten.size() < 2) break;
          CGContextSetStrokeColorWithColor(ctx, k);
          CGContextSetLineWidth(ctx, d.dikte);
          CGContextBeginPath(ctx);
          CGContextMoveToPoint(ctx, d.punten[0].first, d.punten[0].second);
          for (size_t p = 1; p < d.punten.size(); ++p) CGContextAddLineToPoint(ctx, d.punten[p].first, d.punten[p].second);
          CGContextStrokePath(ctx);
          break;
        case Opdracht::Cirkel: {
          const CGRect r = CGRectMake(d.x - d.b, d.y - d.b, 2 * d.b, 2 * d.b);
          CGContextSetFillColorWithColor(ctx, k);
          CGContextFillEllipseInRect(ctx, r);
          CGColorRef rand = maakKleur(d.rand);
          CGContextSetStrokeColorWithColor(ctx, rand);
          CGContextSetLineWidth(ctx, d.dikte);
          CGContextStrokeEllipseInRect(ctx, r);
          CGColorRelease(rand);
          break;
        }
        case Opdracht::Tekst: {
          CTLineRef r = maakRegel(letter[(int)d.letter], d.tekst, k);
          if (!r) break;
          const double w = CTLineGetTypographicBounds(r, nullptr, nullptr, nullptr);
          CGContextSaveGState(ctx);
          CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1, -1));  // de view is omgekeerd (y omlaag)
          CGContextSetTextPosition(ctx, d.midden ? d.x - w / 2 : d.x, d.y);
          CTLineDraw(r, ctx);
          CGContextRestoreGState(ctx);
          CFRelease(r);
          break;
        }
      }
      CGColorRelease(k);
    }
    CGContextRestoreGState(ctx);
  }
};

namespace {

PbVenster*& vensterVan(id self) {
  return *reinterpret_cast<PbVenster**>(reinterpret_cast<char*>(self) + ivar_getOffset(vensterIvar));
}

NSPoint inView(id self, NSEvent* e) { return [(NSView*)self convertPoint:[e locationInWindow] fromView:nil]; }
bool shift(NSEvent* e) { return ([e modifierFlags] & NSEventModifierFlagShift) != 0; }

BOOL ja(id, SEL) { return YES; }
BOOL jaMetEvent(id, SEL, NSEvent*) { return YES; }

void tekenImp(id self, SEL, NSRect) {
  PbVenster* v = vensterVan(self);
  NSGraphicsContext* g = [NSGraphicsContext currentContext];
  if (!v || !g) return;
  v->teken([g CGContext]);
}

void neerImp(id self, SEL, NSEvent* e) {
  PbVenster* v = vensterVan(self);
  if (!v) return;
  v->volgMaat();
  const NSPoint p = inView(self, e);
  v->paneel.muisNeer(p.x, p.y, (int)[e clickCount], shift(e));
  [(NSView*)self setNeedsDisplay:YES];
}

void sleepImp(id self, SEL, NSEvent* e) {
  PbVenster* v = vensterVan(self);
  if (!v) return;
  const NSPoint p = inView(self, e);
  v->paneel.muisSleep(p.x, p.y, shift(e));
  [(NSView*)self setNeedsDisplay:YES];
}

void opImp(id self, SEL, NSEvent*) {
  PbVenster* v = vensterVan(self);
  if (!v) return;
  v->paneel.muisOp();
  [(NSView*)self setNeedsDisplay:YES];
}

void maatImp(id self, SEL cmd, NSSize s) {
  struct objc_super ouder;
  ouder.receiver = self;
  ouder.super_class = [NSView class];
  reinterpret_cast<void (*)(struct objc_super*, SEL, NSSize)>(objc_msgSendSuper)(&ouder, cmd, s);
  if (PbVenster* v = vensterVan(self)) {
    v->zetTips();
    [(NSView*)self setNeedsDisplay:YES];
  }
}

void voegToe(Class c, SEL s, IMP i) {
  class_addMethod(c, s, i, method_getTypeEncoding(class_getInstanceMethod([NSView class], s)));
}

Class paneelKlasse() {
  static Class klasse = nil;
  if (klasse) return klasse;
  char naam[96];
  std::snprintf(naam, sizeof naam, "PBToetsenpadPaneel_%p", (void*)&paneelKlasse);
  Class c = objc_lookUpClass(naam);  // deze code al eens geladen op hetzelfde adres: dezelfde klasse
  if (!c) {
    c = objc_allocateClassPair([NSView class], naam, 0);
    if (!c) return nil;
    class_addIvar(c, "pbVenster", sizeof(PbVenster*), (uint8_t)std::log2(sizeof(PbVenster*)), "^v");
    voegToe(c, @selector(isFlipped), (IMP)ja);  // y omlaag, zoals de mockup
    voegToe(c, @selector(isOpaque), (IMP)ja);
    voegToe(c, @selector(acceptsFirstMouse:), (IMP)jaMetEvent);
    voegToe(c, @selector(drawRect:), (IMP)tekenImp);
    voegToe(c, @selector(mouseDown:), (IMP)neerImp);
    voegToe(c, @selector(mouseDragged:), (IMP)sleepImp);
    voegToe(c, @selector(mouseUp:), (IMP)opImp);
    voegToe(c, @selector(setFrameSize:), (IMP)maatImp);
    objc_registerClassPair(c);
  }
  vensterIvar = class_getInstanceVariable(c, "pbVenster");
  if (!vensterIvar) return nil;
  klasse = c;
  return klasse;
}

void tik(CFRunLoopTimerRef, void* info) {
  PbVenster* v = static_cast<PbVenster*>(info);
  if (v->paneel.tik()) [v->view setNeedsDisplay:YES];
}

}  // namespace

PbVenster* pbVensterMaak(const Koppeling& k, std::vector<Knop> knoppen, std::string naam, uint32_t breed,
                         uint32_t hoog) {
  @autoreleasepool {
    Class c = paneelKlasse();
    if (!c) return nullptr;
    auto* v = new PbVenster(k, std::move(knoppen), std::move(naam));
    for (int i = 0; i < v->paneel.aantal(); ++i)
      v->tips.push_back([[NSString alloc] initWithUTF8String:v->paneel.knop(i).naam.c_str()]);
    v->view = [[c alloc] initWithFrame:NSMakeRect(0, 0, breed, hoog)];
    if (!v->view) {
      delete v;
      return nullptr;
    }
    vensterVan(v->view) = v;
    [v->view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    v->zetTips();
    CFRunLoopTimerContext ctx = {0, v, nullptr, nullptr, nullptr};
    v->timer = CFRunLoopTimerCreate(nullptr, CFAbsoluteTimeGetCurrent() + 1.0 / 30, 1.0 / 30, 0, 0, tik, &ctx);
    if (v->timer) CFRunLoopAddTimer(CFRunLoopGetMain(), v->timer, kCFRunLoopCommonModes);
    return v;
  }
}

void pbVensterWeg(PbVenster* v) {
  if (!v) return;
  @autoreleasepool {
    v->paneel.stop();  // een open gebaar sluiten (de host krijgt GESTURE_END)
    if (v->timer) {
      CFRunLoopTimerInvalidate(v->timer);
      CFRelease(v->timer);
      v->timer = nullptr;
    }
    if (v->view) {
      vensterVan(v->view) = nullptr;
      [v->view removeAllToolTips];
      [v->view removeFromSuperview];
      [v->view release];
      v->view = nil;
    }
    delete v;
  }
}

bool pbVensterOuder(PbVenster* v, void* nsview) {
  if (!v || !v->view || !nsview) return false;
  @autoreleasepool {
    NSView* ouder = (NSView*)nsview;
    [ouder addSubview:v->view];
    [v->view setNeedsDisplay:YES];
  }
  return true;
}

void pbVensterMaat(PbVenster* v, uint32_t breed, uint32_t hoog) {
  if (!v || !v->view) return;
  @autoreleasepool {
    [v->view setFrameSize:NSMakeSize(breed, hoog)];
    [v->view setNeedsDisplay:YES];
  }
}

void pbVensterToon(PbVenster* v, bool zichtbaar) {
  if (!v || !v->view) return;
  [v->view setHidden:zichtbaar ? NO : YES];
  if (zichtbaar) [v->view setNeedsDisplay:YES];
}
