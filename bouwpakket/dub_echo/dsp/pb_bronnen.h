// pb_bronnen.h — blokken die zelf geluid maken, het bespelen, en de effecten die er in de bibliotheek bij kwamen
// (15 okt 2026). Zelfde regels als de rest van dsp/ (CLAUDE.md): alleen pb_math, per sample, geen allocatie in stap(),
// toeval uit een geseede xorshift. De ketenmotor (pb_graaf.h) roept ze aan; tests/bronnen.test.mjs legt ze tegen de
// theorie (Fourier-reeksen, tijden, toonhoogte, statische kromme, nagalmtijd).
#pragma once
#include "pb_delay.h"
#include "pb_svf.h"

namespace pb {

// 2^(halve tonen / 12): de verhouding waarmee een toonhoogte verschuift
inline double toonVerhouding(double halveTonen) { return exp_(halveTonen * (LN2 / 12.0)); }
// −60 dB na t seconden: de factor per sample (sr samples per seconde). 6,907755 = ln(1000).
inline double factor60(double tSec, double sr) { return exp_(-6.90775527898213705205 / (tSec * sr)); }

// ---- oscillator ------------------------------------------------------------------------------------------------
// Zaag en blok met polyBLEP: rond elke sprong een tweede-orde polynoom die de sprong bandbegrenst (V. Välimäki,
// A. Huovilainen, "Antialiasing Oscillators in Subtractive Synthesis", IEEE SP Magazine 24(2), 2007). Driehoek naïef
// (zijn boventonen vallen met 1/n², aliasing ligt laag), sinus exact. Fase in [0, 1).
enum class Golf : int { sinus = 0, zaag = 1, blok = 2, driehoek = 3 };
inline double polyBlep(double t, double dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
  if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
  return 0.0;
}
struct Osc {
  double fase = 0;
  void reset() { fase = 0; }
  // f mag negatief zijn (door FM): dan loopt de fase terug. pw = pulsbreedte van het blok.
  double stap(double f, double sr, int golf, double pw) {
    double dt = f / sr;
    if (dt > 0.49) dt = 0.49;
    if (dt < -0.49) dt = -0.49;
    double adt = dt < 0 ? -dt : dt, t = fase, y;
    if (adt < 1e-9) adt = 1e-9;
    switch (golf) {
      case 1: y = 2.0 * t - 1.0 - polyBlep(t, adt); break;
      case 2: {
        pw = klem(pw, 0.02, 0.98);
        double t2 = t + 1.0 - pw; if (t2 >= 1.0) t2 -= 1.0;
        y = (t < pw ? 1.0 : -1.0) + polyBlep(t, adt) - polyBlep(t2, adt);
        break;
      }
      case 3: y = t < 0.5 ? 4.0 * t - 1.0 : 3.0 - 4.0 * t; break;
      default: y = sin_(TWO_PI * t); break;
    }
    fase += dt;
    if (fase >= 1.0) fase -= 1.0;
    if (fase < 0.0) fase += 1.0;
    return y;
  }
};

// ---- ruis ------------------------------------------------------------------------------------------------------
// Wit uit xorshift; "kleur" zet er een één-pool laagdoor achter (donkerder) en compenseert het niveau, zodat de
// effectieve waarde gelijk blijft: de variantie van wit door y += a(x − y) is a/(2 − a) keer die van wit.
struct Ruis {
  Toeval t; double y = 0;
  void reset(uint32_t zaad) { t.zaai(zaad); y = 0; }
  double stap(double kleur) {
    double a = 1.0 - 0.995 * klem(kleur, 0.0, 1.0);
    y = flush(y + (t.tussen(-1.0, 1.0) - y) * a);
    return y * sqrt_((2.0 - a) / a);
  }
};

// ---- sequencer -------------------------------------------------------------------------------------------------
// Zestien stappen van een zestiende. Per stap een toon in halve tonen (RUST = stil) en een accent. De patronen
// staan hier als vaste tabel: de tool noemt ze bij naam (tool/src/keten.js, PATRONEN), in dezelfde volgorde.
constexpr int RUST = -128, N_PATRONEN = 12;
struct Patroon { int8_t toon[16]; uint16_t accent; };
constexpr Patroon PATRONEN[N_PATRONEN] = {
  {{0, RUST, RUST, RUST, 0, RUST, RUST, RUST, 0, RUST, RUST, RUST, 0, RUST, RUST, RUST}, 0x1111},          // vier op de vloer
  {{RUST, RUST, RUST, RUST, 0, RUST, RUST, RUST, RUST, RUST, RUST, RUST, 0, RUST, RUST, RUST}, 0x1010},    // achterslag (2 en 4)
  {{RUST, RUST, 0, RUST, RUST, RUST, 0, RUST, RUST, RUST, 0, RUST, RUST, RUST, 0, RUST}, 0x0000},          // tegentijd (skank)
  {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0x1111},                                              // zestienden
  {{0, RUST, RUST, 0, RUST, RUST, 7, RUST, 5, RUST, RUST, 3, RUST, 0, RUST, RUST}, 0x0101},                // dubbas
  {{0, 0, 12, 0, 3, 0, 15, 0, 0, 12, 0, 7, 10, 0, 3, 12}, 0x2492},                                         // acid
  {{0, 3, 7, 12, 7, 3, 0, 3, 7, 12, 15, 12, 7, 3, 7, 10}, 0x1111},                                         // arpeggio
  {{0, RUST, RUST, 0, RUST, RUST, 0, RUST, RUST, RUST, 0, RUST, 0, RUST, RUST, RUST}, 0x0001},             // clave
  {{0, RUST, RUST, RUST, RUST, RUST, RUST, RUST, RUST, RUST, 0, RUST, RUST, RUST, RUST, RUST}, 0x0001},    // halftijd kick
  {{RUST, RUST, RUST, RUST, RUST, RUST, RUST, RUST, 0, RUST, RUST, RUST, RUST, RUST, RUST, RUST}, 0x0100}, // halftijd snare
  {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0x0000},                                              // aanhouden (drone)
  {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0x0000},                                              // toeval (zie stap)
};
constexpr int PATROON_AANHOUDEN = 10, PATROON_TOEVAL = 11;
struct Sequencer {
  int patroon = 0, stap = 0, toon = 0; bool noot = false, accent = false;
  double fase = 0;
  uint32_t zaad = 1; Toeval t;
  void zet(int p, uint32_t z) { patroon = p < 0 ? 0 : (p >= N_PATRONEN ? N_PATRONEN - 1 : p); zaad = z ? z : 1; }
  void reset() { t.zaai(zaad); stap = 0; fase = 0; laad(); }
  void laad() {
    const Patroon& p = PATRONEN[patroon];
    if (patroon == PATROON_TOEVAL) {
      static constexpr int8_t PENTA[10] = {0, 3, 5, 7, 10, 12, 15, 17, 19, 22};   // mineur-pentatoniek, twee octaven
      uint32_t r = t.volgende();
      noot = (r & 7u) >= 2u;                    // drie op de vier stappen een noot
      if (noot) toon = PENTA[(r >> 3) % 10u];
      accent = ((r >> 8) & 3u) == 0u;
      return;
    }
    int x = p.toon[stap];
    noot = x != RUST;
    if (noot) toon = x;                         // bij een rust blijft de laatste toon staan (voor de staart)
    accent = (p.accent >> (15 - stap)) & 1u;
  }
  // tempo in BPM, lengte = deel van de stap dat de poort open staat, zwaai = swing (even stappen langer)
  void stapVerder(double tempo, double lengte, double zwaai, double sr) {
    double z = klem(zwaai, 0.0, 0.45);
    double duur = (stap & 1) ? 1.0 - z : 1.0 + z;           // in zestienden
    fase += klem(tempo, 1.0, 999.0) * (4.0 / 60.0) / (sr * duur);
    if (fase >= 1.0) { fase -= 1.0; stap = (stap + 1) & 15; laad(); }
    (void)lengte;
  }
  bool poort(double lengte) const {
    if (patroon == PATROON_AANHOUDEN) return true;
    return noot && fase < klem(lengte, 0.01, 1.0);
  }
};

// ---- omhulling (ADSR) ------------------------------------------------------------------------------------------
// Aanzet lineair in precies aanzet·sr samples; verval en loslaten exponentieel: na de ingestelde tijd 99 % van de weg
// (tijdconstante t/4,6). Een nieuwe aanslag begint vanaf waar hij is (geen tik).
struct Omhulling {
  double y = 0; int fase = 0; bool aan = false;
  double dMs = -1, rMs = -1, dK = 0, rK = 0, srC = 0;
  void reset() { y = 0; fase = 0; aan = false; }
  double stap(double poort, double aMs, double vMs, double s, double lMs, double sr) {
    bool nu = poort > 0.5;
    if (nu && !aan) fase = 1;
    if (!nu && aan) fase = 4;
    aan = nu;
    s = klem(s, 0.0, 1.0);
    if (vMs != dMs || sr != srC) { dMs = vMs; dK = exp_(-4.60517018598809136804 / (klem(vMs, 0.1, 1e6) * 0.001 * sr)); }
    if (lMs != rMs || sr != srC) { rMs = lMs; rK = exp_(-4.60517018598809136804 / (klem(lMs, 0.1, 1e6) * 0.001 * sr)); }
    srC = sr;
    switch (fase) {
      case 1: {
        double n = klem(aMs, 0.0, 1e6) * 0.001 * sr;
        y = n < 1.0 ? 1.0 : y + 1.0 / n;
        if (y >= 1.0) { y = 1.0; fase = 2; }
        break;
      }
      case 2: y = s + (y - s) * dK; if (fabs_(y - s) < 1e-6) { y = s; fase = 3; } break;
      case 3: y = s; break;
      case 4: y = flush(y * rK); if (y < 1e-6) { y = 0; fase = 0; } break;
      default: y = 0; break;
    }
    return y;
  }
};

// ---- snaar (Karplus–Strong) ------------------------------------------------------------------------------------
// K. Karplus, A. Strong, "Digital Synthesis of Plucked-String and Drum Timbres", CMJ 7(2), 1983, met de stemming van
// D. Jaffe en J. O. Smith, "Extensions of the Karplus-Strong Plucked-String Algorithm", CMJ 7(2), 1983: de lus is
// periode = sr/f samples lang; het middelende filter telt mee (½ sample bij helderheid 0) en de fractionele rest leest
// Lagrange. Een aanslag (poort omhoog) stuurt één periode ruis de lus in.
struct Snaar {
  Vertraging lijn; Toeval t; double vorige = 0, poortVorige = 0; int stoot = 0;
  double fC = -1, vC = -1, g = 0;
  static double maxSamples(double sr) { return sr / 20.0 + 8.0; }
  void koppel(Geheugen& geh, double sr) { lijn.koppel(geh, maxSamples(sr)); }
  void reset(uint32_t zaad) { lijn.reset(); t.zaai(zaad); vorige = 0; poortVorige = 0; stoot = 0; }
  double stap(double poort, double f, double vervalSec, double helder, double sr) {
    f = klem(f, 20.0, sr * 0.2);
    double periode = sr / f, h = klem(helder, 0.0, 1.0);
    if (poort > 0.5 && poortVorige <= 0.5) stoot = (int)periode;
    poortVorige = poort;
    if (f != fC || vervalSec != vC) { fC = f; vC = vervalSec; g = exp_(-6.90775527898213705205 * periode / (klem(vervalSec, 0.01, 100.0) * sr)); }
    double r = lijn.leesLagrange(periode - 1.0 - 0.5 * (1.0 - h));
    double lp = h * r + (1.0 - h) * 0.5 * (r + vorige);
    vorige = r;
    double x = 0;
    if (stoot > 0) { x = t.tussen(-1.0, 1.0); --stoot; }
    double y = flush(x + g * lp);
    lijn.schrijf(y);
    return y;
  }
};

// ---- trommel ---------------------------------------------------------------------------------------------------
// Eén stem voor kick, tom, snare en hihat: een sinus die van toon·2^val naar toon zakt (in ~35 ms), plus ruis
// (eerste-orde verschil: hoog). Bij elke aanslag start de fase op 0 (elke slag klinkt hetzelfde).
struct Trommel {
  double fase = 0, amp = 0, toonOmh = 0, ruisOmh = 0, vorigeRuis = 0, poortVorige = 0;
  double vC = -1, kA = 0, kR = 0, kT = 0, srC = 0;
  Toeval t;
  void reset(uint32_t zaad) { fase = amp = toonOmh = ruisOmh = vorigeRuis = poortVorige = 0; t.zaai(zaad); }
  double stap(double poort, double f, double val, double vervalMs, double ruis, double sr) {
    if (poort > 0.5 && poortVorige <= 0.5) { amp = 1; toonOmh = 1; ruisOmh = 1; fase = 0; }
    poortVorige = poort;
    if (vervalMs != vC || sr != srC) {
      vC = vervalMs; srC = sr;
      double v = klem(vervalMs, 1.0, 1e5) * 0.001;
      kA = factor60(v, sr); kR = factor60(v * 0.5, sr); kT = factor60(0.035, sr);
    }
    double fNu = klem(f, 10.0, sr * 0.45) * exp_(klem(val, 0.0, 8.0) * LN2 * toonOmh);
    if (fNu > sr * 0.45) fNu = sr * 0.45;
    fase += fNu / sr; if (fase >= 1.0) fase -= 1.0;
    double n = t.tussen(-1.0, 1.0), hoog = 0.5 * (n - vorigeRuis);
    vorigeRuis = n;
    double m = klem(ruis, 0.0, 1.0);
    double y = amp * (1.0 - m) * sin_(TWO_PI * fase) + ruisOmh * m * hoog;   // beide pieken hooguit 1
    amp = flush(amp * kA); ruisOmh = flush(ruisOmh * kR); toonOmh = flush(toonOmh * kT);
    return y;
  }
};

// ---- galm (feedback delay network, 8 lijnen) -------------------------------------------------------------------
// J.-M. Jot, A. Chaigne, "Digital Delay Networks for Designing Artificial Reverberators", AES 90th Convention, 1991:
// acht vertragingen, een Hadamard-matrix als terugkoppeling (verliesvrij, alles mengt met alles) en per lijn een
// versterking g = 10^(−3·L/(T60·sr)), zodat elke lijn in T60 seconden 60 dB zakt. Een één-pool laagdoor per lijn
// maakt het hoog korter (demping). Kanaal 1 heeft iets andere lengtes: zo wordt een mono ingang toch breed.
struct Galm {
  static constexpr int N = 8;
  static constexpr double BASIS_MS[N] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 79.3};
  static constexpr double MAX_GROOTTE = 2.0, KANAAL1 = 1.0137;
  Vertraging lijn[N]; EenPool demp[N];
  double L[N] = {0}, g[N] = {0};
  double tC = -1, dC = -1, gC = -1, srC = 0;
  static double maxSamples(double sr) { return BASIS_MS[N - 1] * KANAAL1 * MAX_GROOTTE * 0.001 * sr + 8.0; }
  void koppel(Geheugen& geh, double sr) { for (auto& l : lijn) l.koppel(geh, maxSamples(sr)); }
  void reset() { for (auto& l : lijn) l.reset(); for (auto& d : demp) d.reset(); }
  double stap(double x, double t60, double dempHz, double grootte, int kanaal, double sr) {
    if (t60 != tC || dempHz != dC || grootte != gC || sr != srC) {
      tC = t60; dC = dempHz; gC = grootte; srC = sr;
      double s = klem(grootte, 0.25, MAX_GROOTTE) * (kanaal ? KANAAL1 : 1.0);
      for (int i = 0; i < N; ++i) {
        L[i] = BASIS_MS[i] * s * 0.001 * sr;
        g[i] = exp_(-6.90775527898213705205 * L[i] / (klem(t60, 0.05, 100.0) * sr));
        demp[i].zet(dempHz, sr);
      }
    }
    double v[N];
    for (int i = 0; i < N; ++i) v[i] = demp[i].laag(lijn[i].leesLagrange(L[i] - 1.0)) * g[i];
    // snelle Walsh–Hadamard (8 punten), genormaliseerd met 1/√8: orthogonaal, dus de lus verliest alleen door g
    for (int h = 1; h < N; h <<= 1)
      for (int i = 0; i < N; i += 2 * h)
        for (int j = i; j < i + h; ++j) { double a = v[j], b = v[j + h]; v[j] = a + b; v[j + h] = a - b; }
    const double norm = 0.35355339059327376220;   // 1/√8
    double uit = 0;
    for (int i = 0; i < N; ++i) {
      double w = v[i] * norm;
      lijn[i].schrijf(flush(x + w));
      uit += (i & 1) ? -w : w;
    }
    return uit * norm;
  }
};

// ---- chorus / flanger ------------------------------------------------------------------------------------------
// Een korte vertraging die een sinus laat wiebelen; met terugkoppeling wordt het een flanger. Kanaal 1 loopt een
// kwart periode voor (breed).
struct Chorus {
  Vertraging lijn; double fase = 0;
  static double maxSamples(double sr) { return 0.06 * sr + 8.0; }
  void koppel(Geheugen& geh, double sr) { lijn.koppel(geh, maxSamples(sr)); }
  void reset(int kanaal) { lijn.reset(); fase = kanaal ? 0.25 : 0.0; }
  double stap(double x, double tijdMs, double diepteMs, double hz, double terug, double sr) {
    fase += klem(hz, 0.0, 50.0) / sr; if (fase >= 1.0) fase -= 1.0;
    double m = 0.5 * (1.0 + sin_(TWO_PI * fase));
    double d = (klem(tijdMs, 0.1, 40.0) + klem(diepteMs, 0.0, 15.0) * m) * 0.001 * sr;
    double y = lijn.leesLagrange(d);
    lijn.schrijf(flush(x + klem(terug, -0.95, 0.95) * y));
    return y;
  }
};

// ---- compressor ------------------------------------------------------------------------------------------------
// Feed-forward, harde knie: een piekvolger (aanzet/loslaten) op de detector (de ingang zelf, of de zijketen), en
// boven de drempel D: uit = D + (in − D)/verhouding, in dB. Per kanaal (niet gekoppeld).
struct Compressor {
  double env = 0, aC = -1, rC = -1, kA = 0, kR = 0, srC = 0;
  void reset() { env = 0; }
  double stap(double x, double det, double drempelDb, double verhouding, double aMs, double rMs, double sr) {
    if (aMs != aC || rMs != rC || sr != srC) {
      aC = aMs; rC = rMs; srC = sr;
      kA = exp_(-1.0 / (klem(aMs, 0.01, 1e5) * 0.001 * sr));
      kR = exp_(-1.0 / (klem(rMs, 0.1, 1e6) * 0.001 * sr));
    }
    double a = fabs_(det), k = a > env ? kA : kR;
    env = flush(k * env + (1.0 - k) * a);
    double niveau = 8.68588963806503655302 * log_(env + 1e-12);   // 20·log10
    double over = niveau - drempelDb, r = klem(verhouding, 1.0, 100.0);
    double gr = over > 0 ? over * (1.0 - 1.0 / r) : 0.0;
    return x * dbNaarVersterking(-gr);
  }
};

// ---- vergruizer (bits en samplerate omlaag) --------------------------------------------------------------------
struct Vergruizer {
  double vast = 0; int teller = 0;
  void reset() { vast = 0; teller = 0; }
  double stap(double x, double bits, double deling) {
    int n = (int)klem(deling, 1.0, 256.0);
    if (++teller >= n) {
      teller = 0;
      double stapje = exp_((1.0 - klem(bits, 1.0, 24.0)) * LN2);   // 2^(1−bits): het bereik −1..1 in 2^bits treden
      vast = floor_(x / stapje + 0.5) * stapje;
    }
    return vast;
  }
};

// ---- vouwer (wavefolder) ---------------------------------------------------------------------------------------
// sin(π/2 · u): tot |u| = 1 een zachte verzadiging, daarboven vouwt de golf terug. Scheef = een gelijkspanning vóór het
// vouwen (even boventonen); een hoogdoor op 10 Hz haalt die er na afloop weer uit.
struct Vouwer {
  EenPool dc;
  void zet(double sr) { dc.zet(10.0, sr); }
  void reset() { dc.reset(); }
  double stap(double x, double driveDb, double scheef) {
    double u = x * dbNaarVersterking(klem(driveDb, -24.0, 48.0)) + klem(scheef, -1.0, 1.0);
    return dc.hoog(sin_(0.5 * PI * u));
  }
};

// ---- willekeur (sample & hold) ---------------------------------------------------------------------------------
struct Willekeur {
  double fase = 0, doel = 0, y = 0; Toeval t;
  void reset(uint32_t zaad) { t.zaai(zaad); fase = 0; doel = t.tussen(-1.0, 1.0); y = doel; }
  double stap(double hz, double glad, double sr) {
    fase += klem(hz, 0.0, 1000.0) / sr;
    if (fase >= 1.0) { fase -= 1.0; doel = t.tussen(-1.0, 1.0); }
    double gl = klem(glad, 0.0, 1.0);
    if (gl <= 0.0) y = doel;
    else y += (doel - y) * (1.0 - exp_(-klem(hz, 0.01, 1000.0) * 6.0 / (gl * sr)));
    return y;
  }
};

}  // namespace pb
