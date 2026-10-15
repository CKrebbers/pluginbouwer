// pb.mjs — laadt een ontwerp-WASM en rendert ermee; WAV lezen en schrijven (32-bit float). Gedeeld door alle toetsen.
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');

export function laadOntwerp(naam = 'pijplijn_biquad') {
  const bytes = readFileSync(join(ROOT, 'dsp', 'wasm', `${naam}.wasm`));
  const { exports: e } = new WebAssembly.Instance(new WebAssembly.Module(bytes), {});
  const tekst = (p) => { const m = new Uint8Array(e.memory.buffer); let s = ''; while (m[p]) s += String.fromCharCode(m[p++]); return s; };
  const params = [];
  for (let i = 0; i < e.pb_aantal_params(); i++)
    params.push({ index: i, id: e.pb_param_id(i), naam: tekst(e.pb_param_naam(i)), eenheid: tekst(e.pb_param_eenheid(i)),
      min: e.pb_param_min(i), max: e.pb_param_max(i), standaard: e.pb_param_standaard(i) });
  return { e, params, tekst };
}

// Rendert in (Float32Array[] per kanaal) door een verse instantie. waarden: { index: waarde } in eigen eenheid.
// blok: vaste blokgrootte, of een array die rondloopt (voor de wisselende-blokgrootte-toets).
export function render(ontwerp, invoer, { sr = 48000, waarden = {}, blok = 128 } = {}) {
  const { e } = ontwerp;
  const h = e.pb_maak(sr);
  if (h < 0) throw new Error('geen vrije instantie');
  for (const [i, v] of Object.entries(waarden)) e.pb_zet(h, Number(i), v);
  const kanalen = invoer.length, n = invoer[0].length;
  const uit = invoer.map(() => new Float32Array(n));
  const blokken = Array.isArray(blok) ? blok : [blok];
  let pos = 0, b = 0;
  while (pos < n) {
    const m = Math.min(blokken[b++ % blokken.length], n - pos);
    for (let c = 0; c < kanalen; c++) new Float32Array(e.memory.buffer, e.pb_in(h, c), m).set(invoer[c].subarray(pos, pos + m));
    e.pb_verwerk(h, kanalen, m);
    for (let c = 0; c < kanalen; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pb_uit(h, c), m), pos);
    pos += m;
  }
  e.pb_weg(h);
  return uit;
}

// genormaliseerd (0..1, lineair zoals CLAP → VST3/AU het doorgeeft) naar de eigen eenheid
export function uitNorm(p, n) { return p.min + n * (p.max - p.min); }

export function schrijfWav(pad, kanalen, sr) {
  const n = kanalen[0].length, k = kanalen.length, data = n * k * 4;
  const b = Buffer.alloc(44 + data);
  b.write('RIFF', 0); b.writeUInt32LE(36 + data, 4); b.write('WAVE', 8); b.write('fmt ', 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(3, 20); b.writeUInt16LE(k, 22); b.writeUInt32LE(sr, 24);
  b.writeUInt32LE(sr * k * 4, 28); b.writeUInt16LE(k * 4, 32); b.writeUInt16LE(32, 34);
  b.write('data', 36); b.writeUInt32LE(data, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < k; c++) b.writeFloatLE(kanalen[c][i], 44 + (i * k + c) * 4);
  writeFileSync(pad, b);
}

export function leesWav(pad) {
  const b = readFileSync(pad);
  if (b.toString('ascii', 0, 4) !== 'RIFF' || b.toString('ascii', 8, 12) !== 'WAVE') throw new Error(`${pad}: geen WAV`);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { tag: b.readUInt16LE(p + 8), k: b.readUInt16LE(p + 10), sr: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22) };
    if (id === 'data') data = { off: p + 8, len };
    p += 8 + len + (len & 1);
  }
  if (!fmt || !data) throw new Error(`${pad}: fmt of data ontbreekt`);
  let tag = fmt.tag;
  if (tag === 0xfffe) tag = fmt.bits === 32 ? 3 : 1;   // WAVE_FORMAT_EXTENSIBLE (JUCE schrijft dit soms)
  const bps = fmt.bits / 8, n = Math.floor(data.len / (bps * fmt.k));
  const kanalen = Array.from({ length: fmt.k }, () => new Float32Array(n));
  for (let i = 0; i < n; i++) for (let c = 0; c < fmt.k; c++) {
    const o = data.off + (i * fmt.k + c) * bps;
    kanalen[c][i] = tag === 3 ? b.readFloatLE(o) : (bps === 2 ? b.readInt16LE(o) / 32768 : b.readInt32LE(o) / 2147483648);
  }
  return { kanalen, sr: fmt.sr };
}

// null-test: piek en RMS van het verschil in dBFS (−Infinity = bit voor bit gelijk)
export function nullTest(a, b, { vanaf = 0 } = {}) {
  let piek = 0, som = 0, n = 0, eersteVerschil = -1;
  const k = Math.min(a.length, b.length);
  for (let c = 0; c < k; c++) {
    const m = Math.min(a[c].length, b[c].length);
    for (let i = vanaf; i < m; i++) {
      const d = a[c][i] - b[c][i];
      if (!Number.isFinite(d)) return { piekDb: Infinity, rmsDb: Infinity, eersteVerschil: i, nietEindig: true };
      if (d !== 0 && eersteVerschil < 0) eersteVerschil = i;
      piek = Math.max(piek, Math.abs(d)); som += d * d; n++;
    }
  }
  const db = (x) => (x === 0 ? -Infinity : 20 * Math.log10(x));
  return { piekDb: db(piek), rmsDb: db(Math.sqrt(som / Math.max(1, n))), eersteVerschil, monsters: n };
}

export function fmtDb(x) { return x === -Infinity ? '−∞ (bit voor bit gelijk)' : `${x.toFixed(1)} dBFS`; }
