// De fractionele delay (Lagrange 3e orde) tegen Laakso e.a. 1996. Twee vragen:
//  1. doet de code wat de formule zegt? Gemeten respons = biquad-formule × Lagrange-formule, ≤ 0,001 dB en 0,01°.
//  2. hoe ver zit Lagrange-3 van een IDEALE fractionele delay e^{-jωD}? Dat is de eigenschap van de methode, geen
//     fout van de code. Gemeten (tests/uit/lagrange.txt): bij fractie ½ −0,031 dB op 0,1·fs, maar −1,07 dB op 0,25·fs
//     en −12,7 dB op 0,45·fs. Grens: ≤ 0,05 dB en ≤ 0,001 sample tot 0,1·fs (4,8 kHz bij 48 kHz). Reden: in de
//     dub-echo zit het feedbackfilter rond 2–5 kHz, daarboven dempt de lus zelf al. De demping boven 0,1·fs (en dat
//     hij meebeweegt met de fractie als de tijd moduleert) staat in de BIB bij het blok; een fijnere interpolator
//     (hogere orde of oversampling) is daar een keuze, geen stille verruiming van deze grens.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, render, ROOT } from './lib/pb.mjs';
import { impuls } from './lib/signalen.mjs';

const o = laadOntwerp('proef_delay');

function cmul([a, b], [c, d]) { return [a * c - b * d, a * d + b * c]; }
function biquadH(f0, Q, sr, w) {
  const w0 = 2 * Math.PI * f0 / sr, c = Math.cos(w0), al = Math.sin(w0) / (2 * Q);
  const b = [(1 - c) / 2, 1 - c, (1 - c) / 2], a = [1 + al, -2 * c, 1 - al];
  let nr = 0, ni = 0, dr = 0, di = 0;
  for (let k = 0; k < 3; k++) { nr += b[k] * Math.cos(k * w); ni -= b[k] * Math.sin(k * w); dr += a[k] * Math.cos(k * w); di -= a[k] * Math.sin(k * w); }
  const d = dr * dr + di * di;
  return [(nr * dr + ni * di) / d, (ni * dr - nr * di) / d];
}
// Lagrange: punten op vertraging i-1 .. i+2, gewichten als product (Laakso vgl. 42), hier los uitgeschreven
function lagrangeH(D, w) {
  const i = Math.floor(D), f = D - i, punten = [-1, 0, 1, 2];
  let re = 0, im = 0;
  for (const k of punten) {
    let h = 1;
    for (const j of punten) if (j !== k) h *= (f - j) / (k - j);
    re += h * Math.cos(w * (i + k)); im -= h * Math.sin(w * (i + k));
  }
  return [re, im];
}
function dft(x, w) { let re = 0, im = 0; for (let n = 0; n < x.length; n++) { re += x[n] * Math.cos(w * n); im -= x[n] * Math.sin(w * n); } return [re, im]; }
const db = ([r, i]) => 20 * Math.log10(Math.hypot(r, i));

test('delay doet wat Lagrange-3 belooft (formule × formule)', () => {
  const sr = 48000, f0 = 20000, Q = 0.5;
  for (const ms of [1.0, 10.3, 99.99, 250.0078125]) {
    const D = ms * 0.001 * sr;
    const n = 1 << 14;
    const [h] = render(o, [impuls(n)], { sr, waarden: { 0: f0, 1: Q, 2: ms, 3: 1 } });
    let slechtst = 0, slechtstFase = 0;
    for (let k = 1; k <= 200; k++) {
      const w = Math.PI * 0.9 * k / 200;
      const theorie = cmul(biquadH(f0, Q, sr, w), lagrangeH(D, w));
      if (Math.hypot(...theorie) < 1e-4) continue;
      const m = dft(h, w);
      slechtst = Math.max(slechtst, Math.abs(db(m) - db(theorie)));
      let df = Math.abs(Math.atan2(m[1], m[0]) - Math.atan2(theorie[1], theorie[0])) * 180 / Math.PI;
      if (df > 180) df = 360 - df;
      slechtstFase = Math.max(slechtstFase, df);
    }
    assert.ok(slechtst <= 0.001, `D=${D}: ${slechtst} dB naast de formule`);
    assert.ok(slechtstFase <= 0.01, `D=${D}: ${slechtstFase}° naast de formule`);
  }
});

test('Lagrange-3 tegen de ideale fractionele delay (eigenschap van de methode)', () => {
  const regels = ['fractie  f/fs   amplitude-dB   fasevertraging-fout (samples)'];
  let maxDb = 0, maxTau = 0;
  for (const frac of [0, 0.1, 0.25, 0.5, 0.75, 0.9]) {
    const D = 10 + frac;
    for (const rel of [0.02, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.4, 0.45]) {
      const w = 2 * Math.PI * rel;
      const H = lagrangeH(D, w);
      const aDb = db(H);
      let fase = -Math.atan2(H[1], H[0]);           // = ω·τ (mod 2π)
      const ideaal = w * D;
      fase += 2 * Math.PI * Math.round((ideaal - fase) / (2 * Math.PI));
      const tauFout = fase / w - D;
      regels.push(`${frac.toFixed(2)}     ${rel.toFixed(2)}   ${aDb.toFixed(4).padStart(9)}      ${tauFout.toFixed(5)}`);
      if (rel <= 0.1) { maxDb = Math.max(maxDb, Math.abs(aDb)); maxTau = Math.max(maxTau, Math.abs(tauFout)); }
    }
  }
  mkdirSync(join(ROOT, 'tests', 'uit'), { recursive: true });
  writeFileSync(join(ROOT, 'tests', 'uit', 'lagrange.txt'), regels.join('\n') + `\n\ntot 0,1·fs: max ${maxDb.toFixed(4)} dB, max ${maxTau.toFixed(5)} sample\n`);
  assert.ok(maxDb <= 0.05, `amplitudefout ${maxDb} dB tot 0,1·fs`);
  assert.ok(maxTau <= 0.001, `fasevertraging ${maxTau} sample tot 0,1·fs`);
});
