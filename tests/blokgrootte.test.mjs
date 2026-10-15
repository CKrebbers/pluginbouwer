// Regel 3 uit CLAUDE.md: de uitvoer hangt niet af van de blokgrootte. Bit voor bit gelijk bij 1, 7, 128, 512, 4096
// en bij een wisselende reeks. Zonder dit kan een AudioWorklet (128) nooit nullen tegen een host (512).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, render, nullTest, ROOT } from './lib/pb.mjs';
import { ruis, stukjes, stereo } from './lib/signalen.mjs';

const ontwerpen = readdirSync(join(ROOT, 'dsp', 'wasm')).filter((f) => f.endsWith('.wasm')).map((f) => f.slice(0, -5));

for (const naam of ontwerpen) {
  test(`${naam}: blokgrootte doet er niet toe`, () => {
    const o = laadOntwerp(naam);
    const invoer = stereo(ruis(48000, 1.5, 0.3, 7), stukjes(48000, 1.5));
    const waarden = Object.fromEntries(o.params.map((p) => [p.index, p.min + 0.37 * (p.max - p.min)]));
    const basis = render(o, invoer, { waarden, blok: 128 });
    for (const blok of [1, 7, 512, 4096, [3, 128, 1, 4096, 77, 512]]) {
      const r = nullTest(basis, render(o, invoer, { waarden, blok }));
      assert.equal(r.piekDb, -Infinity, `blok ${blok}: verschil ${r.piekDb} dBFS vanaf sample ${r.eersteVerschil}`);
    }
  });
}
