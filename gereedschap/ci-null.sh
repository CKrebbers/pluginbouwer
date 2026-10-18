#!/bin/sh
# ci-null.sh — de null-test van het bouwpakket in CI (en lokaal).
#   sh gereedschap/ci-null.sh clap <build-dir>                          pb-render door het .clap (blok 512, wisselend, state-rondje,
#                                                                       en automatisering met events midden in blokken; een
#                                                                       instrument ook noten (CLAP en MIDI) en/of hosttempo)
#   sh gereedschap/ci-null.sh pluginval <plugin> "<pluginval>" <label>  pluginval test (VST3/AU): render opnemen, meten, poort
# Fase 5, de matrix van samplerates en kanalen (de verwachting uit wasm/graaf.wasm, niet uit testsignalen/):
#   sh gereedschap/ci-null.sh verwacht <build-dir> <sr,…>                wasm-render.mjs doet eerst testsignalen/ bit voor
#                                                                       bit na, maakt dan per samplerate, stereo en mono de
#                                                                       verwachting in uit/matrix (met <build-dir>/pb-noottabel)
#   sh gereedschap/ci-null.sh matrix <build-dir> <blok> [<blok> …]      pb-render door het .clap: elke regel van de lijst
#                                                                       met elke blokindeling (bv. 1, 4096, 1,4096,7,512,333):
#                                                                       de gevallen, automatisering, noten (CLAP en MIDI),
#                                                                       bypass; mono om en om via audio-ports-config en
#                                                                       configurable-audio-ports
#   sh gereedschap/ci-null.sh au-matrix <build-dir> <blok> [<blok> …]   pb-au-render door de geïnstalleerde AU (macOS):
#                                                                       de gevallen, noten en bypass op een blokgrens
#   sh gereedschap/ci-null.sh pluginval-matrix <plugin> "<pluginval>" <label>
#                                                                       pluginval test (VST3/AU, stereo) per samplerate:
#                                                                       de gevallen en de noten als MIDI-bestand
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
elif [ "$SOORT" = verwacht ]; then
  BUILD="$1"; SRS="$2"; M="$PAKKET/uit/matrix"; rm -rf "$M"; mkdir -p "$M"
  "$BUILD/pb-noottabel" > "$M/noottabel.txt"
  # eerst bewijzen dat de verwachting uit de WASM precies is wat de tool deed (testsignalen/, bit voor bit)
  node "$PAKKET/gereedschap/wasm-render.mjs" --na "$M/noottabel.txt"
  node "$PAKKET/gereedschap/wasm-render.mjs" --maak "$M" "$M/noottabel.txt" "$SRS"
  echo "$SRS" > "$M/samplerates.txt"
elif [ "$SOORT" = matrix ] || [ "$SOORT" = au-matrix ]; then
  BUILD="$1"; shift
  M="$PAKKET/uit/matrix"; [ -f "$M/lijst.txt" ] || { echo "eerst: ci-null.sh verwacht" >&2; exit 2; }
  mkdir -p "$M/render-$SOORT"
  if [ "$SOORT" = matrix ]; then
    CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
    [ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
    RENDER="${PB_RENDER:-$BUILD/pb-render}"; DOEL="$CLAPPAD"; WAT="CLAP (pb-render)"
  else
    RENDER="$BUILD/pb-au-render"; DOEL="${AU_CODES:-aumu:PTgg:Vrve}"; WAT="AU (pb-au-render)"
  fi
  { echo "### Null-test $WAT, matrix ($(uname -m)): samplerates $(cat "$M/samplerates.txt"), stereo en mono, blokken $*"; echo
    echo "| render | piek | RMS | bit voor bit |"; echo "|---|---|---|---|"; } | tee /dev/stderr | som
  totaal=0; mis=0; bits=0
  while read -r geval sr kan opties; do
    # de AU zet parameters en bypass alleen tussen blokken: geen automatisering midden in een blok
    if [ "$SOORT" = au-matrix ]; then case "$geval" in automatie|bypass) continue ;; esac; fi
    d="$M/$sr-$kan"; nr=0
    for blok in "$@"; do
      nr=$((nr + 1))
      # mono: om en om via audio-ports-config (CLAP-hosts, de AU) en configurable-audio-ports (de VST3 van clap-wrapper)
      via=config; [ "$kan" = 1 ] && [ $((nr % 2)) = 0 ] && via=configurable
      for dialect in clap midi; do
        extra=""
        if [ "$dialect" = midi ]; then
          # MIDI-bytes alleen voor de noten door pb-render (de AU krijgt altijd MIDI)
          [ "$SOORT" = matrix ] || continue
          case "$opties" in *--noten*) extra="--midi" ;; *) continue ;; esac
        fi
        uit="$M/render-$SOORT/$geval-$sr-$kan-$nr-$dialect.wav"
        indeling=stereo; [ "$kan" = 1 ] && indeling="mono"
        [ "$kan" = 1 ] && [ "$SOORT" = matrix ] && indeling="mono ($via)"
        label="$geval, $sr Hz, $indeling, blok $blok"; [ -n "$extra" ] && label="$label, MIDI"
        # shellcheck disable=SC2086
        "$RENDER" "$DOEL" "$d/$geval.in.wav" "$uit" --sr "$sr" --kanalen "$kan" --via "$via" --blok "$blok" $opties $extra
        totaal=$((totaal + 1))
        if rij=$(node "$PAKKET/gereedschap/vergelijk.mjs" --paar "$d/$geval.verwacht.wav" "$uit" "$label"); then :; else mis=$((mis + 1)); fi
        case "$rij" in *"| ja |"*) bits=$((bits + 1)) ;; esac
        echo "$rij" | tee /dev/stderr | som
        rm -f "$uit"
      done
    done
  done < "$M/lijst.txt"
  { echo; echo "$SOORT ($WAT): $totaal renders, $bits bit voor bit gelijk, $mis boven de grens of met een andere vorm"; echo; } | tee /dev/stderr | som
  [ "$totaal" -gt 0 ] && [ "$mis" = 0 ]
elif [ "$SOORT" = pluginval-matrix ]; then
  PLUGIN="$1"; PV="$2"; LABEL="$3"; M="$PAKKET/uit/matrix"
  [ -f "$M/lijst.txt" ] || { echo "eerst: ci-null.sh verwacht" >&2; exit 2; }
  { echo "### Null-test $LABEL (pluginval test, stereo), matrix: samplerates $(cat "$M/samplerates.txt")"; echo
    echo "| config | blok | uitslag |"; echo "|---|---|---|"; } | tee /dev/stderr | som
  totaal=0; mis=0; nul=0
  for cfg in "$M"/*-2/pluginval/*.json; do
    sr="$(basename "$(dirname "$(dirname "$cfg")")")"; sr="${sr%-2}"; geval="$(basename "$cfg" .json)"
    blok="$(node -e "console.log(require('$cfg').block_size)")"
    totaal=$((totaal + 1))
    if $PV test --plugin "$PLUGIN" "$cfg" > "$M/pv.log" 2>&1; then status=0; else status=$?; fi
    rij="$(grep -m1 "\[sample\]" "$M/pv.log" | sed 's/^ *//')"
    [ "$status" = 0 ] || { mis=$((mis + 1)); cat "$M/pv.log"; rij="**MISLUKT** (exit $status) $rij"; }
    case "$rij" in *"max abs diff 0 <="*) nul=$((nul + 1)) ;; esac
    echo "| $geval, $sr Hz | $blok | $rij |" | tee /dev/stderr | som
  done
  { echo; echo "pluginval-matrix $LABEL: $totaal configs, $nul met max abs diff 0, $mis mislukt"; echo; } | tee /dev/stderr | som
  [ "$totaal" -gt 0 ] && [ "$mis" = 0 ]
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
  echo "gebruik: ci-null.sh clap|pluginval|verwacht|matrix|au-matrix|pluginval-matrix …" >&2; exit 2
fi
