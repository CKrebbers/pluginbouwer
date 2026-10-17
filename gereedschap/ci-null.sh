#!/bin/sh
# ci-null.sh — de null-test van het bouwpakket in CI (en lokaal).
#   sh gereedschap/ci-null.sh clap <build-dir>                          pb-render door het .clap (blok 512, wisselend, state-rondje)
#   sh gereedschap/ci-null.sh pluginval <plugin> "<pluginval>" <label>  pluginval test (VST3/AU): render opnemen, meten, poort
#   sh gereedschap/ci-null.sh matrix <build-dir> <matrix>                 (fase 5) pb-render op elke samplerate en indeling
#        in <matrix> (gemaakt door maak-matrix.mjs): blok 512, wisselend (1..4096) en in-place
#   sh gereedschap/ci-null.sh pluginval-matrix <plugin> "<pluginval>" <label> <matrix> [sr,sr,...]
#        (fase 5) pluginval test (stereo, de standaardindeling) op elke samplerate, en op 48 kHz met blok 1 en 4096;
#        en met de parameter Bypass aan (48 en 192 kHz): uitgang = ingang
#   sh gereedschap/ci-null.sh au-matrix <pb-au-render> <matrix>          (fase 5, macOS) de geïnstalleerde AU op elke
#        samplerate en indeling (blok 512 en wisselend), de BypassEffect van de host, en een geweigerde indeling
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
elif [ "$SOORT" = matrix ]; then
  BUILD="$1"; M="$2"
  CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
  RENDER="${PB_RENDER:-$BUILD/pb-render}"
  [ -n "$CLAPPAD" ] && [ -d "$M" ] || { echo "gebruik: ci-null.sh matrix <build-dir> <matrix>" >&2; exit 2; }
  fout=0
  for srmap in "$M"/*/; do
    sr="$(basename "$srmap")"
    for indeling in stereo mono mono-stereo; do
      V="$M/$sr/$indeling"
      for variant in "blok512|--blok 512" "wisselend|--blok 3,128,1,4096,77" "in-place|--blok 512 --in-place"; do
        naam="${variant%%|*}"; opties="${variant#*|}"; UIT="$PAKKET/uit-matrix/clap/$sr-$indeling-$naam"; mkdir -p "$UIT"
        node -e "for (const g of require('$T/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" |
        while read -r geval params; do
          # shellcheck disable=SC2086
          "$RENDER" "$CLAPPAD" "$V/$geval.in.wav" "$UIT/$geval.wav" --indeling "$indeling" $opties $params
        done
        if node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" --verwacht "$V" > "$UIT/uitslag.md"; then u=groen; else u=ROOD; fout=1; fi
        echo "- $sr Hz, $indeling, $naam: $u ($(grep -c 'bit voor bit gelijk) |.*| ja |' "$UIT/uitslag.md")/4 bit voor bit)" | tee /dev/stderr | som
        [ "$u" = groen ] || cat "$UIT/uitslag.md" >&2
      done
    done
  done
  [ "$fout" = 0 ] || { echo "FOUT: de matrix (CLAP) is niet groen" >&2; exit 1; }
  echo "Null-test CLAP-matrix: alles groen"
elif [ "$SOORT" = pluginval-matrix ]; then
  PLUGIN="$1"; PV="$2"; LABEL="$3"; M="$4"; SRS="${5:-$(ls "$M" | tr '\n' ',' | sed 's/,$//')}"
  fout=0
  for paar in $(echo "$SRS" | tr ',' ' ' | sed 's/\([0-9][0-9]*\)/\1:512/g') 48000:1 48000:4096; do
    sr="${paar%%:*}"; blok="${paar#*:}"; V="$M/$sr/stereo"; UIT="$PAKKET/uit-matrix/$LABEL/$sr-blok$blok"; mkdir -p "$UIT/cfg"
    for cfg in "$T"/pluginval/*.json; do
      geval="$(basename "$cfg" .json)"
      node -e "const c=require('$cfg'); c.input.audio='$V/$geval.in.wav'; c.reference='$UIT/'+c.name+'.wav'; c.sample_rate=$sr; c.block_size=$blok; require('fs').writeFileSync('$UIT/cfg/$geval.json', JSON.stringify(c,null,2));"
      rm -f "${UIT:?}/${geval:?}.wav" "${UIT:?}/${geval:?}.wav.json"
      $PV test --record-missing --plugin "$PLUGIN" "$UIT/cfg/$geval.json" > "$UIT/$geval.log" 2>&1 || { cat "$UIT/$geval.log" >&2; fout=1; }
      # niet-statistisch: ook pluginval zelf vergelijkt met de verwachte uitvoer (grens 1e-4.5 = −90 dBFS)
      if ! node -e "process.exit(require('$T/gevallen.json').find(g => g.naam === '$geval').statistisch ? 0 : 1)"; then
        node -e "const c=require('$UIT/cfg/$geval.json'); c.reference='$V/$geval.verwacht.wav'; require('fs').writeFileSync('$UIT/cfg/$geval-ref.json', JSON.stringify(c,null,2));"
        $PV test --plugin "$PLUGIN" "$UIT/cfg/$geval-ref.json" > "$UIT/$geval-ref.log" 2>&1 || { cat "$UIT/$geval-ref.log" >&2; fout=1; }
      fi
    done
    if node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" --verwacht "$V" > "$UIT/uitslag.md"; then u=groen; else u=ROOD; fout=1; fi
    echo "- $LABEL $sr Hz, blok $blok: $u ($(grep -c 'bit voor bit gelijk) |.*| ja |' "$UIT/uitslag.md")/4 bit voor bit)" | tee /dev/stderr | som
    [ "$u" = groen ] || cat "$UIT/uitslag.md" >&2
  done
  # bypass zoals de host hem zet (VST3 kIsBypass, AU BypassEffect; voor pluginval de parameter "Bypass"): de uitgang
  # is de ingang, bit voor bit (state vóór prepareToPlay, dus zonder overvloeiing)
  for sr in 48000 192000; do
    V="$M/$sr/stereo"; UIT="$PAKKET/uit-matrix/$LABEL/bypass-$sr"; mkdir -p "$UIT/cfg"
    node -e "const c=require('$T/pluginval/echo-standaard.json'); c.input.audio='$V/echo-standaard.in.wav'; c.reference='$V/echo-standaard.in.wav'; c.sample_rate=$sr; c.state.parameters.Bypass=1; require('fs').writeFileSync('$UIT/cfg/bypass.json', JSON.stringify(c,null,2));"
    if $PV test --plugin "$PLUGIN" "$UIT/cfg/bypass.json" > "$UIT/bypass.log" 2>&1 && grep -q '"outcome": "passed"' "$UIT/bypass.log"; then
      echo "- $LABEL bypass $sr Hz: groen (uitgang = ingang)" | tee /dev/stderr | som
    else
      cat "$UIT/bypass.log" >&2; echo "- $LABEL bypass $sr Hz: ROOD" | tee /dev/stderr | som; fout=1
    fi
  done
  [ "$fout" = 0 ] || { echo "FOUT: de matrix ($LABEL) is niet groen" >&2; exit 1; }
  echo "Null-test $LABEL-matrix: alles groen"
elif [ "$SOORT" = au-matrix ]; then
  AUR="$1"; M="$2"
  [ -x "$AUR" ] && [ -d "$M" ] || { echo "gebruik: ci-null.sh au-matrix <pb-au-render> <matrix>" >&2; exit 2; }
  fout=0
  for srmap in "$M"/*/; do
    sr="$(basename "$srmap")"
    for indeling in stereo mono mono-stereo; do
      V="$M/$sr/$indeling"
      for variant in "blok512|--blok 512" "wisselend|--blok 3,128,1,4096,77"; do
        naam="${variant%%|*}"; opties="${variant#*|}"; UIT="$PAKKET/uit-matrix/au/$sr-$indeling-$naam"; mkdir -p "$UIT"
        node -e "for (const g of require('$T/gevallen.json')) console.log(g.naam + ' ' + g.pbRender.map(p => '--p ' + p).join(' '))" |
        while read -r geval params; do
          # shellcheck disable=SC2086
          "$AUR" "$V/$geval.in.wav" "$UIT/$geval.wav" --indeling "$indeling" $opties $params || echo "FOUT: pb-au-render $geval" >&2
        done
        if node "$PAKKET/gereedschap/vergelijk.mjs" "$UIT" --verwacht "$V" > "$UIT/uitslag.md"; then u=groen; else u=ROOD; fout=1; fi
        echo "- AU $sr Hz, $indeling, $naam: $u ($(grep -c 'bit voor bit gelijk) |.*| ja |' "$UIT/uitslag.md")/4 bit voor bit)" | tee /dev/stderr | som
        [ "$u" = groen ] || cat "$UIT/uitslag.md" >&2
      done
      # de bypassknop van de host (kAudioUnitProperty_BypassEffect): uitgang = ingang
      params="$(node -e "console.log(require('$T/gevallen.json').find(g => g.naam === 'echo-standaard').pbRender.map(p => '--p ' + p).join(' '))")"
      UIT="$PAKKET/uit-matrix/au/$sr-$indeling-bypass"; mkdir -p "$UIT"
      # shellcheck disable=SC2086
      if "$AUR" "$V/echo-standaard.in.wav" "$UIT/echo-standaard.wav" --indeling "$indeling" --blok 3,128,1,4096,77 $params --bypass &&
         regel="$(node "$PAKKET/gereedschap/vergelijk-bypass.mjs" aan "$UIT/echo-standaard.wav" "$V/echo-standaard.verwacht.wav" "$V/echo-standaard.in.wav")"; then
        echo "- AU $sr Hz, $indeling, BypassEffect: groen ${regel#- }" | tee /dev/stderr | som
      else
        echo "- AU $sr Hz, $indeling, BypassEffect: ROOD $regel" | tee /dev/stderr | som; fout=1
      fi
    done
  done
  # een indeling die de plugin niet aanbiedt (stereo in, mono uit) weigert Initialize
  if regel="$("$AUR" --weiger 2 1)"; then echo "- AU $regel" | tee /dev/stderr | som; else echo "$regel" >&2; fout=1; fi
  [ "$fout" = 0 ] || { echo "FOUT: de matrix (AU) is niet groen" >&2; exit 1; }
  echo "Null-test AU-matrix: alles groen"
else
  echo "gebruik: ci-null.sh clap|pluginval|matrix|pluginval-matrix|au-matrix …" >&2; exit 2
fi
