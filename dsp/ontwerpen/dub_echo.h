// dub_echo.h — de eerste echte plugin: een tape-echo voor dub, met saturatie en een filter in de terugkoppeling.
// SPEC: bouwpakket/dub-echo/SPEC.md. Per kanaal (beide kanalen delen één bandbeweging, het is één band):
//
//   in ──┬────────────────────────────────────────────────────────(1−mix)──┐
//        └─(+)── tape-saturatie (ADAA) ── delaylijn ──(+)── laag weg ── toon (SVF) ──┬──(mix)──(+)── uit
//           ↑                lezen op T − (E(t) − E(t−T)) − ½    bandruis              │
//           └───────────────────────────── × terugkoppeling ──────────────────────────┘
//
// Sinds 17 okt 2026 naar een echte band (ref/referentie/RAPPORT.md): de bandsnelheid wijkt af zoals bij een Akai
// GX-4000D en de vertraging leest wat de band tussen de koppen deed (los van de delaytijd); de saturatie is licht
// asymmetrisch (β = 0,099609375: de tweede harmonische van de band); bandruis is parameter 9 (standaard uit).
// Elke stap rekent precies zoals de ketenmotor met de dub-echo uit tool/src/archetypen.js (tests/graaf.test.mjs).
//
// Terugkoppeling > 1 = zelfoscillatie; de saturatie begrenst hem (geen harde klem). Saturatie werkt als
// tanh(g·u)/g: voor kleine signalen versterking 1, dus "terugkoppeling" blijft de lusversterking voor zacht
// materiaal, en "verzadiging" zet hoe vroeg de band dichtloopt.
#pragma once
#include "../pb_delay.h"
#include "../pb_svf.h"
#include "../pb_tape.h"

#define PB_NAAM "PB Dub Echo"
#define PB_CLAP_ID "nl.varve.pluginbouwer.dubecho"
#define PB_VERSIE "0.1.0"
#define PB_AU_MAKER "Vrve"
#define PB_AU_SUBTYPE "PbDe"

namespace pb {

struct Meta {
  const char* clapId; const char* naam; const char* maker; const char* url; const char* versie; const char* uitleg;
  const char* auMaker; const char* auSubtype;
};

struct Ontwerp {
  static constexpr Meta META = {PB_CLAP_ID, PB_NAAM, "Varve", "https://varve.nl", PB_VERSIE,
                                "Tape-echo voor dub: saturatie en een filter in de terugkoppeling", PB_AU_MAKER, PB_AU_SUBTYPE};
  enum { P_TIJD = 0, P_TERUG, P_TOON, P_LAAG, P_RESO, P_VERZ, P_HAPER, P_MIX, P_RUIS };
  static constexpr int N_PARAMS = 9;
  static constexpr double BETA = 0.099609375;   // asymmetrie van de tape (51/512, exact in float32)
  // het zaad van de bandruis per kanaal: dat van knoop 1 in de keten (Graaf::zaad), zodat beide hetzelfde rekenen
  static uint32_t ruisZaad(int c) { return 0x9E3779B9u ^ ((uint32_t)(1 * 2 + c + 1) * 0x85EBCA6Bu); }
  static constexpr double MAX_MS = 1200.0;
  // Alle grenzen en standaarden exact in float32 (een AU geeft parameters als float door).
  static constexpr ParamInfo PARAMS[N_PARAMS] = {
      {1, "Time", "Hoe lang tot de echo komt. Draaien laat de toon glijden, zoals een band die sneller gaat", "ms",
       20.0, MAX_MS, 375.0, 250.0, AUTOMATEERBAAR | GLIJDEN},
      {2, "Feedback", "Hoeveel van de echo weer de band op gaat. Boven 1 gaat hij vanzelf door (zelfoscillatie)", "",
       0.0, 1.25, 0.5, 30.0, AUTOMATEERBAAR | GLIJDEN},
      {3, "Tone", "Hoe donker elke herhaling wordt: het filter in de lus haalt hoog weg", "Hz",
       300.0, 12000.0, 3000.0, 20.0, AUTOMATEERBAAR | GLIJDEN},
      {4, "Low cut", "Haalt laag uit de lus zodat de herhalingen niet dichtslibben", "Hz",
       20.0, 1000.0, 100.0, 20.0, AUTOMATEERBAAR | GLIJDEN},
      {5, "Resonance", "Een piek op de toonfrequentie in de lus; hoger = fluitender herhalingen", "Q",
       0.5, 4.0, 0.70703125, 20.0, AUTOMATEERBAAR | GLIJDEN},
      {6, "Saturation", "Hoe vroeg de band dichtloopt: warmer, en het remt de zelfoscillatie", "dB",
       0.0, 24.0, 6.0, 20.0, AUTOMATEERBAAR | GLIJDEN},
      {7, "Flutter", "Wow en flutter: hoe onrustig de band loopt. 0,25 is een goede bandrecorder, 1 een versleten band", "",
       0.0, 1.0, 0.25, 20.0, AUTOMATEERBAAR | GLIJDEN},
      {8, "Mix", "Hoeveel echo je hoort naast het origineel", "",
       0.0, 1.0, 0.375, 20.0, AUTOMATEERBAAR | GLIJDEN},
      {9, "Hiss", "De sis van de band in de echo; bij 1 zoals een echte bandrecorder, en hij stapelt in de herhalingen", "",
       0.0, 1.0, 0.0, 20.0, AUTOMATEERBAAR | GLIJDEN},
  };

  double sr = 48000;
  Glijder glij[N_PARAMS];
  Vertraging vt[2];
  TapeVerzadiging sat[2];
  EenPool laag[2];
  Svf toon[2];
  TapeSnelheid band;
  BandGeheugen bandE;
  BandRuis sis[2];
  double tbeta = 0, lcbeta = 0, inorm = 1;
  double terug[2] = {0, 0};
  double drive = 1, invDrive = 1;
  bool vers = true;

  // Standaardwaarden bij constructie, dus in WASM en plugin hetzelfde (eerst vergeten: de WASM begon op 0).
  Ontwerp() { for (int i = 0; i < N_PARAMS; ++i) glij[i].doel = glij[i].nu = PARAMS[i].standaard; }
  static double maxSamples(double samplerate) { return MAX_MS * 0.001 * samplerate * 1.02 + 8; }
  static unsigned long geheugenNodig(double samplerate) {
    return 2 * Vertraging::geheugen(maxSamples(samplerate)) + BandGeheugen::geheugen(maxSamples(samplerate));
  }

  void activeer(double samplerate, uint32_t, Geheugen g) {
    sr = samplerate;
    for (int i = 0; i < N_PARAMS; ++i) glij[i].zetTijd(PARAMS[i].glijMs, sr);
    for (auto& v : vt) v.koppel(g, maxSamples(sr));
    bandE.koppel(g, maxSamples(sr));
    band.zet(sr);
    for (auto& r : sis) r.zet(sr);
    tbeta = tanh_(BETA); lcbeta = logcosh_(BETA); inorm = 1.0 / (1.0 - tbeta * tbeta);
    reset();
  }
  void reset() {
    for (auto& v : vt) v.reset();
    for (auto& s : sat) s.reset();
    for (auto& l : laag) l.reset();
    for (auto& t : toon) t.reset();
    band.reset();
    bandE.reset();
    for (int c = 0; c < 2; ++c) sis[c].reset(ruisZaad(c));
    terug[0] = terug[1] = 0;
    vers = true;
  }
  void zetParam(int i, double v) { if (i >= 0 && i < N_PARAMS) glij[i].doel = klem(v, PARAMS[i].min, PARAMS[i].max); }
  double leesParam(int i) const { return (i >= 0 && i < N_PARAMS) ? glij[i].doel : 0.0; }
  uint32_t latentie() const { return 0; }
  // Staart: tot −90 dB onder de eerste echo. Terugkoppeling ≥ 1 = oneindig (CLAP: INT32_MAX).
  double staartSec() const {
    double fb = glij[P_TERUG].doel, t = glij[P_TIJD].doel * 0.001 * 1.01;
    if (fb >= 0.999) return -1;
    if (fb < 1e-3) return t + 0.1;
    double n = -4.5 / (log_(fb) * 0.43429448190325182765);  // herhalingen tot −90 dB
    double s = (n + 1.0) * t + 0.1;
    return s > 60.0 ? 60.0 : s;
  }

  void herbereken(bool filters) {
    drive = dbNaarVersterking(glij[P_VERZ].nu);
    invDrive = 1.0 / drive;
    if (filters)
      for (int c = 0; c < 2; ++c) {
        laag[c].zet(glij[P_LAAG].nu, sr);
        toon[c].zet(glij[P_TOON].nu, glij[P_RESO].nu, sr);
      }
  }

  void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t n) {
    for (uint32_t s = 0; s < n; ++s) {
      bool filt = false, verz = false;
      if (vers) { for (auto& g : glij) g.spring(); vers = false; filt = verz = true; }
      glij[P_TIJD].stap(); glij[P_TERUG].stap(); glij[P_HAPER].stap(); glij[P_MIX].stap(); glij[P_RUIS].stap();
      filt |= glij[P_TOON].stap(); filt |= glij[P_LAAG].stap(); filt |= glij[P_RESO].stap();
      verz |= glij[P_VERZ].stap();
      if (filt || verz) herbereken(filt);

      bandE.schrijf(band.stap() * glij[P_HAPER].nu);
      double Ts = glij[P_TIJD].nu * 0.001 * sr;
      double d = Ts - bandE.verschil(Ts) - 0.5;   // −½: de vertraging van ADAA
      double fb = glij[P_TERUG].nu, mix = glij[P_MIX].nu;
      for (int c = 0; c < kanalen; ++c) {
        double n = sis[c].stap() * glij[P_RUIS].nu;
        double x = invoer(in[c][s]);
        double u = x + terug[c];
        vt[c].schrijf(sat[c].stapAsym(u * drive, BETA, tbeta, lcbeta) * inorm * invDrive);
        double r = vt[c].leesLagrange(d) + n;
        toon[c].stap(laag[c].hoog(r));
        double nat = toon[c].lp;
        terug[c] = flush(fb * nat);
        uit[c][s] = uitvoer((1.0 - mix) * x + mix * nat);
      }
    }
  }
};

}  // namespace pb
