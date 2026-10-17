#!/bin/sh
# ci-formaten.sh — poorten op de logs van de validators voor fase 5 (mono en stereo, samplerates, blokgroottes).
#   sh gereedschap/ci-formaten.sh pluginval <log>   pluginval validate --verbose: de busindeling kent Mono en Stereo
#        (in én uit), setBusesLayout neemt precies 4 indelingen aan (de standaard, mono, mono naar stereo, stereo;
#        stereo naar mono niet), en "Audio processing" draaide met elke samplerate × blokgrootte uit $SAMPLERATES en
#        $BLOKGROOTTES
#   sh gereedschap/ci-formaten.sh auval <log>       auval -v: de AU meldt [1, 1], [1, 2] en [2, 2], en niet [2, 1]
set -e
SOORT="$1"; LOG="$2"
[ -f "$LOG" ] || { echo "gebruik: ci-formaten.sh pluginval|auval <log>" >&2; exit 2; }
fout() { echo "FOUT: $*"; exit 1; }
if [ "$SOORT" = pluginval ]; then
  n="$(grep -c "Named layouts: Mono, Stereo" "$LOG" || true)"
  [ "$n" -ge 2 ] || fout "de busindeling noemt niet Mono en Stereo voor in en uit ($n keer)"
  grep "Named layouts:" "$LOG" | head -2
  regel="$(grep "accepted by setBusesLayout:" "$LOG" | head -1)"
  echo "$regel"
  echo "$regel" | grep -q "accepted by setBusesLayout: 4$" || fout "setBusesLayout neemt niet precies 4 indelingen aan"
  for sr in $(echo "${SAMPLERATES:?}" | tr ',' ' '); do
    for blok in $(echo "${BLOKGROOTTES:?}" | tr ',' ' '); do
      grep -q "Testing with sample rate \[$sr\] and block size \[$blok\]" "$LOG" || fout "niet gedraaid: $sr Hz, blok $blok"
    done
  done
  echo "pluginval: Mono en Stereo, 4 indelingen; alle samplerates ($SAMPLERATES) × blokgroottes ($BLOKGROOTTES) gedraaid"
elif [ "$SOORT" = auval ]; then
  grep -A3 "Reported Channel Capabilities" "$LOG" > auval-kanalen.txt || fout "auval meldt geen kanaalindelingen"
  cat auval-kanalen.txt
  for paar in "1, 1" "1, 2" "2, 2"; do
    grep -q "\[$paar\]" auval-kanalen.txt || fout "auval meldt [$paar] niet"
  done
  if grep -q "\[2, 1\]" auval-kanalen.txt; then fout "auval meldt [2, 1] (stereo naar mono kan de plugin niet)"; fi
  echo "auval: [1, 1] [1, 2] [2, 2]"
else
  echo "gebruik: ci-formaten.sh pluginval|auval <log>" >&2; exit 2
fi
