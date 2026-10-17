#!/bin/sh
# toets-fase2.sh — de eigen toetsen van fase 2 (in CI en lokaal), na `cmake --build <build>` met PB_TOETSEN=ON.
#   sh plugin/toets/toets-fase2.sh <build-dir>
# 0. tekst <-> waarde in gewone eenheden, ook onder een locale met een decimale komma (pb-toets tekst; met
#    PB_EIS_KOMMA=1 faalt hij als zo'n locale ontbreekt);
# 1. state-toets: voor elke parameter N laadt de state van build/zonder/zonder-N.clap (een build zonder N) in de echte
#    .clap, en omgekeerd (pb-toets state);
# 2. automatisering: events midden in blokken werken op hun sample, los van de blokindeling (pb-toets automatisering);
# 3. null-test met de waarden als automatisering: elk testgeval met zijn waarden als events midden in wisselende
#    blokken, tegen de verwachte uitvoer uit de WASM (gereedschap/vergelijk.mjs).
set -e
PAKKET="$(cd "$(dirname "$0")/../.." && pwd)"
T="$PAKKET/testsignalen"
BUILD="$1"
[ -n "$BUILD" ] || { echo "gebruik: toets-fase2.sh <build-dir>" >&2; exit 2; }
CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
TOETS="$BUILD/pb-toets"
[ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }

echo "### State-toets (vooruit- en achteruit-compatibel, $(uname -m))"
aantal=0
for oud in "$BUILD"/zonder/zonder-*.clap; do
  n="$(basename "$oud" .clap)"; n="${n#zonder-}"
  "$TOETS" state "$oud" "$CLAPPAD" "$n"
  aantal=$((aantal + 1))
done
[ "$aantal" -gt 0 ] || { echo "geen builds zonder parameter gevonden in $BUILD/zonder" >&2; exit 1; }
echo "state-toets: $aantal van $aantal builds zonder parameter N geslaagd" | tee /dev/stderr | som

echo "### Tekst <-> waarde ($(uname -m))"
"$TOETS" tekst "$CLAPPAD"
echo "tekst <-> waarde: bekende paren en rondgang over het hele bereik geslaagd" | som

echo "### Automatisering (events op hun sample, $(uname -m))"
"$TOETS" automatisering "$CLAPPAD"
echo "automatisering: bit voor bit gelijk bij blok 512, wisselend, 1, 4096 en gesplitst op events" | som

UIT="$PAKKET/uit/clap-automatisering"; mkdir -p "$UIT"
node -e "for (const g of require('$T/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" |
while read -r geval params; do
  # shellcheck disable=SC2086
  "$TOETS" render "$CLAPPAD" "$T/$geval.in.wav" "$UIT/$geval.wav" $params
done
{ echo "### Null-test CLAP (waarden als automatisering, $(uname -m))"; echo; node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" || true; echo; } | tee /dev/stderr | som
node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" > /dev/null
