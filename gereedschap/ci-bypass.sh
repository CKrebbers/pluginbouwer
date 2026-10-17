#!/bin/sh
# ci-bypass.sh — de bypass van de plugin (fase 5; parameter 9, CLAP_PARAM_IS_BYPASS) door pb-render.
#   sh gereedschap/ci-bypass.sh <build-dir> <matrix>       (<matrix> uit maak-matrix.mjs, met 48000 en 192000)
# Per samplerate (48 en 192 kHz) en indeling (stereo, mono, mono naar stereo), gewoon en in-place, wisselende blokken:
#   1. bypass aan vóór activate: uitgang = ingang, bit voor bit
#   2. automatisering 0 → 1 en 1 → 0 (events elke 64 samples): vóór de omslag en na 10 ms overvloeiing bit voor bit
#      de verwachte uitvoer of de ingang; het Ontwerp rekent tijdens de bypass door (vergelijk-bypass.mjs)
set -e
PAKKET="$(cd "$(dirname "$0")/.." && pwd)"
T="$PAKKET/testsignalen"
BUILD="$1"; M="$(cd "$2" && pwd)"
CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
RENDER="${PB_RENDER:-$BUILD/pb-render}"
[ -n "$CLAPPAD" ] && [ -d "$M" ] || { echo "gebruik: ci-bypass.sh <build-dir> <matrix>" >&2; exit 2; }
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }
{ echo "### Bypass ($(uname -m))"; echo; } | som
params="$(node -e "console.log(require('$T/gevallen.json').find(g => g.naam === 'echo-standaard').pbRender.map(p => '--p ' + p).join(' '))")"
UIT="$PAKKET/uit-matrix/bypass"; mkdir -p "$UIT"
# toets <soort> <render> <matrixmap> [K]: vergelijk-bypass.mjs; de exitcode is de poort, de regel gaat naar log en samenvatting
toets() {
  regel="$(node "$PAKKET/gereedschap/vergelijk-bypass.mjs" "$1" "$2" "$3/echo-standaard.verwacht.wav" "$3/echo-standaard.in.wav" $4)" ||
    { echo "$regel" >&2; exit 1; }
  echo "$regel" | tee /dev/stderr | som
}
for sr in 48000 192000; do
  for indeling in stereo mono mono-stereo; do
    V="$M/$sr/$indeling"
    for plek in "" "--in-place"; do
      tag="$sr-$indeling${plek:+-in-place}"
      # shellcheck disable=SC2086
      "$RENDER" "$CLAPPAD" "$V/echo-standaard.in.wav" "$UIT/$tag-aan.wav" --indeling "$indeling" --blok 3,128,1,4096,77 $plek $params --p 9=1
      toets aan "$UIT/$tag-aan.wav" "$V"
      for richting in naar-aan:0:1 naar-uit:1:0; do
        soort="${richting%%:*}"; glij="9=${richting#*:}"
        # shellcheck disable=SC2086
        "$RENDER" "$CLAPPAD" "$V/echo-standaard.in.wav" "$UIT/$tag-$soort.wav" --indeling "$indeling" --blok 3,128,1,4096,77 $plek \
          --automatie 64 $params --p 9=0 --glij "$glij"
        toets "$soort" "$UIT/$tag-$soort.wav" "$V" 64
      done
    done
  done
done
echo "Bypass: alles groen"
