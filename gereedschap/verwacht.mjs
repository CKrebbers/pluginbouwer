// verwacht.mjs — de verwachte uitvoer voor de null-test op een andere samplerate of kanaalindeling (fase 5).
// testsignalen/*.verwacht.wav komt uit de WASM van de tool op de samplerate van het geval. Voor de matrix
// (44,1–192 kHz, mono en stereo) rekent dit script hetzelfde uit met diezelfde WASM (wasm/graaf.wasm + keten.json),
// zodat de toets nog steeds "binary tegen de tool" is. Eerst controleert het dat de WASM hier testsignalen/*.verwacht.wav
// bit voor bit nadoet; anders stopt het (dan is de referentie niet te vertrouwen).
//   node gereedschap/verwacht.mjs <map> --sr <Hz> --kanalen 2|1|1-2
// Schrijft <map>/verwacht/<geval>.wav (de referentie) en <map>/in/<geval>.wav (de invoer, met <Hz> in de kop en de
// kanalen van de ingang: voor pluginval test, dat de samplerate van de invoer eist). Kanalen: 2 = stereo, 1 = mono
// (kanaal 0 van de invoer), 1-2 = mono in, stereo uit (de mono-uitvoer op beide kanalen).
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
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
  if (!(fmt.tag === 3 || fmt.tag === 0xfffe) || fmt.bits !== 32) throw new Error(`alleen 32-bit float WAV: ${pad}`);
  const n = Math.floor(data.len / (4 * fmt.k));
  const k = Array.from({ length: fmt.k }, () => new Float32Array(n));
  for (let i = 0; i < n; i++) for (let c = 0; c < fmt.k; c++) k[c][i] = b.readFloatLE(data.off + (i * fmt.k + c) * 4);
  return { kanalen: k, sr: fmt.sr };
}
export function schrijfWav(pad, kanalen, sr) {
  const k = kanalen.length, n = kanalen[0].length, b = Buffer.alloc(44 + n * k * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * k * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(3, 20); b.writeUInt16LE(k, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * k * 4, 28);
  b.writeUInt16LE(k * 4, 32); b.writeUInt16LE(32, 34); b.write('data', 36); b.writeUInt32LE(n * k * 4, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < k; c++) b.writeFloatLE(kanalen[c][i], 44 + (i * k + c) * 4);
  writeFileSync(pad, b);
}
// de ketenmotor van de tool: invoer (kanalen) door de keten op samplerate sr, met waarden per INDEX in PARAMS
export function renderWasm(invoer, sr, waarden) {
  const { exports: e } = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(PAKKET, 'wasm', 'graaf.wasm'))), {});
  const { rij } = JSON.parse(readFileSync(join(PAKKET, 'wasm', 'keten.json'), 'utf8'));
  const h = e.pg_maak(sr);
  if (h < 0) throw new Error(`pg_maak(${sr}) mislukt`);
  new Float64Array(e.memory.buffer, e.pg_rij(), rij.length).set(rij);
  e.pg_laad(h, rij.length);
  for (const [i, v] of Object.entries(waarden)) e.pg_zet(h, Number(i), v);
  const n = invoer[0].length, uit = invoer.map(() => new Float32Array(n));
  for (let p = 0; p < n; p += 128) {
    const m = Math.min(128, n - p);
    for (let c = 0; c < invoer.length; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).set(invoer[c].subarray(p, p + m));
    e.pg_verwerk(h, invoer.length, m);
    for (let c = 0; c < invoer.length; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), p);
  }
  e.pg_weg(h);
  return uit;
}
const gelijk = (a, b) => a.length === b.length && a.every((x, c) => x.length === b[c].length && x.every((v, i) => Object.is(v, b[c][i])));

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const map = args[0], optie = (naam) => { const i = args.indexOf(naam); return i > 0 ? args[i + 1] : undefined; };
  const sr = Number(optie('--sr')), kanalen = optie('--kanalen') ?? '2';
  if (!map || !(sr > 0) || !['2', '1', '1-2'].includes(kanalen)) {
    console.error('gebruik: node gereedschap/verwacht.mjs <map> --sr <Hz> --kanalen 2|1|1-2'); process.exit(2);
  }
  const gevallen = JSON.parse(readFileSync(join(PAKKET, 'testsignalen', 'gevallen.json'), 'utf8'));
  mkdirSync(join(map, 'verwacht'), { recursive: true }); mkdirSync(join(map, 'in'), { recursive: true });
  for (const g of gevallen) {
    const inv = leesWav(join(PAKKET, 'testsignalen', `${g.naam}.in.wav`));
    // de referentie van de tool zelf: rekent de WASM hier hetzelfde?
    if (!gelijk(renderWasm(inv.kanalen, g.sr, g.waarden), leesWav(join(PAKKET, 'testsignalen', `${g.naam}.verwacht.wav`)).kanalen)) {
      console.error(`FOUT: wasm/graaf.wasm doet testsignalen/${g.naam}.verwacht.wav niet bit voor bit na`); process.exit(1);
    }
    const ingang = kanalen === '2' ? inv.kanalen : [inv.kanalen[0]];
    const uit = renderWasm(ingang, sr, g.waarden);
    schrijfWav(join(map, 'verwacht', `${g.naam}.wav`), kanalen === '1-2' ? [uit[0], uit[0]] : uit, sr);
    schrijfWav(join(map, 'in', `${g.naam}.wav`), ingang, sr);
  }
  console.log(`verwacht: ${gevallen.length} gevallen op ${sr} Hz, kanalen ${kanalen} (uit wasm/graaf.wasm) in ${map}`);
}
