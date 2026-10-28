// meten.test.mjs — de readout van de tool (tool/src/meten.js) op de ketenmotor: hij meet pas na het inlopen van de
// echo, de bandbeweging staat stil (dat doet app.js; hier met de hand), en een zelfoscillerende keten krijgt een reden
// in plaats van een getal.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { compileer } from '../tool/src/keten.js';
import { DUB_ECHO } from '../tool/src/archetypen.js';
import { readout } from '../tool/src/meten.js';
import { ROOT } from './lib/pb.mjs';

const e = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(ROOT, 'dsp/wasm/motor/graaf.wasm'))), {}).exports;
const meet = (zet) => {
  const o = JSON.parse(JSON.stringify(DUB_ECHO));
  for (const [naam, v] of Object.entries(zet)) o.parameters.find((p) => p.naam === naam).standaard = v;
  const c = compileer(o);
  return readout(e, c.rij, { waarden: Object.fromEntries(c.params.map((p, i) => [i, p.standaard])) });
};

test('readout: de dub-echo stil gemeten heeft geen aliasing boven −100 dB, en loopt eerst in', () => {
  const r = meet({ Flutter: 0 });
  assert.ok(r.inloop > 1 && r.inloop <= 8, `inloop ${r.inloop}`);
  assert.ok(r.aliasingDb < -100, `aliasing ${r.aliasingDb}`);
  assert.ok(r.thdDb < -20 && r.thdDb > -60, `thd ${r.thdDb}`);
});
test('readout: meer verzadiging = meer vervorming', () => {
  assert.ok(meet({ Flutter: 0, Saturation: 18 }).thdDb > meet({ Flutter: 0, Saturation: 0 }).thdDb);
});
test('readout: zelfoscillatie geeft een reden, geen getal', () => {
  const r = meet({ Flutter: 0, Feedback: 1.2 });
  assert.equal(r.staart, -1);
  assert.equal(r.aliasingDb, null);
  assert.match(r.reden, /zelfoscillatie/);
});
