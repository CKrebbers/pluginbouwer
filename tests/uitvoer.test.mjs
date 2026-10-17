// uitvoer.test.mjs — de DSP geeft nooit een subnormale float aan de host, ook niet bij invoer vlak boven FLT_MIN
// (clap-validator fuzz vond het in verse sessie F2: zachte invoer maal de mix werd een subnormale float).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { laadOntwerp, render } from './lib/pb.mjs';

const FLT_MIN = 1.1754943508222875e-38;
for (const naam of ['pijplijn_biquad', 'proef_delay', 'dub_echo', 'keten_dub_echo']) {
  test(`${naam}: geen subnormale uitvoer bij invoer rond FLT_MIN`, () => {
    const o = laadOntwerp(naam), n = 48000;
    const x = new Float32Array(n);
    for (let i = 0; i < n; i++) x[i] = Math.fround((i % 7 - 3) * FLT_MIN * (1 + (i % 13) / 4));   // normaal, maar klein
    const uit = render(o, [x, Float32Array.from(x, (v) => -v)], { blok: 128 });
    let sub = 0;
    for (const k of uit) for (const v of k) if (v !== 0 && Math.abs(v) < FLT_MIN) sub++;
    assert.equal(sub, 0, `${sub} subnormale samples in de uitvoer`);
  });
}
