// rend.cpp — native render van de door Faust gegenereerde C++ (zelfde rol als host/pb-render voor onze stack).
//   rend <in.wav> <uit.wav> <blok> naam=waarde ...
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif
#include "arch.h"
#define virtual
#include PB_FAUST_GEN
#undef virtual

static std::vector<std::vector<float>> leesWav(const char* p, unsigned& sr) {
  FILE* f = std::fopen(p, "rb"); std::vector<unsigned char> b; unsigned char t[65536]; size_t r;
  while ((r = std::fread(t, 1, sizeof t, f)) > 0) b.insert(b.end(), t, t + r); std::fclose(f);
  auto u16 = [&](size_t o) { return (unsigned)b[o] | ((unsigned)b[o + 1] << 8); };
  auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
  size_t q = 12, off = 0, len = 0; unsigned k = 2;
  while (q + 8 <= b.size()) { unsigned l = u32(q + 4); if (!std::memcmp(&b[q], "fmt ", 4)) { k = u16(q + 10); sr = u32(q + 12); }
    if (!std::memcmp(&b[q], "data", 4)) { off = q + 8; len = l; } q += 8 + l + (l & 1); }
  size_t n = len / (4 * k); std::vector<std::vector<float>> x(k, std::vector<float>(n));
  for (size_t i = 0; i < n; ++i) for (unsigned c = 0; c < k; ++c) std::memcpy(&x[c][i], &b[off + (i * k + c) * 4], 4);
  return x;
}
static void schrijfWav(const char* p, const std::vector<std::vector<float>>& x, unsigned sr) {
  FILE* f = std::fopen(p, "wb"); unsigned k = x.size(), n = x[0].size(), d = n * k * 4;
  auto w32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); }; auto w16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); w32(36 + d); std::fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(3); w16(k); w32(sr); w32(sr * k * 4); w16(k * 4); w16(32);
  std::fwrite("data", 1, 4, f); w32(d); for (unsigned i = 0; i < n; ++i) for (unsigned c = 0; c < k; ++c) std::fwrite(&x[c][i], 4, 1, f); std::fclose(f);
}
int main(int argc, char** argv) {
  unsigned sr = 48000; auto in = leesWav(argv[1], sr); int blok = std::atoi(argv[3]);
  FaustProef d; UI ui; d.buildUserInterface(&ui); d.init((int)sr);
  for (int i = 4; i < argc; ++i) { std::string a = argv[i]; auto k = a.find('='); *ui.zones.at(a.substr(0, k)) = (FAUSTFLOAT)std::atof(a.substr(k + 1).c_str()); }
  size_t n = in[0].size(); std::vector<std::vector<float>> uit(2, std::vector<float>(n));
  std::vector<FAUSTFLOAT> bi[2], bu[2]; for (int c = 0; c < 2; ++c) { bi[c].resize(blok); bu[c].resize(blok); }
  for (size_t p = 0; p < n; p += blok) {
    int m = (int)std::min<size_t>(blok, n - p);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < m; ++i) bi[c][i] = in[c][p + i];
    FAUSTFLOAT* ip[2] = {bi[0].data(), bi[1].data()}; FAUSTFLOAT* op[2] = {bu[0].data(), bu[1].data()};
    d.compute(m, ip, op);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < m; ++i) uit[c][p + i] = (float)bu[c][i];
  }
  schrijfWav(argv[2], uit, sr);
}
