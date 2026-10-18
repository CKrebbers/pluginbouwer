// maak-verwacht.mjs — de verwachte uitvoer van de null-test op andere samplerates (en mono), uit de ketenmotor van het
// pakket (wasm/graaf.wasm + wasm/keten.json): dezelfde WASM waar testsignalen/ uit komt. Staat los: alleen Node.
//
//   node gereedschap/maak-verwacht.mjs controle
//        doet testsignalen/ eerst na: de vijf gevallen (gevallen.json), automatie/ en noten/ moeten bit voor bit gelijk
//        zijn aan hun verwacht.wav; anders exit 1 (dan is deze generator fout, niet de plugin)
//   node gereedschap/maak-verwacht.mjs <sr> <map> [--kanalen 1|2]
//        schrijft <map>/<geval>.wav (de gevallen, met hun waarden), <map>/automatie.wav en <map>/noten.wav, gerekend
//        op samplerate <sr> met hetzelfde aantal samples als de invoer in testsignalen/ (pb-render --sr <sr> doet dat
//        ook: de invoer van een instrument is stil, alleen de lengte telt). Noten en events staan op dezelfde sample.
//        Voor `pluginval test` (VST3, AU) ook <map>/<geval>.in.wav (de invoer, met <sr> in de kop; pluginval eist dat
//        de samplerate van de invoer gelijk is aan sample_rate) en <map>/noten.mid: de noten als MIDI-bestand waarin
//        één tick precies één sample is (PPQ en tempo zo gekozen; pluginval rekent tick → seconden → lround(· sr)).
//
// Noten volgen ARCHITECTUUR.md "Noten" (de schil): pb::NootStapel en pb::nootVerhouding uit dsp/pb_noten.h, hier in
// JS nagedaan; exp_ is dsp/pb_math.h regel voor regel (alleen + − × ÷ en floor in double, dus in JS dezelfde bits).
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');
const T = join(PAKKET, 'testsignalen');
const { rij, params } = JSON.parse(readFileSync(join(PAKKET, 'wasm', 'keten.json'), 'utf8'));
const MODULE = new WebAssembly.Module(readFileSync(join(PAKKET, 'wasm', 'graaf.wasm')));
const NOOT = 0, POORT = 1;  // PB_NOOT_PARAM, PB_POORT_PARAM (indexen in PARAMS)

// --- dsp/pb_math.h exp_ en dsp/pb_noten.h ----------------------------------------------------------------------------
const LN2 = 0.69314718055994530942;
function exp_(x) {
  if (x !== x) return x;
  if (x > 709.78) return Infinity;
  if (x < -745.13) return 0.0;
  const ln2hi = 6.93147180369123816490e-01, ln2lo = 1.90821492927058770002e-10, invln2 = 1.44269504088896338700e+00;
  const P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
    P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08;
  const k = Math.floor(x * invln2 + 0.5);
  const hi = x - k * ln2hi, lo = k * ln2lo, r = hi - lo;
  const t = r * r;
  const c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  const y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
  if (k < -1021 || k > 1023) throw new Error('exp_: buiten het bereik van deze generator');
  return y * Math.pow(2, k);  // 2^k is exact (normaal bereik), zoals vanBits((k + 1023) << 52)
}
const nootVerhouding = (n) => exp_((n - 57) / 12.0 * LN2);
class NootStapel {
  constructor() { this.n = []; }
  aan(noot) { this.uit(noot); if (this.n.length === 16) this.n.shift(); this.n.push(noot); }
  uit(noot) { this.n = this.n.filter((x) => x !== noot); }
  leeg() { this.n = []; }
  boven() { return this.n.length ? this.n[this.n.length - 1] : -1; }
}

// --- WAV ---------------------------------------------------------------------------------------------------------------
function leesWav(pad) {
  const b = readFileSync(pad);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { k: b.readUInt16LE(p + 10), sr: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22) };
    if (id === 'data') data = { off: p + 8, len };
    p += 8 + len + (len & 1);
  }
  if (fmt.bits !== 32) throw new Error(`${pad}: alleen 32-bit float`);
  const n = Math.floor(data.len / (4 * fmt.k)), k = Array.from({ length: fmt.k }, () => new Float32Array(n));
  for (let i = 0; i < n; i++) for (let c = 0; c < fmt.k; c++) k[c][i] = b.readFloatLE(data.off + (i * fmt.k + c) * 4);
  return { kanalen: k, sr: fmt.sr, n };
}
function schrijfWav(pad, kanalen, sr) {
  const k = kanalen.length, n = kanalen[0].length, b = Buffer.alloc(44 + n * k * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * k * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(3, 20); b.writeUInt16LE(k, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * k * 4, 28);
  b.writeUInt16LE(k * 4, 32); b.writeUInt16LE(32, 34); b.write('data', 36); b.writeUInt32LE(n * k * 4, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < k; c++) b.writeFloatLE(kanalen[c][i], 44 + (i * k + c) * 4);
  writeFileSync(pad, b);
}

// noten als standaard-MIDI-bestand (formaat 0): 0x90 noot 100 / 0x80 noot 0, tick = sample
function schrijfMidi(pad, noten, sr) {
  let ppq = 0;
  for (let q = 32767; q >= 1 && !ppq; q--) if ((q * 1e6) % sr === 0 && (q * 1e6) / sr <= 0xffffff) ppq = q;
  if (!ppq) throw new Error(`geen PPQ waarbij een tick precies een sample is op ${sr} Hz`);
  const tempo = (ppq * 1e6) / sr, spoor = [];
  const vlq = (n) => { const b = [n & 0x7f]; while ((n >>= 7)) b.unshift((n & 0x7f) | 0x80); return b; };
  spoor.push(0, 0xff, 0x51, 3, (tempo >> 16) & 0xff, (tempo >> 8) & 0xff, tempo & 0xff);
  let t = 0;
  for (const x of noten) {
    if (x.noot < 0 || x.noot > 127) throw new Error('noot buiten 0..127 past niet in MIDI');
    spoor.push(...vlq(x.sample - t), x.aan ? 0x90 : 0x80, x.noot, x.aan ? 100 : 0);
    t = x.sample;
  }
  spoor.push(0, 0xff, 0x2f, 0);
  const kop = Buffer.from([0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, (ppq >> 8) & 0xff, ppq & 0xff]);
  const sk = Buffer.alloc(8); sk.write('MTrk', 0); sk.writeUInt32BE(spoor.length, 4);
  writeFileSync(pad, Buffer.concat([kop, sk, Buffer.from(spoor)]));
}

// --- de render op de ketenmotor -------------------------------------------------------------------------------------
// events: [{ sample, index, waarde }] (een parameter) of [{ sample, noot, aan }] (een noot), gesorteerd (stabiel).
// Het blok wordt bij elk event gesplitst, zoals de schil doet; de motor rekent per sample.
function render(sr, n, kanalen, begin, events) {
  const { exports: e } = new WebAssembly.Instance(MODULE, {});
  const h = e.pg_maak(sr);
  if (h < 0) throw new Error('pg_maak mislukt');
  new Float64Array(e.memory.buffer, e.pg_rij(), rij.length).set(rij);
  e.pg_laad(h, rij.length);
  for (const [i, v] of Object.entries(begin)) e.pg_zet(h, Number(i), v);
  const stapel = new NootStapel();
  const uit = Array.from({ length: kanalen }, () => new Float32Array(n));
  let pos = 0, k = 0;
  const reken = (tot) => {
    while (pos < tot) {
      const m = Math.min(4096, tot - pos);
      for (let c = 0; c < kanalen; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).fill(0);
      e.pg_verwerk(h, kanalen, m);
      for (let c = 0; c < kanalen; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), pos);
      pos += m;
    }
  };
  for (; k < events.length; k++) {
    const ev = events[k];
    reken(Math.min(ev.sample, n));
    if (ev.index !== undefined) {
      const p = params[ev.index];
      e.pg_zet(h, ev.index, Math.min(p.max, Math.max(p.min, ev.waarde)));
    } else {
      if (ev.noot < 0 || ev.noot > 127) continue;
      if (ev.aan) stapel.aan(ev.noot); else stapel.uit(ev.noot);
      const b = stapel.boven();
      if (b >= 0) { e.pg_zet(h, NOOT, nootVerhouding(b)); e.pg_zet(h, POORT, 1.0); } else e.pg_zet(h, POORT, 0.0);
    }
  }
  reken(n);
  return uit;
}

const sorteer = (ev) => ev.map((x, i) => [x, i]).sort((a, b) => a[0].sample - b[0].sample || a[1] - b[1]).map((x) => x[0]);
function leesEvents(pad) {  // automatie: "sample id waarde" (id = nummer in SPEC.md = index + 1)
  return readFileSync(pad, 'utf8').split('\n').filter((r) => r.trim()).map((r) => {
    const [s, id, v] = r.trim().split(/\s+/).map(Number);
    return { sample: s, index: id - 1, waarde: v };
  });
}
function leesNoten(pad) {  // noten: "sample aan|uit noot"
  return readFileSync(pad, 'utf8').split('\n').filter((r) => r.trim()).map((r) => {
    const [s, wat, noot] = r.trim().split(/\s+/);
    return { sample: Number(s), noot: Number(noot), aan: wat === 'aan' };
  });
}
const gevallen = JSON.parse(readFileSync(join(T, 'gevallen.json'), 'utf8'));
const beginVan = (g) => Object.fromEntries(Object.entries(g.waarden).map(([i, v]) => [Number(i), v]));

// alle renders: [naam, invoer.wav, verwacht.wav (in testsignalen), (sr, kanalen) → uitvoer]
const RENDERS = [
  ...gevallen.map((g) => [g.naam, join(T, `${g.naam}.in.wav`), join(T, `${g.naam}.verwacht.wav`),
    (sr, n, k) => render(sr, n, k, beginVan(g), [])]),
  ['automatie', join(T, 'automatie', 'in.wav'), join(T, 'automatie', 'verwacht.wav'),
    (sr, n, k) => render(sr, n, k, {}, sorteer(leesEvents(join(T, 'automatie', 'events.txt'))))],
  ['noten', join(T, 'noten', 'in.wav'), join(T, 'noten', 'verwacht.wav'),
    (sr, n, k) => render(sr, n, k, {}, sorteer(leesNoten(join(T, 'noten', 'noten.txt'))))],
];

function bitGelijk(a, b) {
  if (a.length !== b.length) return false;
  for (let c = 0; c < a.length; c++) {
    if (a[c].length !== b[c].length) return false;
    const x = new Uint32Array(a[c].buffer, a[c].byteOffset, a[c].length), y = new Uint32Array(b[c].buffer, b[c].byteOffset, b[c].length);
    for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return false;
  }
  return true;
}

const [opdracht, map] = process.argv.slice(2);
if (opdracht === 'controle') {
  let fout = 0;
  console.log('| testsignaal | sr | WASM-generator = testsignalen/ |\n|---|---|---|');
  for (const [naam, inPad, verwachtPad, maak] of RENDERS) {
    const inv = leesWav(inPad), verwacht = leesWav(verwachtPad);
    const ok = bitGelijk(maak(inv.sr, inv.n, verwacht.kanalen.length), verwacht.kanalen);
    if (!ok) fout++;
    console.log(`| ${naam} | ${inv.sr} | ${ok ? 'bit voor bit gelijk' : '**ANDERS**'} |`);
  }
  if (fout) console.error(`maak-verwacht: ${fout} testsignalen niet nagedaan; de generator is fout`);
  process.exit(fout ? 1 : 0);
} else if (Number(opdracht) > 0 && map) {
  const sr = Number(opdracht), i = process.argv.indexOf('--kanalen'), kanalen = i > 0 ? Number(process.argv[i + 1]) : 2;
  if (kanalen !== 1 && kanalen !== 2) { console.error('--kanalen 1 of 2'); process.exit(2); }
  mkdirSync(map, { recursive: true });
  for (const [naam, inPad, , maak] of RENDERS) {
    const inv = leesWav(inPad);
    schrijfWav(join(map, `${naam}.wav`), maak(sr, inv.n, kanalen), sr);
    schrijfWav(join(map, `${naam}.in.wav`), inv.kanalen, sr);
  }
  schrijfMidi(join(map, 'noten.mid'), sorteer(leesNoten(join(T, 'noten', 'noten.txt'))), sr);
  console.log(`maak-verwacht: ${RENDERS.length} renders op ${sr} Hz, ${kanalen} kanaal/kanalen in ${map}`);
} else {
  console.error('gebruik: node gereedschap/maak-verwacht.mjs controle | <sr> <map> [--kanalen 1|2]');
  process.exit(2);
}
