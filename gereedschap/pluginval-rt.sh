#!/bin/sh
# pluginval-rt.sh <pluginval> <plugin> [opties…] — pluginval strictness 10 met --rtcheck relaxed (de opties, bv.
# --sample-rates en --block-sizes, komen na het pluginpad; valkuil 18), en dan zelf streng: elke
# real-time-schending faalt, behalve die van pluginval's eigen VST3-host (juce::MidiEventList::addEvent alloceert als hij
# MIDI naar een instrument stuurt; 18 okt, macOS-runner, geen frame van de plugin in de stack). Ook zonder schending moet
# pluginval slagen.
set -e
PV="$1"; PLUGIN="$2"; shift 2; LOG="${TMPDIR:-/tmp}/pluginval-rt.$$.log"
status=0
$PV validate "$PLUGIN" --strictness-level 10 --rtcheck relaxed "$@" > "$LOG" 2>&1 || status=$?
cat "$LOG"
node -e "
  const t = require('fs').readFileSync('$LOG', 'utf8').split('\n');
  let n = 0, toegestaan = 0, fout = [];
  for (let i = 0; i < t.length; i++) {
    if (!/Real-time violation/.test(t[i])) continue;
    n++;
    // de eerste frame na rtc::… en wrap_… is wie de onveilige functie aanroept
    let j = i + 1, wie = '';
    for (; j < Math.min(t.length, i + 40); j++) { const r = t[j].trim(); if (!r || /^rtc::|^wrap_|Stack trace/.test(r)) continue; if (/^[A-Za-z_]/.test(r) && r.includes('(')) { wie = r; break; } }
    if (/^juce::MidiEventList::addEvent\(/.test(wie)) toegestaan++; else fout.push(t[i].trim() + ' <- ' + (wie || '(geen stack)'));
  }
  console.log('real-time-schendingen: ' + n + ', waarvan pluginval-host (MidiEventList::addEvent): ' + toegestaan);
  for (const f of fout) console.log('NIET TOEGESTAAN: ' + f);
  process.exit(fout.length ? 1 : 0);"
grep -q "^SUCCESS" "$LOG" || { echo "pluginval meldde geen SUCCESS (exit $status)"; exit 1; }
