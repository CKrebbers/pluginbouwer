// Het bouwpakket: de testsignalen die de tool (ketenmotor) maakt zijn byte voor byte die waarop onze eigen CI de
// keten-plugin toetst (testsignalen/keten_<id>/, gemaakt door tools/null.mjs met de pb_*-WASM van de header).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './lib/pb.mjs';
import { compileer, volgorde } from '../tool/src/keten.js';
import { DUB_ECHO } from '../tool/src/archetypen.js';
import { maakTestsignalen } from '../tool/src/testsignalen.js';
import { renderKeten } from '../tool/src/meten.js';
import { zip } from '../tool/src/zip.js';
import { execFileSync } from 'node:child_process';
import { writeFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';

test('testsignalen van de tool = testsignalen van de CI (byte voor byte)', () => {
  const e = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(ROOT, 'dsp/wasm/motor/graaf.wasm'))), {}).exports;
  const c = compileer(DUB_ECHO);
  const best = maakTestsignalen(e, c.rij, c.params, DUB_ECHO, { terugkoppeling: volgorde(DUB_ECHO).terug.length > 0, render: renderKeten });
  for (const [pad, inhoud] of Object.entries(best)) {
    const ci = readFileSync(join(ROOT, 'testsignalen', 'keten_dub_echo', pad.replace('testsignalen/', '')));
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
