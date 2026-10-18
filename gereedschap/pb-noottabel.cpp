// pb-noottabel — schrijft pb::nootVerhouding(n) voor n = 0..127 (dsp/pb_noten.h, gegeven), één regel "n verhouding"
// met 17 cijfers (exact terug te lezen als double). gereedschap/wasm-render.mjs zet daarmee noten om naar de Noot-
// parameter, zodat de verwachte uitvoer van de noten precies ARCHITECTUUR.md "Noten" volgt. Gebouwd met de DSP-vlaggen.
#include <cstdio>

#include "pb_noten.h"

int main() {
  for (int n = 0; n < 128; ++n) std::printf("%d %.17g\n", n, pb::nootVerhouding(n));
  return 0;
}
