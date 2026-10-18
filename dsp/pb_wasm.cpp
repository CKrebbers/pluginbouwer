// pb_wasm.cpp — C-ABI rond een Ontwerp, voor WASM (browser, tool, Node-toetsen).
// Geen libc, geen malloc: een vaste pool van instanties en vaste buffers. JavaScript schrijft invoer in pb_in(),
// roept pb_verwerk() en leest pb_uit(). Gebouwd door dsp/bouw-wasm.sh.
#ifndef PB_ONTWERP
#define PB_ONTWERP "ontwerpen/pijplijn_biquad.h"
#endif
#include PB_ONTWERP
#include "pb_tape.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

// placement-new zonder <new> (er is geen libc/libc++)
inline void* operator new(unsigned long, void* p) noexcept { return p; }

namespace {
constexpr double MAX_SR = 192000;  // geheugen per instantie wordt voor de hoogste samplerate gereserveerd
constexpr int MAX_INST = 8;
constexpr uint32_t MAX_BLOK = 4096;
// Ruwe opslag + placement-new: geen globale constructor (zie bouw-wasm.sh waarom dat hier fataal is).
alignas(pb::Ontwerp) unsigned char instOpslag[MAX_INST][sizeof(pb::Ontwerp)];
struct InstTabel { pb::Ontwerp& operator[](int h) { return *reinterpret_cast<pb::Ontwerp*>(instOpslag[h]); } } inst;
bool bezet[MAX_INST];
float inBuf[MAX_INST][2][MAX_BLOK];
float uitBuf[MAX_INST][2][MAX_BLOK];
unsigned char* geheugen[MAX_INST];
unsigned long geheugenGrootte[MAX_INST];
extern "C" unsigned char __heap_base;
unsigned long heapEind = 0;

// bump-allocator op het einde van het WASM-geheugen; groeit met memory.grow. Alleen bij pb_maak, nooit in verwerk.
unsigned char* reserveer(unsigned long n) {
  if (!heapEind) heapEind = (unsigned long)&__heap_base;
  unsigned long begin = (heapEind + 15u) & ~(unsigned long)15u, eind = begin + n;
  unsigned long nu = __builtin_wasm_memory_size(0) * 65536ul;
  if (eind > nu && __builtin_wasm_memory_grow(0, (eind - nu + 65535ul) / 65536ul) == (unsigned long)-1) return nullptr;
  heapEind = eind;
  return (unsigned char*)begin;
}
pb::Geheugen geheugenVan(int h) {
  pb::Geheugen g; g.p = geheugen[h]; g.over = geheugenGrootte[h];
  for (unsigned long i = 0; i < g.over; ++i) g.p[i] = 0;
  return g;
}
}  // namespace

// geheugen voor memset/memcpy die clang zelf kan genereren bij het kopiëren van structs
extern "C" void* memset(void* d, int c, unsigned long n) {
  unsigned char* p = (unsigned char*)d; while (n--) *p++ = (unsigned char)c; return d;
}
extern "C" void* memcpy(void* d, const void* s, unsigned long n) {
  unsigned char* p = (unsigned char*)d; const unsigned char* q = (const unsigned char*)s; while (n--) *p++ = *q++; return d;
}

EXPORT int pb_maak(double sr) {
  for (int i = 0; i < MAX_INST; ++i)
    if (!bezet[i]) {
      unsigned long n = pb::Ontwerp::geheugenNodig(MAX_SR);
      if (n > geheugenGrootte[i]) { geheugen[i] = reserveer(n); geheugenGrootte[i] = geheugen[i] ? n : 0; if (!geheugen[i]) return -1; }
      bezet[i] = true; new (instOpslag[i]) pb::Ontwerp(); inst[i].activeer(sr, MAX_BLOK, geheugenVan(i)); return i;
    }
  return -1;
}
EXPORT void pb_weg(int h) { if (h >= 0 && h < MAX_INST) bezet[h] = false; }
EXPORT void pb_activeer(int h, double sr) { inst[h].activeer(sr > MAX_SR ? MAX_SR : sr, MAX_BLOK, geheugenVan(h)); }
EXPORT void pb_reset(int h) { inst[h].reset(); }
EXPORT int pb_aantal_params() { return pb::Ontwerp::N_PARAMS; }
EXPORT double pb_param_min(int i) { return pb::Ontwerp::PARAMS[i].min; }
EXPORT double pb_param_max(int i) { return pb::Ontwerp::PARAMS[i].max; }
EXPORT double pb_param_standaard(int i) { return pb::Ontwerp::PARAMS[i].standaard; }
EXPORT unsigned pb_param_id(int i) { return pb::Ontwerp::PARAMS[i].id; }
EXPORT const char* pb_param_naam(int i) { return pb::Ontwerp::PARAMS[i].naam; }
EXPORT const char* pb_param_eenheid(int i) { return pb::Ontwerp::PARAMS[i].eenheid; }
EXPORT const char* pb_param_uitleg(int i) { return pb::Ontwerp::PARAMS[i].uitleg; }
EXPORT void pb_zet(int h, int i, double v) { inst[h].zetParam(i, v); }
EXPORT double pb_lees(int h, int i) { return inst[h].leesParam(i); }
EXPORT unsigned pb_latentie(int h) { return inst[h].latentie(); }
EXPORT double pb_staart(int h) { return inst[h].staartSec(); }
EXPORT float* pb_in(int h, int c) { return inBuf[h][c]; }
EXPORT float* pb_uit(int h, int c) { return uitBuf[h][c]; }
EXPORT unsigned pb_max_blok() { return MAX_BLOK; }
#ifdef PB_HEEFT_TRANSPORT
// transport van de host (ketenontwerp): tempo in bpm, plek in tellen, speelt. De plugin roept dit per blok, net als hier.
EXPORT void pb_transport(int h, double bpm, double beat, int speelt) { inst[h].zetTransport(bpm, beat, speelt != 0); }
#endif
EXPORT void pb_verwerk(int h, int kanalen, unsigned n) {
  if (n > MAX_BLOK) n = MAX_BLOK;
  const float* in[2] = {inBuf[h][0], inBuf[h][1]};
  float* uit[2] = {uitBuf[h][0], uitBuf[h][1]};
  inst[h].verwerk(in, uit, kanalen, n);
}

// alleen voor tests/math.test.mjs: de wiskunde zelf, zodat we hem tegen Math.* kunnen leggen
EXPORT double pb_t_sin(double x) { return pb::sin_(x); }
EXPORT double pb_t_cos(double x) { return pb::cos_(x); }
EXPORT double pb_t_exp(double x) { return pb::exp_(x); }
EXPORT double pb_t_log(double x) { return pb::log_(x); }
EXPORT double pb_t_tanh(double x) { return pb::tanh_(x); }
EXPORT double pb_t_logcosh(double x) { return pb::logcosh_(x); }

// alleen voor tests/dubecho.test.mjs: het saturatieblok los, op een buffer (in-place), voor de aliasingmeting
EXPORT void pb_t_tape(float* x, unsigned n, double drive) {
  pb::TapeVerzadiging t; double g = drive, ig = 1.0 / drive;
  for (unsigned i = 0; i < n; ++i) x[i] = (float)(t.stap(g * (double)x[i]) * ig);
}
EXPORT float* pb_t_buffer() { return inBuf[MAX_INST - 1][0]; }
