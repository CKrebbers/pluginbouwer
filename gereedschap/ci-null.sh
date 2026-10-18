#!/bin/sh
# ci-null.sh — de null-test van het bouwpakket in CI (en lokaal).
#   sh gereedschap/ci-null.sh clap <build-dir>                          pb-render door het .clap (blok 512, wisselend, state-rondje,
#                                                                       en automatisering met events midden in blokken; een
#                                                                       instrument ook noten (CLAP en MIDI) en/of hosttempo)
#   sh gereedschap/ci-null.sh pluginval <plugin> "<pluginval>" <label>  pluginval test (VST3/AU): render opnemen, meten, poort
#   sh gereedschap/ci-null.sh sr <build-dir> ["<samplerates>"]          fase 5: pb-render door het .clap op elke samplerate,
#                                                                       stereo en mono (audio-ports-config), alle gevallen,
#                                                                       automatisering en noten (CLAP en MIDI), blok 512,
#                                                                       wisselend (3,128,1,4096,77), 1 en 4096; de verwachting
#                                                                       komt uit gereedschap/maak-verwacht.mjs (wasm/graaf.wasm),
#                                                                       die eerst testsignalen/ bit voor bit moet nadoen
#   sh gereedschap/ci-null.sh pluginval-sr <plugin> "<pluginval>" <label> ["<samplerates>"]
#                                                                       fase 5: pluginval test (VST3/AU) op elke samplerate:
#                                                                       de gevallen en de noten (als MIDI-bestand)
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
  # automatisering: parameter-events op willekeurige samples, ook midden in een blok (bewijst het splitsen, op de bits)
  if [ -f "$T/automatie/events.txt" ]; then
    UIT="$PAKKET/uit/clap-automatie"; mkdir -p "$UIT"
    for blok in 512 3,128,1,4096,77; do
      "$RENDER" "$CLAPPAD" "$T/automatie/in.wav" "$UIT/blok-$blok.wav" --blok "$blok" --automatie "$T/automatie/events.txt"
      { echo "### Null-test CLAP automatisering, blok $blok ($(uname -m))"; echo; node "$PAKKET/gereedschap/vergelijk.mjs" --automatie "$UIT/blok-$blok.wav" || true; echo; } | tee /dev/stderr | som
      node "$PAKKET/gereedschap/vergelijk.mjs" --automatie "$UIT/blok-$blok.wav" > /dev/null
    done
  fi
  # instrument: noten op hun sample, als CLAP-noten en als MIDI-bytes, en het transport van de host
  los() {  # los <geval> <render> <label>
    { node "$PAKKET/gereedschap/vergelijk.mjs" "--$1" "$2" "$3" || true; } | tee /dev/stderr | som
    node "$PAKKET/gereedschap/vergelijk.mjs" "--$1" "$2" "$3" > /dev/null
  }
  if [ -f "$T/noten/noten.txt" ] || [ -f "$T/hosttempo/tempo.txt" ]; then
    UIT="$PAKKET/uit/clap-instrument"; mkdir -p "$UIT"
    { echo "### Null-test CLAP instrument ($(uname -m))"; echo; echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
  fi
  if [ -f "$T/noten/noten.txt" ]; then
    for blok in 512 3,128,1,4096,77; do
      "$RENDER" "$CLAPPAD" "$T/noten/in.wav" "$UIT/noten-$blok.wav" --blok "$blok" --noten "$T/noten/noten.txt"
      los noten "$UIT/noten-$blok.wav" "noten (CLAP), blok $blok"
      "$RENDER" "$CLAPPAD" "$T/noten/in.wav" "$UIT/midi-$blok.wav" --blok "$blok" --noten "$T/noten/noten.txt" --midi
      los noten "$UIT/midi-$blok.wav" "noten (MIDI), blok $blok"
    done
  fi
  if [ -f "$T/hosttempo/tempo.txt" ]; then
    for blok in 512 3,128,1,4096,77; do
      "$RENDER" "$CLAPPAD" "$T/hosttempo/in.wav" "$UIT/tempo-$blok.wav" --blok "$blok" --tempo "$(cat "$T/hosttempo/tempo.txt")"
      los hosttempo "$UIT/tempo-$blok.wav" "hosttempo, blok $blok"
    done
  fi
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
elif [ "$SOORT" = sr ]; then
  BUILD="$1"; SRS="${2:-44100 48000 88200 96000 176400 192000}"
  CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
  RENDER="${PB_RENDER:-$BUILD/pb-render}"
  [ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
  # eerst: doet de generator testsignalen/ bit voor bit na? Anders is een verschil hieronder niet te duiden.
  { echo "### Verwachte uitvoer uit wasm/graaf.wasm (maak-verwacht.mjs) tegen testsignalen/"; echo; node "$PAKKET/gereedschap/maak-verwacht.mjs" controle; echo; } | tee /dev/stderr | som
  node "$PAKKET/gereedschap/maak-verwacht.mjs" controle > /dev/null
  WISSEL=3,128,1,4096,77
  fout=0
  for sr in $SRS; do
    for kan in 2 1; do
      naamk=$([ "$kan" = 1 ] && echo mono || echo stereo)
      UIT="$PAKKET/uit/sr-$sr-$naamk"; V="$UIT/verwacht"; mkdir -p "$UIT"
      node "$PAKKET/gereedschap/maak-verwacht.mjs" "$sr" "$V" --kanalen "$kan" > /dev/null
      { echo "### Null-test CLAP op $sr Hz, $naamk ($(uname -m))"; echo; echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
      paar() {  # paar <naam in V> <render> <label>
        node "$PAKKET/gereedschap/vergelijk.mjs" --paar "$V/$1.wav" "$2" "$3" | tee /dev/stderr | som
        node "$PAKKET/gereedschap/vergelijk.mjs" --paar "$V/$1.wav" "$2" "$3" > /dev/null || fout=$((fout + 1))
      }
      node -e "for (const g of require('$T/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" > "$UIT/gevallen.txt"
      while read -r geval params; do
        # shellcheck disable=SC2086
        "$RENDER" "$CLAPPAD" "$T/$geval.in.wav" "$UIT/$geval.wav" --sr "$sr" --kanalen "$kan" --blok 512 $params
        paar "$geval" "$UIT/$geval.wav" "$geval, blok 512"
      done < "$UIT/gevallen.txt"
      "$RENDER" "$CLAPPAD" "$T/automatie/in.wav" "$UIT/automatie.wav" --sr "$sr" --kanalen "$kan" --blok "$WISSEL" --automatie "$T/automatie/events.txt"
      paar automatie "$UIT/automatie.wav" "automatisering, blok $WISSEL"
      for blok in 512 "$WISSEL"; do
        "$RENDER" "$CLAPPAD" "$T/noten/in.wav" "$UIT/noten-$blok.wav" --sr "$sr" --kanalen "$kan" --blok "$blok" --noten "$T/noten/noten.txt"
        paar noten "$UIT/noten-$blok.wav" "noten (CLAP), blok $blok"
        "$RENDER" "$CLAPPAD" "$T/noten/in.wav" "$UIT/midi-$blok.wav" --sr "$sr" --kanalen "$kan" --blok "$blok" --noten "$T/noten/noten.txt" --midi
        paar noten "$UIT/midi-$blok.wav" "noten (MIDI), blok $blok"
      done
      # de uitersten van de blokgrootte (1..4096) eens per samplerate
      blok=$([ "$kan" = 2 ] && echo 1 || echo 4096)
      "$RENDER" "$CLAPPAD" "$T/noten/in.wav" "$UIT/noten-$blok.wav" --sr "$sr" --kanalen "$kan" --blok "$blok" --noten "$T/noten/noten.txt"
      paar noten "$UIT/noten-$blok.wav" "noten (CLAP), blok $blok"
      echo | som
    done
  done
  echo "null-test samplerates ($SRS; stereo en mono): $fout render(s) boven de grens"
  [ "$fout" -eq 0 ]
elif [ "$SOORT" = pluginval-sr ]; then
  PLUGIN="$1"; PV="$2"; LABEL="$3"; SRS="${4:-44100 48000 88200 96000 176400 192000}"
  fout=0
  for sr in $SRS; do
    UIT="$PAKKET/uit/pluginval-$LABEL-sr-$sr"; V="$UIT/verwacht"; mkdir -p "$UIT/cfg"
    node "$PAKKET/gereedschap/maak-verwacht.mjs" "$sr" "$V" > /dev/null
    # blokgrootte per samplerate wisselend, zodat 64 tot 4096 ergens langskomt
    case "$sr" in 44100|96000) blok=64 ;; 48000|176400) blok=512 ;; *) blok=4096 ;; esac
    for cfg in "$T"/pluginval/*.json; do
      geval="$(basename "$cfg" .json)"
      node -e "const c=require('$cfg'); c.input.audio='$V/$geval.in.wav'; c.reference='$UIT/$geval.wav'; c.sample_rate=$sr; c.block_size=$blok; require('fs').writeFileSync('$UIT/cfg/$geval.json', JSON.stringify(c,null,2));"
    done
    # de noten: MIDI-bestand (tick = sample) op de stille invoer, alles op de standaardwaarden
    node -e "require('fs').writeFileSync('$UIT/cfg/noten.json', JSON.stringify({name:'noten', input:{audio:'$V/noten.in.wav', midi:'$V/noten.mid'}, reference:'$UIT/noten.wav', sample_rate:$sr, block_size:$blok}, null, 2));"
    for c in "$UIT"/cfg/*.json; do
      geval="$(basename "$c" .json)"
      rm -f "${UIT:?}/${geval:?}.wav" "${UIT:?}/${geval:?}.wav.json"
      $PV test --record-missing --plugin "$PLUGIN" "$c"
    done
    { echo "### Null-test $LABEL op $sr Hz (pluginval test, blok $blok; noten als MIDI-bestand)"; echo; echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
    for c in "$UIT"/cfg/*.json; do
      geval="$(basename "$c" .json)"
      node "$PAKKET/gereedschap/vergelijk.mjs" --paar "$V/$geval.wav" "$UIT/$geval.wav" "$geval" | tee /dev/stderr | som
      node "$PAKKET/gereedschap/vergelijk.mjs" --paar "$V/$geval.wav" "$UIT/$geval.wav" "$geval" > /dev/null || fout=$((fout + 1))
    done
    echo | som
  done
  echo "null-test $LABEL samplerates ($SRS): $fout render(s) boven de grens"
  [ "$fout" -eq 0 ]
else
  echo "gebruik: ci-null.sh clap|pluginval|sr|pluginval-sr …" >&2; exit 2
fi
