// paneel_mac.mm — het frontpaneel als NSView (CoreGraphics + CoreText), voor de CLAP gui-extensie met api "cocoa".
// clap-wrapper hangt deze view in de AU (kAudioUnitProperty_CocoaUI) en in de VST3 (IPlugView, "NSView").
// Draait alleen op de main thread. De view leest de waarden via de Bediening (atomische hostkopie) en stuurt
// gebaar/waarde terug via de Bediening; hij raakt de DSP en de audiothread nooit aan.
// Gecompileerd met ARC (-fobjc-arc, zie CMakeLists.txt).
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include <cstdio>
#include <cstring>

#include "paneel_mac.h"

// de lettertypen zitten in het binary (lettertypen.cpp wordt bij het configureren gemaakt uit plugin/ui/lettertypen/)
extern const unsigned char PB_LETTER_SILKSCREEN[];
extern const unsigned long PB_LETTER_SILKSCREEN_N;
extern const unsigned char PB_LETTER_PLEXMONO[];
extern const unsigned long PB_LETTER_PLEXMONO_N;

using namespace pb_ui;

namespace {

CTFontDescriptorRef beschrijving(const unsigned char* data, unsigned long n) {
  CFDataRef d = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, data, (CFIndex)n, kCFAllocatorNull);
  if (!d) return nullptr;
  CTFontDescriptorRef f = CTFontManagerCreateFontDescriptorFromData(d);
  CFRelease(d);
  return f;  // blijft voor de duur van het proces bestaan (één keer per lettertype)
}

NSFont* letter(bool kop, double grootte) {
  static CTFontDescriptorRef silkscreen = beschrijving(PB_LETTER_SILKSCREEN, PB_LETTER_SILKSCREEN_N);
  static CTFontDescriptorRef plex = beschrijving(PB_LETTER_PLEXMONO, PB_LETTER_PLEXMONO_N);
  CTFontDescriptorRef d = kop ? silkscreen : plex;
  if (grootte < 1) grootte = 1;
  if (d) {
    CTFontRef f = CTFontCreateWithFontDescriptor(d, grootte, nullptr);
    if (f) return (__bridge_transfer NSFont*)f;
  }
  return [NSFont monospacedSystemFontOfSize:grootte weight:NSFontWeightRegular];  // valt terug, maar zou niet moeten
}

NSColor* kleur(Kleur k, double alfa = 1) { return [NSColor colorWithSRGBRed:k.r green:k.g blue:k.b alpha:alfa]; }
void zetVul(CGContextRef c, Kleur k, double alfa = 1) { CGContextSetRGBFillColor(c, k.r, k.g, k.b, alfa); }
void zetLijn(CGContextRef c, Kleur k, double alfa = 1) { CGContextSetRGBStrokeColor(c, k.r, k.g, k.b, alfa); }

// rechthoek met rand 2 px en een harde schaduw (3 px 3 px 0), alles in hele schaal
void blok(CGContextRef c, Vak r, double s, Kleur vul) {
  zetVul(c, SCHADUW);
  CGContextFillRect(c, CGRectMake(r.x + SCHADUW_DX * s, r.y + SCHADUW_DY * s, r.b, r.h));
  zetVul(c, vul);
  CGContextFillRect(c, CGRectMake(r.x, r.y, r.b, r.h));
  const double w = RAND * s;
  zetLijn(c, INKT);
  CGContextSetLineWidth(c, w);
  CGContextStrokeRect(c, CGRectMake(r.x + w / 2, r.y + w / 2, r.b - w, r.h - w));
}

// een boog als veelhoek: hoek 0 = omhoog, met de klok mee (y omlaag). Geen twijfel over de richting van CGContextAddArc.
void boog(CGContextRef c, double cx, double cy, double r, double van, double tot) {
  const int stappen = 96;
  for (int k = 0; k <= stappen; ++k) {
    const double a = van + (tot - van) * k / stappen;
    const double x = cx + r * std::sin(a), y = cy - r * std::cos(a);
    if (k == 0) CGContextMoveToPoint(c, x, y);
    else CGContextAddLineToPoint(c, x, y);
  }
  CGContextStrokePath(c);
}

void tekst(NSString* t, NSFont* f, Kleur k, Vak r, bool midden = true) {
  NSDictionary* a = @{NSFontAttributeName : f, NSForegroundColorAttributeName : kleur(k)};
  NSAttributedString* s = [[NSAttributedString alloc] initWithString:t attributes:a];
  const NSSize m = [s size];
  const double x = midden ? r.x + (r.b - m.width) / 2 : r.x + r.b - m.width;
  const double y = r.y + (r.h - m.height) / 2;
  [s drawAtPoint:NSMakePoint(x, y)];
}

}  // namespace

@interface PBDubEchoPaneel : NSView <NSViewToolTipOwner> {
 @public
  Bediening* bediening;
  Indeling ind;
  double lokaal[16];   // wat de knop toont tijdens slepen en tot de audiothread het verwerkte
  double getekend[16]; // wat de laatste keer getekend is (de timer tekent alleen opnieuw als iets veranderde)
  int gekozen;         // de laatst aangeraakte knop: zijn boog is tape-geel
  int sleept;          // knop onder de muis tijdens slepen, of -1
  double startStand;
  NSPoint startPunt;
  bool startFijn;
  bool begonnen;       // het gebaar is bij de host begonnen (pas bij de eerste beweging: een klik alleen meldt niets)
  NSTimer* timer;
}
- (double)toon:(int)k;
- (void)herindeel;
- (void)ververs;
@end

@implementation PBDubEchoPaneel

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }

- (double)toon:(int)k {
  if (!bediening) return 0;
  if (sleept == k || bediening->wachtend(k)) return lokaal[k];
  lokaal[k] = bediening->waarde(k);
  return lokaal[k];
}

- (void)herindeel {
  const NSSize m = self.bounds.size;
  ind = indeling(m.width, m.height, bediening ? bediening->aantal() : 0);
  [self removeAllToolTips];
  for (int k = 0; bediening && k < ind.n; ++k) {
    const Vak r = ind.cel[k];
    [self addToolTipRect:NSMakeRect(r.x, r.y, r.b, r.h) owner:self userData:(void*)(intptr_t)k];
  }
}

- (NSString*)view:(NSView*)v stringForToolTip:(NSToolTipTag)t point:(NSPoint)p userData:(void*)d {
  const int k = (int)(intptr_t)d;
  if (!bediening || k < 0 || k >= ind.n) return @"";
  return [NSString stringWithUTF8String:bediening->knop(k).uitleg];
}

- (void)setFrameSize:(NSSize)m {
  [super setFrameSize:m];
  [self herindeel];
  [self setNeedsDisplay:YES];
}

- (void)ververs {
  if (!bediening) return;
  bool anders = false;
  for (int k = 0; k < ind.n; ++k) {
    const double v = [self toon:k];
    if (!(v == getekend[k])) anders = true;
  }
  if (anders) [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty {
  CGContextRef c = [[NSGraphicsContext currentContext] CGContext];
  const NSRect b = self.bounds;
  zetVul(c, PANEEL);
  CGContextFillRect(c, b);
  if (!bediening) return;
  const double s = ind.schaal;

  // naamlabel (tape) en een regel uitleg
  blok(c, ind.naamlabel, s, TAPE);
  tekst(@"DUB-ECHO", letter(true, ind.kopGrootte), PANEEL, ind.naamlabel);
  const Vak onder = {ind.rij.x, ind.naamlabel.y, ind.rij.b, ind.naamlabel.h};
  tekst(@"tape-echo voor dub", letter(false, ind.onderGrootte), INKT, onder, false);

  // de rij met acht draaiknoppen
  blok(c, ind.rij, s, PANEEL);
  NSFont* naamLetter = letter(true, ind.naamGrootte);
  NSFont* waardeLetter = letter(false, ind.waardeGrootte);
  CGContextSetLineJoin(c, kCGLineJoinRound);
  for (int k = 0; k < ind.n; ++k) {
    const Knop kn = bediening->knop(k);
    const double v = [self toon:k];
    getekend[k] = v;
    const double stand = naarStand(kn, v);
    const double cx = ind.knopX[k], cy = ind.knopY[k], r = ind.knopR;

    NSString* naam = [[NSString stringWithUTF8String:kn.naam] uppercaseString];
    tekst(naam, naamLetter, INKT, ind.naam[k]);

    // spoor van de boog, dan de stand
    CGContextSetLineCap(c, kCGLineCapButt);
    CGContextSetLineWidth(c, 4 * s);
    zetLijn(c, INKT, 0.16);
    boog(c, cx, cy, r, hoekVan(0), hoekVan(1));
    if (stand > 0.0005) {
      zetLijn(c, k == gekozen ? TAPE : SPOOR);
      boog(c, cx, cy, r, hoekVan(0), hoekVan(stand));
    }

    // de knop zelf: rond, rand 2 px, harde schaduw
    const double kr = r - 9 * s;
    zetVul(c, SCHADUW);
    CGContextFillEllipseInRect(c, CGRectMake(cx - kr + SCHADUW_DX * s, cy - kr + SCHADUW_DY * s, 2 * kr, 2 * kr));
    zetVul(c, PANEEL);
    CGContextFillEllipseInRect(c, CGRectMake(cx - kr, cy - kr, 2 * kr, 2 * kr));
    zetLijn(c, INKT);
    CGContextSetLineWidth(c, RAND * s);
    CGContextStrokeEllipseInRect(c, CGRectMake(cx - kr + s, cy - kr + s, 2 * kr - 2 * s, 2 * kr - 2 * s));
    const double a = hoekVan(stand);
    CGContextSetLineCap(c, kCGLineCapSquare);
    CGContextMoveToPoint(c, cx + 0.25 * kr * std::sin(a), cy - 0.25 * kr * std::cos(a));
    CGContextAddLineToPoint(c, cx + 0.72 * kr * std::sin(a), cy - 0.72 * kr * std::cos(a));
    CGContextStrokePath(c);

    char t[64];
    bediening->alsTekst(k, v, t, sizeof t);
    tekst([NSString stringWithUTF8String:t], waardeLetter, INKT, ind.waarde[k]);
  }
}

// --- muis ------------------------------------------------------------------------------------------------------
- (NSPoint)punt:(NSEvent*)e { return [self convertPoint:[e locationInWindow] fromView:nil]; }

- (void)mouseDown:(NSEvent*)e {
  if (!bediening) return;
  const NSPoint p = [self punt:e];
  const int k = knopOnder(ind, p.x, p.y);
  if (k < 0) return;
  gekozen = k;
  const Knop kn = bediening->knop(k);
  if (e.clickCount == 2) {  // dubbelklik = standaard
    lokaal[k] = kn.standaard;
    bediening->gebaarBegin(k);
    bediening->zet(k, kn.standaard);
    bediening->gebaarEinde(k);
    [self setNeedsDisplay:YES];
    return;
  }
  sleept = k;
  startStand = naarStand(kn, [self toon:k]);
  startPunt = p;
  startFijn = (e.modifierFlags & NSEventModifierFlagShift) != 0;
  begonnen = false;
  [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)e {
  if (!bediening || sleept < 0) return;
  const int k = sleept;
  const Knop kn = bediening->knop(k);
  const NSPoint p = [self punt:e];
  const bool fijn = (e.modifierFlags & NSEventModifierFlagShift) != 0;
  if (fijn != startFijn) {  // shift tijdens het slepen: verder vanaf hier, zonder sprong
    startStand = naarStand(kn, lokaal[k]);
    startPunt = p;
    startFijn = fijn;
  }
  const double v = uitStand(kn, sleepStand(startStand, p.x - startPunt.x, startPunt.y - p.y, fijn));
  if (v != lokaal[k]) {
    if (!begonnen) bediening->gebaarBegin(k);
    begonnen = true;
    lokaal[k] = v;
    bediening->zet(k, v);
    [self setNeedsDisplay:YES];
  }
}

- (void)mouseUp:(NSEvent*)e {
  if (!bediening || sleept < 0) return;
  if (begonnen) bediening->gebaarEinde(sleept);
  begonnen = false;
  sleept = -1;
}

- (void)scrollWheel:(NSEvent*)e {
  if (!bediening || sleept >= 0) return;
  const NSPoint p = [self punt:e];
  const int k = knopOnder(ind, p.x, p.y);
  if (k < 0) return;
  double d = e.scrollingDeltaY;
  if (!e.hasPreciseScrollingDeltas) d *= 4;
  if (d == 0) return;
  const Knop kn = bediening->knop(k);
  const bool fijn = (e.modifierFlags & NSEventModifierFlagShift) != 0;
  const double v = uitStand(kn, sleepStand(naarStand(kn, [self toon:k]), 0, d, fijn));
  gekozen = k;
  lokaal[k] = v;
  bediening->gebaarBegin(k);
  bediening->zet(k, v);
  bediening->gebaarEinde(k);
  [self setNeedsDisplay:YES];
}

@end

namespace pb_ui {

void* maakView(Bediening* b, double breedte, double hoogte) {
  PBDubEchoPaneel* v = [[PBDubEchoPaneel alloc] initWithFrame:NSMakeRect(0, 0, breedte, hoogte)];
  if (!v) return nullptr;
  v->bediening = b;
  v->gekozen = -1;
  v->sleept = -1;
  v->begonnen = false;
  for (int k = 0; k < 16; ++k) v->lokaal[k] = v->getekend[k] = 0;
  for (int k = 0; b && k < b->aantal() && k < 16; ++k) v->lokaal[k] = b->waarde(k);
  [v herindeel];
  // 30 keer per seconde kijken of de host een waarde veranderde (automatisering, state, een andere bediening)
  __weak PBDubEchoPaneel* zwak = v;
  v->timer = [NSTimer timerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer* t) { [zwak ververs]; }];
  [[NSRunLoop mainRunLoop] addTimer:v->timer forMode:NSRunLoopCommonModes];
  return (__bridge_retained void*)v;
}

bool zetOuder(void* view, void* ouder) {
  PBDubEchoPaneel* v = (__bridge PBDubEchoPaneel*)view;
  NSView* o = (__bridge NSView*)ouder;
  if (!v || !o) return false;
  [o addSubview:v];
  return true;
}

void zetMaat(void* view, double breedte, double hoogte) {
  PBDubEchoPaneel* v = (__bridge PBDubEchoPaneel*)view;
  [v setFrame:NSMakeRect(0, 0, breedte, hoogte)];
}

void toon(void* view, bool zichtbaar) {
  PBDubEchoPaneel* v = (__bridge PBDubEchoPaneel*)view;
  [v setHidden:!zichtbaar];
}

void sluit(void* view) {
  PBDubEchoPaneel* v = (__bridge_transfer PBDubEchoPaneel*)view;  // neemt de referentie van maakView over
  [v->timer invalidate];
  v->timer = nil;
  if (v->bediening && v->sleept >= 0 && v->begonnen) v->bediening->gebaarEinde(v->sleept);  // dicht tijdens slepen
  v->sleept = -1;
  v->begonnen = false;
  v->bediening = nullptr;
  [v removeFromSuperview];
}

}  // namespace pb_ui
