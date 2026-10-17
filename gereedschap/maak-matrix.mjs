// maak-matrix.mjs — invoer en verwachte uitvoer voor de null-test op elke samplerate en kanaalindeling (fase 5).
// De verwachte uitvoer komt, net als die in testsignalen/, uit de ketenmotor van de tool (wasm/graaf.wasm +
// keten.json): dezelfde samples als testsignalen/<geval>.in.wav, alleen de samplerate in de kop is anders.
// Op de samplerate van testsignalen/ is de uitkomst bit voor bit gelijk aan <geval>.verwacht.wav (dat wordt hier
// getoetst: zo is deze generator zelf bewezen tegen de uitvoer van de tool). testsignalen/ verandert niet.
//   node gereedschap/maak-matrix.mjs <uitmap> <sr,sr,...>
// Schrijft <uitmap>/<sr>/<indeling>/<geval>.in.wav en .verwacht.wav, indeling = stereo | mono | mono-stereo:
//   stereo       de invoer zoals hij is
//   mono         kanaal 0 van de invoer, de motor rekent één kanaal
//   mono-stereo  kanaal 0 van de invoer op beide ingangen van de motor, twee kanalen uit
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');
const T = join(PAKKET, 'testsignalen');

export function leesWav(pad) {
  const b = readFileSync(pad);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { tag: b.readUInt16LE(p + 8), k: b.readUInt16LE(p + 10), sr: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22) };
    if (id === 'data') data = { off: p + 8, len };
    p += 8 + len + (len & 1);
  }
  if (!(fmt.tag === 3 || fmt.tag === 0xfffe) || fmt.bits !== 32) throw new Error(`${pad}: alleen 32-bit float`);
  const n = Math.floor(data.len / (4 * fmt.k));
  const k = Array.from({ length: fmt.k }, () => new Float32Array(n));
  for (let i = 0; i < n; i++) for (let c = 0; c < fmt.k; c++) k[c][i] = b.readFloatLE(data.off + (i * fmt.k + c) * 4);
  return { kanalen: k, sr: fmt.sr };
}

export function schrijfWav(pad, kanalen, sr) {
  const k = kanalen.length, n = kanalen[0].length, b = Buffer.alloc(44 + n * k * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * k * 4, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(3, 20); b.writeUInt16LE(k, 22); b.writeUInt32LE(sr, 24);
  b.writeUInt32LE(sr * k * 4, 28); b.writeUInt16LE(k * 4, 32); b.writeUInt16LE(32, 34);
  b.write('data', 36); b.writeUInt32LE(n * k * 4, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < k; c++) b.writeFloatLE(kanalen[c][i], 44 + (i * k + c) * 4);
  writeFileSync(pad, b);
}

// de ketenmotor van de tool; waarden = { index in PARAMS: waarde in eenheden } (gevallen.json "waarden")
export function wasmRender(invoer, sr, waarden) {
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
  return uit;
}

const gelijk = (a, b) => a.length === b.length && a.every((k, c) => k.length === b[c].length && k.every((x, i) => Object.is(x, b[c][i])));

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const [uitmap, lijst] = process.argv.slice(2);
  if (!uitmap || !lijst) { console.error('gebruik: node gereedschap/maak-matrix.mjs <uitmap> <sr,sr,...>'); process.exit(2); }
  const srs = lijst.split(',').map(Number);
  const gevallen = JSON.parse(readFileSync(join(T, 'gevallen.json'), 'utf8'));
  let bewezen = 0;
  for (const g of gevallen) {
    const inv = leesWav(join(T, `${g.naam}.in.wav`));
    // eerst de generator zelf: op de eigen samplerate precies de verwachte uitvoer van de tool
    if (!gelijk(wasmRender(inv.kanalen, inv.sr, g.waarden), leesWav(join(T, `${g.naam}.verwacht.wav`)).kanalen)) {
      console.error(`FOUT: ${g.naam}: de ketenmotor geeft op ${inv.sr} Hz niet testsignalen/${g.naam}.verwacht.wav`);
      process.exit(1);
    }
    bewezen++;
    const links = inv.kanalen[0];
    for (const sr of srs) {
      for (const [indeling, invoer, motor] of [
        ['stereo', inv.kanalen, inv.kanalen],
        ['mono', [links], [links]],
        ['mono-stereo', [links], [links, links]],
      ]) {
        const map = join(uitmap, String(sr), indeling);
        mkdirSync(map, { recursive: true });
        schrijfWav(join(map, `${g.naam}.in.wav`), invoer, sr);
        schrijfWav(join(map, `${g.naam}.verwacht.wav`), wasmRender(motor, sr, g.waarden), sr);
      }
    }
  }
  console.log(`maak-matrix: ${gevallen.length} gevallen × ${srs.length} samplerates (${srs.join(', ')}) × 3 indelingen in ${uitmap}; ` +
              `de ketenmotor geeft op de eigen samplerate bit voor bit testsignalen/*.verwacht.wav (${bewezen}/${gevallen.length})`);
}
