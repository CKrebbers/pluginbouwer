// tekst.cpp — zie tekst.h.
#include "tekst.h"

#include <cmath>
#include <cstring>

namespace dub {
namespace {

// n / 10^dec met `dec` decimalen en een komma, n al afgerond
std::string vast(long long n, int dec) {
  std::string t;
  if (n < 0) { t = "-"; n = -n; }
  long long schaal = 1;
  for (int i = 0; i < dec; ++i) schaal *= 10;
  t += std::to_string(n / schaal);
  if (dec > 0) {
    std::string f = std::to_string(n % schaal);
    while ((int)f.size() < dec) f = "0" + f;
    t += "," + f;
  }
  return t;
}
long long rond(double x) { return (long long)std::floor(x + 0.5); }
bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

}  // namespace

std::string waardeNaarTekst(const char* e, double v) {
  if (!std::isfinite(v)) v = 0;
  if (is(e, "ms")) {  // onder 100: 1 decimaal; vanaf 100 hele ms (afronden eerst, zodat de tekst terug dezelfde geeft)
    long long n = rond(v * 10);
    if (n < 1000) return vast(n, 1) + " ms";
    return vast(rond(v), 0) + " ms";
  }
  if (is(e, "Hz")) {  // onder 1000 hele Hz; dan kHz met 2 decimalen, vanaf 10 000 met 1
    long long n = rond(v);
    if (n < 1000) return vast(n, 0) + " Hz";
    n = rond(v / 10);
    if (n < 1000) return vast(n, 2) + " kHz";
    return vast(rond(v / 100), 1) + " kHz";
  }
  if (is(e, "Q")) return "Q " + vast(rond(v * 100), 2);
  if (is(e, "dB")) return vast(rond(v * 10), 1) + " dB";
  return vast(rond(v * 100), 0) + " %";  // eenheidloos: procenten
}

bool tekstNaarWaarde(const char* e, const char* tekst, double min, double max, double& waarde) {
  if (!tekst) return false;
  std::string t;
  for (const char* p = tekst; *p; ++p) {  // spaties weg, kleine letters
    char c = *p;
    if (c == ' ' || c == '\t') continue;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    t += c;
  }
  size_t i = 0;
  if (is(e, "Q") && i < t.size() && t[i] == 'q') ++i;  // "Q 0,71"
  bool neg = false;
  if (i < t.size() && (t[i] == '-' || t[i] == '+')) neg = t[i++] == '-';
  double x = 0, schaal = 1;
  bool cijfers = false, komma = false;
  for (; i < t.size(); ++i) {
    const char c = t[i];
    if (c >= '0' && c <= '9') {
      cijfers = true;
      if (komma) { schaal /= 10; x += (c - '0') * schaal; }
      else x = x * 10 + (c - '0');
    } else if ((c == ',' || c == '.') && !komma) komma = true;
    else break;
  }
  if (!cijfers) return false;
  if (neg) x = -x;
  const std::string rest = t.substr(i);
  if (is(e, "ms")) { if (!(rest.empty() || rest == "ms")) return false; }
  else if (is(e, "Hz")) {
    if (rest == "k" || rest == "khz") x *= 1000;
    else if (!(rest.empty() || rest == "hz")) return false;
  } else if (is(e, "Q")) { if (!(rest.empty() || rest == "q")) return false; }
  else if (is(e, "dB")) { if (!(rest.empty() || rest == "db")) return false; }
  else {  // eenheidloos: de tekst is in procenten
    if (!(rest.empty() || rest == "%")) return false;
    x /= 100;
  }
  if (!std::isfinite(x)) return false;
  waarde = x < min ? min : (x > max ? max : x);
  return true;
}

}  // namespace dub
