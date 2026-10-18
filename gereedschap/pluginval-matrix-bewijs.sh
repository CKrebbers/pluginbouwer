#!/bin/sh
# pluginval-matrix-bewijs.sh <pluginval.log> — fase 5: pluginval validate draaide echt op elke samplerate en blokgrootte
# van de matrix ($SAMPLERATES, $PV_BLOKKEN), en de uitgang biedt mono en stereo aan (Basic bus: "Named layouts: Mono,
# Stereo"; Bus layout processing nam elke indeling aan).
set -e
LOG="$1"
for sr in $(echo "$SAMPLERATES" | tr ',' ' '); do
  for blok in $(echo "$PV_BLOKKEN" | tr ',' ' '); do
    grep -q "Testing with sample rate \[$sr\] and block size \[$blok\]" "$LOG" ||
      { echo "FOUT: pluginval draaide niet op $sr Hz met blok $blok"; exit 1; }
  done
done
echo "pluginval: elke samplerate ($SAMPLERATES) met elke blokgrootte ($PV_BLOKKEN)"
grep -q "Named layouts: Mono, Stereo" "$LOG" || { echo "FOUT: pluginval ziet geen mono- en stereo-uitgang"; exit 1; }
grep "Layouts tested:" "$LOG" | sort | uniq -c
node -e "
  const t = require('fs').readFileSync('$LOG', 'utf8');
  const r = [...t.matchAll(/Layouts tested: (\d+), accepted by setBusesLayout: (\d+)/g)];
  if (!r.length) { console.log('FOUT: geen Bus layout processing'); process.exit(1); }
  for (const [, a, b] of r) if (a !== b || +a < 2) { console.log('FOUT: ' + b + ' van ' + a + ' indelingen aangenomen'); process.exit(1); }
  console.log('pluginval: Named layouts Mono, Stereo; Bus layout processing nam elke indeling aan (' + r.length + ' keer)');"
