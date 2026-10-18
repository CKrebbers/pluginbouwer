// instrument.test.mjs — een ontwerp zonder ingang als plugin (18 okt 2026): de codegen maakt er een instrument van
// (aumu, geen audio-ingang), MIDI-noten worden Noot en Poort op hun sample, en de sequencer volgt het tempo van de host.
// De native kant (pb-render door het .clap, als CLAP-noten en als MIDI, blok 512 en wisselend) staat in tools/ci-null.sh.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, render, nullTest, ROOT } from './lib/pb.mjs';
import { NOTEN, notenAlsEvents } from './lib/noten.mjs';
import { ketenHeader } from '../tool/src/codegen.js';
import { TOETSENPAD, ACIDLIJN, DUB_ECHO } from '../tool/src/archetypen.js';

const SR = 48000;
const stil = (sec) => [new Float32Array(SR * sec), new Float32Array(SR * sec)];
const def = (h, n) => { const m = h.match(new RegExp(`#define ${n} (\\S+)`)); return m ? m[1] : null; };

test('codegen: zonder ingang = instrument (aumu), met toetsen = noten op Noot en Poort; een effect blijft aufx', () => {
  const kop = (o) => ketenHeader(o);
  const t = kop(TOETSENPAD), a = kop(ACIDLIJN), d = kop(DUB_ECHO);
  assert.equal(def(t, 'PB_INSTRUMENT'), '1'); assert.equal(def(t, 'PB_AU_TYPE'), '"aumu"'); assert.equal(def(t, 'PB_NOTEN'), '1');
  assert.equal(def(t, 'PB_NOOT_PARAM'), '0'); assert.equal(def(t, 'PB_POORT_PARAM'), '1');
  assert.equal(def(a, 'PB_INSTRUMENT'), '1'); assert.equal(def(a, 'PB_NOTEN'), null);   // een sequencer speelt zelf
  assert.equal(def(d, 'PB_INSTRUMENT'), null); assert.equal(def(d, 'PB_AU_TYPE'), '"aufx"');
  // de headers in de repo zijn wat de codegen nu maakt
  assert.equal(readFileSync(join(ROOT, 'dsp', 'ontwerpen', 'keten_toetsenpad.h'), 'utf8'), t);
  assert.equal(readFileSync(join(ROOT, 'dsp', 'ontwerpen', 'keten_acidlijn.h'), 'utf8'), a);
});

test('noten: A3 (57) is verhouding 1, een octaaf hoger 2, legato terug naar de noot eronder', () => {
  const o = laadOntwerp('keten_toetsenpad');
  const ev = notenAlsEvents(o.e, [[0, 'aan', 57], [10, 'aan', 69], [20, 'uit', 69], [30, 'uit', 57]], 0, 1);
  assert.deepEqual(ev.map(([s, i]) => [s, i]), [[0, 0], [0, 1], [10, 0], [10, 1], [20, 0], [20, 1], [30, 1]]);
  assert.equal(ev[0][2], 1);
  assert.ok(Math.abs(ev[2][2] - 2) < 1e-15, `69: ${ev[2][2]}`);
  assert.equal(ev[4][2], 1);                     // 69 los: terug naar 57, de poort blijft open
  assert.equal(ev[6][2], 0);                     // alles los: poort dicht
});

function goertzel(y, van, tot, f) {
  const w = 2 * Math.PI * f / SR, c = 2 * Math.cos(w);
  let s1 = 0, s2 = 0;
  for (let i = van; i < tot; i++) { const s = y[i] + c * s1 - s2; s2 = s1; s1 = s; }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

test('noten: stil vóór de eerste noot, en de toon volgt de toets (220 Hz bij 57, 261,6 Hz bij 60)', () => {
  const o = laadOntwerp('keten_toetsenpad');
  const noten = NOTEN.map(([t, w, n]) => [Math.round(t * SR) + 37, w, n]);
  const [y] = render(o, stil(2), { blok: 128, events: notenAlsEvents(o.e, noten, 0, 1) });
  let piek = 0; for (let i = 0; i < 0.1 * SR; i++) piek = Math.max(piek, Math.abs(y[i]));
  assert.ok(piek < 1e-6, `vóór de eerste noot: ${piek}`);
  const db = (a, b) => 10 * Math.log10(a / b);
  const a57 = goertzel(y, 0.3 * SR, 0.55 * SR, 220), b57 = goertzel(y, 0.3 * SR, 0.55 * SR, 261.63);
  const a60 = goertzel(y, 0.85 * SR, 0.99 * SR, 261.63), b60 = goertzel(y, 0.85 * SR, 0.99 * SR, 220);
  assert.ok(db(a57, b57) > 10, `57: 220 Hz ${db(a57, b57).toFixed(1)} dB boven 261,6 Hz`);
  assert.ok(db(a60, b60) > 10, `60: 261,6 Hz ${db(a60, b60).toFixed(1)} dB boven 220 Hz`);
});

test('noten en hosttempo zijn blokgrootte-onafhankelijk (bit voor bit, ook met events midden in een blok)', () => {
  const t = laadOntwerp('keten_toetsenpad');
  const ev = notenAlsEvents(t.e, NOTEN.map(([s, w, n]) => [Math.round(s * SR) + 37, w, n]), 0, 1);
  const r = (blok) => render(t, stil(5), { blok, events: ev });
  assert.equal(nullTest(r(128), r([3, 128, 1, 4096, 77])).piekDb, -Infinity);
  const a = laadOntwerp('keten_acidlijn');
  const q = (blok) => render(a, stil(4), { blok, tempo: 140 });
  assert.equal(nullTest(q(128), q([3, 128, 1, 4096, 77])).piekDb, -Infinity);
});

test('hosttempo: de sequencer volgt de host en niet zijn Tempo-knop; zonder transport de knop', () => {
  const a = laadOntwerp('keten_acidlijn');
  const tempo = a.params.findIndex((p) => p.naam === 'Tempo');
  assert.ok(tempo >= 0);
  const host = render(a, stil(4), { tempo: 140 });
  assert.equal(nullTest(host, render(a, stil(4), { tempo: 140, waarden: { [tempo]: 80 } })).piekDb, -Infinity);
  const eigen = render(a, stil(4));
  assert.ok(nullTest(host, eigen).piekDb > -20, 'met en zonder hosttempo klinkt hetzelfde: de host komt niet aan');
});
