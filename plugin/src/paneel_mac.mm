// paneel_mac.mm — het frontpaneel als NSView (macOS). De NSView-klasse wordt bij het laden geregistreerd met een
// naam die per geladen binary uniek is: de AU, VST3 en CLAP van deze plugin kunnen in één host-proces zitten, en twee
// Objective-C-klassen met dezelfde naam botsen. Maten zijn punten; de schaal (Retina) vraagt de view aan zijn venster.
// Een NSTimer (±30 Hz, mainthread) ververst de waarden; de AU-host van clap-wrapper biedt geen timer-support.
#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

#include <cstdio>
#include <string>

#include "venster.h"

namespace paneel {

namespace {

class MacVenster;
Ivar g_ivar = nullptr;

MacVenster* van(id self) {
  return g_ivar ? *reinterpret_cast<MacVenster**>(reinterpret_cast<char*>(self) + ivar_getOffset(g_ivar)) : nullptr;
}
void zetVan(id self, MacVenster* v) {
  *reinterpret_cast<MacVenster**>(reinterpret_cast<char*>(self) + ivar_getOffset(g_ivar)) = v;
}

class MacVenster final : public Venster {
 public:
  MacVenster(Paneel* p, NSView* v) : paneel_(p), view_(v) { zetVan(view_, this); }
  ~MacVenster() override {
    stopTimer();
    zetVan(view_, nullptr);
    [view_ removeFromSuperview];
    [view_ release];
  }

  bool zetOuder(const clap_window_t* w) override {
    if (!w || !w->cocoa) return false;
    [(NSView*)w->cocoa addSubview:view_];
    startTimer();  // de AU-wrapper roept show niet aan
    [view_ setNeedsDisplay:YES];
    return true;
  }
  void zetMaat(uint32_t b, uint32_t h) override {
    [view_ setFrameSize:NSMakeSize(b, h)];
    [view_ setNeedsDisplay:YES];
  }
  bool zetSchaal(double) override { return false; }
  void toon(bool ja) override {
    [view_ setHidden:!ja];
    if (ja) startTimer(); else stopTimer();
  }

  void teken() {
    const NSSize m = [view_ bounds].size;
    const double schaal = [view_ window] ? [[view_ window] backingScaleFactor] : 1.0;
    const Beeld& b = paneel_->teken(m.width, m.height, schaal);
    CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);  // de kleuren van design.md zijn sRGB
    CGDataProviderRef bron = CGDataProviderCreateWithData(nullptr, b.pixels(), (size_t)b.breedte() * b.hoogte() * 4, nullptr);
    CGImageRef im = CGImageCreate(b.breedte(), b.hoogte(), 8, 32, (size_t)b.breedte() * 4, rgb,
                                  (CGBitmapInfo)(kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst), bron, nullptr, false,
                                  kCGRenderingIntentDefault);
    CGContextRef c = [[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(c);
    CGContextSetInterpolationQuality(c, kCGInterpolationNone);
    CGContextTranslateCTM(c, 0, m.height);  // de view is geflipt (y omlaag); CGImage tekent met y omhoog
    CGContextScaleCTM(c, 1, -1);
    CGContextDrawImage(c, CGRectMake(0, 0, m.width, m.height), im);
    CGContextRestoreGState(c);
    CGImageRelease(im);
    CGDataProviderRelease(bron);
    CGColorSpaceRelease(rgb);
  }

  void muis(NSEvent* e, int soort) {
    const NSPoint p = [view_ convertPoint:[e locationInWindow] fromView:nil];
    const bool shift = ([e modifierFlags] & NSEventModifierFlagShift) != 0;
    if (soort == 0) paneel_->muisNeer(p.x, p.y, shift, [e clickCount] == 2);
    else if (soort == 1) paneel_->muisSleep(p.x, p.y, shift);
    else paneel_->muisOp();
    [view_ setNeedsDisplay:YES];
  }

 private:
  void startTimer() {
    if (timer_) return;
    Paneel* p = paneel_;
    NSView* v = view_;
    timer_ = [[NSTimer timerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer* t) {
      (void)t;
      if (p->ververs()) [v setNeedsDisplay:YES];
    }] retain];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
  }
  void stopTimer() {
    if (!timer_) return;
    [timer_ invalidate];
    [timer_ release];
    timer_ = nil;
  }

  Paneel* paneel_;
  NSView* view_;
  NSTimer* timer_ = nil;
};

// --- de methoden van de NSView ---------------------------------------------------------------------------------
BOOL ja(id, SEL) { return YES; }
BOOL jaMetEvent(id, SEL, NSEvent*) { return YES; }
void tekenRect(id self, SEL, NSRect) { if (auto* v = van(self)) v->teken(); }
void muisNeer(id self, SEL, NSEvent* e) { if (auto* v = van(self)) v->muis(e, 0); }
void muisSleep(id self, SEL, NSEvent* e) { if (auto* v = van(self)) v->muis(e, 1); }
void muisOp(id self, SEL, NSEvent* e) { if (auto* v = van(self)) v->muis(e, 2); }
void schaalAnders(id self, SEL) { [(NSView*)self setNeedsDisplay:YES]; }

Class viewKlasse() {
  static Class k = nil;
  if (k) return k;
  char naam[96];
  std::snprintf(naam, sizeof naam, "PBDubEchoPaneel_%p", (void*)&viewKlasse);  // uniek per geladen binary
  k = objc_allocateClassPair([NSView class], naam, 0);
  class_addIvar(k, "pbVenster", sizeof(void*), alignof(void*) == 8 ? 3 : 2, "^v");
  const std::string rect = std::string("v@:") + @encode(NSRect);
  class_addMethod(k, @selector(isFlipped), (IMP)ja, "c@:");
  class_addMethod(k, @selector(isOpaque), (IMP)ja, "c@:");
  class_addMethod(k, @selector(acceptsFirstMouse:), (IMP)jaMetEvent, "c@:@");
  class_addMethod(k, @selector(drawRect:), (IMP)tekenRect, rect.c_str());
  class_addMethod(k, @selector(mouseDown:), (IMP)muisNeer, "v@:@");
  class_addMethod(k, @selector(mouseDragged:), (IMP)muisSleep, "v@:@");
  class_addMethod(k, @selector(mouseUp:), (IMP)muisOp, "v@:@");
  class_addMethod(k, @selector(viewDidChangeBackingProperties), (IMP)schaalAnders, "v@:");
  objc_registerClassPair(k);
  g_ivar = class_getInstanceVariable(k, "pbVenster");
  return k;
}

}  // namespace

const char* Venster::api() { return CLAP_WINDOW_API_COCOA; }

Venster* Venster::maak(Paneel* p, const clap_host_t*, uint32_t b, uint32_t h) {
  NSView* v = [[viewKlasse() alloc] initWithFrame:NSMakeRect(0, 0, b, h)];
  if (!v) return nullptr;
  return new MacVenster(p, v);
}

}  // namespace paneel
