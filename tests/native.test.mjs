// Regel 1 en 2 (CLAUDE.md) voor elk voorbeeld: de ketenmotor NATIVE (de C++-compiler van deze machine, met de vlaggen
// van de plugin) rekent bit voor bit hetzelfde als de WASM van de tool. In CI draait dit op Linux x86-64; op een Mac
// op arm64. Geen compiler gevonden = overgeslagen (met de reden), niet groen.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { ROOT, nullTest } from './lib/pb.mjs';
import { render, stilte, groove, heeftIngang } from './lib/keten.mjs';
import { compileer } from '../tool/src/keten.js';
import { ARCHETYPEN, GROOVE } from '../tool/src/archetypen.js';

const SR = 48000, SEC = 3;
const map = mkdtempSync(join(tmpdir(), 'pb-native-'));
const bin = join(map, 'graaf_render');
let compiler = null;
for (const c of [process.env.CXX, 'c++', 'clang++', 'g++'].filter(Boolean)) {
  try {
    execFileSync(c, ['-std=c++17', '-O2', '-ffp-contract=off', '-fno-fast-math', '-fno-math-errno', '-I', join(ROOT, 'dsp'),
      '-o', bin, join(ROOT, 'tests', 'native', 'graaf_render.cpp')], { stdio: 'pipe' });
    compiler = c; break;
  } catch { /* volgende */ }
}
function renderNative(rij, invoer, { blok = 512, waarden = {}, gebeurtenissen = [] } = {}) {
  const geb = [...Object.entries(waarden).map(([i, v]) => [0, Number(i), v]), ...gebeurtenissen].sort((a, b) => a[0] - b[0]);
  const n = invoer[0].length, k = invoer.length;
  const buf = Buffer.alloc(8 + 16 + rij.length * 8 + 4 + geb.length * 16 + k * n * 4);
  let o = 0;
  buf.writeDoubleLE(SR, o); o += 8;
  for (const v of [n, k, blok, rij.length]) { buf.writeInt32LE(v, o); o += 4; }
  for (const v of rij) { buf.writeDoubleLE(v, o); o += 8; }
  buf.writeInt32LE(geb.length, o); o += 4;
  for (const [s, p, v] of geb) { buf.writeInt32LE(s, o); buf.writeInt32LE(p, o + 4); buf.writeDoubleLE(v, o + 8); o += 16; }
  for (const c of invoer) { Buffer.from(c.buffer, c.byteOffset, c.byteLength).copy(buf, o); o += c.byteLength; }
  writeFileSync(join(map, 'in.bin'), buf);
  execFileSync(bin, [join(map, 'in.bin'), join(map, 'uit.f32')]);
  const ruw = readFileSync(join(map, 'uit.f32'));
  return Array.from({ length: k }, (_, c) => new Float32Array(ruw.buffer.slice(ruw.byteOffset + c * n * 4, ruw.byteOffset + (c + 1) * n * 4)));
}
const SPEEL = Array.from({ length: 6 }, (_, i) => [[i * SR / 2, 0, 2 ** ([0, 3, 7, 12, 7, 3][i] / 12)], [i * SR / 2, 1, 1], [i * SR / 2 + SR / 4, 1, 0]]).flat();

test('de groove (bron voor de effecten) native = WASM', { skip: compiler ? false : 'geen C++-compiler gevonden' }, () => {
  const rij = compileer(GROOVE).rij;
  const r = nullTest(render(rij, stilte(SR, SEC)), renderNative(rij, stilte(SR, SEC)));
  assert.equal(r.piekDb, -Infinity, `verschil vanaf sample ${r.eersteVerschil}`);
});
for (const a of ARCHETYPEN) {
  test(`${a.naam}: native (${compiler || '—'}, blok 512) = WASM (blok 128), bit voor bit`, { skip: compiler ? false : 'geen C++-compiler gevonden' }, () => {
    const c = compileer(a);
    const waarden = Object.fromEntries(c.params.map((p, i) => [i, p.standaard]));
    const gebeurtenissen = a.knopen.some((k) => k.type === 'toetsen') ? SPEEL : [];
    const invoer = heeftIngang(a) ? groove(SR, SEC) : stilte(SR, SEC);
    const w = render(c.rij, invoer, { waarden, gebeurtenissen, blok: 128 });
    const nat = renderNative(c.rij, invoer, { waarden, gebeurtenissen, blok: 512 });
    const r = nullTest(w, nat);
    assert.equal(r.piekDb, -Infinity, `verschil ${r.piekDb} dBFS vanaf sample ${r.eersteVerschil}`);
  });
}
