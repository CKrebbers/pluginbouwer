#!/bin/sh
# ci-null.sh — de null-test van het bouwpakket in CI (en lokaal).
#   sh gereedschap/ci-null.sh clap <build-dir>                          pb-render door het .clap (blok 512, wisselend, state-rondje)
#   sh gereedschap/ci-null.sh pluginval <plugin> "<pluginval>" <label>  pluginval test (VST3/AU): render opnemen, meten, poort
# Verwacht <build-dir>/pb-render (uit gereedschap/pb-render.cpp) en <build-dir>/plugins/*.clap; of zet CLAP=… en PB_RENDER=….
set -e
PAKKET="$(cd "$(dirname "$0")/.." && pwd)"
T="$PAKKET/testsignalen"
SOORT="$1"; shift
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }
if [ "$SOORT" = clap ]; then
  BUILD="$1"
  CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
  RENDER="${PB_RENDER:-$BUILD/pb-render}"
  [ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
  for variant in "blok512|--blok 512" "wisselend|--blok 3,128,1,4096,77" "state|--blok 512 --state-rondje"; do
    naam="${variant%%|*}"; opties="${variant#*|}"; UIT="$PAKKET/uit/clap-$naam"; mkdir -p "$UIT"
    node -e "for (const g of require('$T/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" |
    while read -r geval params; do
      # shellcheck disable=SC2086
      "$RENDER" "$CLAPPAD" "$T/$geval.in.wav" "$UIT/$geval.wav" $opties $params
    done
    { echo "### Null-test CLAP ($naam, $(uname -m))"; echo; node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" || true; echo; } | tee /dev/stderr | som
    node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" > /dev/null
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
else
  echo "gebruik: ci-null.sh clap|pluginval …" >&2; exit 2
fi
