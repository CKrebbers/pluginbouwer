// wasm-render.mjs — de referentie voor de null-test bij elke samplerate en kanaalindeling: rendert een WAV door de
// ketenmotor van de tool (wasm/graaf.wasm + wasm/keten.json; dezelfde WASM waar testsignalen/*.verwacht.wav uit komt).
//   node gereedschap/wasm-render.mjs <in.wav> <uit.wav> [--sr 96000] [--kanalen stereo|mono|mono-stereo] [--p index=waarde ...]
// --sr: de samplerate waarmee gerekend wordt (de samples van de invoer blijven dezelfde; de WAV krijgt deze sr).
// --kanalen: stereo = beide kanalen; mono = alleen kanaal 0 (1 kanaal uit); mono-stereo = kanaal 0 naar beide
// DSP-ingangen (2 kanalen uit). --p zet een parameter op INDEX (0..7, zoals gevallen.json "waarden"), vóór de eerste
// sample (de glijders springen dan, net als in de plugin).
import { readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');

export function leesWav(pad) {
  const b = readFileSync(pad);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { tag: b.readUInt16LE(p + 8), k: b.readUInt16LE(p + 10), sr: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22) };
    if (id === 'data') data = { off: p + 8, len };
    p += 8 + len + (len & 1);
  }
  const tag = fmt.tag === 0xfffe ? 3 : fmt.tag;
  if (tag !== 3 || fmt.bits !== 32) throw new Error(`${pad}: alleen 32-bit float WAV`);
  const n = Math.floor(data.len / (4 * fmt.k));
  const k = Array.from({ length: fmt.k }, () => new Float32Array(n));
  for (let i = 0; i < n; i++) for (let c = 0; c < fmt.k; c++) k[c][i] = b.readFloatLE(data.off + (i * fmt.k + c) * 4);
  return { kanalen: k, sr: fmt.sr };
}

export function schrijfWav(pad, kanalen, sr) {
  const k = kanalen.length, n = kanalen[0].length, b = Buffer.alloc(44 + n * k * 4);
  b.write('RIFF', 0, 'ascii'); b.writeUInt32LE(36 + n * k * 4, 4); b.write('WAVEfmt ', 8, 'ascii');
  b.writeUInt32LE(16, 16); b.writeUInt16LE(3, 20); b.writeUInt16LE(k, 22); b.writeUInt32LE(sr, 24);
  b.writeUInt32LE(sr * k * 4, 28); b.writeUInt16LE(k * 4, 32); b.writeUInt16LE(32, 34);
  b.write('data', 36, 'ascii'); b.writeUInt32LE(n * k * 4, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < k; c++) b.writeFloatLE(kanalen[c][i], 44 + (i * k + c) * 4);
  writeFileSync(pad, b);
}

// invoer: Float32Array per kanaal (1 of 2); waarden: {index: waarde}. Geeft Float32Array per DSP-kanaal terug.
export function wasmRender(invoer, sr, waarden = {}) {
  const { exports: e } = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(PAKKET, 'wasm', 'graaf.wasm'))), {});
  const { rij } = JSON.parse(readFileSync(join(PAKKET, 'wasm', 'keten.json'), 'utf8'));
  const h = e.pg_maak(sr);
  if (h < 0) throw new Error('pg_maak mislukt');
  new Float64Array(e.memory.buffer, e.pg_rij(), rij.length).set(rij);
  e.pg_laad(h, rij.length);
  for (const [i, v] of Object.entries(waarden)) e.pg_zet(h, Number(i), v);
  const kan = invoer.length, n = invoer[0].length, uit = invoer.map(() => new Float32Array(n));
  for (let p = 0; p < n; p += 128) {
    const m = Math.min(128, n - p);
    for (let c = 0; c < kan; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).set(invoer[c].subarray(p, p + m));
    e.pg_verwerk(h, kan, m);
    for (let c = 0; c < kan; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), p);
  }
  e.pg_weg(h);
  return uit;
}

// de invoer zoals de DSP hem krijgt bij een kanaalindeling
export function invoerVoor(kanalen, indeling) {
  if (indeling === 'stereo') return kanalen.length >= 2 ? [kanalen[0], kanalen[1]] : [kanalen[0], kanalen[0]];
  if (indeling === 'mono') return [kanalen[0]];
  if (indeling === 'mono-stereo') return [kanalen[0], kanalen[0]];
  throw new Error('onbekende indeling ' + indeling);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const [, , inPad, uitPad, ...rest] = process.argv;
  if (!inPad || !uitPad) { console.error('gebruik: node gereedschap/wasm-render.mjs <in.wav> <uit.wav> [--sr N] [--kanalen stereo|mono|mono-stereo] [--p index=waarde]'); process.exit(2); }
  let sr = 0, indeling = 'stereo';
  const waarden = {};
  for (let i = 0; i < rest.length; i++) {
    if (rest[i] === '--sr') sr = Number(rest[++i]);
    else if (rest[i] === '--kanalen') indeling = rest[++i];
    else if (rest[i] === '--p') { const [k, v] = rest[++i].split('='); waarden[k] = Number(v); }
    else { console.error('onbekende optie ' + rest[i]); process.exit(2); }
  }
  const inv = leesWav(inPad);
  if (!sr) sr = inv.sr;
  schrijfWav(uitPad, wasmRender(invoerVoor(inv.kanalen, indeling), sr, waarden), sr);
}
