// param_tekst.h — parameterwaarde ↔ tekst in gewone eenheden (CLAP params.value_to_text / text_to_value).
// De waarde zelf blijft in de eenheid van PARAMS (ms, Hz, dB, Q, 0..1); alleen de tekst rondt af. Regels:
//   ms  : "375 ms" (vanaf 100 ms hele ms), "93.8 ms" (daaronder op 0,1 ms); lezen kent ook "1.2 s".
//   Hz  : "12.0 kHz" (vanaf 10 kHz), "3.00 kHz" (vanaf 1 kHz), "265 Hz" (vanaf 100 Hz), "81.3 Hz"; lezen kent "k"/"kHz".
//   dB  : "6.0 dB".   Q: "0.71 Q".   "" met bereik ≤ 2 (terugkoppeling, mix): procent, "37.5 %".
//   Een parameter met de vlag STAPPEN toont en leest hele getallen.
// Tekst → waarde → tekst geeft dezelfde tekst (clap-validator param-conversions toetst dat). De tekst hangt niet af van
// de locale van de host: we schrijven en lezen zelf (punt; bij lezen mag het ook een komma zijn).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pb_core.h"

namespace tekst {

struct Trede {
  double vanaf;      // in de eenheid van PARAMS: deze trede geldt als de afgeronde waarde ≥ vanaf
  double mul, deel;  // getoond getal = waarde * mul / deel
  int decimalen;
  const char* achter;
};

// treden van grof naar fijn; de laatste is de terugval
struct Opmaak { Trede t[4]; int n; };

inline Opmaak opmaakVoor(const pb::ParamInfo& p) {
  const char* e = p.eenheid;
  if (p.vlaggen & pb::STAPPEN) return {{{-1e300, 1, 1, 0, e}}, 1};
  if (!std::strcmp(e, "ms")) return {{{100, 1, 1, 0, "ms"}, {-1e300, 1, 1, 1, "ms"}}, 2};
  if (!std::strcmp(e, "Hz"))
    return {{{10000, 1, 1000, 1, "kHz"}, {1000, 1, 1000, 2, "kHz"}, {100, 1, 1, 0, "Hz"}, {-1e300, 1, 1, 1, "Hz"}}, 4};
  if (!std::strcmp(e, "dB")) return {{{-1e300, 1, 1, 1, "dB"}}, 1};
  if (!std::strcmp(e, "Q")) return {{{-1e300, 1, 1, 2, "Q"}}, 1};
  if (!std::strcmp(e, "%")) return {{{-1e300, 1, 1, 1, "%"}}, 1};
  if (!*e && p.min >= -2 && p.max <= 2) return {{{-1e300, 100, 1, 1, "%"}}, 1};
  return {{{-1e300, 1, 1, 2, e}}, 1};
}

inline double macht10(int d) { double m = 1; while (d-- > 0) m *= 10; return m; }
inline long long rond(double x) { return (long long)(x < 0 ? x - 0.5 : x + 0.5); }

// waarde → tekst; false als het niet past
inline bool schrijf(const pb::ParamInfo& p, double v, char* uit, uint32_t n) {
  const Opmaak o = opmaakVoor(p);
  const Trede* t = &o.t[o.n - 1];
  long long k = 0;
  for (int j = 0; j < o.n; ++j) {
    const double m = macht10(o.t[j].decimalen);
    k = rond(v * o.t[j].mul / o.t[j].deel * m);
    if ((double)k / m * o.t[j].deel / o.t[j].mul >= o.t[j].vanaf) { t = &o.t[j]; break; }
  }
  const bool neg = k < 0;
  unsigned long long a = (unsigned long long)(neg ? -k : k), m = (unsigned long long)macht10(t->decimalen);
  char getal[48];
  if (t->decimalen > 0)
    std::snprintf(getal, sizeof getal, "%s%llu.%0*llu", neg ? "-" : "", a / m, t->decimalen, a % m);
  else
    std::snprintf(getal, sizeof getal, "%s%llu", neg ? "-" : "", a);
  int w = *t->achter ? std::snprintf(uit, n, "%s %s", getal, t->achter) : std::snprintf(uit, n, "%s", getal);
  return w > 0 && (uint32_t)w < n;
}

inline char klein(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
inline bool isGelijk(const char* a, const char* b) {  // hoofdletterongevoelig
  for (; *a && *b; ++a, ++b) if (klein(*a) != klein(*b)) return false;
  return *a == *b;
}

// tekst → waarde (in de eenheid van PARAMS, nog niet begrensd); false als het geen getal is
inline bool lees(const pb::ParamInfo& p, const char* s, double* v) {
  while (*s == ' ' || *s == '\t') ++s;
  bool neg = false;
  if (*s == '+' || *s == '-') neg = *s++ == '-';
  unsigned long long mant = 0;
  int cijfers = 0, achterKomma = 0;
  bool komma = false;
  for (;; ++s) {
    if (*s >= '0' && *s <= '9') {
      if (cijfers < 17) { mant = mant * 10 + (unsigned)(*s - '0'); ++cijfers; if (komma) ++achterKomma; }
      else if (!komma) return false;  // te groot
    } else if ((*s == '.' || *s == ',') && !komma) {
      komma = true;
    } else {
      break;
    }
  }
  if (cijfers == 0) return false;
  double x = (double)mant / macht10(achterKomma);
  if (neg) x = -x;
  while (*s == ' ' || *s == '\t') ++s;
  char eenheid[16] = {0};
  size_t k = 0;
  while (*s && *s != ' ' && *s != '\t' && k + 1 < sizeof eenheid) eenheid[k++] = *s++;
  while (*s == ' ' || *s == '\t') ++s;
  if (*s) return false;

  const char* e = p.eenheid;
  const Opmaak o = opmaakVoor(p);
  const bool ms = !std::strcmp(e, "ms"), hz = !std::strcmp(e, "Hz");
  double mul = 0, deel = 0;  // getoond getal → waarde: x * deel / mul (zoals de treden, omgekeerd)
  if (!*eenheid) {  // een kaal getal is in de fijnste eenheid: "50" bij procent = 50 %, bij Hz = 50 Hz
    mul = ms || hz ? 1 : o.t[o.n - 1].mul;
    deel = ms || hz ? 1 : o.t[o.n - 1].deel;
  } else if (ms && isGelijk(eenheid, "s")) {
    mul = 1; deel = 1000;
  } else if (hz && isGelijk(eenheid, "k")) {
    mul = 1; deel = 1000;
  } else {
    for (int j = 0; j < o.n && mul == 0; ++j)
      if (isGelijk(eenheid, o.t[j].achter)) { mul = o.t[j].mul; deel = o.t[j].deel; }
    if (mul == 0 && *e && isGelijk(eenheid, e)) mul = deel = 1;
    if (mul == 0) return false;
  }
  x = x * deel / mul;
  if (p.vlaggen & pb::STAPPEN) x = (double)rond(x);
  *v = x;
  return true;
}

}  // namespace tekst
