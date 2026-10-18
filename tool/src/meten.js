// meten.js — de metingen van de tool, op de echte ketenmotor (WASM). Dezelfde functies gebruiken de CI-toetsen
// (tests/dubecho.test.mjs), dus wat de tool laat zien is wat CI toetst.
import { fft, spectrum, volgendeMacht } from './fft.js';

// ---- de motor (pg_*-ABI van dsp/pb_graaf_wasm.cpp) -----------------------------------------------------------------
export function motorInstantie(wasmModule) {
  return new WebAssembly.Instance(wasmModule, {}).exports;
}
export function laadKeten(e, rij, sr) {
  const h = e.pg_maak(sr);
  if (h < 0) throw new Error('geen vrije instantie in de motor');
  new Float64Array(e.memory.buffer, e.pg_rij(), rij.length).set(rij);
  if (!e.pg_laad(h, rij.length)) { e.pg_weg(h); throw new Error('de motor weigerde de keten (te groot of ongeldig)'); }
  return h;
}
// events: [[sample, index, waarde], …]; elk event geldt vanaf zijn sample (zoals een CLAP-event). gebeurtenissen is
// dezelfde lijst onder de naam van de instrumentkant (een toets indrukken en loslaten: Noot en Poort).
// tempo: een transport dat speelt vanaf tel 0 op dit tempo (zoals pb-render --tempo), bij elk deelblok gezet met de
// plek als clap_beattime afgerond, zoals een host hem doorgeeft.
export function renderKeten(e, rij, invoer, { sr = 48000, waarden = {}, blok = 128, events = [], gebeurtenissen = [], tempo = 0 } = {}) {
  events = [...events, ...gebeurtenissen].sort((a, b) => a[0] - b[0]);
  const h = laadKeten(e, rij, sr);
  try {
    for (const [i, v] of Object.entries(waarden)) e.pg_zet(h, Number(i), v);
    const k = invoer.length, n = invoer[0].length, uit = invoer.map(() => new Float32Array(n));
    for (let pos = 0, ev = 0; pos < n;) {
      while (ev < events.length && events[ev][0] <= pos) { e.pg_zet(h, events[ev][1], events[ev][2]); ev++; }
      const grens = Math.min(n, (Math.floor(pos / blok) + 1) * blok, ev < events.length ? events[ev][0] : n);
      const m = grens - pos;
      if (tempo > 0) { const F = 2 ** 31; e.pg_transport(h, tempo, Math.floor(pos / sr * tempo / 60 * F + 0.5) / F, 1); }
      for (let c = 0; c < k; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).set(invoer[c].subarray(pos, pos + m));
      e.pg_verwerk(h, k, m);
      for (let c = 0; c < k; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), pos);
      pos += m;
    }
    return { uit, staart: e.pg_staart(h) };
  } finally { e.pg_weg(h); }
}

// ---- aliasing: energie buiten de harmonische bins t.o.v. de grondtoon (coherent op bin k0, rechthoekig venster) ----
export function aliasing(y, k0) {
  const p = spectrum(y), N = y.length;
  const harm = new Set(); for (let k = 1; k * k0 <= N / 2; k++) harm.add(k * k0);
  let rest = 0;
  for (let b = 1; b <= N / 2; b++) if (!harm.has(b)) rest += p[b];
  return 10 * Math.log10(rest / p[k0]);
}
export const N_AL = 1 << 16, EXTRA = 8192;
export function sinusBin(k0, amp, sr = 48000) {
  const x = new Float32Array(N_AL + 2 * EXTRA), f0 = sr * k0 / N_AL;
  for (let i = 0; i < x.length; i++) x[i] = amp * Math.sin(2 * Math.PI * f0 * i / sr);
  return x;
}
export const midden = (y) => y.subarray(EXTRA, EXTRA + N_AL);

// ---- harmonischen: grondtoon 1 kHz (coherent), H1..H5 in dB t.o.v. H1, en THD ---------------------------------------
export function harmonischen(y, k0) {
  const p = spectrum(y), h = [];
  for (let k = 1; k <= 5; k++) h.push(10 * Math.log10((p[k * k0] || 1e-300) / p[k0]));
  let som = 0; for (let k = 2; k * k0 <= y.length / 2; k++) som += p[k * k0];
  return { niveaus: h, thdDb: 10 * Math.log10(som / p[k0] + 1e-300) };
}

// ---- exponential sine sweep (Farina 2000) -----------------------------------------------------------------------------
export function ess(y, x, f1, f2, T, sr) {
  const n = x.length, L = T / Math.log(f2 / f1);
  const inv = new Float64Array(n);
  for (let i = 0; i < n; i++) inv[i] = x[n - 1 - i] * Math.exp(-(i / sr) / L);
  const m = volgendeMacht(2 * n);
  const ar = new Float64Array(m), ai = new Float64Array(m), br = new Float64Array(m), bi = new Float64Array(m);
  ar.set(y); br.set(inv);
  fft(ar, ai); fft(br, bi);
  for (let k = 0; k < m; k++) { const r = ar[k] * br[k] - ai[k] * bi[k], i = ar[k] * bi[k] + ai[k] * br[k]; ar[k] = r; ai[k] = i; }
  fft(ar, ai, true);
  return { ir: ar, nul: n - 1, L };
}
export function harmonischeNiveausESS(y, x, f1, f2, T, delay, sr) {
  const { ir, nul, L } = ess(y, x, f1, f2, T, sr);
  const uit = {};
  for (const k of [1, 2, 3, 5]) {
    const plek = Math.round(nul + delay - L * Math.log(k) * sr);
    const len = 2048, re = new Float64Array(len), im = new Float64Array(len);
    for (let i = 0; i < len; i++) re[i] = ir[plek - 256 + i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / (len - 1)));
    fft(re, im);
    let som = 0, nb = 0;
    for (let b = 0; b < len / 2; b++) { const f = b * sr / len / k; if (f >= 300 && f <= 3000) { som += re[b] * re[b] + im[b] * im[b]; nb++; } }
    uit[k] = 10 * Math.log10(som / nb + 1e-300);
  }
  return uit;
}

// ---- frequentierespons (klein signaal: impuls van 1e-3) op log-frequenties ------------------------------------------
export function respons(e, rij, { sr = 48000, waarden = {}, punten = 96, n = 1 << 15 } = {}) {
  const x = new Float32Array(n); x[0] = 1e-3;
  const { uit } = renderKeten(e, rij, [x], { sr, waarden });
  const re = Float64Array.from(uit[0], (v) => v / 1e-3), im = new Float64Array(n);
  fft(re, im);
  const lijst = [];
  for (let i = 0; i < punten; i++) {
    const f = 20 * Math.pow((0.45 * sr) / 20, i / (punten - 1)), b = Math.round(f / sr * n);
    lijst.push({ f, db: 10 * Math.log10(re[b] * re[b] + im[b] * im[b] + 1e-30) });
  }
  return lijst;
}

// ---- de readout van een ontwerp: wat bovenin de tool staat ----------------------------------------------------------
// Een sinus meet je pas als de keten stil staat op hem: met een echo in de lus komt elke herhaling er later bij, en dat
// inzetten is lek over het hele spectrum (geen aliasing). Daarom eerst inlopen: zo lang als de staart tot ~ −70 dB
// (¾ van pg_staart, dat tot −90 dB rekent), hooguit 8 s. Gaat de keten vanzelf door (zelfoscillatie), dan is een
// sinusmeting zinloos: aliasing en thd zijn dan null, met de reden.
export const INLOOP_MAX = 8;
export function inloopSec(e, rij, sr = 48000, waarden = {}) {
  const h = laadKeten(e, rij, sr);
  try { for (const [i, v] of Object.entries(waarden)) e.pg_zet(h, Number(i), v); return e.pg_staart(h); } finally { e.pg_weg(h); }
}
function sinusNaInloop(k0, amp, inloop, sr) {
  const n0 = Math.round(inloop * sr), x = new Float32Array(n0 + N_AL + 2 * EXTRA), f0 = sr * k0 / N_AL;
  for (let i = 0; i < x.length; i++) x[i] = amp * Math.sin(2 * Math.PI * f0 * (i - n0) / sr);   // fase 0 op het meetvenster
  return { x, n0 };
}
// Een instrument (geen ingang): wat hij zelf maakt in de eerste 2 s — effectieve waarde, piek en staart.
export function instrumentReadout(e, rij, { sr = 48000, waarden = {}, gebeurtenissen = [] } = {}) {
  const nu = () => (typeof performance !== 'undefined' ? performance : Date).now();
  const n = 2 * sr, t0 = nu();
  const uit = renderKeten(e, rij, [new Float32Array(n), new Float32Array(n)], { sr, waarden, gebeurtenissen }).uit;
  const t1 = nu();
  let piek = 0, som = 0, slecht = false;
  for (const k of uit) for (const v of k) { if (!Number.isFinite(v)) slecht = true; piek = Math.max(piek, Math.abs(v)); som += v * v; }
  return { instrument: true, bespeeld: gebeurtenissen.length > 0, rmsDb: 10 * Math.log10(som / (2 * n) + 1e-30), piekDb: 20 * Math.log10(piek + 1e-30),
    staart: inloopSec(e, rij, sr, waarden), aliasingDb: null, thdDb: slecht ? NaN : null,
    reden: 'instrument: geen ingang, dus geen vervorming of aliasing van een testtoon; gemeten is wat hij zelf maakt',
    cpuProcent: ((t1 - t0) / 1000) / 2 * 100 };
}
// gemiddeld vermogensspectrum (Welch: blokken van 4096 met Hann, half overlappend), op log-frequenties, in dB
export function gemiddeldSpectrum(y, sr, { punten = 120 } = {}) {
  const L = 4096, som = new Float64Array(L / 2 + 1), w = Float64Array.from({ length: L }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (L - 1)));
  let blokken = 0;
  for (let p = 0; p + L <= y.length; p += L / 2) {
    const re = new Float64Array(L), im = new Float64Array(L);
    for (let i = 0; i < L; i++) re[i] = y[p + i] * w[i];
    fft(re, im);
    for (let b = 0; b <= L / 2; b++) som[b] += re[b] * re[b] + im[b] * im[b];
    blokken++;
  }
  const lijst = [];
  for (let i = 0; i < punten; i++) {
    const f = 20 * Math.pow((0.45 * sr) / 20, i / (punten - 1)), b = Math.max(1, Math.round(f / sr * L));
    lijst.push({ f, db: 10 * Math.log10(som[b] / Math.max(1, blokken) + 1e-30) });
  }
  return lijst;
}
export function readout(e, rij, { sr = 48000, waarden = {} } = {}) {
  const nu = () => (typeof performance !== 'undefined' ? performance : Date).now();
  const staart = inloopSec(e, rij, sr, waarden);
  if (staart < 0) return { aliasingDb: null, thdDb: null, niveaus: null, staart, cpuProcent: null, reden: 'zelfoscillatie: de keten gaat vanzelf door, een sinusmeting zegt dan niets' };
  const inloop = Math.min(INLOOP_MAX, staart * 0.75);
  const t0 = nu();
  const k5 = 6827, a = sinusNaInloop(k5, 0.5, inloop, sr);
  const ya = renderKeten(e, rij, [a.x], { sr, waarden }).uit[0].subarray(a.n0);
  const k1 = 1365, b = sinusNaInloop(k1, 0.5, inloop, sr);   // ≈ 1000 Hz
  const yb = renderKeten(e, rij, [b.x], { sr, waarden }).uit[0].subarray(b.n0);
  const t1 = nu();
  return {
    aliasingDb: aliasing(midden(ya), k5),
    ...harmonischen(midden(yb), k1),
    staart, inloop,
    cpuProcent: ((t1 - t0) / 1000) / ((a.x.length + b.x.length) / sr) * 100,
  };
}
