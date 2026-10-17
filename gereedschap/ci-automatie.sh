#!/bin/sh
# ci-automatie.sh — automatisering verandert niets aan de null-test (fase 2).
#   sh gereedschap/ci-automatie.sh <build-dir>
# 1. Null-test met automatisering: de parameters komen niet via flush vóór activate, maar als events in process (op
#    sample 0 en daarna elke 100 samples opnieuw, midden in blokken), zoals een DAW met een automatiseringslijn.
#    De uitvoer moet gelijk zijn aan de verwachte (vergelijk.mjs, ≤ −90 dBFS; verwacht bit voor bit).
# 2. Glijdende automatisering (Tijd 200→800 ms, Toon 500→8000 Hz in stappen van 64 samples): de render met blok 512,
#    met wisselende blokken en met blok 1 moet bit voor bit gelijk zijn (events op hun sample; splitsen verandert niets).
set -e
PAKKET="$(cd "$(dirname "$0")/.." && pwd)"
T="$PAKKET/testsignalen"
BUILD="$1"
CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
RENDER="${PB_RENDER:-$BUILD/pb-render}"
[ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }

for variant in "automatie-blok512|--blok 512" "automatie-wisselend|--blok 3,128,1,4096,77"; do
  naam="${variant%%|*}"; opties="${variant#*|} --automatie 100"; UIT="$PAKKET/uit/clap-$naam"; mkdir -p "$UIT"
  node -e "for (const g of require('$T/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" |
  while read -r geval params; do
    # shellcheck disable=SC2086
    "$RENDER" "$CLAPPAD" "$T/$geval.in.wav" "$UIT/$geval.wav" $opties $params
  done
  { echo "### Null-test CLAP met automatisering ($naam, $(uname -m))"; echo; node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" || true; echo; } | tee /dev/stderr | som
  node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" > /dev/null
done

UIT="$PAKKET/uit/clap-glij"; mkdir -p "$UIT"
params="$(node -e "console.log(require('$T/gevallen.json').find(g => g.naam === 'echo-standaard').pbRender.map(p => '--p ' + p).join(' '))")"
{ echo "### Glijdende automatisering: blokgrootte verandert niets ($(uname -m))"; echo; } | tee /dev/stderr | som
for glij in "1=200:800" "3=500:8000"; do
  g="id$(echo "$glij" | tr '=:' '_-')"  # bestandsnaam zonder : en = (upload-artifact weigert die)
  for blok in 512 3,128,1,4096,77 1; do
    # shellcheck disable=SC2086
    "$RENDER" "$CLAPPAD" "$T/echo-standaard.in.wav" "$UIT/glij-$g-$blok.wav" --blok "$blok" --automatie 64 --glij "$glij" $params
  done
  for blok in 3,128,1,4096,77 1; do
    if cmp -s "$UIT/glij-$g-512.wav" "$UIT/glij-$g-$blok.wav"; then
      echo "- glij $glij: blok $blok = blok 512, bit voor bit" | tee /dev/stderr | som
    else
      echo "- glij $glij: blok $blok VERSCHILT van blok 512" | tee /dev/stderr | som; exit 1
    fi
  done
  if cmp -s "$UIT/glij-$g-512.wav" "$PAKKET/uit/clap-automatie-blok512/echo-standaard.wav"; then
    echo "- glij $glij: de automatisering deed niets (toets zegt niets)" | tee /dev/stderr | som; exit 1
  fi
done
