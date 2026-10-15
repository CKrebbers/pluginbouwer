// keten.mjs — de ketenmotor (dsp/wasm/motor/graaf.wasm) in Node: laden, renderen, de groove maken. Gedeeld door
// tests/bronnen.test.mjs, tests/native.test.mjs en tools/meet-voorbeelden.mjs.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT } from './pb.mjs';
import { compileer } from '../../tool/src/keten.js';
import { GROOVE } from '../../tool/src/archetypen.js';

let motor = null;
export function laadMotor() {
  if (!motor) motor = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(ROOT, 'dsp', 'wasm', 'motor', 'graaf.wasm'))), {}).exports;
  return motor;
}

// Rendert een rij door een verse instantie. invoer: Float32Array[] per kanaal. waarden: { index: waarde } vooraf.
// gebeurtenissen: [[sample, index, waarde], …] — een parameter zetten op een bepaalde sample (voor poort aan/uit).
export function render(rij, invoer, { sr = 48000, waarden = {}, blok = 128, gebeurtenissen = [] } = {}) {
  const e = laadMotor();
  const h = e.pg_maak(sr);
  if (h < 0) throw new Error('geen vrije instantie');
  try {
    new Float64Array(e.memory.buffer, e.pg_rij(), rij.length).set(rij);
    if (!e.pg_laad(h, rij.length)) throw new Error('pg_laad weigerde de keten');
    for (const [i, v] of Object.entries(waarden)) e.pg_zet(h, Number(i), v);
    const k = invoer.length, n = invoer[0].length, uit = invoer.map(() => new Float32Array(n));
    const blokken = Array.isArray(blok) ? blok : [blok];
    const ev = [...gebeurtenissen].sort((a, b) => a[0] - b[0]);
    let pos = 0, b = 0, j = 0;
    while (pos < n) {
      while (j < ev.length && ev[j][0] <= pos) { e.pg_zet(h, ev[j][1], ev[j][2]); j++; }
      let m = Math.min(blokken[b++ % blokken.length], n - pos);
      if (j < ev.length) m = Math.min(m, ev[j][0] - pos);
      for (let c = 0; c < k; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).set(invoer[c].subarray(pos, pos + m));
      e.pg_verwerk(h, k, m);
      for (let c = 0; c < k; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), pos);
      pos += m;
    }
    return uit;
  } finally { e.pg_weg(h); }
}
export const stilte = (sr, sec, kanalen = 2) => Array.from({ length: kanalen }, () => new Float32Array(Math.round(sr * sec)));

// Wat een ontwerp hoort te krijgen: een effect de groove, een instrument stilte.
const grooveCache = new Map();
export function groove(sr, sec) {
  const sleutel = sr + '/' + sec;
  if (!grooveCache.has(sleutel)) grooveCache.set(sleutel, render(compileer(GROOVE).rij, stilte(sr, sec), { sr }));
  return grooveCache.get(sleutel);
}
export const heeftIngang = (o) => o.knopen.some((k) => k.type === 'in');
export function renderOntwerp(o, { sr = 48000, sec = 6, blok = 128 } = {}) {
  const c = compileer(o);
  if (c.fouten.length) throw new Error(o.naam + ': ' + c.fouten.join('; '));
  const invoer = heeftIngang(o) ? groove(sr, sec) : stilte(sr, sec);
  return render(c.rij, invoer, { sr, blok, waarden: Object.fromEntries(c.params.map((p, i) => [i, p.standaard])) });
}

export function niveau(x, van = 0) {
  let piek = 0, som = 0, n = 0, nietEindig = false;
  for (const k of x) for (let i = van; i < k.length; i++) { const v = k[i]; if (!Number.isFinite(v)) nietEindig = true; else { piek = Math.max(piek, Math.abs(v)); som += v * v; n++; } }
  return { piekDb: 20 * Math.log10(piek || 1e-30), rmsDb: 10 * Math.log10(som / Math.max(1, n) || 1e-30), nietEindig };
}
