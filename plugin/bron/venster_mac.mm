// venster_mac.mm — het paneel in een NSView (macOS), als subview van de view die de host (of clap-wrapper) geeft.
// De NSView-klasse wordt bij het laden geregistreerd met een naam die per geladen binary verschilt: de AU, VST3 en
// CLAP van deze plugin kunnen in één proces zitten, en een gedeelde Objective-C-klassenaam zou dan botsen.
// Een NSTimer (30 Hz, ook tijdens slepen) kijkt of de host waarden veranderde. Alles op de mainthread.
// Zonder ARC (-fno-objc-arc in CMake): retain/release staan er expliciet.
#include "paneel.h"

#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace pb {

namespace {

class VensterMac;
const char* const IVAR = "pbVenster";

VensterMac* vensterVan(id zelf) {
  Ivar iv = class_getInstanceVariable(object_getClass(zelf), IVAR);
  return iv ? *(VensterMac**)((char*)(void*)zelf + ivar_getOffset(iv)) : nullptr;
}
void zetVenster(id zelf, VensterMac* v) {
  Ivar iv = class_getInstanceVariable(object_getClass(zelf), IVAR);
  if (iv) *(VensterMac**)((char*)(void*)zelf + ivar_getOffset(iv)) = v;
}

class VensterMac : public Venster {
 public:
  VensterMac(Paneel& p, NSView* view, uint32_t b, uint32_t h) : paneel_(p), view_(view), b_(b), h_(h) {
    zetVenster(view_, this);
    [view_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    VensterMac* zelf = this;
    timer_ = [[NSTimer timerWithTimeInterval:1.0 / 30
                                     repeats:YES
                                       block:^(NSTimer*) {
                                         if (zelf->paneel_.tik()) [zelf->view_ setNeedsDisplay:YES];
                                       }] retain];
    [[NSRunLoop currentRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
  }
  ~VensterMac() override {
    [timer_ invalidate];
    [timer_ release];
    zetVenster(view_, nullptr);
    [view_ removeFromSuperview];
    [view_ release];
  }

  bool zetOuder(const clap_window_t* ouder) override {
    if (!ouder || !ouder->cocoa) return false;
    NSView* o = (NSView*)ouder->cocoa;
    [view_ setFrame:NSMakeRect(0, 0, b_, h_)];
    [o addSubview:view_];
    [view_ setNeedsDisplay:YES];
    return true;
  }
  bool zetGrootte(uint32_t b, uint32_t h) override {
    b_ = b; h_ = h;
    [view_ setFrameSize:NSMakeSize(b_, h_)];
    [view_ setNeedsDisplay:YES];
    return true;
  }
  bool toon() override {
    [view_ setHidden:NO];
    [view_ setNeedsDisplay:YES];
    return true;
  }
  bool verberg() override {
    [view_ setHidden:YES];
    return true;
  }

  void teken(NSRect) {
    const NSRect r = [view_ bounds];
    if (r.size.width < 1 || r.size.height < 1) return;
    const NSSize px = [view_ convertSizeToBacking:r.size];
    paneel_.zetGrootte(r.size.width, r.size.height, px.width / r.size.width);
    paneel_.teken();
    CGContextRef cg = [[NSGraphicsContext currentContext] CGContext];
    if (!cg) return;
    const size_t b = (size_t)paneel_.pixelB(), h = (size_t)paneel_.pixelH();
    // een kopie: CoreGraphics mag het beeld later pas lezen
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(paneel_.pixels()), (CFIndex)(b * h * 4));
    CGDataProviderRef bron = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef ruimte = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    // 0x00RRGGBB per uint32 (little-endian): xRGB
    CGImageRef beeld = CGImageCreate(b, h, 8, 32, b * 4, ruimte,
                                     kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst, bron,
                                     nullptr, false, kCGRenderingIntentDefault);
    if (beeld) {
      CGContextSetInterpolationQuality(cg, kCGInterpolationNone);
      CGContextDrawImage(cg, NSRectToCGRect(r), beeld);
      CGImageRelease(beeld);
    }
    CGColorSpaceRelease(ruimte);
    CGDataProviderRelease(bron);
    CFRelease(data);
  }
  NSPoint punt(NSEvent* e) {
    NSPoint p = [view_ convertPoint:[e locationInWindow] fromView:nil];
    p.y = [view_ bounds].size.height - p.y;  // de view is niet geflipt; het paneel telt van boven
    return p;
  }
  static bool fijn(NSEvent* e) { return ([e modifierFlags] & NSEventModifierFlagShift) != 0; }
  void muisNeer(NSEvent* e) {
    const NSPoint p = punt(e);
    paneel_.muisNeer(p.x, p.y, fijn(e), [e clickCount] == 2);
    [view_ setNeedsDisplay:YES];
  }
  void muisSleep(NSEvent* e) {
    const NSPoint p = punt(e);
    paneel_.muisSleep(p.x, p.y, fijn(e));
    [view_ setNeedsDisplay:YES];
  }
  void muisOp(NSEvent*) {
    paneel_.muisOp();
    [view_ setNeedsDisplay:YES];
  }

 private:
  Paneel& paneel_;
  NSView* view_;
  NSTimer* timer_ = nil;
  uint32_t b_, h_;
};

// --- de methodes van de NSView-klasse -----------------------------------------------------------------------
void mTeken(id zelf, SEL, NSRect r) {
  if (VensterMac* v = vensterVan(zelf)) v->teken(r);
}
BOOL mJa(id, SEL) { return YES; }
BOOL mJaEvent(id, SEL, NSEvent*) { return YES; }
void mMuisNeer(id zelf, SEL, NSEvent* e) {
  if (VensterMac* v = vensterVan(zelf)) v->muisNeer(e);
}
void mMuisSleep(id zelf, SEL, NSEvent* e) {
  if (VensterMac* v = vensterVan(zelf)) v->muisSleep(e);
}
void mMuisOp(id zelf, SEL, NSEvent* e) {
  if (VensterMac* v = vensterVan(zelf)) v->muisOp(e);
}
void mScherpte(id zelf, SEL) { [(NSView*)zelf setNeedsDisplay:YES]; }

Class paneelKlasse() {
  static Class klasse = nil;
  if (klasse) return klasse;
  char naam[96];
  std::snprintf(naam, sizeof naam, "PBDubEchoPaneel_%llx", (unsigned long long)(uintptr_t)&paneelKlasse);
  if ((klasse = objc_getClass(naam))) return klasse;
  Class k = objc_allocateClassPair([NSView class], naam, 0);
  if (!k) return nil;
  class_addIvar(k, IVAR, sizeof(void*), alignof(void*) == 8 ? 3 : 2, "^v");
  auto voeg = [&](SEL s, IMP imp) {
    class_addMethod(k, s, imp, method_getTypeEncoding(class_getInstanceMethod([NSView class], s)));
  };
  voeg(@selector(drawRect:), (IMP)mTeken);
  voeg(@selector(isOpaque), (IMP)mJa);
  voeg(@selector(acceptsFirstMouse:), (IMP)mJaEvent);
  voeg(@selector(mouseDown:), (IMP)mMuisNeer);
  voeg(@selector(mouseDragged:), (IMP)mMuisSleep);
  voeg(@selector(mouseUp:), (IMP)mMuisOp);
  voeg(@selector(viewDidChangeBackingProperties), (IMP)mScherpte);
  objc_registerClassPair(k);
  klasse = k;
  return klasse;
}

}  // namespace

const char* vensterApi() { return CLAP_WINDOW_API_COCOA; }

Venster* maakVenster(Paneel& paneel, const clap_host_t*, uint32_t b, uint32_t h) {
  Class k = paneelKlasse();
  if (!k) return nullptr;
  NSView* view = [[k alloc] initWithFrame:NSMakeRect(0, 0, b, h)];
  if (!view) return nullptr;
  paneel.zetGrootte(b, h, 1);
  return new VensterMac(paneel, view, b, h);
}

}  // namespace pb
