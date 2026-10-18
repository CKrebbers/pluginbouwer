#!/bin/sh
# ci-null.sh — de native kant van de null-test, in CI (en lokaal).
#   sh tools/ci-null.sh clap   <build-dir> [ontwerp]     pb-render door het .clap: blok 512, wisselend, en met een state-rondje
#   sh tools/ci-null.sh pluginval <plugin-pad> <pluginval> <label> [ontwerp]
#                                                         pluginval test (VST3 of AU): eerst de render opnemen en meten,
#                                                         dan de poort: dezelfde config tegen de WASM-referentie
# Schrijft een tabel naar $GITHUB_STEP_SUMMARY als die bestaat. Exit ≠ 0 als iets boven de grens komt.
set -e
WAAR="$(cd "$(dirname "$0")/.." && pwd)"
SOORT="$1"; shift
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }

if [ "$SOORT" = clap ]; then
  BUILD="$1"; ONTWERP="${2:-pijplijn_biquad}"
  MAP="$WAAR/testsignalen/$ONTWERP"
  CLAP="$(ls -d "$BUILD"/plugins/*.clap | head -1)"
  for variant in "blok512|--blok 512" "wisselend|--blok 3,128,1,4096,77" "state|--blok 512 --state-rondje"; do
    naam="${variant%%|*}"; opties="${variant#*|}"
    UIT="$WAAR/uit/clap-$naam"; mkdir -p "$UIT"
    node -e "for (const g of require('$MAP/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" |
    while read -r geval params; do
      # shellcheck disable=SC2086
      "$BUILD/pb-render" "$CLAP" "$MAP/$geval.in.wav" "$UIT/$geval.wav" $opties $params
    done
    { echo "### Null-test CLAP ($naam, $(uname -m)): WASM tegen het gebouwde .clap"; echo;
      node "$WAAR/tools/null.mjs" vergelijk "$UIT" "$ONTWERP"; echo; } | tee /dev/stderr | som
    node "$WAAR/tools/null.mjs" vergelijk "$UIT" "$ONTWERP" > /dev/null
  done
  # automatisering: parameter-events op willekeurige samples, ook midden in een blok (het splitsen, op de bits)
  if [ -f "$MAP/automatie/events.txt" ]; then
    UIT="$WAAR/uit/clap-automatie"; mkdir -p "$UIT"
    { echo "### Null-test CLAP automatisering ($(uname -m)): events midden in blokken"; echo; echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
    for blok in 512 3,128,1,4096,77; do
      "$BUILD/pb-render" "$CLAP" "$MAP/automatie/in.wav" "$UIT/blok-$blok.wav" --blok "$blok" --automatie "$MAP/automatie/events.txt"
      node "$WAAR/tools/null.mjs" automatie "$UIT/blok-$blok.wav" "$ONTWERP" | sed "s/^| automatie/| blok $blok/" | tee /dev/stderr | som
      node "$WAAR/tools/null.mjs" automatie "$UIT/blok-$blok.wav" "$ONTWERP" > /dev/null
    done
  fi
  # instrument: noten op hun sample (als CLAP-noten en als MIDI-bytes: beide dialecten van de note port) en het
  # transport van de host (de sequencer volgt het tempo van de host, niet zijn eigen knop)
  gelijk() {  # gelijk <verwacht> <render> <label>
    node "$WAAR/tools/null.mjs" gelijk "$1" "$2" "$3" | tee /dev/stderr | som
    node "$WAAR/tools/null.mjs" gelijk "$1" "$2" "$3" > /dev/null
  }
  if [ -f "$MAP/noten/noten.txt" ] || [ -f "$MAP/hosttempo/tempo.txt" ]; then
    UIT="$WAAR/uit/clap-instrument"; mkdir -p "$UIT"
    { echo "### Null-test CLAP instrument ($(uname -m)): noten en hosttempo"; echo; echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
  fi
  if [ -f "$MAP/noten/noten.txt" ]; then
    for blok in 512 3,128,1,4096,77; do
      "$BUILD/pb-render" "$CLAP" "$MAP/noten/in.wav" "$UIT/noten-$blok.wav" --blok "$blok" --noten "$MAP/noten/noten.txt"
      gelijk "$MAP/noten/verwacht.wav" "$UIT/noten-$blok.wav" "noten (CLAP), blok $blok"
      "$BUILD/pb-render" "$CLAP" "$MAP/noten/in.wav" "$UIT/midi-$blok.wav" --blok "$blok" --noten "$MAP/noten/noten.txt" --midi
      gelijk "$MAP/noten/verwacht.wav" "$UIT/midi-$blok.wav" "noten (MIDI), blok $blok"
    done
  fi
  if [ -f "$MAP/hosttempo/tempo.txt" ]; then
    TEMPO="$(cat "$MAP/hosttempo/tempo.txt")"
    for blok in 512 3,128,1,4096,77; do
      "$BUILD/pb-render" "$CLAP" "$MAP/hosttempo/in.wav" "$UIT/tempo-$blok.wav" --blok "$blok" --tempo "$TEMPO"
      gelijk "$MAP/hosttempo/verwacht.wav" "$UIT/tempo-$blok.wav" "hosttempo $TEMPO bpm, blok $blok"
    done
  fi
elif [ "$SOORT" = pluginval ]; then
  PLUGIN="$1"; PV="$2"; LABEL="$3"; ONTWERP="${4:-pijplijn_biquad}"
  MAP="$WAAR/testsignalen/$ONTWERP"
  UIT="$WAAR/uit/pluginval-$LABEL"; mkdir -p "$UIT/cfg"
  # 1. opnemen: dezelfde config, maar met een referentie die nog niet bestaat → pluginval schrijft zijn render weg
  for cfg in "$MAP"/pluginval/*.json; do
    geval="$(basename "$cfg" .json)"
    node -e "
      const c = require('$cfg');
      c.input.audio = '$MAP/' + c.input.audio.replace('../', '');
      c.reference = '$UIT/' + c.name + '.wav';
      require('fs').writeFileSync('$UIT/cfg/$geval.json', JSON.stringify(c, null, 2));"
    rm -f "${UIT:?}/${geval:?}.wav" "${UIT:?}/${geval:?}.wav.json"
    $PV test --record-missing --plugin "$PLUGIN" "$UIT/cfg/$geval.json"
  done
  { echo "### Null-test $LABEL: WASM tegen pluginval-render door het binary (blok 512)"; echo;
    node "$WAAR/tools/null.mjs" vergelijk "$UIT" "$ONTWERP" || true; echo; } | tee /dev/stderr | som
  # 2. de poort: pluginval zelf vergelijkt met de WASM-referentie (tolerantie = −90 dBFS per sample)
  for cfg in "$MAP"/pluginval/*.json; do
    geval="$(basename "$cfg" .json)"
    if node -e "process.exit(require('$MAP/gevallen.json').find(g => g.naam === '$geval').statistisch ? 0 : 1)"; then
      echo "($geval: zelfoscillatie, alleen de statistische toets hierboven)"; continue
    fi
    $PV test --plugin "$PLUGIN" "$cfg"
  done
  node "$WAAR/tools/null.mjs" vergelijk "$UIT" "$ONTWERP" > /dev/null
else
  echo "gebruik: ci-null.sh clap|pluginval …" >&2; exit 2
fi
