// De dub-echo tegen onafhankelijke referenties (PROMPT.md "Bewijs"). Grenzen met hun reden staan bij elke toets en
// in bouwpakket/dub-echo/SPEC.md. Meetwaarden gaan naar tests/uit/dubecho.json (de BIB leest ze).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { writeFileSync, readFileSync, mkdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { laadOntwerp, render, nullTest, ROOT } from './lib/pb.mjs';
import { sweep, stukjes, ruis, impuls } from './lib/signalen.mjs';
import { spectrum, fft, volgendeMacht } from './lib/fft.mjs';

const o = laadOntwerp('dub_echo');
const P = Object.fromEntries(o.params.map((p) => [p.naam, p.index]));
const SR = 48000;
const meting = {};
const uitDir = join(ROOT, 'tests', 'uit');
mkdirSync(uitDir, { recursive: true });
const bewaar = () => writeFileSync(join(uitDir, 'dubecho.json'), JSON.stringify(meting, null, 2) + '\n');
const met = (w) => Object.fromEntries(Object.entries(w).map(([k, v]) => [P[k], v]));
const db = (x) => 20 * Math.log10(x);
// zie SPEC.md "Aliasing": vastgesteld NA de eerste meting, met reden
const GRENS_ALIAS_KETEN = -60;

// ---------------------------------------------------------------------------------------------------- complex
const C = (re, im = 0) => ({ re, im });
const mul = (a, b) => C(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
const div = (a, b) => { const d = b.re * b.re + b.im * b.im; return C((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d); };
const add = (a, b) => C(a.re + b.re, a.im + b.im);
const zinv = (w, k = 1) => C(Math.cos(w * k), -Math.sin(w * k));
const mag = (a) => Math.hypot(a.re, a.im);
// s = (1/g)(1 − z⁻¹)/(1 + z⁻¹): de bilineaire transformatie met voorvervorming (g = tan(π fc / fs))
const sVan = (w, fc) => { const g = Math.tan(Math.PI * fc / SR); return div(add(C(1), C(-zinv(w).re, -zinv(w).im)), mul(C(g), add(C(1), zinv(w)))); };

function theorie(w, { tijd, laag, toon, reso }) {
  const adaa = mul(C(0.5), add(C(1), zinv(w)));                          // klein signaal: (x[n] + x[n−1]) / 2
  const D = tijd * 0.001 * SR - 0.5, i = Math.floor(D), f = D - i, pts = [-1, 0, 1, 2];
  let L = C(0);
  for (const k of pts) { let h = 1; for (const j of pts) if (j !== k) h *= (f - j) / (k - j); L = add(L, mul(C(h), zinv(w, i + k))); }
  const sh = sVan(w, laag), hp = div(sh, add(C(1), sh));                 // één-pool hoogdoor: s/(1+s)
  const sl = sVan(w, toon), lp = div(C(1), add(add(mul(sl, sl), mul(C(1 / reso), sl)), C(1)));  // 1/(s² + s/Q + 1)
  return mul(mul(adaa, L), mul(hp, lp));
}
function dft(x, w) { let re = 0, im = 0; for (let n = 0; n < x.length; n++) { re += x[n] * Math.cos(w * n); im -= x[n] * Math.sin(w * n); } return C(re, im); }

test('lineair (klein signaal, geen terugkoppeling): ADAA × Lagrange × hoogdoor × SVF', () => {
  const inst = { tijd: 20.5, laag: 80, toon: 4000, reso: 2 };
  const a = 1e-3;
  const [h] = render(o, [impuls(1 << 14, a)], { waarden: met({ Tijd: inst.tijd, 'Laag weg': inst.laag, Toon: inst.toon, Resonantie: inst.reso, Terugkoppeling: 0, Verzadiging: 0, Haperen: 0, Mix: 1 }) });
  let maxDb = 0, maxGr = 0;
  for (let k = 1; k <= 300; k++) {
    const f = 20 * Math.pow((0.45 * SR) / 20, k / 300), w = 2 * Math.PI * f / SR;
    const t = theorie(w, inst), m = dft(h, w);
    if (mag(t) < 1e-4) continue;
    maxDb = Math.max(maxDb, Math.abs(db(mag(m) / a / mag(t))));
    let d = Math.abs(Math.atan2(m.im, m.re) - Math.atan2(t.im, t.re)) * 180 / Math.PI; if (d > 180) d = 360 - d;
    maxGr = Math.max(maxGr, d);
  }
  meting.lineair = { maxDb, maxGraad: maxGr };
  bewaar();
  // grens: 0,01 dB en 0,05° (de rest is de kubische term van tanh op −60 dBFS: ~3e-7 relatief, en float32-uitvoer)
  assert.ok(maxDb <= 0.01, `${maxDb} dB naast de formules`);
  assert.ok(maxGr <= 0.05, `${maxGr}° naast de formules`);
});

// -------------------------------------------------------------------------------------------- Python-referentie
function python(x, sr, p, ideaal = 0) {
  const d = join(tmpdir(), `pb-ref-${process.pid}-${Math.random().toString(36).slice(2)}`);
  writeFileSync(d + '.in', Buffer.from(Float64Array.from(x).buffer));
  execFileSync('python3', ['-I', join(ROOT, 'ref', 'dub_echo_ref.py'), d + '.in', d + '.uit', String(sr), JSON.stringify(p), String(ideaal)]);
  const b = readFileSync(d + '.uit');
  return new Float64Array(b.buffer, b.byteOffset, b.byteLength / 8);
}
const REF_SETS = [
  { naam: 'gewoon', p: { Tijd: 123.4, Terugkoppeling: 0.6, Verzadiging: 6, Toon: 3000, 'Laag weg': 100, Resonantie: 0.70703125, Haperen: 0, Mix: 0.5 } },
  { naam: 'heet', p: { Tijd: 77.7, Terugkoppeling: 0.95, Verzadiging: 18, Toon: 1500, 'Laag weg': 250, Resonantie: 2, Haperen: 0, Mix: 0.75 } },
];
for (const set of REF_SETS) {
  test(`WASM tegen de Python-referentie (${set.naam}, float64, andere code)`, () => {
    const x = stukjes(SR, 1.5, 0.6);
    const [w] = render(o, [x], { waarden: met(set.p) });
    const r = python(x, SR, set.p);
    const res = nullTest([w], [Float32Array.from(r)]);
    meting[`referentie-${set.naam}`] = { piekDb: res.piekDb, rmsDb: res.rmsDb };
    bewaar();
    // grens −120 dBFS: beide rekenen in float64; verschillen komen van libm tegen pb_math (afronding) en de
    // float32-uitvoer. Ruim onder de −90 van de null-test, ruim boven wat een echte fout zou geven.
    assert.ok(res.piekDb <= -120, `${res.piekDb} dBFS verschil met de referentie (eerste bij sample ${res.eersteVerschil})`);
  });
}

// ------------------------------------------------------------------------------------- harmonischen (ESS)
function ess(y, x, f1, f2, T) {
  // Farina 2000: inverse = tijdomgekeerde sweep × e^{−t/L} (compenseert de 6 dB/oct roze tilt); IR = y ∗ inverse
  const n = x.length, L = T / Math.log(f2 / f1);
  const inv = new Float64Array(n);
  for (let i = 0; i < n; i++) inv[i] = x[n - 1 - i] * Math.exp(-(i / SR) / L);
  const m = volgendeMacht(2 * n);
  const ar = new Float64Array(m), ai = new Float64Array(m), br = new Float64Array(m), bi = new Float64Array(m);
  ar.set(y); br.set(inv);
  fft(ar, ai); fft(br, bi);
  for (let k = 0; k < m; k++) { const r = ar[k] * br[k] - ai[k] * bi[k], i = ar[k] * bi[k] + ai[k] * br[k]; ar[k] = r; ai[k] = i; }
  fft(ar, ai, true);
  return { ir: ar, nul: n - 1, L };
}
function harmonischeNiveaus(y, x, f1, f2, T, delay) {
  const { ir, nul, L } = ess(y, x, f1, f2, T);
  const uit = {};
  const lin = nul + delay;
  for (const k of [1, 2, 3, 5]) {
    const plek = Math.round(lin - L * Math.log(k) * SR);
    const len = 2048, re = new Float64Array(len), im = new Float64Array(len);
    for (let i = 0; i < len; i++) re[i] = ir[plek - 256 + i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / (len - 1)));
    fft(re, im);
    // gemiddeld niveau over 300–3000 Hz (de grondtoon die de harmonische opwekt)
    let som = 0, nb = 0;
    for (let b = 0; b < len / 2; b++) { const f = b * SR / len / k; if (f >= 300 && f <= 3000) { som += re[b] * re[b] + im[b] * im[b]; nb++; } }
    uit[k] = 10 * Math.log10(som / nb + 1e-300);
  }
  return uit;
}
test('harmonischen (exponential sine sweep) tegen de ideale saturatie op 16×', () => {
  const T = 4, f1 = 50, f2 = 20000;
  const x = sweep(f1, f2, SR, T, 0.5);
  const pad = new Float32Array(x.length + SR); pad.set(x);
  const p = { Tijd: 20, Terugkoppeling: 0, Verzadiging: 12, Toon: 12000, 'Laag weg': 20, Resonantie: 0.70703125, Haperen: 0, Mix: 1 };
  const [w] = render(o, [pad], { waarden: met(p) });
  const r = python(pad, SR, p, 16);
  const delay = Math.round(20 * 0.001 * SR);
  const hw = harmonischeNiveaus(w, x, f1, f2, T, delay), hr = harmonischeNiveaus(r, x, f1, f2, T, delay);
  const rel = (h) => ({ H3: h[3] - h[1], H5: h[5] - h[1], H2: h[2] - h[1] });
  meting.harmonischen = { wasm: rel(hw), ideaal: rel(hr) };
  bewaar();
  // grens 1 dB op H3 en H5 (PROMPT.md); H2 moet ver weg zijn (tanh is oneven) in beide
  assert.ok(Math.abs(rel(hw).H3 - rel(hr).H3) <= 1, `H3: ${rel(hw).H3.toFixed(2)} tegen ${rel(hr).H3.toFixed(2)} dB`);
  assert.ok(Math.abs(rel(hw).H5 - rel(hr).H5) <= 1, `H5: ${rel(hw).H5.toFixed(2)} tegen ${rel(hr).H5.toFixed(2)} dB`);
  assert.ok(rel(hw).H2 < -60 && rel(hr).H2 < -60, `H2 hoort er niet te zijn: ${rel(hw).H2.toFixed(1)} / ${rel(hr).H2.toFixed(1)} dB`);
});

// ---------------------------------------------------------------------------------------------------- aliasing
function aliasing(y, k0) {
  const p = spectrum(y), N = y.length;
  const harm = new Set(); for (let k = 1; k * k0 <= N / 2; k++) harm.add(k * k0);
  let grond = p[k0], rest = 0;
  for (let b = 1; b <= N / 2; b++) if (!harm.has(b)) rest += p[b];
  return 10 * Math.log10(rest / grond);
}
function saturatieAdaa(x, drive) {
  // het echte blok (pb_t_tape), het hele signaal door één TapeVerzadiging: in stukken met overlap
  const { e } = o, n = x.length, uit = new Float32Array(n), stuk = 4096, overlap = 64;
  for (let i = 0; i < n; i += stuk - overlap) {
    const van = Math.max(0, i - overlap), m = Math.min(stuk, n - van);
    new Float32Array(e.memory.buffer, e.pb_t_buffer(), m).set(x.subarray(van, van + m));
    e.pb_t_tape(e.pb_t_buffer(), m, drive);
    const res = new Float32Array(e.memory.buffer, e.pb_t_buffer(), m);
    uit.set(res.subarray(i - van, Math.min(m, i - van + stuk - overlap)), i);
    if (van + m >= n) break;
  }
  return uit;
}
function pythonIdeaal(x, drive) {
  const d = join(tmpdir(), `pb-sat-${process.pid}`);
  writeFileSync(d + '.in', Buffer.from(Float64Array.from(x).buffer));
  execFileSync('python3', ['-I', '-c', `import sys; sys.path.insert(0, ${JSON.stringify(join(ROOT, 'ref'))}); import numpy as np; import dub_echo_ref as r; x=np.fromfile(${JSON.stringify(d + '.in')}, dtype='<f8'); r.ideale_saturatie(x, ${drive}, 16).astype('<f8').tofile(${JSON.stringify(d + '.uit')})`]);
  const b = readFileSync(d + '.uit');
  return Float32Array.from(new Float64Array(b.buffer, b.byteOffset, b.byteLength / 8));
}
// het venster ligt in het MIDDEN: aan het eind mist de 16×-referentie de staart van zijn eigen filter (randeffect)
const N_AL = 1 << 16, EXTRA = 8192;
const sinusBin = (k0, amp) => { const x = new Float32Array(N_AL + 2 * EXTRA), f0 = SR * k0 / N_AL; for (let i = 0; i < x.length; i++) x[i] = amp * Math.sin(2 * Math.PI * f0 * i / SR); return x; };

test('aliasing van het saturatieblok los: naïef, ADAA (het echte blok) en ideaal (16×)', () => {
  // f0 ≈ 5000 Hz, oneven bin (dan valt een spiegeling nooit op een harmonische), drive 12 dB, amplitude 0,5
  const k0 = 6827, drive = Math.pow(10, 12 / 20), x = sinusBin(k0, 0.5);
  const r = {
    naief: aliasing(x.map((v) => Math.tanh(drive * v) / drive).subarray(EXTRA, EXTRA + N_AL), k0),
    adaa: aliasing(saturatieAdaa(x, drive).subarray(EXTRA, EXTRA + N_AL), k0),
    ideaal: aliasing(pythonIdeaal(x, drive).subarray(EXTRA, EXTRA + N_AL), k0),
  };
  meting.aliasingBlok = { f0: SR * k0 / N_AL, driveDb: 12, ...r };
  bewaar();
  assert.ok(r.ideaal <= -100, `de meetlat zelf aliast (${r.ideaal.toFixed(1)} dB)`);
  // eigenschap, geen klankeis: ADAA-1 moet beter zijn dan naïef; hoeveel staat in de BIB (zie de keten-toets)
  assert.ok(r.adaa < r.naief, `ADAA ${r.adaa.toFixed(1)} dB is niet beter dan naïef ${r.naief.toFixed(1)} dB`);
});

test('aliasing van de hele keten (zoals je hem hoort) tegen dezelfde keten met ideale saturatie', () => {
  // standaardinstelling, één herhaling (terugkoppeling 0), mix 1; f0 ≈ 2000 Hz, drive 12 dB, amplitude 0,5
  const k0 = 2731, x = sinusBin(k0, 0.5);
  const p = { Tijd: 20, Terugkoppeling: 0, Verzadiging: 12, Toon: 3000, 'Laag weg': 100, Resonantie: 0.70703125, Haperen: 0, Mix: 1 };
  const [w] = render(o, [x], { waarden: met(p) });
  const r = Float32Array.from(python(x, SR, p, 16));
  const res = { wasm: aliasing(w.subarray(EXTRA, EXTRA + N_AL), k0), ideaal: aliasing(r.subarray(EXTRA, EXTRA + N_AL), k0) };
  // ter informatie (geen eis): dezelfde keten met de toon helemaal open, 5 kHz — daar telt het blok zelf
  const k5 = 6827, x5 = sinusBin(k5, 0.5), p5 = { ...p, Toon: 12000, 'Laag weg': 20 };
  const [w5] = render(o, [x5], { waarden: met(p5) });
  const r5 = Float32Array.from(python(x5, SR, p5, 16));
  meting.aliasingKeten = { f0: SR * k0 / N_AL, driveDb: 12, toon: 3000, ...res,
    toonOpen5kHz: { wasm: aliasing(w5.subarray(EXTRA, EXTRA + N_AL), k5), ideaal: aliasing(r5.subarray(EXTRA, EXTRA + N_AL), k5) } };
  bewaar();
  assert.ok(res.ideaal <= -100, `de meetlat zelf aliast (${res.ideaal.toFixed(1)} dB)`);
  assert.ok(res.wasm <= GRENS_ALIAS_KETEN, `keten aliast ${res.wasm.toFixed(1)} dB onder de grondtoon (grens ${GRENS_ALIAS_KETEN})`);
});
// ---------------------------------------------------------------------------------------------------- stabiliteit
test('stabiel over het hele raster: nooit NaN/Inf, begrensd, zelfoscillatie houdt aan', () => {
  const x = new Float32Array(SR * 3); x.set(ruis(SR, 0.25, 0.8, 99));
  let slechtst = 0, n = 0;
  const zelf = [];
  for (const fb of [0, 0.5, 0.95, 1, 1.1, 1.25]) for (const toon of [300, 3000, 12000]) for (const reso of [0.5, 2, 4])
    for (const verz of [0, 12, 24]) for (const haper of [0, 1]) {
      const [y] = render(o, [x], { waarden: met({ Tijd: 60, Terugkoppeling: fb, Toon: toon, Resonantie: reso, Verzadiging: verz, Haperen: haper, Mix: 1 }) });
      let piek = 0, laatste = 0;
      for (let i = 0; i < y.length; i++) {
        if (!Number.isFinite(y[i])) assert.fail(`NaN/Inf bij fb ${fb} toon ${toon} reso ${reso} verz ${verz} haper ${haper}`);
        piek = Math.max(piek, Math.abs(y[i])); if (i >= y.length - SR / 2) laatste += y[i] * y[i];
      }
      slechtst = Math.max(slechtst, piek); n++;
      if (fb >= 1.1 && verz <= 12 && toon >= 3000 && reso <= 2) zelf.push(10 * Math.log10(laatste / (SR / 2) + 1e-30));
    }
  meting.stabiliteit = { combinaties: n, hoogstePiek: slechtst, zelfoscillatieRmsDb: [Math.min(...zelf), Math.max(...zelf)] };
  bewaar();
  // grens 8 (≈ +18 dBFS): de saturatie houdt de lus onder 1/g, het resonerende filter erna kan tot ~Q extra geven
  assert.ok(slechtst <= 8, `piek ${slechtst}`);
  // zelfoscillatie: bij terugkoppeling ≥ 1,1 zonder zware filtering moet er na 2,5 s nog iets klinken (> −40 dBFS)
  assert.ok(Math.min(...zelf) > -40, `zelfoscillatie stierf uit: ${Math.min(...zelf).toFixed(1)} dBFS`);
});

test('staart: komt onder −90 dBFS binnen de gemelde staarttijd', () => {
  const uit = [];
  for (const fb of [0.3, 0.8, 0.95]) {
    const p = met({ Tijd: 200, Terugkoppeling: fb, Haperen: 0.5, Mix: 1, Verzadiging: 6 });
    const h = o.e.pb_maak(SR); for (const [i, v] of Object.entries(p)) o.e.pb_zet(h, Number(i), v);
    const staart = o.e.pb_staart(h); o.e.pb_weg(h);
    const lengte = Math.ceil((staart + 1) * SR), x = new Float32Array(lengte); x.set(ruis(SR, 0.2, 0.5, 5));
    const [y] = render(o, [x], { waarden: p });
    let laatsteLuid = 0;
    for (let i = 0; i < y.length; i++) if (Math.abs(y[i]) > Math.pow(10, -90 / 20)) laatsteLuid = i;
    const echt = laatsteLuid / SR - 0.2;
    uit.push({ terugkoppeling: fb, gemeldS: staart, gemetenS: echt });
    assert.ok(echt <= staart, `fb ${fb}: nog geluid na ${echt.toFixed(2)} s, gemeld ${staart.toFixed(2)} s`);
  }
  meting.staart = uit;
  bewaar();
});

// ---------------------------------------------------------------------------------------------------- IMD
test('intermodulatie (SMPTE, 60 Hz + 7 kHz, 4:1) tegen de ideale saturatie op 16×', () => {
  // coherente bins: 60 Hz = bin 82, 7 kHz = bin 9557 (N = 2^16); zijbanden op 9557 ± n·82
  const kL = 82, kH = 9557, x = new Float32Array(N_AL + 2 * EXTRA);
  for (let i = 0; i < x.length; i++) x[i] = 0.4 * Math.sin(2 * Math.PI * kL * i / N_AL) + 0.1 * Math.sin(2 * Math.PI * kH * i / N_AL);
  const p = { Tijd: 20, Terugkoppeling: 0, Verzadiging: 12, Toon: 12000, 'Laag weg': 20, Resonantie: 0.70703125, Haperen: 0, Mix: 1 };
  const [w] = render(o, [x], { waarden: met(p) });
  const r = Float32Array.from(python(x, SR, p, 16));
  const imd = (y) => { const s = spectrum(y.subarray(EXTRA, EXTRA + N_AL)); let zb = 0; for (let n = 1; n <= 4; n++) zb += s[kH + n * kL] + s[kH - n * kL]; return 10 * Math.log10(zb / s[kH]); };
  const res = { wasm: imd(w), ideaal: imd(r) };
  meting.imd = { ...res, verschilDb: res.wasm - res.ideaal };
  bewaar();
  // grens 1 dB (PROMPT.md): de saturatie moet hetzelfde mengen als de ideale kromme
  assert.ok(Math.abs(res.wasm - res.ideaal) <= 1, `IMD ${res.wasm.toFixed(2)} dB tegen ideaal ${res.ideaal.toFixed(2)} dB`);
});
