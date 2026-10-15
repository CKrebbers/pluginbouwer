// Elk ontwerp begint op zijn standaardwaarden, ook in WASM (de CLAP-schil zet ze ook in init). Zonder dit klonk
// een verse WASM-instantie anders dan een verse plugin (gevonden 15 okt: de dub-echo begon met tijd 0).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, ROOT } from './lib/pb.mjs';

for (const naam of readdirSync(join(ROOT, 'dsp', 'wasm')).filter((f) => f.endsWith('.wasm')).map((f) => f.slice(0, -5))) {
  test(`${naam}: begint op de standaardwaarden`, () => {
    const o = laadOntwerp(naam);
    const h = o.e.pb_maak(48000);
    for (const p of o.params) {
      assert.equal(o.e.pb_lees(h, p.index), p.standaard, `${p.naam}`);
      assert.ok(Math.fround(p.min) === p.min && Math.fround(p.max) === p.max && Math.fround(p.standaard) === p.standaard,
        `${p.naam}: grenzen en standaard moeten exact in float32 passen (AU geeft float door)`);
      assert.ok(p.min <= p.standaard && p.standaard <= p.max, `${p.naam}: standaard binnen het bereik`);
    }
    // een gezette waarde blijft staan, ook na andere aanroepen (zie dsp/bouw-wasm.sh: geen globale constructors)
    for (const p of o.params) {
      const v = p.min + 0.25 * (p.max - p.min);
      o.e.pb_zet(h, p.index, v);
      o.e.pb_latentie(h);
      assert.equal(o.e.pb_lees(h, p.index), v, `${p.naam}: gezette waarde kwam niet terug`);
    }
    o.e.pb_weg(h);
  });
}
