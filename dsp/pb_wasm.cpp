// pb_wasm.cpp — C-ABI rond een Ontwerp, voor WASM (browser, tool, Node-toetsen).
// Geen libc, geen malloc: een vaste pool van instanties en vaste buffers. JavaScript schrijft invoer in pb_in(),
// roept pb_verwerk() en leest pb_uit(). Gebouwd door dsp/bouw-wasm.sh.
#ifndef PB_ONTWERP
#define PB_ONTWERP "ontwerpen/pijplijn_biquad.h"
#endif
#include PB_ONTWERP

#define EXPORT extern "C" __attribute__((visibility("default")))

namespace {
constexpr int MAX_INST = 8;
constexpr uint32_t MAX_BLOK = 4096;
pb::Ontwerp inst[MAX_INST];
bool bezet[MAX_INST];
float inBuf[MAX_INST][2][MAX_BLOK];
float uitBuf[MAX_INST][2][MAX_BLOK];
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
    if (!bezet[i]) { bezet[i] = true; inst[i] = pb::Ontwerp(); inst[i].activeer(sr, MAX_BLOK); return i; }
  return -1;
}
EXPORT void pb_weg(int h) { if (h >= 0 && h < MAX_INST) bezet[h] = false; }
EXPORT void pb_activeer(int h, double sr) { inst[h].activeer(sr, MAX_BLOK); }
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
