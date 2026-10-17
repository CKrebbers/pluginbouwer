// pb_graaf.h — de ketenmotor: een signaalketen als DATA, per sample uitgevoerd. Dit is wat de ontwerper tekent.
//
// Dezelfde code draait in de tool (WASM, keten geladen tijdens het ontwerpen) en in de plugin (keten als constante,
// gegenereerd door tools/bouwpakket.mjs). Toets: de dub-echo als keten klinkt bit voor bit als de handgeschreven
// dsp/ontwerpen/dub_echo.h (tests/graaf.test.mjs).
//
// Beschrijving (gehele getallen en doubles, zodat JS en een gegenereerde header hem op dezelfde manier geven):
//   knopen in UITVOERVOLGORDE (de tool rekent die uit). Een ingang die naar een knoop wijst die LATER komt, leest
//   diens uitgang van de vorige sample: zo werkt een terugkoppellus (z⁻¹), zonder aparte vlag.
//   Gedeelde knopen (bandbeweging, LFO) rekenen één keer per sample, vóór de kanalen.
//   Een knoopparameter is een constante of een plugin-parameter (index), met optioneel een modulatiebron.
//   Uitbreidingen van 15 okt 2026, terugwaarts gelijk (oude rijen lezen en klinken bit voor bit hetzelfde):
//   param ≥ 1000 = param − 1000 met "waarde" als schaal (één parameter op meer knoppen, bv. een octaaf hoger);
//   modPoort ≥ 8 = modPoort − 8, exponentieel (× 2^(diepte·m), diepte in octaven) voor frequentie- en tijdknoppen.
#pragma once
#include "pb_biquad.h"
#include "pb_delay.h"
#include "pb_svf.h"
#include "pb_tape.h"
#include "pb_bronnen.h"

namespace pb {

enum class Blok : int32_t {
  in = 0, uit = 1, som = 2, versterker = 3, tape = 4, vertraging = 5, hoogdoor1 = 6, svf = 7,
  bandbeweging = 8, mix = 9, biquad = 10, lfo = 11, laagdoor1 = 12,
  // erbij op 15 okt 2026 (pb_bronnen.h): bronnen, bespelen en meer effecten
  osc = 13, ruis = 14, sequencer = 15, toetsen = 16, omhulling = 17, snaar = 18, trommel = 19, vermenigvuldig = 20,
  galm = 21, chorus = 22, compressor = 23, vergruizer = 24, vouwer = 25, panner = 26, willekeur = 27, aantal
};
constexpr int MAX_KNOPEN = 32, MAX_IN = 2, MAX_UIT = 3, MAX_KP = 4, MAX_PARAMS = 32, MAX_VERTRAGING = 6;

struct Bron { int32_t knoop = -1, poort = 0; };                         // -1 = niets aangesloten (0)
struct KnoopParam {
  int32_t param = -1;     // index in de plugin-parameters, of -1 = constante
  double waarde = 0;      // de constante
  double schaal = 1.0;    // knop = parameter × schaal (één parameter op meer knoppen; in de rij: param + 1000, waarde = schaal)
  int32_t modKnoop = -1, modPoort = 0; double modDiepte = 0;  // optioneel: + diepte·(max−min)·m, m ∈ [−1, 1]
  bool modExp = false;    // exponentieel: × 2^(diepte·m), diepte in octaven (in de rij: modPoort + 8)
};
struct KnoopBeschrijving {
  Blok soort = Blok::in;
  Bron in[MAX_IN];
  KnoopParam kp[MAX_KP];
  double vast[2] = {0, 0};   // vaste eigenschappen (vertraging: max ms, compensatie in samples; svf: welke uitgang)
};
struct ParamBeschrijving { double min = 0, max = 1, standaard = 0, glijMs = 0; };
struct GraafBeschrijving {
  int32_t nKnopen = 0, nParams = 0;
  KnoopBeschrijving knoop[MAX_KNOPEN];
  ParamBeschrijving param[MAX_PARAMS];
};

// De platte vorm (één rij doubles), gedeeld door JS (tool) en de gegenereerde header (plugin):
//   [nKnopen, nParams,
//    per knoop: soort, in0.knoop, in0.poort, in1.knoop, in1.poort,
//               4× (param, waarde, modKnoop, modPoort, modDiepte), vast0, vast1,     → 27 getallen
//    per param: min, max, standaard, glijMs]                                         →  4 getallen
// Geeft het aantal gelezen getallen terug, of 0 als de beschrijving niet klopt.
constexpr int GETALLEN_PER_KNOOP = 27, GETALLEN_PER_PARAM = 4;
inline int leesBeschrijving(const double* d, int n, GraafBeschrijving& g) {
  if (n < 2) return 0;
  int nk = (int)d[0], np = (int)d[1];
  if (nk < 0 || nk > MAX_KNOPEN || np < 0 || np > MAX_PARAMS) return 0;
  int nodig = 2 + nk * GETALLEN_PER_KNOOP + np * GETALLEN_PER_PARAM;
  if (n < nodig) return 0;
  g.nKnopen = nk; g.nParams = np;
  const double* p = d + 2;
  for (int k = 0; k < nk; ++k, p += GETALLEN_PER_KNOOP) {
    auto& kn = g.knoop[k];
    int soort = (int)p[0];
    if (soort < 0 || soort >= (int)Blok::aantal) return 0;
    kn.soort = (Blok)soort;
    for (int i = 0; i < MAX_IN; ++i) {
      kn.in[i].knoop = (int32_t)p[1 + 2 * i]; kn.in[i].poort = (int32_t)p[2 + 2 * i];
      if (kn.in[i].knoop >= nk || kn.in[i].poort < 0 || kn.in[i].poort >= MAX_UIT) return 0;
    }
    for (int j = 0; j < MAX_KP; ++j) {
      const double* q = p + 5 + 5 * j;
      kn.kp[j].param = (int32_t)q[0]; kn.kp[j].waarde = q[1]; kn.kp[j].schaal = 1.0;
      if (kn.kp[j].param >= 1000) { kn.kp[j].param -= 1000; kn.kp[j].schaal = q[1]; }
      kn.kp[j].modKnoop = (int32_t)q[2]; kn.kp[j].modPoort = (int32_t)q[3]; kn.kp[j].modDiepte = q[4];
      kn.kp[j].modExp = kn.kp[j].modPoort >= 8;
      if (kn.kp[j].modExp) kn.kp[j].modPoort -= 8;
      if (kn.kp[j].param >= np || kn.kp[j].modKnoop >= nk) return 0;
      if (kn.kp[j].modKnoop >= 0 && (kn.kp[j].modPoort < 0 || kn.kp[j].modPoort >= MAX_UIT)) return 0;
    }
    kn.vast[0] = p[25]; kn.vast[1] = p[26];
  }
  for (int i = 0; i < np; ++i, p += GETALLEN_PER_PARAM) {
    g.param[i].min = p[0]; g.param[i].max = p[1]; g.param[i].standaard = p[2]; g.param[i].glijMs = p[3];
  }
  return nodig;
}

inline bool isGedeeld(Blok b) {
  return b == Blok::bandbeweging || b == Blok::lfo || b == Blok::sequencer || b == Blok::toetsen || b == Blok::willekeur;
}

// geheugen dat een beschrijving nodig heeft op samplerate sr (vertragingslijnen, 2 kanalen)
inline unsigned long graafGeheugen(const GraafBeschrijving& g, double sr) {
  unsigned long n = 0;
  for (int k = 0; k < g.nKnopen; ++k) {
    switch (g.knoop[k].soort) {
      case Blok::vertraging: n += 2 * Vertraging::geheugen(g.knoop[k].vast[0] * 0.001 * sr * 1.02 + 8); break;
      case Blok::snaar: n += 2 * Vertraging::geheugen(Snaar::maxSamples(sr)); break;
      case Blok::chorus: n += 2 * Vertraging::geheugen(Chorus::maxSamples(sr)); break;
      case Blok::galm: n += 2 * Galm::N * Vertraging::geheugen(Galm::maxSamples(sr)); break;
      default: break;
    }
  }
  return n;
}

struct Graaf {
  const GraafBeschrijving* b = nullptr;
  double sr = 48000;
  Glijder glij[MAX_PARAMS];
  bool vers = true;
  // toestand per knoop en kanaal
  double uitw[MAX_KNOPEN][2][MAX_UIT];
  Biquad bq[MAX_KNOPEN][2];
  Svf svf[MAX_KNOPEN][2];
  EenPool een[MAX_KNOPEN][2];
  TapeVerzadiging sat[MAX_KNOPEN][2];
  Vertraging vt[MAX_KNOPEN][2];
  TapeBeweging band[MAX_KNOPEN];
  double lfoFase[MAX_KNOPEN];
  double drive[MAX_KNOPEN], invDrive[MAX_KNOPEN];
  // pb_bronnen.h
  Osc osc[MAX_KNOPEN][2];
  Ruis ruis[MAX_KNOPEN][2];
  Omhulling omh[MAX_KNOPEN][2];
  Snaar snaar[MAX_KNOPEN][2];
  Trommel trom[MAX_KNOPEN][2];
  Galm galm[MAX_KNOPEN][2];
  Chorus chorus[MAX_KNOPEN][2];
  Compressor comp[MAX_KNOPEN][2];
  Vergruizer gruis[MAX_KNOPEN][2];
  Vouwer vouw[MAX_KNOPEN][2];
  Sequencer seq[MAX_KNOPEN];
  Willekeur wil[MAX_KNOPEN];

  // vast zaad per knoop en kanaal: ruis en aanslagen zijn overal hetzelfde, en links en rechts verschillen
  static uint32_t zaad(int k, int c) { return 0x9E3779B9u ^ ((uint32_t)(k * 2 + c + 1) * 0x85EBCA6Bu); }

  void koppel(const GraafBeschrijving* beschr) {
    b = beschr;
    for (int i = 0; i < b->nParams; ++i) glij[i].doel = glij[i].nu = b->param[i].standaard;
  }
  void activeer(double samplerate, Geheugen g) {
    sr = samplerate;
    for (int i = 0; i < b->nParams; ++i) glij[i].zetTijd(b->param[i].glijMs, sr);
    for (int k = 0; k < b->nKnopen; ++k) {
      const auto& kn = b->knoop[k];
      if (kn.soort == Blok::vertraging) for (auto& v : vt[k]) v.koppel(g, kn.vast[0] * 0.001 * sr * 1.02 + 8);
      if (kn.soort == Blok::bandbeweging) band[k].zet(sr);
      if (kn.soort == Blok::snaar) for (auto& v : snaar[k]) v.koppel(g, sr);
      if (kn.soort == Blok::chorus) for (auto& v : chorus[k]) v.koppel(g, sr);
      if (kn.soort == Blok::galm) for (auto& v : galm[k]) v.koppel(g, sr);
      if (kn.soort == Blok::vouwer) for (auto& v : vouw[k]) v.zet(sr);
      if (kn.soort == Blok::sequencer) seq[k].zet((int)kn.vast[0], (uint32_t)kn.vast[1]);
    }
    reset();
  }
  void reset() {
    for (int k = 0; k < MAX_KNOPEN; ++k) {
      for (int c = 0; c < 2; ++c) {
        for (auto& u : uitw[k][c]) u = 0;
        bq[k][c].reset(); svf[k][c].reset(); een[k][c].reset(); sat[k][c].reset(); vt[k][c].reset();
        osc[k][c].reset(); ruis[k][c].reset(zaad(k, c)); omh[k][c].reset(); snaar[k][c].reset(zaad(k, c));
        trom[k][c].reset(zaad(k, c)); galm[k][c].reset(); chorus[k][c].reset(c); comp[k][c].reset(); gruis[k][c].reset();
        vouw[k][c].reset();
      }
      band[k].reset(); lfoFase[k] = 0; seq[k].reset(); wil[k].reset(zaad(k, 0));
    }
    vers = true;
  }
  void zetParam(int i, double v) { if (i >= 0 && i < b->nParams) glij[i].doel = klem(v, b->param[i].min, b->param[i].max); }
  double leesParam(int i) const { return (i >= 0 && i < b->nParams) ? glij[i].doel : 0.0; }

  // de waarde van knoopparameter j van knoop k, nu (zonder modulatie: die komt per kanaal in waardeMod)
  double basis(int k, int j) const { const auto& p = b->knoop[k].kp[j]; return p.param >= 0 ? glij[p.param].nu * p.schaal : p.waarde; }
  bool beweegt(int k, int j) const {
    const auto& p = b->knoop[k].kp[j];
    return p.modKnoop >= 0;
  }
  double waardeMod(int k, int j, int c) const {
    const auto& p = b->knoop[k].kp[j];
    double v = basis(k, j);
    if (p.modKnoop >= 0) {
      const auto& pb = b->param[p.param >= 0 ? p.param : 0];
      double m = uitw[p.modKnoop][c][p.modPoort];
      if (p.modExp) v *= exp_(p.modDiepte * m * LN2);
      else v += p.modDiepte * (p.param >= 0 ? (pb.max - pb.min) * p.schaal : 1.0) * m;
      if (p.param >= 0) v = klem(v, pb.min * p.schaal, pb.max * p.schaal);
    }
    return v;
  }
  bool verbonden(int k, int i) const { return b->knoop[k].in[i].knoop >= 0; }
  double ingang(int k, int i, int c) const {
    const auto& s = b->knoop[k].in[i];
    return s.knoop < 0 ? 0.0 : uitw[s.knoop][c][s.poort];
  }

  // coëfficiënten opnieuw: alleen knopen waarvan een (niet-gemoduleerde) parameter veranderde
  void herbereken(int k, bool alles) {
    const auto& kn = b->knoop[k];
    switch (kn.soort) {
      case Blok::tape: drive[k] = dbNaarVersterking(basis(k, 0)); invDrive[k] = 1.0 / drive[k]; break;
      case Blok::hoogdoor1: case Blok::laagdoor1: if (!beweegt(k, 0)) for (auto& e : een[k]) e.zet(basis(k, 0), sr); break;
      case Blok::svf: if (!beweegt(k, 0) && !beweegt(k, 1)) for (auto& s : svf[k]) s.zet(basis(k, 0), basis(k, 1), sr); break;
      case Blok::biquad:
        if (!beweegt(k, 0) && !beweegt(k, 1)) for (auto& q : bq[k]) q.zet((BiquadSoort)(int)kn.vast[0], basis(k, 0), basis(k, 1), sr);
        break;
      default: break;
    }
    (void)alles;
  }

  void verwerk(const float* const* in, float* const* uit, int kanalen, uint32_t n) {
    const int N = b->nKnopen;
    for (uint32_t s = 0; s < n; ++s) {
      bool anders[MAX_PARAMS];
      bool eerste = vers;
      if (vers) { for (int i = 0; i < b->nParams; ++i) glij[i].spring(); vers = false; }
      for (int i = 0; i < b->nParams; ++i) anders[i] = glij[i].stap() || eerste;
      for (int k = 0; k < N; ++k) {
        bool her = eerste;
        for (int j = 0; j < MAX_KP; ++j) { int p = b->knoop[k].kp[j].param; if (p >= 0 && anders[p]) her = true; }
        if (her) herbereken(k, eerste);
      }
      // gedeelde knopen: één keer per sample (beide kanalen krijgen dezelfde waarde)
      for (int k = 0; k < N; ++k) {
        const auto& kn = b->knoop[k];
        if (kn.soort == Blok::bandbeweging) { double v = band[k].stap() * basis(k, 0); uitw[k][0][0] = uitw[k][1][0] = v; }
        else if (kn.soort == Blok::lfo) {
          lfoFase[k] += TWO_PI * basis(k, 0) / sr; if (lfoFase[k] >= TWO_PI) lfoFase[k] -= TWO_PI;
          double v = sin_(lfoFase[k]); uitw[k][0][0] = uitw[k][1][0] = v;
        }
        else if (kn.soort == Blok::sequencer) {
          // knoppen: tempo, lengte, zwaai, transponeer (halve tonen)
          auto& q = seq[k];
          bool open = q.poort(basis(k, 1));
          double r = toonVerhouding(q.toon + basis(k, 3));
          for (int c = 0; c < 2; ++c) { uitw[k][c][0] = open ? 1.0 : 0.0; uitw[k][c][1] = r; uitw[k][c][2] = q.accent ? 1.0 : 0.0; }
          q.stapVerder(basis(k, 0), basis(k, 1), basis(k, 2), sr);
        }
        else if (kn.soort == Blok::toetsen) {
          // knoppen: toon (verhouding), poort (0/1); de tool zet ze als plugin-parameter
          for (int c = 0; c < 2; ++c) { uitw[k][c][0] = basis(k, 1) > 0.5 ? 1.0 : 0.0; uitw[k][c][1] = basis(k, 0); }
        }
        else if (kn.soort == Blok::willekeur) { double v = wil[k].stap(basis(k, 0), basis(k, 1), sr); uitw[k][0][0] = uitw[k][1][0] = v; }
      }
      for (int c = 0; c < kanalen; ++c) {
        for (int k = 0; k < N; ++k) {
          const auto& kn = b->knoop[k];
          double* u = uitw[k][c];
          switch (kn.soort) {
            case Blok::in: u[0] = invoer(in[c][s]); break;
            case Blok::uit: {
              // beveiliging: een ontplofte keten (NaN, oneindig, of meer dan +18 dBFS) geeft stilte in plaats van een knal
              // in de speakers. Binnen ±8 verandert er niets, dus de null-tests merken het niet. uitvoer() poetst
              // subnormale uitvoer weg (een host mag die niet krijgen).
              double y = ingang(k, 0, c);
              uit[c][s] = (y == y && y < 8.0 && y > -8.0) ? uitvoer(y) : 0.0f;
              break;
            }
            case Blok::som: u[0] = ingang(k, 0, c) + ingang(k, 1, c); break;
            case Blok::versterker: u[0] = flush(waardeMod(k, 0, c) * ingang(k, 0, c)); break;
            case Blok::tape: u[0] = sat[k][c].stap(ingang(k, 0, c) * drive[k]) * invDrive[k]; break;
            case Blok::vertraging: {
              double d = waardeMod(k, 0, c) * 0.001 * sr * (1.0 + ingang(k, 1, c)) - kn.vast[1];
              vt[k][c].schrijf(ingang(k, 0, c));
              u[0] = vt[k][c].leesLagrange(d);
              break;
            }
            case Blok::hoogdoor1: case Blok::laagdoor1: {
              if (beweegt(k, 0)) een[k][c].zet(waardeMod(k, 0, c), sr);
              double x = ingang(k, 0, c);
              u[0] = kn.soort == Blok::hoogdoor1 ? een[k][c].hoog(x) : een[k][c].laag(x);
              break;
            }
            case Blok::svf: {
              if (beweegt(k, 0) || beweegt(k, 1)) svf[k][c].zet(waardeMod(k, 0, c), waardeMod(k, 1, c), sr);
              svf[k][c].stap(ingang(k, 0, c));
              u[0] = svf[k][c].lp; u[1] = svf[k][c].bp; u[2] = svf[k][c].hp;
              break;
            }
            case Blok::biquad: {
              if (beweegt(k, 0) || beweegt(k, 1)) bq[k][c].zet((BiquadSoort)(int)kn.vast[0], waardeMod(k, 0, c), waardeMod(k, 1, c), sr);
              u[0] = bq[k][c].stap(ingang(k, 0, c));
              break;
            }
            case Blok::mix: { double m = waardeMod(k, 0, c); u[0] = (1.0 - m) * ingang(k, 0, c) + m * ingang(k, 1, c); break; }
            // ---- pb_bronnen.h
            case Blok::osc: {
              // ingang toon = verhouding (1 = de knop), fm = lineaire FM: f · (1 + diepte · fm)
              double f = waardeMod(k, 0, c) * (verbonden(k, 0) ? ingang(k, 0, c) : 1.0) * toonVerhouding(waardeMod(k, 3, c) * 0.01);
              if (verbonden(k, 1)) f *= 1.0 + waardeMod(k, 2, c) * ingang(k, 1, c);
              u[0] = osc[k][c].stap(f, sr, (int)kn.vast[0], waardeMod(k, 1, c));
              break;
            }
            case Blok::ruis: u[0] = ruis[k][c].stap(waardeMod(k, 0, c)); break;
            case Blok::omhulling:
              u[0] = omh[k][c].stap(ingang(k, 0, c), waardeMod(k, 0, c), waardeMod(k, 1, c), waardeMod(k, 2, c), waardeMod(k, 3, c), sr);
              break;
            case Blok::snaar:
              u[0] = snaar[k][c].stap(ingang(k, 0, c), waardeMod(k, 0, c) * (verbonden(k, 1) ? ingang(k, 1, c) : 1.0), waardeMod(k, 1, c), waardeMod(k, 2, c), sr);
              break;
            case Blok::trommel:
              u[0] = trom[k][c].stap(ingang(k, 0, c), waardeMod(k, 0, c) * (verbonden(k, 1) ? ingang(k, 1, c) : 1.0), waardeMod(k, 1, c), waardeMod(k, 2, c), waardeMod(k, 3, c), sr);
              break;
            case Blok::vermenigvuldig: u[0] = flush(ingang(k, 0, c) * (verbonden(k, 1) ? ingang(k, 1, c) : 1.0)); break;
            case Blok::galm: u[0] = galm[k][c].stap(ingang(k, 0, c), waardeMod(k, 0, c), waardeMod(k, 1, c), waardeMod(k, 2, c), c, sr); break;
            case Blok::chorus: u[0] = chorus[k][c].stap(ingang(k, 0, c), waardeMod(k, 0, c), waardeMod(k, 1, c), waardeMod(k, 2, c), waardeMod(k, 3, c), sr); break;
            case Blok::compressor: {
              double x = ingang(k, 0, c);
              u[0] = comp[k][c].stap(x, verbonden(k, 1) ? ingang(k, 1, c) : x, waardeMod(k, 0, c), waardeMod(k, 1, c), waardeMod(k, 2, c), waardeMod(k, 3, c), sr);
              break;
            }
            case Blok::vergruizer: u[0] = gruis[k][c].stap(ingang(k, 0, c), waardeMod(k, 0, c), waardeMod(k, 1, c)); break;
            case Blok::vouwer: u[0] = vouw[k][c].stap(ingang(k, 0, c), waardeMod(k, 0, c), waardeMod(k, 1, c)); break;
            case Blok::panner: {
              double th = (klem(waardeMod(k, 0, c), -1.0, 1.0) + 1.0) * (0.25 * PI);   // gelijk vermogen
              u[0] = ingang(k, 0, c) * (kanalen < 2 ? 1.0 : (c == 0 ? cos_(th) : sin_(th)));
              break;
            }
            default: break;   // gedeelde knopen zijn al gerekend
          }
        }
      }
    }
  }

  // staart (s): de som van de vertragingen in een lus, met de lusversterking. Grof maar veilig: ≥ 1 = oneindig.
  double staartSec() const {
    double lus = 1.0, tijd = 0.0; bool vertraging = false;
    for (int k = 0; k < b->nKnopen; ++k) {
      const auto& kn = b->knoop[k];
      if (kn.soort == Blok::vertraging) { tijd += basis(k, 0) * 0.001 * 1.01; vertraging = true; }
      if (kn.soort == Blok::versterker) {
        // alleen versterkers in een terugkoppeling tellen: hun uitgang gaat naar een EERDERE knoop
        bool terug = false;
        for (int j = 0; j < b->nKnopen; ++j)
          for (const auto& i : b->knoop[j].in) if (i.knoop == k && j < k) terug = true;
        if (terug) lus *= fabs_(basis(k, 0));
      }
    }
    // blokken met een eigen staart (pb_bronnen.h): galm, snaar, trommel, loslaten van een omhulling
    double eigen = 0;
    for (int k = 0; k < b->nKnopen; ++k) {
      double t = 0;
      switch (b->knoop[k].soort) {
        case Blok::galm: t = basis(k, 0) * 1.1; break;
        case Blok::snaar: t = basis(k, 1); break;
        case Blok::trommel: t = basis(k, 2) * 0.001; break;
        case Blok::omhulling: t = basis(k, 3) * 0.001; break;
        default: break;
      }
      if (t > eigen) eigen = t;
    }
    if (!vertraging) return eigen > 0 ? (eigen > 60.0 ? 60.0 : eigen + 0.05) : 0.05;
    if (lus >= 0.999) return -1;
    if (lus < 1e-3) return tijd + 0.1 + eigen;
    double n = -4.5 / (log_(lus) * 0.43429448190325182765);
    double t = (n + 1.0) * tijd + 0.1 + eigen;
    return t > 60.0 ? 60.0 : t;
  }
};

}  // namespace pb
