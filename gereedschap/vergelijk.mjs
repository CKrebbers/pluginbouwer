// vergelijk.mjs — de null-test van het bouwpakket: verwachte uitvoer (uit de WASM van de Pluginbouwer) tegen een
// render door het gebouwde binary. Staat los: alleen Node, geen andere bestanden dan testsignalen/ en wasm/.
//   node gereedschap/vergelijk.mjs <map met renders>      exit 1 als iets boven de grens komt
//   node gereedschap/vergelijk.mjs --automatie <render.wav>  de render van pb-render --automatie (events midden in blokken)
//   node gereedschap/vergelijk.mjs --noten <render.wav>      instrument: pb-render --noten (CLAP-noten of --midi)
//   node gereedschap/vergelijk.mjs --hosttempo <render.wav>  instrument met sequencer: pb-render --tempo
//   node gereedschap/vergelijk.mjs --paar <verwacht.wav> <render.wav> [label]
//        twee bestanden (de verwachting van gereedschap/maak-verwacht.mjs op een andere samplerate of mono); ook het
//        aantal kanalen, de samplerate en de lengte moeten gelijk zijn
// Grens: piek ≤ −90 dBFS. Gevallen met "statistisch" (zelfoscillatie): terts-spectrum en omhullende binnen 1 dB.
import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');
const GRENS_DB = -90;

function leesWav(pad) {
  const b = readFileSync(pad);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { tag: b.readUInt16LE(p + 8), k: b.readUInt16LE(p + 10), sr: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22) };
    if (id === 'data') data = { off: p + 8, len };
    p += 8 + len + (len & 1);
  }
  const tag = fmt.tag === 0xfffe ? (fmt.bits === 32 ? 3 : 1) : fmt.tag, bps = fmt.bits / 8, n = Math.floor(data.len / (bps * fmt.k));
  const k = Array.from({ length: fmt.k }, () => new Float32Array(n));
  for (let i = 0; i < n; i++) for (let c = 0; c < fmt.k; c++) {
    const o = data.off + (i * fmt.k + c) * bps;
    k[c][i] = tag === 3 ? b.readFloatLE(o) : (bps === 2 ? b.readInt16LE(o) / 32768 : b.readInt32LE(o) / 2147483648);
  }
  return { kanalen: k, sr: fmt.sr };
}
function nullTest(a, b) {
  let piek = 0, som = 0, n = 0, eerste = -1;
  for (let c = 0; c < Math.min(a.length, b.length); c++) for (let i = 0; i < Math.min(a[c].length, b[c].length); i++) {
    const d = a[c][i] - b[c][i];
    if (!Number.isFinite(d)) return { piekDb: Infinity, rmsDb: Infinity, eerste: i };
    if (d !== 0 && eerste < 0) eerste = i;
    piek = Math.max(piek, Math.abs(d)); som += d * d; n++;
  }
  const db = (x) => (x === 0 ? -Infinity : 20 * Math.log10(x));
  return { piekDb: db(piek), rmsDb: db(Math.sqrt(som / Math.max(1, n))), eerste };
}
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= n; len <<= 1) {
    const h = -2 * Math.PI / len, wr = Math.cos(h), wi = Math.sin(h);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2, tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}
function spectrum(x) {
  const n = x.length, re = Float64Array.from(x), im = new Float64Array(n);
  fft(re, im);
  const p = new Float64Array(n / 2 + 1);
  for (let k = 0; k <= n / 2; k++) p[k] = re[k] ** 2 + im[k] ** 2;
  return p;
}
function statistisch(a, b, sr) {
  let band = 0, omh = 0;
  for (let c = 0; c < Math.min(a.length, b.length); c++) {
    let n = 1; while (n < a[c].length) n <<= 1; n >>= 1;
    const pa = spectrum(a[c].subarray(0, n)), pb = spectrum(b[c].subarray(0, n)), tot = pa.reduce((s, v) => s + v, 0);
    for (let f = 20; f < 20000; f *= Math.pow(2, 1 / 3)) {
      const lo = Math.floor(f / sr * n), hi = Math.max(lo + 1, Math.floor(f * Math.pow(2, 1 / 3) / sr * n));
      let ea = 0, eb = 0; for (let k = lo; k < hi; k++) { ea += pa[k]; eb += pb[k]; }
      if (ea >= tot * 1e-8) band = Math.max(band, Math.abs(10 * Math.log10(ea / eb)));
    }
    const v = Math.round(sr / 10);
    for (let i = 0; i + v <= a[c].length; i += v) {
      let ea = 0, eb = 0; for (let k = i; k < i + v; k++) { ea += a[c][k] ** 2; eb += b[c][k] ** 2; }
      if (ea / v >= 1e-6) omh = Math.max(omh, Math.abs(10 * Math.log10(ea / eb)));
    }
  }
  return { band, omh };
}
// diagnose: klinkt de render als de STANDAARDwaarden? Op de ketenmotor uit het pakket (wasm/graaf.wasm + keten.json).
function standaardRender(invoer, sr) {
  const { exports: e } = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(PAKKET, 'wasm', 'graaf.wasm'))), {});
  const { rij } = JSON.parse(readFileSync(join(PAKKET, 'wasm', 'keten.json'), 'utf8'));
  const h = e.pg_maak(sr);
  new Float64Array(e.memory.buffer, e.pg_rij(), rij.length).set(rij);
  e.pg_laad(h, rij.length);
  const n = invoer[0].length, uit = invoer.map(() => new Float32Array(n));
  for (let p = 0; p < n; p += 128) {
    const m = Math.min(128, n - p);
    for (let c = 0; c < invoer.length; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).set(invoer[c].subarray(p, p + m));
    e.pg_verwerk(h, invoer.length, m);
    for (let c = 0; c < invoer.length; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), p);
  }
  return uit;
}

const LOS = {
  automatie: ['automatie (events midden in blokken)', 'splitst de plugin het blok bij elk event (header.time)?'],
  noten: ['noten (op hun sample)', 'doet de schil bij elke noot precies wat ARCHITECTUUR.md "Noten" zegt (Noot en Poort op de sample van de noot)?'],
  hosttempo: ['hosttempo (transport van de host)', 'krijgt het Ontwerp per blok zetTransport met het tempo uit clap_event_transport?'],
};
if (process.argv[2]?.startsWith('--') && LOS[process.argv[2].slice(2)]) {
  // node gereedschap/vergelijk.mjs --<geval> <render.wav> [label]: één render tegen testsignalen/<geval>/verwacht.wav
  const geval = process.argv[2].slice(2), [label, hint] = LOS[geval];
  const r = nullTest(leesWav(join(PAKKET, 'testsignalen', geval, 'verwacht.wav')).kanalen, leesWav(process.argv[3]).kanalen);
  const ok = r.piekDb <= GRENS_DB, f = (x) => (x === -Infinity ? '−∞ (bit voor bit gelijk)' : `${x.toFixed(1)} dBFS`);
  console.log(`| ${process.argv[4] || label} | ${f(r.piekDb)} | ${f(r.rmsDb)} | ${r.piekDb === -Infinity ? 'ja' : 'nee'} |${ok ? '' : ' **BOVEN DE GRENS** — ' + hint}`);
  process.exit(ok ? 0 : 1);
}
if (process.argv[2] === '--paar') {
  const [verwachtPad, renderPad, label] = process.argv.slice(3);
  const v = leesWav(verwachtPad), g = leesWav(renderPad), f = (x) => (x === -Infinity ? '−∞ (bit voor bit gelijk)' : `${x.toFixed(1)} dBFS`);
  const vorm = v.kanalen.length === g.kanalen.length && v.sr === g.sr && v.kanalen[0].length === g.kanalen[0].length;
  const r = nullTest(v.kanalen, g.kanalen), ok = vorm && r.piekDb <= GRENS_DB;
  const anders = vorm ? '' : ` **VORM ANDERS**: verwacht ${v.kanalen.length} kanalen, ${v.sr} Hz, ${v.kanalen[0].length} samples; kreeg ${g.kanalen.length}, ${g.sr} Hz, ${g.kanalen[0].length}`;
  console.log(`| ${label || renderPad} | ${f(r.piekDb)} | ${f(r.rmsDb)} | ${vorm && r.piekDb === -Infinity ? 'ja' : 'nee'} |${anders}${ok || !vorm ? '' : ' **BOVEN DE GRENS**'}`);
  process.exit(ok ? 0 : 1);
}
const map = process.argv[2];
if (!map) { console.error('gebruik: node gereedschap/vergelijk.mjs <map met renders> | --automatie|--noten|--hosttempo <render.wav>'); process.exit(2); }
const gevallen = JSON.parse(readFileSync(join(PAKKET, 'testsignalen', 'gevallen.json'), 'utf8'));
let mis = 0;
console.log('| geval | piek | RMS | bit voor bit |\n|---|---|---|---|');
for (const g of gevallen) {
  const pad = join(map, `${g.naam}.wav`);
  if (!existsSync(pad)) { console.log(`| ${g.naam} | ontbreekt | | |`); mis++; continue; }
  const verwacht = leesWav(join(PAKKET, 'testsignalen', `${g.naam}.verwacht.wav`)).kanalen, gekregen = leesWav(pad).kanalen;
  const r = nullTest(verwacht, gekregen);
  let ok = r.piekDb <= GRENS_DB, extra = '';
  if (g.statistisch) {
    const s = statistisch(verwacht, gekregen, g.sr);
    ok = s.band <= 1 && s.omh <= 1;
    extra = ` statistisch: terts ${s.band.toFixed(3)} dB, omhullende ${s.omh.toFixed(3)} dB`;
  }
  if (!ok && !g.statistisch) {
    const inv = leesWav(join(PAKKET, 'testsignalen', `${g.naam}.in.wav`));
    if (nullTest(standaardRender(inv.kanalen, inv.sr), gekregen).piekDb <= GRENS_DB)
      extra += ' — gelijk aan de STANDAARDwaarden: de parameters kwamen niet aan (CLAUDE.md valkuil 1)';
  }
  if (!ok) mis++;
  const f = (x) => (x === -Infinity ? '−∞ (bit voor bit gelijk)' : `${x.toFixed(1)} dBFS`);
  console.log(`| ${g.naam} | ${f(r.piekDb)} | ${f(r.rmsDb)} | ${r.piekDb === -Infinity ? 'ja' : 'nee'} |${extra}${ok ? '' : ' **BOVEN DE GRENS**'}`);
}
process.exit(mis ? 1 : 0);
