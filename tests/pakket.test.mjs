// Het bouwpakket: de testsignalen die de tool (ketenmotor) maakt zijn byte voor byte die waarop onze eigen CI de
// keten-plugin toetst (testsignalen/keten_<id>/, gemaakt door tools/null.mjs met de pb_*-WASM van de header).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/pb.mjs';
import { compileer, volgorde } from '../tool/src/keten.js';
import { DUB_ECHO, TOETSENPAD, ACIDLIJN } from '../tool/src/archetypen.js';
import { maakTestsignalen } from '../tool/src/testsignalen.js';
import { renderKeten } from '../tool/src/meten.js';
import { zip } from '../tool/src/zip.js';
import { execFileSync } from 'node:child_process';
import { writeFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';

// ook voor de instrumenten: noten/ en hosttempo/ komen uit de tool even goed als uit tools/null.mjs
for (const ontwerp of [DUB_ECHO, TOETSENPAD, ACIDLIJN]) test(`testsignalen van de tool = testsignalen van de CI (byte voor byte): ${ontwerp.id}`, () => {
  const e = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(ROOT, 'dsp/wasm/motor/graaf.wasm'))), {}).exports;
  const c = compileer(ontwerp);
  const best = maakTestsignalen(e, c.rij, c.params, ontwerp, { terugkoppeling: volgorde(ontwerp).terug.length > 0, render: renderKeten });
  if (ontwerp !== DUB_ECHO) assert.ok(Object.keys(best).some((p) => /\/(noten|hosttempo)\//.test(p)), 'een instrument zonder noten- of tempogeval');
  for (const [pad, inhoud] of Object.entries(best)) {
    const ci = readFileSync(join(ROOT, 'testsignalen', `keten_${ontwerp.id}`, pad.replace('testsignalen/', '')));
    assert.ok(Buffer.from(inhoud).equals(ci), `${pad} verschilt`);
  }
});

test('zip.js maakt een ZIP die unzip leest', () => {
  const z = zip({ 'a.txt': 'hallo', 'map/b.bin': new Uint8Array([1, 2, 3]) }, 'pakket');
  const d = mkdtempSync(join(tmpdir(), 'pbzip-'));
  writeFileSync(join(d, 't.zip'), z);
  const lijst = execFileSync('unzip', ['-l', join(d, 't.zip')]).toString();
  assert.ok(lijst.includes('pakket/a.txt') && lijst.includes('pakket/map/b.bin'), lijst);
  assert.equal(execFileSync('unzip', ['-p', join(d, 't.zip'), 'pakket/a.txt']).toString(), 'hallo');
});

test('presets: elke parameter genoemd (bij een instrument nooit Noot en Poort), binnen het bereik en exact in float32', async () => {
  const { ARCHETYPEN } = await import('../tool/src/archetypen.js');
  const { presetBestanden } = await import('../tool/src/bouwpakket.js');
  const { notenParams } = await import('../tool/src/testsignalen.js');
  for (const a of ARCHETYPEN) {
    const np = notenParams(a), gespeeld = np ? [np.noot, np.poort] : [];
    const genoemd = a.parameters.filter((q, i) => !gespeeld.includes(i));
    for (const p of a.presets || []) {
      assert.deepEqual(Object.keys(p.waarden).sort(), genoemd.map((q) => q.naam).sort(), `${a.id}/${p.naam}: niet de goede parameters`);
      for (const q of genoemd) {
        const v = p.waarden[q.naam];
        assert.ok(v >= q.min && v <= q.max, `${a.id}/${p.naam}: ${q.naam} = ${v} buiten ${q.min}..${q.max}`);
        assert.equal(Math.fround(v), v, `${a.id}/${p.naam}: ${q.naam} = ${v} niet exact in float32`);
      }
    }
    assert.equal(Object.keys(presetBestanden(a)).length, (a.presets || []).length, 'dubbele presetnaam');
  }
});
