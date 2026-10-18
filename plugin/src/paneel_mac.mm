// paneel_mac.mm — de Cocoa-view van het paneel (CLAP gui, CLAP_WINDOW_API_COCOA; clap-wrapper geeft hem door aan VST3
// en AUv2). De view tekent wat Paneel::teken in een buffer zet en geeft de muis door aan Paneel::muis; een timer (30 Hz)
// kijkt of een getoonde waarde veranderde (automatisering, state) en tekent dan opnieuw. Alles op de mainthread.
//
// De klasse wordt tijdens het draaien aangemaakt met een naam die per geladen binary verschilt: laadt een host de AU en
// de VST3 (of twee versies) in één proces, dan botsen twee klassen met dezelfde naam niet. Handmatig geheugenbeheer
// (geen ARC), zoals clap-wrapper.
#import <Cocoa/Cocoa.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "paneel.h"
#include "paneel_mac.h"

namespace {

struct ViewData {
  pb_paneel::Paneel* paneel = nullptr;
  NSTimer* timer = nil;
  NSString* namen[pb_paneel::MAX_KNOPPEN] = {};  // tooltips (addToolTipRect houdt de eigenaar niet vast)
  std::vector<uint8_t> buf;
};

ptrdiff_t gDataOffset = 0;

ViewData* data(id self) { return *reinterpret_cast<ViewData**>(reinterpret_cast<char*>(self) + gDataOffset); }
void zetData(id self, ViewData* d) { *reinterpret_cast<ViewData**>(reinterpret_cast<char*>(self) + gDataOffset) = d; }

void tooltips(NSView* v, ViewData* d) {
  [v removeAllToolTips];
  for (int k = 0; k < d->paneel->aantal(); ++k) {
    double x, y, b, h;
    d->paneel->knopVlak(k, &x, &y, &b, &h);
    if (d->namen[k]) [v addToolTipRect:NSMakeRect(x, y, b, h) owner:d->namen[k] userData:nullptr];
  }
}

BOOL isFlipped(id, SEL) { return YES; }
BOOL acceptsFirstMouse(id, SEL, NSEvent*) { return YES; }

void drawRect(id self, SEL, NSRect) {
  ViewData* d = data(self);
  if (!d || !d->paneel) return;
  NSView* v = self;
  const NSRect r = [v bounds];
  const double schaal = [v window] ? [[v window] backingScaleFactor] : 1.0;
  const int bw = (int)std::ceil(r.size.width * schaal), bh = (int)std::ceil(r.size.height * schaal);
  if (bw <= 0 || bh <= 0) return;
  d->paneel->maat(r.size.width, r.size.height);
  d->buf.assign((size_t)bw * bh * 4, 0);
  d->paneel->teken(d->buf.data(), bw, bh, schaal);

  CGContextRef c = [[NSGraphicsContext currentContext] CGContext];
  if (!c) return;
  CFDataRef cf = CFDataCreate(nullptr, d->buf.data(), (CFIndex)d->buf.size());  // een kopie: het beeld mag blijven
  CGDataProviderRef dp = CGDataProviderCreateWithCFData(cf);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGImageRef img = CGImageCreate(bw, bh, 8, 32, (size_t)bw * 4, cs,
                                 (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault, dp, nullptr,
                                 false, kCGRenderingIntentDefault);
  if (img) {
    // de view is omgedraaid (y omlaag), een CGImage niet: spiegel terug
    CGContextSaveGState(c);
    CGContextTranslateCTM(c, 0, r.size.height);
    CGContextScaleCTM(c, 1, -1);
    CGContextSetInterpolationQuality(c, kCGInterpolationNone);
    CGContextDrawImage(c, CGRectMake(0, 0, r.size.width, r.size.height), img);
    CGContextRestoreGState(c);
    CGImageRelease(img);
  }
  CGColorSpaceRelease(cs);
  CGDataProviderRelease(dp);
  CFRelease(cf);
}

void muis(id self, NSEvent* e, pb_paneel::Muis soort) {
  ViewData* d = data(self);
  if (!d || !d->paneel) return;
  NSView* v = self;
  const NSPoint p = [v convertPoint:[e locationInWindow] fromView:nil];
  const bool shift = ([e modifierFlags] & NSEventModifierFlagShift) != 0;
  if (soort == pb_paneel::NEER && [e clickCount] >= 2) soort = pb_paneel::DUBBEL;  // dubbelklik = standaard
  if (d->paneel->muis(soort, p.x, p.y, shift)) [v setNeedsDisplay:YES];
}

void mouseDown(id self, SEL, NSEvent* e) { muis(self, e, pb_paneel::NEER); }
void mouseDragged(id self, SEL, NSEvent* e) { muis(self, e, pb_paneel::SLEEP); }
void mouseUp(id self, SEL, NSEvent* e) { muis(self, e, pb_paneel::OP); }

void setFrameSize(id self, SEL cmd, NSSize s) {
  struct objc_super sup = {self, [NSView class]};
  reinterpret_cast<void (*)(struct objc_super*, SEL, NSSize)>(objc_msgSendSuper)(&sup, cmd, s);
  if (ViewData* d = data(self)) {
    if (d->paneel) {
      d->paneel->maat(s.width, s.height);
      tooltips(self, d);
    }
  }
  [(NSView*)self setNeedsDisplay:YES];
}

void viewDidMoveToWindow(id self, SEL cmd) {
  struct objc_super sup = {self, [NSView class]};
  reinterpret_cast<void (*)(struct objc_super*, SEL)>(objc_msgSendSuper)(&sup, cmd);
  [(NSView*)self setNeedsDisplay:YES];  // de backing scale kan anders zijn
}

template <class F>
void methode(Class c, SEL sel, F f, const std::string& types) {
  if (!class_addMethod(c, sel, (IMP)f, types.c_str())) class_replaceMethod(c, sel, (IMP)f, types.c_str());
}

Class viewKlasse() {
  static Class cls = [] {
    char naam[96];
    std::snprintf(naam, sizeof naam, "PBToetsenpadPaneel_%llx", (unsigned long long)(uintptr_t)&gDataOffset);
    Class c = objc_allocateClassPair([NSView class], naam, 0);
    const bool nieuw = c != Nil;
    if (nieuw) {
      class_addIvar(c, "pbData", sizeof(void*), (uint8_t)std::log2(sizeof(void*)), "^v");
    } else {
      c = objc_getClass(naam);  // dezelfde binary op hetzelfde adres opnieuw geladen: de methoden opnieuw zetten
    }
    const std::string B = @encode(BOOL), V = @encode(void), O = @encode(id), S = @encode(SEL);
    methode(c, @selector(isFlipped), isFlipped, B + O + S);
    methode(c, @selector(acceptsFirstMouse:), acceptsFirstMouse, B + O + S + O);
    methode(c, @selector(drawRect:), drawRect, V + O + S + @encode(NSRect));
    methode(c, @selector(mouseDown:), mouseDown, V + O + S + O);
    methode(c, @selector(mouseDragged:), mouseDragged, V + O + S + O);
    methode(c, @selector(mouseUp:), mouseUp, V + O + S + O);
    methode(c, @selector(setFrameSize:), setFrameSize, V + O + S + @encode(NSSize));
    methode(c, @selector(viewDidMoveToWindow), viewDidMoveToWindow, V + O + S);
    if (nieuw) objc_registerClassPair(c);
    gDataOffset = ivar_getOffset(class_getInstanceVariable(c, "pbData"));
    return c;
  }();
  return cls;
}

}  // namespace

void* pbMacMaak(pb_paneel::Paneel* paneel) {
  Class c = viewKlasse();
  NSView* v = [[c alloc] initWithFrame:NSMakeRect(0, 0, paneel->breedte(), paneel->hoogte())];
  if (!v) return nullptr;
  auto* d = new ViewData();
  d->paneel = paneel;
  for (int k = 0; k < paneel->aantal(); ++k) d->namen[k] = [[NSString alloc] initWithUTF8String:paneel->knopNaam(k)];
  zetData(v, d);
  [v setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  tooltips(v, d);

  // 30 Hz: een waarde die van de host komt (automatisering, state) verschijnt zonder muis. Het blok houdt een kale
  // pointer vast (geen retain: anders houden view en timer elkaar in leven); pbMacWeg zet de timer eerst stil.
  void* kaal = v;
  d->timer = [NSTimer timerWithTimeInterval:1.0 / 30
                                    repeats:YES
                                      block:^(NSTimer*) {
                                        NSView* view = (NSView*)kaal;
                                        ViewData* vd = data(view);
                                        if (vd && vd->paneel && vd->paneel->veranderd()) [view setNeedsDisplay:YES];
                                      }];
  [[NSRunLoop mainRunLoop] addTimer:d->timer forMode:NSRunLoopCommonModes];
  return v;
}

void pbMacWeg(void* view) {
  if (!view) return;
  NSView* v = (NSView*)view;
  if (ViewData* d = data(v)) {
    [d->timer invalidate];
    d->timer = nil;
    [v removeAllToolTips];
    for (auto& n : d->namen) {
      [n release];
      n = nil;
    }
    zetData(v, nullptr);
    delete d;
  }
  [v removeFromSuperview];
  [v release];
}

void pbMacOuder(void* view, void* ouder) {
  if (!view || !ouder) return;
  NSView* v = (NSView*)view;
  [(NSView*)ouder addSubview:v];
  [v setNeedsDisplay:YES];
}

void pbMacMaat(void* view, double b, double h) {
  if (!view) return;
  [(NSView*)view setFrameSize:NSMakeSize(b, h)];
}

void pbMacToon(void* view, bool zichtbaar) {
  if (!view) return;
  [(NSView*)view setHidden:!zichtbaar];
}
