// De biquad tegen zijn eigen formule: H(e^{jω}) = (b0 + b1 z⁻¹ + b2 z⁻²) / (a0 + a1 z⁻¹ + a2 z⁻²) met de
// RBJ-coëfficiënten, hier OPNIEUW uitgeschreven in JavaScript met Math.* (onafhankelijk van dsp/).
// Gemeten: impulsrespons van de WASM, DFT op losse frequenties. Grens uit PROMPT.md: ≤ 0,1 dB en ≤ 1° tot 0,9·Nyquist.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { laadOntwerp, render } from './lib/pb.mjs';
import { impuls } from './lib/signalen.mjs';

const ontwerp = laadOntwerp('pijplijn_biquad');

function rbjLaagdoor(f0, Q, sr) {
  const w0 = 2 * Math.PI * f0 / sr, c = Math.cos(w0), alpha = Math.sin(w0) / (2 * Q);
  return { b: [(1 - c) / 2, 1 - c, (1 - c) / 2], a: [1 + alpha, -2 * c, 1 - alpha] };
}
function H({ b, a }, w) {
  const re = (k) => Math.cos(-k * w), im = (k) => Math.sin(-k * w);
  const nr = b[0] + b[1] * re(1) + b[2] * re(2), ni = b[1] * im(1) + b[2] * im(2);
  const dr = a[0] + a[1] * re(1) + a[2] * re(2), di = a[1] * im(1) + a[2] * im(2);
  const d = dr * dr + di * di;
  return [(nr * dr + ni * di) / d, (ni * dr - nr * di) / d];
}
function dft(x, w) {
  let re = 0, im = 0;
  for (let n = 0; n < x.length; n++) { re += x[n] * Math.cos(w * n); im -= x[n] * Math.sin(w * n); }
  return [re, im];
}

for (const [f0, Q, sr] of [[1000, 0.70703125, 48000], [80, 4, 44100], [12000, 0.5, 96000], [5000, 9, 48000]]) {
  test(`laagdoor ${f0} Hz, Q ${Q}, ${sr} Hz tegen de formule`, () => {
    const n = 1 << 15;
    const [h] = render(ontwerp, [impuls(n)], { sr, waarden: { 0: f0, 1: Q } });
    const coef = rbjLaagdoor(f0, Q, sr);
    let maxDb = 0, maxGraad = 0, waar = 0;
    for (let k = 1; k <= 400; k++) {
      const f = 20 * Math.pow((0.9 * sr / 2) / 20, k / 400);
      const w = 2 * Math.PI * f / sr;
      const [mr, mi] = dft(h, w), [tr, ti] = H(coef, w);
      const magT = Math.hypot(tr, ti), magM = Math.hypot(mr, mi);
      if (magT < 1e-5) continue;  // onder −100 dB: daar meet je float32-afronding, geen filter
      const dDb = Math.abs(20 * Math.log10(magM / magT));
      let dFase = Math.abs(Math.atan2(mi, mr) - Math.atan2(ti, tr)) * 180 / Math.PI;
      if (dFase > 180) dFase = 360 - dFase;
      if (dDb > maxDb) { maxDb = dDb; waar = f; }
      maxGraad = Math.max(maxGraad, dFase);
    }
    assert.ok(maxDb <= 0.1, `${maxDb.toFixed(4)} dB naast de formule bij ${waar.toFixed(0)} Hz`);
    assert.ok(maxGraad <= 1, `${maxGraad.toFixed(3)}° naast de formule`);
  });
}
