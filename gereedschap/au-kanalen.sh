#!/bin/sh
# au-kanalen.sh <auval-log> — fase 5 (valkuil 17): auval moet de kanaalindelingen 0 -> 2 (stereo) en 0 -> 1 (mono)
# melden ("Reported Channel Capabilities"), uit audio-ports-config via
# patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch. Drukt het stuk van de log af als bewijs.
set -e
LOG="$1"
[ -f "$LOG" ] || { echo "au-kanalen: geen log $LOG"; exit 1; }
node -e "
  const t = require('fs').readFileSync('$LOG', 'utf8').split('\n');
  const i = t.findIndex((r) => /Channel Capabilities/i.test(r));
  if (i < 0) { console.log('au-kanalen: FOUT, geen \"Channel Capabilities\" in de uitvoer van auval'); process.exit(1); }
  const stuk = t.slice(i, i + 4);
  for (const r of stuk) console.log('  ' + r);
  const plat = stuk.join(' ').replace(/\s+/g, '');
  const heeft = (a, b) => plat.includes('[' + a + ',' + b + ']') || plat.includes(a + '-' + b);
  if (!heeft(0, 2) || !heeft(0, 1)) { console.log('au-kanalen: FOUT, auval meldt niet zowel 0 -> 2 als 0 -> 1'); process.exit(1); }
  console.log('au-kanalen: de AU biedt 0 -> 2 (stereo) en 0 -> 1 (mono) aan');
"
