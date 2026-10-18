#!/bin/sh
# ci-null.sh — de null-test van het bouwpakket in CI (en lokaal).
#   sh gereedschap/ci-null.sh clap <build-dir>                          pb-render door het .clap (blok 512, wisselend, state-rondje,
#                                                                       en automatisering met events midden in blokken; een
#                                                                       instrument ook noten (CLAP en MIDI) en/of hosttempo)
#   sh gereedschap/ci-null.sh pluginval <plugin> "<pluginval>" <label>  pluginval test (VST3/AU): render opnemen, meten, poort
#   sh gereedschap/ci-null.sh sr <build-dir>                            fase 5: 44,1 tot 192 kHz, stereo en mono, blok 1 tot
#                                                                       4096, noten (CLAP en MIDI), vaste knoppen en bypass,
#                                                                       door het .clap tegen gereedschap/maak-sr.mjs (WASM)
#   sh gereedschap/ci-null.sh pluginval-sr <plugin> "<pluginval>" <label>  fase 5: pluginval test (VST3/AU) op elke
#                                                                       samplerate, met de noten als MIDI-bestand
#   sh gereedschap/ci-null.sh au-sr <pb-aurender>                    fase 5 (macOS): de geïnstalleerde AU in stereo én mono
#                                                                       op elke samplerate (noten als MIDI, knoppen, bypass)
# De modi sr en pluginval-sr maken de verwachte uitvoer in uit/sr/ als hij er nog niet is (node gereedschap/maak-sr.mjs,
# die eerst testsignalen/ bit voor bit nadoet).
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
elif [ "$SOORT" = sr ] || [ "$SOORT" = pluginval-sr ] || [ "$SOORT" = au-sr ]; then
  SR="$PAKKET/uit/sr"
  [ -f "$SR/sr.json" ] || node "$PAKKET/gereedschap/maak-sr.mjs" "$SR"
  paar() {  # paar <verwacht> <render> <label>
    { node "$PAKKET/gereedschap/vergelijk.mjs" --paar "$1" "$2" "$3" || true; } | tee /dev/stderr | som
    node "$PAKKET/gereedschap/vergelijk.mjs" --paar "$1" "$2" "$3" > /dev/null
  }
  RATES="$(node -e "console.log(require('$SR/sr.json').samplerates.map(x => x.sr).join(' '))")"
  if [ "$SOORT" = sr ]; then
    BUILD="$1"
    CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
    RENDER="${PB_RENDER:-$BUILD/pb-render}"
    [ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
    KNOPPEN="$(node -e "console.log(require('$SR/sr.json').knoppen.pbRender.map(p => '--p ' + p).join(' '))")"
    UIT="$PAKKET/uit/clap-sr"; mkdir -p "$UIT"
    { echo "### Null-test CLAP samplerates en kanalen ($(uname -m))"; echo; echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
    for sr in $RATES; do
      D="$SR/$sr"
      for k in 2 1; do
        if [ $k = 2 ]; then kn=stereo; else kn=mono; fi
        U="$UIT/$sr-$kn"
        "$RENDER" "$CLAPPAD" "$D/stil.wav" "$U-noten-512.wav" --kanalen $k --blok 512 --noten "$D/noten.txt"
        paar "$D/noten-$kn.wav" "$U-noten-512.wav" "noten (CLAP), $sr Hz, $kn, blok 512"
        "$RENDER" "$CLAPPAD" "$D/stil.wav" "$U-midi-w.wav" --kanalen $k --blok 3,128,1,4096,77 --noten "$D/noten.txt" --midi
        paar "$D/noten-$kn.wav" "$U-midi-w.wav" "noten (MIDI), $sr Hz, $kn, blok 3,128,1,4096,77"
        "$RENDER" "$CLAPPAD" "$D/stil.wav" "$U-noten-4096.wav" --kanalen $k --blok 4096 --noten "$D/noten.txt"
        paar "$D/noten-$kn.wav" "$U-noten-4096.wav" "noten (CLAP), $sr Hz, $kn, blok 4096"
        # shellcheck disable=SC2086
        "$RENDER" "$CLAPPAD" "$D/stil.wav" "$U-knoppen-1.wav" --kanalen $k --blok 1 $KNOPPEN
        paar "$D/knoppen-$kn.wav" "$U-knoppen-1.wav" "vaste knoppen, $sr Hz, $kn, blok 1"
      done
    done
    # bypass (id 5): aan na 1 s, uit na 2,5 s, midden in een blok; tegen dezelfde render zonder bypass
    { echo; echo "### Bypass CLAP ($(uname -m))"; echo; echo "| render | events | gemeten | goed |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
    for sr in $RATES; do
      D="$SR/$sr"; aan=$sr; uit=$((sr * 5 / 2))
      printf "%s 5 1\n%s 5 0\n" "$aan" "$uit" > "$UIT/bypass-$sr.txt"
      for k in 2 1; do
        if [ $k = 2 ]; then kn=stereo; else kn=mono; fi
        "$RENDER" "$CLAPPAD" "$D/stil.wav" "$UIT/$sr-$kn-bypass.wav" --kanalen $k --blok 3,128,1,4096,77 --noten "$D/noten.txt" --midi --automatie "$UIT/bypass-$sr.txt"
        { node "$PAKKET/gereedschap/vergelijk.mjs" --bypass "$D/noten-$kn.wav" "$UIT/$sr-$kn-bypass.wav" "$aan" "$uit" "bypass, $sr Hz, $kn" || true; } | tee /dev/stderr | som
        node "$PAKKET/gereedschap/vergelijk.mjs" --bypass "$D/noten-$kn.wav" "$UIT/$sr-$kn-bypass.wav" "$aan" "$uit" > /dev/null
      done
    done
  elif [ "$SOORT" = au-sr ]; then
    AUR="$1"; UIT="$PAKKET/uit/au-sr"; mkdir -p "$UIT"
    KNOPPEN="$(node -e "console.log(require('$SR/sr.json').knoppen.pbRender.map(p => '--p ' + p).join(' '))")"
    { echo "### Null-test AU samplerates en kanalen (pb-aurender, $(uname -m))"; echo; echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
    for sr in $RATES; do
      D="$SR/$sr"; n="$(node -e "console.log(require('$SR/sr.json').samplerates.find(x => x.sr === $sr).samples)")"
      for k in 2 1; do
        if [ $k = 2 ]; then kn=stereo; else kn=mono; fi
        "$AUR" "$UIT/$sr-$kn-noten.wav" --sr "$sr" --kanalen $k --samples "$n" --blok 3,128,1,4096,77 --noten "$D/noten.txt" >&2
        paar "$D/noten-$kn.wav" "$UIT/$sr-$kn-noten.wav" "AU noten (MIDI), $sr Hz, $kn, blok 3,128,1,4096,77"
        # shellcheck disable=SC2086
        "$AUR" "$UIT/$sr-$kn-knoppen.wav" --sr "$sr" --kanalen $k --samples "$n" --blok 512 $KNOPPEN >&2
        paar "$D/knoppen-$kn.wav" "$UIT/$sr-$kn-knoppen.wav" "AU vaste knoppen, $sr Hz, $kn, blok 512"
      done
    done
    { echo; echo "### Bypass AU (pb-aurender, $(uname -m))"; echo; echo "| render | events | gemeten | goed |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
    for sr in $RATES; do
      D="$SR/$sr"; n="$(node -e "console.log(require('$SR/sr.json').samplerates.find(x => x.sr === $sr).samples)")"
      aan=$sr; uit=$((sr * 5 / 2))
      for k in 2 1; do
        if [ $k = 2 ]; then kn=stereo; else kn=mono; fi
        "$AUR" "$UIT/$sr-$kn-bypass.wav" --sr "$sr" --kanalen $k --samples "$n" --blok 3,128,1,4096,77 --noten "$D/noten.txt" --bypass "$aan" "$uit" >&2
        { node "$PAKKET/gereedschap/vergelijk.mjs" --bypass "$D/noten-$kn.wav" "$UIT/$sr-$kn-bypass.wav" "$aan" "$uit" "AU bypass, $sr Hz, $kn" || true; } | tee /dev/stderr | som
        node "$PAKKET/gereedschap/vergelijk.mjs" --bypass "$D/noten-$kn.wav" "$UIT/$sr-$kn-bypass.wav" "$aan" "$uit" > /dev/null
      done
    done
  else
    PLUGIN="$1"; PV="$2"; LABEL="$3"; UIT="$PAKKET/uit/pluginval-sr-$LABEL"; mkdir -p "$UIT"
    # per samplerate een andere blokgrootte (1 tot 4096); de noten als MIDI-bestand, de knoppen als state.parameters
    blok() { case "$1" in 44100) echo 512;; 48000) echo 1;; 88200) echo 4096;; 96000) echo 333;; 176400) echo 64;; *) echo 4096;; esac; }
    { echo "### Null-test $LABEL samplerates (pluginval test)"; echo; echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
    for sr in $RATES; do
      b=$(blok "$sr")
      for geval in noten knoppen; do
        node -e "
          const sr = require('$SR/sr.json'), c = { name: '$geval-$sr', sample_rate: $sr, block_size: $b, render_duration: sr.duurSec,
            reference: '$UIT/$geval-$sr.wav', comparison: { sample: 0.0000316228 } };
          if ('$geval' === 'noten') c.input = { midi: '$SR/noten.mid' };
          else c.state = require('$T/pluginval/' + sr.knoppen.naam + '.json').state;  // dezelfde knoppen, genormaliseerd
          require('fs').writeFileSync('$UIT/$geval-$sr.json', JSON.stringify(c, null, 2));"
        rm -f "$UIT/$geval-$sr.wav" "$UIT/$geval-$sr.wav.json"
        $PV test --record-missing --plugin "$PLUGIN" "$UIT/$geval-$sr.json"
        paar "$SR/$sr/$geval-stereo.wav" "$UIT/$geval-$sr.wav" "$geval, $sr Hz, blok $b ($LABEL)"
        # en dan echt tegen de verwachte uitvoer van de WASM (pluginval's eigen vergelijking)
        node -e "const c = require('$UIT/$geval-$sr.json'); c.reference = '$SR/$sr/$geval-stereo.wav'; require('fs').writeFileSync('$UIT/$geval-$sr-wasm.json', JSON.stringify(c, null, 2));"
        $PV test --plugin "$PLUGIN" "$UIT/$geval-$sr-wasm.json"
      done
    done
  fi
else
  echo "gebruik: ci-null.sh clap|pluginval|sr|pluginval-sr|au-sr …" >&2; exit 2
fi
