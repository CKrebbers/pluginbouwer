// De ketenmotor (dsp/pb_graaf.h) tegen de handgeschreven dub-echo (dsp/ontwerpen/dub_echo.h): de dub-echo als
// ketendata (tool/src/archetypen.js) moet BIT VOOR BIT hetzelfde klinken. Dan rekent de motor wat de ontwerper tekent.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, render, nullTest, ROOT } from './lib/pb.mjs';
import { compileer, SOORT, volgorde } from '../tool/src/keten.js';
import { DUB_ECHO, ARCHETYPEN } from '../tool/src/archetypen.js';
import { stukjes, ruis, stereo } from './lib/signalen.mjs';

export function laadMotor() {
  const { exports: e } = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(ROOT, 'dsp', 'wasm', 'motor', 'graaf.wasm'))), {});
  return e;
}
export function renderKeten(e, rij, invoer, { sr = 48000, waarden = {}, blok = 128 } = {}) {
  const h = e.pg_maak(sr);
  new Float64Array(e.memory.buffer, e.pg_rij(), rij.length).set(rij);
  if (!e.pg_laad(h, rij.length)) throw new Error('pg_laad weigerde de keten');
  for (const [i, v] of Object.entries(waarden)) e.pg_zet(h, Number(i), v);
  const k = invoer.length, n = invoer[0].length, uit = invoer.map(() => new Float32Array(n));
  for (let pos = 0; pos < n; pos += blok) {
    const m = Math.min(blok, n - pos);
    for (let c = 0; c < k; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).set(invoer[c].subarray(pos, pos + m));
    e.pg_verwerk(h, k, m);
    for (let c = 0; c < k; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), pos);
  }
  e.pg_weg(h);
  return uit;
}

test('de soorten in keten.js zijn die van enum Blok in pb_graaf.h', () => {
  const h = readFileSync(join(ROOT, 'dsp', 'pb_graaf.h'), 'utf8');
  const lijst = h.match(/enum class Blok : int32_t \{([^}]*)\}/)[1];
  for (const [naam, nr] of Object.entries(SOORT)) assert.ok(new RegExp(`\\b${naam} = ${nr}\\b`).test(lijst), `${naam} = ${nr}`);
});

test('elk archetype compileert zonder fouten', () => {
  for (const a of ARCHETYPEN) {
    const c = compileer(a);
    assert.deepEqual(c.fouten, [], `${a.naam}: ${c.fouten.join('; ')}`);
  }
});

test('een terugkoppeling zonder vertraging wordt geweigerd', () => {
  const o = { knopen: [{ id: 'in', type: 'in' }, { id: 's', type: 'som' }, { id: 'v', type: 'versterker' }, { id: 'uit', type: 'uit' }],
    kabels: [{ van: ['in', 'uit'], naar: ['s', 'a'] }, { van: ['s', 'uit'], naar: ['v', 'in'] }, { van: ['v', 'uit'], naar: ['s', 'b'] }, { van: ['s', 'uit'], naar: ['uit', 'in'] }],
    parameters: [] };
  assert.ok(volgorde(o).fouten.some((f) => f.includes('geen vertraging')));
});

test('de dub-echo als keten klinkt bit voor bit als de handgeschreven dub-echo', () => {
  const c = compileer(DUB_ECHO);
  assert.deepEqual(c.fouten, []);
  const hand = laadOntwerp('dub_echo'), e = laadMotor();
  assert.deepEqual(c.params.map((p) => p.naam), hand.params.map((p) => p.naam), 'zelfde parameters in dezelfde volgorde');
  for (const [i, p] of c.params.entries()) {
    const h = hand.params[i];
    assert.deepEqual([p.min, p.max, p.standaard], [h.min, h.max, h.standaard], `${p.naam}: bereik en standaard`);
  }
  const sets = [{}, { 1: 1.1, 6: 1, 5: 12 }, { 0: 77.5, 1: 0.95, 2: 900, 4: 3.5, 7: 1 }];
  for (const w of sets) {
    const invoer = stereo(stukjes(48000, 3), ruis(48000, 3, 0.2, 4));
    const a = render(hand, invoer, { waarden: w, blok: 128 });
    const b = renderKeten(e, c.rij, invoer, { waarden: w, blok: 512 });
    const r = nullTest(a, b);
    assert.equal(r.piekDb, -Infinity, `set ${JSON.stringify(w)}: verschil ${r.piekDb} dBFS vanaf sample ${r.eersteVerschil}`);
  }
});
