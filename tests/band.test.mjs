// band.test.mjs — de blokken naar een echte band (17 okt 2026, ref/referentie/RAPPORT.md) op de ketenmotor:
// bandsnelheid (wow en flutter als snelheid, los van de delaytijd) en bandruis (de sis van een Akai GX-4000D).
// De doelen komen uit de meting aan de Akai; de grenzen zijn ruim genoeg voor toeval (120 s ruis) en krap genoeg om
// het oude model (bandbeweging: ×3,2 van 375 naar 1200 ms) en een verkeerd ruisspectrum te vangen.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { compileer } from '../tool/src/keten.js';
import { fft } from '../tool/src/fft.js';
import { render } from './lib/keten.mjs';

const SR = 48000;

function bandOntwerp() {
  return {
    naam: 'bandproef', id: 'bandproef',
    knopen: [
      { id: 'in', type: 'in' }, { id: 'band', type: 'bandsnelheid', waarden: { diepte: 0.25 } },
      { id: 'lijn', type: 'vertraging', waarden: { tijd: 160 }, vast: { max: 1300 } }, { id: 'uit', type: 'uit' },
    ],
    kabels: [
      { van: ['in', 'uit'], naar: ['lijn', 'in'] }, { van: ['band', 'snelheid'], naar: ['lijn', 'beweging'] },
      { van: ['lijn', 'uit'], naar: ['uit', 'in'] },
    ],
    parameters: [
      { naam: 'Haperen', knoop: 'band', knop: 'diepte', min: 0, max: 1, standaard: 0.25, glijMs: 0 },
      { naam: 'Tijd', knoop: 'lijn', knop: 'tijd', min: 20, max: 1200, standaard: 160, glijMs: 0 },
    ],
  };
}

// Pulsen (100 per s) door de vertraging; de plek van elke puls = zijn zwaartepunt (Σ k·y / Σ y). Voor Lagrange-3 is
// dat precies de fractionele vertraging. Geeft de snelheidsafwijking ε = −dd/dt per puls (100 per s).
function snelheid(haperen, tijdMs, sec = 120) {
  const c = compileer(bandOntwerp());
  assert.deepEqual(c.fouten, []);
  const n = SR * sec, x = new Float32Array(n), stap = SR / 100;
  for (let i = 0; i < n; i += stap) x[i] = 1;
  const [y] = render(c.rij, [x], { waarden: { 0: haperen, 1: tijdMs } });
  const T = tijdMs * 0.001 * SR, d = [];
  // het venster volgt de puls: de vertraging zwerft rond T (±10 samples bij haperen 0,25, zoals de Akai; ±40 bij 1).
  // De eerste puls breed zoeken.
  let vorige = T, best = 0;
  for (let k = Math.round(T) - 60; k <= Math.round(T) + 60; k++) if (Math.abs(y[k]) > best) { best = Math.abs(y[k]); vorige = k; }
  for (let i = 0; i + T + 40 < n; i += stap) {
    const m = Math.round(i + vorige);
    let s = 0, sk = 0;
    for (let k = m - 16; k <= m + 16; k++) { s += y[k]; sk += (k - i) * y[k]; }
    vorige = sk / s;
    d.push(vorige / SR);
  }
  const eps = [];
  for (let j = 1; j < d.length - 1; j++) eps.push(-(d[j + 1] - d[j - 1]) / 2 * 100);
  return eps;
}
const rms = (a) => { const m = a.reduce((s, v) => s + v, 0) / a.length; return Math.sqrt(a.reduce((s, v) => s + (v - m) ** 2, 0) / a.length); };
function piekHz(eps, lo, hi) {
  let beste = 0, bij = 0;
  for (let f = lo; f <= hi; f += 0.02) {
    let re = 0, im = 0;
    for (let j = 0; j < eps.length; j++) { const w = 2 * Math.PI * f * j / 100; re += eps[j] * Math.cos(w); im -= eps[j] * Math.sin(w); }
    const p = re * re + im * im;
    if (p > beste) { beste = p; bij = f; }
  }
  return bij;
}

test('bandsnelheid: haperen 0,25 beweegt als de Akai (0,13–0,15 % RMS als echo), met de capstanlijn op 7,7 Hz', () => {
  const eps = snelheid(0.25, 160);
  const r = rms(eps) * 100;
  assert.ok(r >= 0.10 && r <= 0.20, `RMS ${r.toFixed(3)} % (Akai: 0,136–0,151 %)`);
  const f = piekHz(eps, 6, 12);
  assert.ok(Math.abs(f - 7.7) <= 0.3, `flutterpiek ${f.toFixed(2)} Hz (Akai: 7,7 Hz)`);
});

test('bandsnelheid: de toonhoogte jankt even veel bij een korte als bij een lange echo, en haperen schaalt lineair', () => {
  const kort = rms(snelheid(0.25, 160)), lang = rms(snelheid(0.25, 1200)), vol = rms(snelheid(1, 160));
  assert.ok(lang / kort >= 0.6 && lang / kort <= 1.6, `1200 ms / 160 ms = ${(lang / kort).toFixed(2)} (oud model: 7,5)`);
  assert.ok(vol / kort >= 3.6 && vol / kort <= 4.4, `haperen 1 / 0,25 = ${(vol / kort).toFixed(2)}`);
});

test('bandsnelheid op 0 = geen beweging: de vertraging is precies de tijd', () => {
  const eps = snelheid(0, 160, 4);
  assert.ok(rms(eps) < 1e-9, `RMS ${rms(eps)}`);
});

// het doel per terts: a + b·f (vermogen, dBFS²), gefit op de sis van de Akai tussen 400 Hz en 16 kHz (RAPPORT.md §3)
const A = 2.418917578933803e-9, B = 1.1576076497478606e-12;
function tertsen(y, sr) {
  const N = 1 << 14, w = new Float64Array(N);
  let sw = 0;
  for (let i = 0; i < N; i++) { w[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1)); sw += w[i] * w[i]; }
  const P = new Float64Array(N / 2 + 1);
  let k = 0;
  for (let i = 0; i + N <= y.length; i += N, k++) {
    const re = new Float64Array(N), im = new Float64Array(N);
    for (let j = 0; j < N; j++) re[j] = y[i + j] * w[j];
    fft(re, im);
    for (let j = 0; j <= N / 2; j++) P[j] += re[j] * re[j] + im[j] * im[j];
  }
  for (let j = 0; j <= N / 2; j++) P[j] *= 2 / (k * N * sw);
  const uit = {};
  for (const fc of [500, 1000, 2000, 4000, 8000, 16000]) {
    let s = 0;
    for (let j = 1; j <= N / 2; j++) { const f = j * sr / N; if (f >= fc * 2 ** (-1 / 6) && f < fc * 2 ** (1 / 6)) s += P[j]; }
    uit[fc] = 10 * Math.log10(s);
  }
  return uit;
}
function ruis(sr, niveau = 1) {
  const c = compileer({
    naam: 'ruisproef', id: 'ruisproef', knopen: [{ id: 'sis', type: 'bandruis', waarden: { niveau } }, { id: 'uit', type: 'uit' }],
    kabels: [{ van: ['sis', 'uit'], naar: ['uit', 'in'] }], parameters: [],
  });
  assert.deepEqual(c.fouten, []);
  const [y] = render(c.rij, [new Float32Array(sr * 20)], { sr });
  return y;
}

test('bandruis: het spectrum per terts volgt de sis van de Akai (±1,5 dB, 500 Hz – 16 kHz)', () => {
  const t = tertsen(ruis(SR), SR);
  for (const [fc, l] of Object.entries(t)) {
    const doel = 10 * Math.log10(A + B * Number(fc));
    assert.ok(Math.abs(l - doel) <= 1.5, `${fc} Hz: ${l.toFixed(1)} dB tegen ${doel.toFixed(1)} dB`);
  }
});

test('bandruis: dezelfde dichtheid per Hz op 96 kHz (±1 dB per terts)', () => {
  const a = tertsen(ruis(48000), 48000), b = tertsen(ruis(96000), 96000);
  for (const fc of Object.keys(a)) assert.ok(Math.abs(a[fc] - b[fc]) <= 1, `${fc} Hz: ${a[fc].toFixed(1)} / ${b[fc].toFixed(1)} dB`);
});
