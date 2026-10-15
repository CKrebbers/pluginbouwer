// pb_graaf_wasm.cpp — de ketenmotor als WASM, voor de tool: ketens worden tijdens het ontwerpen geladen.
// Zelfde regels als pb_wasm.cpp: geen libc, geen globale constructors, geheugen per instantie vooraf gereserveerd.
#include "pb_graaf.h"

#define EXPORT extern "C" __attribute__((visibility("default")))
inline void* operator new(unsigned long, void* p) noexcept { return p; }
extern "C" void* memset(void* d, int c, unsigned long n) { unsigned char* p = (unsigned char*)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void* memcpy(void* d, const void* s, unsigned long n) { unsigned char* p = (unsigned char*)d; const unsigned char* q = (const unsigned char*)s; while (n--) *p++ = *q++; return d; }

namespace {
constexpr int MAX_INST = 6;
constexpr uint32_t MAX_BLOK = 4096;
constexpr unsigned long ARENA = 24ul << 20;   // per instantie; genoeg voor 6 vertragingen van 2 s op 96 kHz, stereo
alignas(pb::Graaf) unsigned char opslag[MAX_INST][sizeof(pb::Graaf)];
alignas(pb::GraafBeschrijving) unsigned char beschrOpslag[MAX_INST][sizeof(pb::GraafBeschrijving)];
bool bezet[MAX_INST], geladen[MAX_INST];
unsigned char* arena[MAX_INST];
double sr_[MAX_INST];
float inBuf[MAX_INST][2][MAX_BLOK], uitBuf[MAX_INST][2][MAX_BLOK];
double invoerRij[2 + pb::MAX_KNOPEN * pb::GETALLEN_PER_KNOOP + pb::MAX_PARAMS * pb::GETALLEN_PER_PARAM];
extern "C" unsigned char __heap_base;
unsigned long heapEind = 0;
unsigned char* reserveer(unsigned long n) {
  if (!heapEind) heapEind = (unsigned long)&__heap_base;
  unsigned long begin = (heapEind + 15u) & ~(unsigned long)15u, eind = begin + n;
  unsigned long nu = __builtin_wasm_memory_size(0) * 65536ul;
  if (eind > nu && __builtin_wasm_memory_grow(0, (eind - nu + 65535ul) / 65536ul) == (unsigned long)-1) return nullptr;
  heapEind = eind; return (unsigned char*)begin;
}
pb::Graaf& G(int h) { return *reinterpret_cast<pb::Graaf*>(opslag[h]); }
pb::GraafBeschrijving& B(int h) { return *reinterpret_cast<pb::GraafBeschrijving*>(beschrOpslag[h]); }
}  // namespace

EXPORT double* pg_rij() { return invoerRij; }
EXPORT int pg_rij_max() { return (int)(sizeof(invoerRij) / sizeof(double)); }
EXPORT int pg_maak(double sr) {
  for (int i = 0; i < MAX_INST; ++i)
    if (!bezet[i]) {
      if (!arena[i] && !(arena[i] = reserveer(ARENA))) return -1;
      bezet[i] = true; geladen[i] = false; sr_[i] = sr; return i;
    }
  return -1;
}
EXPORT void pg_weg(int h) { if (h >= 0 && h < MAX_INST) bezet[h] = false; }
// laadt de keten uit pg_rij() (n getallen). Geeft 1 als het gelukt is, 0 als de beschrijving niet klopt of te veel
// geheugen vraagt. Een nieuwe keten begint stil (de oude staart is weg): de tool vloeit zelf over.
EXPORT int pg_laad(int h, int n) {
  new (beschrOpslag[h]) pb::GraafBeschrijving();
  if (!pb::leesBeschrijving(invoerRij, n, B(h))) return 0;
  if (pb::graafGeheugen(B(h), sr_[h]) > ARENA) return 0;
  for (unsigned long i = 0; i < ARENA; ++i) arena[h][i] = 0;
  new (opslag[h]) pb::Graaf();
  G(h).koppel(&B(h));
  pb::Geheugen g; g.p = arena[h]; g.over = ARENA;
  G(h).activeer(sr_[h], g);
  geladen[h] = true;
  return 1;
}
EXPORT void pg_reset(int h) { if (geladen[h]) G(h).reset(); }
EXPORT void pg_zet(int h, int i, double v) { if (geladen[h]) G(h).zetParam(i, v); }
EXPORT double pg_lees(int h, int i) { return geladen[h] ? G(h).leesParam(i) : 0.0; }
EXPORT double pg_staart(int h) { return geladen[h] ? G(h).staartSec() : 0.0; }
EXPORT float* pg_in(int h, int c) { return inBuf[h][c]; }
EXPORT float* pg_uit(int h, int c) { return uitBuf[h][c]; }
EXPORT void pg_verwerk(int h, int kanalen, unsigned n) {
  if (!geladen[h]) { for (int c = 0; c < kanalen; ++c) for (unsigned i = 0; i < n; ++i) uitBuf[h][c][i] = 0; return; }
  if (n > MAX_BLOK) n = MAX_BLOK;
  const float* in[2] = {inBuf[h][0], inBuf[h][1]};
  float* uit[2] = {uitBuf[h][0], uitBuf[h][1]};
  G(h).verwerk(in, uit, kanalen, n);
}
// meetwaarde van een knoop-uitgang (kanaal 0), voor de tool (meters op de blokken)
EXPORT double pg_knoop_uit(int h, int k, int poort) { return geladen[h] ? G(h).uitw[k][0][poort] : 0.0; }
