// graaf_render.cpp — de ketenmotor (dsp/pb_graaf.h) NATIVE, voor tests/native.test.mjs: rendert een keten uit een
// bestand en schrijft de uitvoer weg. Zo legt de toets de native build (x86-64/arm64, de compiler van de machine)
// bit voor bit naast de WASM van de tool. Zelfde vlaggen als de plugin (-ffp-contract=off, geen fast-math).
//   graaf_render <invoer.bin> <uitvoer.f32>
// invoer.bin (little endian): double sr; int32 n, kanalen, blok, nRij; double rij[nRij]; int32 nGebeurtenissen;
//   per gebeurtenis: int32 sample, int32 param, double waarde; daarna float invoer[kanalen][n]
#include <cstdio>
#include <cstdint>
#include <vector>
#include "pb_graaf.h"

int main(int argc, char** argv) {
  if (argc != 3) { std::fprintf(stderr, "gebruik: graaf_render invoer.bin uitvoer.f32\n"); return 2; }
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) return 3;
  double sr; int32_t n, kanalen, blok, nRij, nGeb;
  if (std::fread(&sr, 8, 1, f) != 1 || std::fread(&n, 4, 1, f) != 1 || std::fread(&kanalen, 4, 1, f) != 1 ||
      std::fread(&blok, 4, 1, f) != 1 || std::fread(&nRij, 4, 1, f) != 1) return 4;
  std::vector<double> rij(nRij);
  if (std::fread(rij.data(), 8, nRij, f) != (size_t)nRij || std::fread(&nGeb, 4, 1, f) != 1) return 5;
  struct Geb { int32_t s, p; double v; };
  std::vector<Geb> geb(nGeb);
  for (auto& g : geb) if (std::fread(&g.s, 4, 1, f) != 1 || std::fread(&g.p, 4, 1, f) != 1 || std::fread(&g.v, 8, 1, f) != 1) return 6;
  std::vector<std::vector<float>> in(kanalen, std::vector<float>(n)), uit(kanalen, std::vector<float>(n));
  for (auto& k : in) if (std::fread(k.data(), 4, n, f) != (size_t)n) return 7;
  std::fclose(f);

  static pb::GraafBeschrijving b;
  if (!pb::leesBeschrijving(rij.data(), nRij, b)) { std::fprintf(stderr, "beschrijving klopt niet\n"); return 8; }
  static pb::Graaf g;
  g.koppel(&b);
  unsigned long bytes = pb::graafGeheugen(b, sr);
  std::vector<unsigned char> geheugen(bytes + 64, 0);
  pb::Geheugen gh; gh.p = geheugen.data(); gh.over = geheugen.size();
  g.activeer(sr, gh);
  size_t j = 0;
  for (int32_t pos = 0; pos < n;) {
    while (j < geb.size() && geb[j].s <= pos) { g.zetParam(geb[j].p, geb[j].v); ++j; }
    int32_t m = blok < n - pos ? blok : n - pos;
    if (j < geb.size() && geb[j].s - pos < m) m = geb[j].s - pos;
    const float* ip[2] = {in[0].data() + pos, in[kanalen > 1 ? 1 : 0].data() + pos};
    float* op[2] = {uit[0].data() + pos, uit[kanalen > 1 ? 1 : 0].data() + pos};
    g.verwerk(ip, op, kanalen, (uint32_t)m);
    pos += m;
  }
  FILE* o = std::fopen(argv[2], "wb");
  if (!o) return 9;
  for (auto& k : uit) std::fwrite(k.data(), 4, n, o);
  std::fclose(o);
  return 0;
}
