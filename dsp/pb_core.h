// pb_core.h — wat elk blok deelt: parameterbeschrijving, smoothing per sample, denormals, toeval.
#pragma once
#include <stdint.h>
#include "pb_math.h"

namespace pb {

// Denormals wegpoetsen. Hosts zetten FTZ/DAZ soms wel (JUCE, Logic) en soms niet (de browser): zonder dit verschilt
// de staart van een echo tussen browser en plugin zodra hij onder ~1e-308 komt, en kost hij op x86 veel CPU.
inline double flush(double x) { return (x > -1e-30 && x < 1e-30) ? 0.0 : x; }

// Invoer van de host: een denormale float wordt 0 zonder dat er met hem gerekend wordt (alleen de exponent-bits
// bekeken). Al het omzetten van een denormale float naar double kost op x86 een microcode-omweg (clap-validator
// "process-audio-denormals" mat tot 5,8× trager).
inline double invoer(float x) {
  uint32_t u; __builtin_memcpy(&u, &x, 4);
  return (u & 0x7f800000u) == 0 ? 0.0 : (double)x;
}

// Geheugen: de schil (plugin of WASM) reserveert vóór activeer één blok van Ontwerp::geheugenNodig(sr) bytes,
// buiten de audiothread. Het ontwerp deelt het zelf op; in verwerk() wordt nooit gealloceerd (regel 4).
struct Geheugen {
  unsigned char* p = nullptr;
  unsigned long over = 0;
  template <typename T> T* neem(unsigned long n) {
    unsigned long a = ((unsigned long)p + 15u) & ~(unsigned long)15u;
    unsigned long verlies = a - (unsigned long)p, nodig = verlies + n * sizeof(T);
    if (!p || nodig > over) return nullptr;
    p += nodig; over -= nodig;
    return reinterpret_cast<T*>(a);
  }
};
// bytes voor n elementen van T, met ruimte voor uitlijning
template <typename T> constexpr unsigned long bytesVoor(unsigned long n) { return n * sizeof(T) + 16; }
inline uint32_t machtVanTwee(double n) { uint32_t m = 1; while ((double)m < n) m <<= 1; return m; }

enum ParamVlag : uint32_t { AUTOMATEERBAAR = 1, STAPPEN = 2, GLIJDEN = 4 };

struct ParamInfo {
  uint32_t id;           // vast: verandert nooit tussen versies (state en automatisering hangen eraan)
  const char* naam;      // wat de host toont (kort)
  const char* uitleg;    // gewone woorden, voor de tool
  const char* eenheid;   // "Hz", "ms", "%", "dB", ""
  double min, max, standaard;
  double glijMs;         // smoothing-tijd; 0 = direct
  uint32_t vlaggen;
};

// Eén-pool-smoother per sample. Blokgrootte doet niet mee: elke sample één stap.
// `spring()` zet hem meteen op het doel: dat gebeurt bij de eerste sample na activeren of reset, zodat het niet
// uitmaakt of de host parameters vóór of na activate zet (pluginval doet het ervoor, een AudioWorklet erna).
struct Glijder {
  double doel = 0, nu = 0, k = 1;
  void zetTijd(double ms, double sr) { k = ms <= 0 ? 1.0 : 1.0 - exp_(-1000.0 / (ms * sr)); }
  void spring() { nu = doel; }
  // geeft true als de waarde veranderde (dan moet een blok zijn coëfficiënten opnieuw rekenen)
  bool stap() {
    if (nu == doel) return false;
    double d = doel - nu;
    if (fabs_(d) <= 1e-12 * (1.0 + fabs_(doel))) { nu = doel; return true; }
    nu += d * k;
    return true;
  }
};

// xorshift32: hetzelfde op elk platform, want alleen gehele getallen.
struct Toeval {
  uint32_t s = 0x9E3779B9u;
  void zaai(uint32_t z) { s = z ? z : 0x9E3779B9u; }
  uint32_t volgende() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  double tussen(double a, double b) { return a + (b - a) * ((double)(volgende() >> 8) * (1.0 / 16777216.0)); }
};

}  // namespace pb
