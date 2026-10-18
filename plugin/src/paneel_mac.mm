// paneel_mac.mm — het frontpaneel als NSView (CLAP gui, api "cocoa"; clap-wrapper geeft hetzelfde paneel aan VST3 en AU).
// Tekent design.md en frontpaneel.svg na: op 560 × 220 valt elke lijn op de plek uit de mockup, groter schaalt alles mee.
//
// Draden: alles hier draait op de mainthread. Het paneel leest de atomaire spiegel en schrijft in de rij (paneel.h);
// het raakt het Ontwerp en de audiothread nooit (valkuil 11 en 14).
//
// De NSView-klasse wordt bij het laden gemaakt met een naam die per binary uniek is: de CLAP, VST3 en AU kunnen samen
// in één host zitten, en twee binaries met dezelfde Objective-C-klasse geven een botsing.
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#include <objc/runtime.h>

#include <clap/clap.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>

#include "paneel.h"

// de lettertypen uit design.md, bij het bouwen in het binary gebakken (plugin/lettertypen.cmake)
extern "C" const unsigned char pb_lettertype_plexmono[];
extern "C" const unsigned long pb_lettertype_plexmono_grootte;
extern "C" const unsigned char pb_lettertype_silkscreen[];
extern "C" const unsigned long pb_lettertype_silkscreen_grootte;

namespace pb_paneel {
namespace {

// kleuren uit design.md
constexpr uint32_t PANEEL = 0x232629, INKT = 0xe7e5dd, TAPE = 0xf2c200, SPOOR = 0x40cbda, GROEF = 0x3a3e42,
                   SCHADUW = 0x0d0e0f;

CGColorRef kleur(uint32_t hex) {
  return CGColorCreateSRGB(((hex >> 16) & 0xff) / 255.0, ((hex >> 8) & 0xff) / 255.0, (hex & 0xff) / 255.0, 1.0);
}

struct Stijl {
  CGColorRef paneel, inkt, tape, spoor, groef, schaduw;
  CTFontRef tekst, kop;  // IBM Plex Mono 11 px, Silkscreen 14 px (op de maat van de mockup; de CTM schaalt)
};

CTFontRef lettertype(const unsigned char* data, unsigned long n, double grootte, CFStringRef reserve) {
  CTFontRef f = nullptr;
  if (n) {
    CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, data, n, nullptr);
    if (dp) {
      CGFontRef cg = CGFontCreateWithDataProvider(dp);
      CGDataProviderRelease(dp);
      if (cg) {
        f = CTFontCreateWithGraphicsFont(cg, grootte, nullptr, nullptr);
        CGFontRelease(cg);
      }
    }
  }
  if (!f) f = CTFontCreateWithName(reserve, grootte, nullptr);
  return f;
}

const Stijl& stijl() {  // één keer per proces, nooit vrijgegeven
  static const Stijl s = {
      kleur(PANEEL), kleur(INKT), kleur(TAPE), kleur(SPOOR), kleur(GROEF), kleur(SCHADUW),
      lettertype(pb_lettertype_plexmono, pb_lettertype_plexmono_grootte, 11, CFSTR("Menlo")),
      lettertype(pb_lettertype_silkscreen, pb_lettertype_silkscreen_grootte, 14, CFSTR("Menlo")),
  };
  return s;
}

// --- tekenen (y omlaag, eenheden van de mockup) ---------------------------------------------------------------
void rechthoek(CGContextRef c, double x, double y, double b, double h, CGColorRef vul) {
  CGContextSetFillColorWithColor(c, vul);
  CGContextFillRect(c, CGRectMake(x, y, b, h));
}

void tekst(CGContextRef c, const char* s, CTFontRef f, CGColorRef kl, double x, double y, bool midden) {
  CFStringRef str = CFStringCreateWithCString(nullptr, s, kCFStringEncodingUTF8);
  if (!str) return;
  const void* sleutels[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
  const void* waarden[] = {f, kl};
  CFDictionaryRef attr = CFDictionaryCreate(nullptr, sleutels, waarden, 2, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef as = CFAttributedStringCreate(nullptr, str, attr);
  CTLineRef regel = CTLineCreateWithAttributedString(as);
  const double b = CTLineGetTypographicBounds(regel, nullptr, nullptr, nullptr);
  CGContextSetTextMatrix(c, CGAffineTransformMakeScale(1, -1));  // de view is omgedraaid (y omlaag)
  CGContextSetTextPosition(c, midden ? x - b / 2 : x, y);          // y = de basislijn, zoals in SVG
  CTLineDraw(regel, c);
  CFRelease(regel);
  CFRelease(as);
  CFRelease(attr);
  CFRelease(str);
}

// een boog rond (cx, cy) van hoek `van` tot `tot` (graden vanaf boven, met de klok mee), als fijne veellijn
void boog(CGContextRef c, double cx, double cy, double r, double van, double tot, CGColorRef kl, double dikte) {
  if (tot <= van) return;  // lengte 0: niets (zoals de mockup bij Poort op 0)
  const int n = (int)std::ceil((tot - van) / 2.0) + 1;
  for (int k = 0; k <= n; ++k) {
    const double a = (van + (tot - van) * k / n) * M_PI / 180.0;
    const double x = cx + r * std::sin(a), y = cy - r * std::cos(a);
    if (k == 0) CGContextMoveToPoint(c, x, y);
    else CGContextAddLineToPoint(c, x, y);
  }
  CGContextSetStrokeColorWithColor(c, kl);
  CGContextSetLineWidth(c, dikte);
  CGContextSetLineCap(c, kCGLineCapButt);
  CGContextSetLineJoin(c, kCGLineJoinRound);
  CGContextStrokePath(c);
}

double knopX(int i) { return KNOP_X0 + KOLOM * i; }

}  // namespace

// --- het paneel ---------------------------------------------------------------------------------------------
struct Paneel {
  Brug brug{};
  NSView* view = nil;
  NSTimer* klok = nil;
  NSMutableArray<NSString*>* tips = nil;  // de view houdt de eigenaar van een tooltip niet vast
  uint32_t breedte = BREEDTE, hoogte = HOOGTE;
  int gekozen = -1;  // de laatst aangeraakte knop: zijn boog is tape
  int sleept = -1;
  double laatsteY = 0;
  double lokaal[MAX_KNOPPEN] = {};     // wat het paneel heeft gestuurd en process nog niet overnam
  uint32_t wacht[MAX_KNOPPEN] = {};    // volgnummer in de rij van de laatste WAARDE van die knop
  bool wachtend[MAX_KNOPPEN] = {};
  double getoond[MAX_KNOPPEN] = {};
  std::deque<Wijziging> buffer;        // wat (nog) niet in de rij paste; niets gaat verloren

  double schaal() const {
    const double s = std::fmin(breedte / (double)MIN_BREEDTE, hoogte / (double)MIN_HOOGTE);
    return s > 0 ? s : 1;
  }

  // de waarde die de knop toont: tijdens slepen en tot process hem overnam de eigen, daarna wat de host ziet
  double toonWaarde(int i) {
    if (sleept == i) return lokaal[i];
    if (wachtend[i]) {
      if ((int32_t)(brug.rij->gelezen() - wacht[i]) >= 0) wachtend[i] = false;
      else return lokaal[i];
    }
    return brug.waarde(brug.ctx, i);
  }

  void pomp() {
    while (!buffer.empty() && brug.rij->zet(buffer.front())) buffer.pop_front();
  }

  void zend(int i, Soort soort, double v = 0) {
    buffer.push_back({(uint32_t)i, (uint32_t)soort, v});
    if (soort == WAARDE) {
      // de rij telt alleen wat dit paneel schreef: dit is het volgnummer van deze wijziging
      wacht[i] = brug.rij->geschreven() + (uint32_t)buffer.size();
      wachtend[i] = true;
      lokaal[i] = v;
    }
    pomp();
    brug.vraagFlush(brug.ctx);
  }

  int knopBij(double x, double y) const {  // in eenheden van de mockup
    for (int i = 0; i < brug.aantal; ++i)
      if (std::fabs(x - knopX(i)) <= KOLOM / 2 && y >= KNOP_Y - 50 && y <= KNOP_Y + 52) return i;
    return -1;
  }

  void neer(double x, double y, long klikken) {
    const double s = schaal();
    const int i = knopBij(x / s, y / s);
    if (i < 0) return;
    gekozen = i;
    if (klikken >= 2) {  // dubbelklik = standaard
      if (sleept >= 0) zend(sleept, EIND);
      sleept = -1;
      zend(i, BEGIN);
      zend(i, WAARDE, brug.knoppen[i].standaard);
      zend(i, EIND);
    } else {
      if (sleept >= 0) zend(sleept, EIND);
      lokaal[i] = toonWaarde(i);
      sleept = i;
      laatsteY = y;
      zend(i, BEGIN);
    }
    [view setNeedsDisplay:YES];
  }

  // 200 px omhoog = het hele bereik, lineair in de CLAP-waarde; Shift = 10× fijner (ook halverwege het slepen)
  void sleep(double y, bool fijn) {
    if (sleept < 0) return;
    const double dy = y - laatsteY;
    laatsteY = y;
    if (dy == 0) return;
    const Knop& k = brug.knoppen[sleept];
    double d = -dy / SLEEP_PX * (k.max - k.min);
    if (fijn) d /= FIJN;
    double v = lokaal[sleept] + d;
    if (v < k.min) v = k.min;
    if (v > k.max) v = k.max;
    if (v == lokaal[sleept]) return;
    zend(sleept, WAARDE, v);
    [view setNeedsDisplay:YES];
  }

  void op() {
    if (sleept < 0) return;
    zend(sleept, EIND);
    sleept = -1;
    [view setNeedsDisplay:YES];
  }

  void tik() {
    pomp();
    bool anders = false;
    for (int i = 0; i < brug.aantal; ++i)
      if (toonWaarde(i) != getoond[i]) anders = true;
    if (anders) [view setNeedsDisplay:YES];
  }

  void teken(CGContextRef c, double b, double h) {
    const Stijl& st = stijl();
    rechthoek(c, 0, 0, b, h, st.paneel);
    CGContextSaveGState(c);
    const double s = schaal();
    CGContextScaleCTM(c, s, s);
    // de doos met harde schaduw, het naamlabel (frontpaneel.svg)
    rechthoek(c, 3, 3, 554, 214, st.schaduw);
    rechthoek(c, 0, 0, 554, 214, st.paneel);
    CGContextSetStrokeColorWithColor(c, st.inkt);
    CGContextSetLineWidth(c, 2);
    CGContextStrokeRect(c, CGRectMake(0, 0, 554, 214));
    rechthoek(c, 24, 18, 129, 24, st.tape);
    tekst(c, "PB TOETSENPAD", st.kop, st.paneel, 30, 35, false);

    for (int i = 0; i < brug.aantal; ++i) {
      const Knop& k = brug.knoppen[i];
      const double cx = knopX(i), cy = KNOP_Y;
      const double v = toonWaarde(i);
      getoond[i] = v;
      double norm = (k.max > k.min) ? (v - k.min) / (k.max - k.min) : 0;
      if (!(norm > 0)) norm = 0;
      if (norm > 1) norm = 1;
      const double hoek = HOEK_BEGIN + HOEK_BEREIK * norm;

      char label[12];
      std::snprintf(label, sizeof label, "%s", k.naam);  // hooguit 11 tekens; de volle naam in de tooltip
      tekst(c, label, st.tekst, st.inkt, cx, cy - 34, true);

      boog(c, cx, cy, BOOG_R, HOEK_BEGIN, HOEK_BEGIN + HOEK_BEREIK, st.groef, 3);
      boog(c, cx, cy, BOOG_R, HOEK_BEGIN, hoek, i == gekozen ? st.tape : st.spoor, 3);

      CGContextAddEllipseInRect(c, CGRectMake(cx - KNOP_R, cy - KNOP_R, 2 * KNOP_R, 2 * KNOP_R));
      CGContextSetFillColorWithColor(c, st.paneel);
      CGContextSetStrokeColorWithColor(c, st.inkt);
      CGContextSetLineWidth(c, 2);
      CGContextDrawPath(c, kCGPathFillStroke);

      const double a = hoek * M_PI / 180.0;
      CGContextMoveToPoint(c, cx, cy);
      CGContextAddLineToPoint(c, cx + WIJZER_R * std::sin(a), cy - WIJZER_R * std::cos(a));
      CGContextSetStrokeColorWithColor(c, st.inkt);
      CGContextSetLineWidth(c, 2);
      CGContextSetLineCap(c, kCGLineCapButt);
      CGContextStrokePath(c);

      char w[64];
      if (!brug.tekst(brug.ctx, i, v, w, sizeof w)) w[0] = 0;
      tekst(c, w, st.tekst, st.inkt, cx, cy + 44, true);
    }
    CGContextRestoreGState(c);
  }

  void zetTips() {
    [view removeAllToolTips];
    const double s = schaal();
    for (int i = 0; i < brug.aantal && i < (int)tips.count; ++i)
      [view addToolTipRect:NSMakeRect((knopX(i) - KOLOM / 2) * s, (KNOP_Y - 50) * s, KOLOM * s, 102 * s)
                     owner:tips[i]
                  userData:nullptr];
  }
};

namespace {

// --- de NSView-klasse, met een naam die per binary uniek is ----------------------------------------------------
Ivar gIvar = nullptr;

Paneel* paneelVan(id self) {
  if (!gIvar || !self) return nullptr;
  return *reinterpret_cast<Paneel**>(static_cast<char*>((__bridge void*)self) + ivar_getOffset(gIvar));
}
void zetPaneel(id self, Paneel* p) {
  *reinterpret_cast<Paneel**>(static_cast<char*>((__bridge void*)self) + ivar_getOffset(gIvar)) = p;
}

void pbTeken(id self, SEL, NSRect) {
  NSView* v = self;
  CGContextRef c = [[NSGraphicsContext currentContext] CGContext];
  if (!c) return;
  const NSRect b = v.bounds;
  if (Paneel* p = paneelVan(self)) p->teken(c, b.size.width, b.size.height);
  else rechthoek(c, 0, 0, b.size.width, b.size.height, stijl().paneel);
}
BOOL pbJa(id, SEL) { return YES; }
BOOL pbJaMet(id, SEL, id) { return YES; }
NSPoint punt(id self, NSEvent* e) { return [(NSView*)self convertPoint:e.locationInWindow fromView:nil]; }
void pbNeer(id self, SEL, NSEvent* e) {
  if (Paneel* p = paneelVan(self)) { const NSPoint q = punt(self, e); p->neer(q.x, q.y, (long)e.clickCount); }
}
void pbSleep(id self, SEL, NSEvent* e) {
  if (Paneel* p = paneelVan(self)) p->sleep(punt(self, e).y, (e.modifierFlags & NSEventModifierFlagShift) != 0);
}
void pbOp(id self, SEL, NSEvent*) {
  if (Paneel* p = paneelVan(self)) p->op();
}

void voeg(Class k, SEL sel, IMP imp) {
  class_addMethod(k, sel, imp, method_getTypeEncoding(class_getInstanceMethod([NSView class], sel)));
}

Class klasse() {
  static Class k = [] {
    char naam[96];
    std::snprintf(naam, sizeof naam, "PBToetsenpadPaneel_%llx", (unsigned long long)(uintptr_t)&gIvar);
    Class c = objc_lookUpClass(naam);
    if (!c) {
      c = objc_allocateClassPair([NSView class], naam, 0);
      class_addIvar(c, "pbPaneel", sizeof(void*), (uint8_t)(sizeof(void*) == 8 ? 3 : 2), "^v");
      voeg(c, @selector(drawRect:), reinterpret_cast<IMP>(pbTeken));
      voeg(c, @selector(isFlipped), reinterpret_cast<IMP>(pbJa));
      voeg(c, @selector(isOpaque), reinterpret_cast<IMP>(pbJa));
      voeg(c, @selector(acceptsFirstMouse:), reinterpret_cast<IMP>(pbJaMet));
      voeg(c, @selector(mouseDown:), reinterpret_cast<IMP>(pbNeer));
      voeg(c, @selector(mouseDragged:), reinterpret_cast<IMP>(pbSleep));
      voeg(c, @selector(mouseUp:), reinterpret_cast<IMP>(pbOp));
      objc_registerClassPair(c);
    }
    gIvar = class_getInstanceVariable(c, "pbPaneel");
    return c;
  }();
  return k;
}

}  // namespace

// --- wat plugin.cpp aanroept ----------------------------------------------------------------------------------
bool paneelApi(const char* api) { return api && !std::strcmp(api, CLAP_WINDOW_API_COCOA); }

Paneel* paneelMaak(const Brug& brug, uint32_t breedte, uint32_t hoogte) {
  Class k = klasse();
  if (!k || !gIvar) return nullptr;
  Paneel* p = new Paneel();
  p->brug = brug;
  p->breedte = breedte;
  p->hoogte = hoogte;
  p->tips = [NSMutableArray array];
  for (int i = 0; i < brug.aantal; ++i) {
    p->getoond[i] = brug.waarde(brug.ctx, i);
    [p->tips addObject:[NSString stringWithUTF8String:brug.knoppen[i].naam]];
  }
  NSView* v = [[k alloc] initWithFrame:NSMakeRect(0, 0, breedte, hoogte)];
  if (!v) {
    delete p;
    return nullptr;
  }
  zetPaneel(v, p);
  p->view = v;
  p->zetTips();
  Paneel* q = p;
  p->klok = [NSTimer timerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer* t) { (void)t; q->tik(); }];
  [[NSRunLoop mainRunLoop] addTimer:p->klok forMode:NSRunLoopCommonModes];
  return p;
}

void paneelWeg(Paneel* p) {
  if (!p) return;
  [p->klok invalidate];
  p->klok = nil;
  if (p->sleept >= 0) p->zend(p->sleept, EIND);  // geen open gebaar achterlaten bij de host
  p->pomp();
  zetPaneel(p->view, nullptr);  // een late muisklik of tekenbeurt doet daarna niets meer
  [p->view removeAllToolTips];
  [p->view removeFromSuperview];
  p->view = nil;
  delete p;
}

bool paneelZetOuder(Paneel* p, void* ouder) {
  if (!p || !ouder) return false;
  NSView* o = (__bridge NSView*)ouder;
  [o addSubview:p->view];
  return true;
}

void paneelZetMaat(Paneel* p, uint32_t breedte, uint32_t hoogte) {
  if (!p) return;
  p->breedte = breedte;
  p->hoogte = hoogte;
  [p->view setFrame:NSMakeRect(0, 0, breedte, hoogte)];
  p->zetTips();
  [p->view setNeedsDisplay:YES];
}

void paneelToon(Paneel* p, bool zichtbaar) {
  if (p) [p->view setHidden:!zichtbaar];
}

}  // namespace pb_paneel
