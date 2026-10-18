#!/bin/sh
# bus-indeling.sh <pluginval-log> — fase 5 (valkuil 17): de bus-toetsen van pluginval moeten op de hoofduitgang mono én
# stereo zien ("Outputs:" / "Named layouts: Mono, Stereo") en "Bus layout processing" moet minstens twee indelingen
# geaccepteerd hebben (stereo en mono). Werkt voor de VST3 (configurable-audio-ports) en de AU (de patch).
set -e
LOG="$1"
[ -f "$LOG" ] || { echo "bus-indeling: geen log $LOG"; exit 1; }
node -e "
  const t = require('fs').readFileSync('$LOG', 'utf8').replace(/\r/g, '\n').split('\n').map((r) => r.trim());
  let uit = [], getest = [];
  for (let i = 0; i < t.length; i++) {
    if (t[i] === 'Outputs:' && /^Named layouts:/.test(t[i + 1] || '')) uit.push(t[i + 1].replace('Named layouts:', '').trim());
    const m = /Layouts tested: (\d+), accepted by setBusesLayout: (\d+)/.exec(t[i]);
    if (m) getest.push([+m[1], +m[2]]);
  }
  const monoStereo = uit.length > 0 && uit.every((u) => /\bMono\b/.test(u) && /\bStereo\b/.test(u));
  const geaccepteerd = getest.length > 0 && getest.every(([, a]) => a >= 2);
  console.log('bus-indeling: uitgang, named layouts: ' + (uit.length ? [...new Set(uit)].join(' | ') : 'niet gevonden') + ' (' + uit.length + ' keer)');
  console.log('bus-indeling: Bus layout processing: ' + (getest.length ? getest.map(([n, a]) => n + ' getest, ' + a + ' geaccepteerd').join('; ') : 'niet gevonden'));
  if (!monoStereo || !geaccepteerd) { console.log('bus-indeling: FOUT, de hoofduitgang biedt niet mono en stereo aan'); process.exit(1); }
  console.log('bus-indeling: mono en stereo op de hoofduitgang');
"
