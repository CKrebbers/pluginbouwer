#!/bin/sh
# ci-null.sh — de null-test van het bouwpakket in CI (en lokaal).
#   sh gereedschap/ci-null.sh clap <build-dir>                          pb-render door het .clap (blok 512, wisselend, 1, 4096,
#                                                                        state-rondje), en de matrix van fase 5
#   sh gereedschap/ci-null.sh pluginval <plugin> "<pluginval>" <label>  pluginval test (VST3/AU): render opnemen, meten, poort,
#                                                                        ook op elke samplerate van de matrix
# Verwacht <build-dir>/pb-render (uit gereedschap/pb-render.cpp) en <build-dir>/plugins/*.clap; of zet CLAP=… en PB_RENDER=….
# De matrix (fase 5): elke samplerate uit PB_SAMPLERATES (standaard 44,1 tot 192 kHz) × kanalen stereo, mono en mono naar
# stereo (CLAP, in-place en met wisselende blokken; pluginval: stereo). De referentie komt dan uit dezelfde WASM van de
# tool (gereedschap/verwacht.mjs), die eerst moet bewijzen dat hij testsignalen/*.verwacht.wav bit voor bit nadoet.
set -e
PAKKET="$(cd "$(dirname "$0")/.." && pwd)"
T="$PAKKET/testsignalen"
SOORT="$1"; shift
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }
SAMPLERATES="${PB_SAMPLERATES:-44100 48000 88200 96000 176400 192000}"
if [ "$SOORT" = clap ]; then
  BUILD="$1"
  CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
  RENDER="${PB_RENDER:-$BUILD/pb-render}"
  [ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
  for variant in "blok512|--blok 512" "wisselend|--blok 3,128,1,4096,77" "blok1|--blok 1" "blok4096|--blok 4096" \
                 "state|--blok 512 --state-rondje"; do
    naam="${variant%%|*}"; opties="${variant#*|}"; UIT="$PAKKET/uit/clap-$naam"; mkdir -p "$UIT"
    node -e "for (const g of require('$T/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" |
    while read -r geval params; do
      # shellcheck disable=SC2086
      "$RENDER" "$CLAPPAD" "$T/$geval.in.wav" "$UIT/$geval.wav" $opties $params
    done
    { echo "### Null-test CLAP ($naam, $(uname -m))"; echo; node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" || true; echo; } | tee /dev/stderr | som
    node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" > /dev/null
  done
  # de matrix: samplerate × kanalen, in-place (de uitgang in de buffers van de ingang) en met wisselende blokken
  for sr in $SAMPLERATES; do
    for kanalen in 2 1 1-2; do
      M="$PAKKET/uit/matrix/clap-$sr-$kanalen"; rm -rf "$M"; mkdir -p "$M/uit"
      node "$PAKKET/gereedschap/verwacht.mjs" "$M" --sr "$sr" --kanalen "$kanalen" > /dev/null
      node -e "for (const g of require('$T/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" |
      while read -r geval params; do
        # shellcheck disable=SC2086
        "$RENDER" "$CLAPPAD" "$T/$geval.in.wav" "$M/uit/$geval.wav" --sr "$sr" --kanalen "$kanalen" --in-place \
          --blok 3,128,1,4096,77 $params
      done
      { echo "### Null-test CLAP (matrix $sr Hz, kanalen $kanalen, in-place, wisselend, $(uname -m))"; echo;
        node "$PAKKET/gereedschap/vergelijk.mjs" "$M/uit" --verwacht "$M/verwacht" || true; echo; } | tee /dev/stderr | som
      node "$PAKKET/gereedschap/vergelijk.mjs" "$M/uit" --verwacht "$M/verwacht" > /dev/null
    done
  done
elif [ "$SOORT" = pluginval ]; then
  PLUGIN="$1"; PV="$2"; LABEL="$3"; UIT="$PAKKET/uit/pluginval-$LABEL"; mkdir -p "$UIT/cfg"
  for cfg in "$T"/pluginval/*.json; do
    geval="$(basename "$cfg" .json)"
    node -e "const c=require('$cfg'); c.input.audio='$T/'+c.input.audio.replace('../',''); c.reference='$UIT/'+c.name+'.wav'; require('fs').writeFileSync('$UIT/cfg/$geval.json', JSON.stringify(c,null,2));"
    rm -f "${UIT:?}/${geval:?}.wav" "${UIT:?}/${geval:?}.wav.json"
    $PV test --record-missing --plugin "$PLUGIN" "$UIT/cfg/$geval.json"
  done
  { echo "### Null-test $LABEL (pluginval test, blok 512)"; echo; node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" || true; echo; } | tee /dev/stderr | som
  for cfg in "$T"/pluginval/*.json; do
    geval="$(basename "$cfg" .json)"
    if node -e "process.exit(require('$T/gevallen.json').find(g => g.naam === '$geval').statistisch ? 0 : 1)"; then continue; fi
    $PV test --plugin "$PLUGIN" "$cfg"
  done
  node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" > /dev/null
  # de matrix: elke samplerate (stereo; pluginval test kiest zelf de kanalen van de plugin), blok 512
  for sr in $SAMPLERATES; do
    M="$PAKKET/uit/matrix/pluginval-$LABEL-$sr"; rm -rf "$M"; mkdir -p "$M/cfg" "$M/uit"
    node "$PAKKET/gereedschap/verwacht.mjs" "$M" --sr "$sr" --kanalen 2 > /dev/null
    for cfg in "$T"/pluginval/*.json; do
      geval="$(basename "$cfg" .json)"
      node -e "const c=require('$cfg'); c.input.audio='$M/in/$geval.wav'; c.sample_rate=$sr; c.reference='$M/uit/'+c.name+'.wav'; require('fs').writeFileSync('$M/cfg/$geval.json', JSON.stringify(c,null,2)); c.reference='$M/verwacht/'+c.name+'.wav'; require('fs').writeFileSync('$M/cfg/$geval.ref.json', JSON.stringify(c,null,2));"
      $PV test --record-missing --plugin "$PLUGIN" "$M/cfg/$geval.json"
      # ook de vergelijking van pluginval zelf tegen de referentie (niet voor het statistische geval)
      if node -e "process.exit(require('$T/gevallen.json').find(g => g.naam === '$geval').statistisch ? 0 : 1)"; then continue; fi
      $PV test --plugin "$PLUGIN" "$M/cfg/$geval.ref.json"
    done
    { echo "### Null-test $LABEL (matrix $sr Hz, pluginval test, blok 512)"; echo;
      node "$PAKKET/gereedschap/vergelijk.mjs" "$M/uit" --verwacht "$M/verwacht" || true; echo; } | tee /dev/stderr | som
    node "$PAKKET/gereedschap/vergelijk.mjs" "$M/uit" --verwacht "$M/verwacht" > /dev/null
  done
else
  echo "gebruik: ci-null.sh clap|pluginval …" >&2; exit 2
fi
