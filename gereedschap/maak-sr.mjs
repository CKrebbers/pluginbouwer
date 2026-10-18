// maak-sr.mjs — de verwachte uitvoer bij andere samplerates en in mono (PROMPTS.md fase 5), met de ketenmotor uit het
// pakket (wasm/graaf.wasm + keten.json): dezelfde DSP als de tool. Staat los: alleen Node.
//   node gereedschap/maak-sr.mjs <uitmap>
// Eerst doet hij testsignalen/ bit voor bit na (alle gevallen, de automatisering en de noten, elk op zijn eigen
// samplerate); lukt dat niet, dan stopt hij met exit 1, want dan is de verwachte uitvoer hieronder niets waard.
// Daarna schrijft hij per samplerate in <uitmap>/<sr>/:
//   stil.wav                 stille stereo-invoer van de juiste lengte (pb-render leest er de lengte en samplerate uit)
//   noten.txt                de noten op hun sample (pb-render --noten)
//   noten-stereo.wav         verwacht: de noten, 2 kanalen
//   noten-mono.wav           verwacht: de noten, 1 kanaal (verwerk met 1 kanaal: wat de DSP in mono doet)
//   knoppen-stereo.wav       verwacht: vaste knopwaarden (Poort open), 2 kanalen
//   knoppen-mono.wav         idem, 1 kanaal
// en in <uitmap>/: noten.mid (dezelfde noten in seconden, voor pluginval test) en sr.json (wat er gemaakt is).
// De noten staan op veelvouden van 10 ms, zodat ze op elke samplerate (44,1 tot 192 kHz) op een hele sample vallen,
// ook na de omrekening die pluginval doet (lround(seconden × samplerate)).
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');
const T = join(PAKKET, 'testsignalen');
export const SAMPLERATES = [44100, 48000, 88200, 96000, 176400, 192000];
const DUUR_SEC = 12;                    // zo lang als testsignalen/noten (576000 samples op 48 kHz)

// --- de ketenmotor -----------------------------------------------------------------------------------------------
const wasmModule = new WebAssembly.Module(readFileSync(join(PAKKET, 'wasm', 'graaf.wasm')));
const { rij } = JSON.parse(readFileSync(join(PAKKET, 'wasm', 'keten.json'), 'utf8'));
const BLOK = 128;
class Motor {
  constructor(sr) {
    this.e = new WebAssembly.Instance(wasmModule, {}).exports;  // eigen instantie: geen gedeelde staat
    this.h = this.e.pg_maak(sr);
    if (this.h < 0) throw new Error('pg_maak mislukt');
    new Float64Array(this.e.memory.buffer, this.e.pg_rij(), rij.length).set(rij);
    if (this.e.pg_laad(this.h, rij.length) < 0) throw new Error('pg_laad mislukt');
  }
  zet(i, v) { this.e.pg_zet(this.h, i, v); }
  // n samples, k kanalen; acties = [{ sample, doe() }] op volgorde: het blok wordt bij elke actie gesplitst
  render(n, k, acties = [], naSample = null) {
    const uit = Array.from({ length: k }, () => new Float32Array(n));
    let a = 0, pos = 0;
    while (pos < n) {
      while (a < acties.length && acties[a].sample <= pos) acties[a++].doe();
      let eind = Math.min(n, pos + BLOK);
      if (a < acties.length && acties[a].sample < eind) eind = acties[a].sample;
      const m = eind - pos;
      for (let c = 0; c < k; c++) new Float32Array(this.e.memory.buffer, this.e.pg_in(this.h, c), m).fill(0);
      this.e.pg_verwerk(this.h, k, m);
      for (let c = 0; c < k; c++) uit[c].set(new Float32Array(this.e.memory.buffer, this.e.pg_uit(this.h, c), m), pos);
      pos = eind;
    }
    if (naSample) naSample(uit);
    return uit;
  }
}

// --- pb_math.h exp_ en pb_noten.h, letterlijk (alleen + − × ÷ en floor: in JS precies dezelfde bits) -------------
function vanBits(u) { const b = new DataView(new ArrayBuffer(8)); b.setBigUint64(0, u); return b.getFloat64(0); }
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
  let y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
  let ki = k | 0;
  if (ki < -1021) { y *= vanBits(BigInt(ki + 1000 + 1023) << 52n); return y * vanBits(BigInt(-1000 + 1023) << 52n); }
  if (ki > 1023) { y *= 2.0; ki -= 1; }
  return y * vanBits(BigInt(ki + 1023) << 52n);
}
const LN2 = 0.69314718055994530942;
const nootVerhouding = (n) => exp_((n - 57) / 12.0 * LN2);
class NootStapel {
  constructor() { this.n = []; }
  aan(x) { this.n = this.n.filter((y) => y !== x); if (this.n.length === 16) this.n.shift(); this.n.push(x); }
  uit(x) { this.n = this.n.filter((y) => y !== x); }
  boven() { return this.n.length ? this.n[this.n.length - 1] : -1; }
}
// ARCHITECTUUR.md "Noten": acties voor de motor uit [{ sample, aan, noot }]
function nootActies(motor, noten) {
  const st = new NootStapel();
  return noten.map((x) => ({
    sample: x.sample,
    doe() {
      if (x.noot < 0 || x.noot > 127) return;
      if (x.aan) st.aan(x.noot); else st.uit(x.noot);
      const b = st.boven();
      if (b >= 0) { motor.zet(0, nootVerhouding(b)); motor.zet(1, 1.0); } else motor.zet(1, 0.0);
    },
  }));
}

// --- WAV en MIDI ---------------------------------------------------------------------------------------------------
function leesWav(pad) {
  const b = readFileSync(pad);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { k: b.readUInt16LE(p + 10), sr: b.readUInt32LE(p + 12) };
    if (id === 'data') data = { off: p + 8, len };
    p += 8 + len + (len & 1);
  }
  const n = data.len / (4 * fmt.k), k = Array.from({ length: fmt.k }, () => new Float32Array(n));
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
// format 0, 1000 ticks per kwartnoot, tempo 1 000 000 µs per kwart: 1 tick = 1 ms
function schrijfMidi(pad, noten) {
  const vlq = (v) => { const r = [v & 0x7f]; while ((v >>= 7)) r.unshift((v & 0x7f) | 0x80); return r; };
  const trk = [0x00, 0xff, 0x51, 0x03, 0x0f, 0x42, 0x40];
  let t = 0;
  for (const x of noten) {
    trk.push(...vlq(x.ms - t), x.aan ? 0x90 : 0x80, x.noot, x.aan ? 100 : 0);
    t = x.ms;
  }
  trk.push(0x00, 0xff, 0x2f, 0x00);
  const kop = Buffer.from([0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xe8]);
  const tl = Buffer.alloc(8); tl.write('MTrk', 0); tl.writeUInt32BE(trk.length, 4);
  writeFileSync(pad, Buffer.concat([kop, tl, Buffer.from(trk)]));
}

function bitVoorBit(a, b) {
  if (a.length !== b.length) return `kanalen ${a.length} ≠ ${b.length}`;
  for (let c = 0; c < a.length; c++) {
    if (a[c].length !== b[c].length) return `lengte ${a[c].length} ≠ ${b[c].length}`;
    for (let i = 0; i < a[c].length; i++)
      if (!Object.is(a[c][i], b[c][i])) return `kanaal ${c}, sample ${i}: ${a[c][i]} ≠ ${b[c][i]}`;
  }
  return null;
}
function leesNoten(pad) {
  return readFileSync(pad, 'utf8').trim().split('\n').map((r) => {
    const [s, w, n] = r.trim().split(/\s+/);
    return { sample: +s, aan: w === 'aan', noot: +n };
  });
}
// parameter-id → index in PARAMS (dsp/ontwerpen/keten_toetsenpad.h: {id, "naam", ...})
const ID_NAAR_INDEX = new Map([...readFileSync(join(PAKKET, 'dsp', 'ontwerpen', 'keten_toetsenpad.h'), 'utf8')
  .matchAll(/^\s*\{(\d+), "[^"]*"/gm)].map((m, i) => [+m[1], i]));

// --- 1. testsignalen/ nadoen ---------------------------------------------------------------------------------------
function zelftoets() {
  const fouten = [];
  const gevallen = JSON.parse(readFileSync(join(T, 'gevallen.json'), 'utf8'));
  for (const g of gevallen) {
    const verwacht = leesWav(join(T, `${g.naam}.verwacht.wav`)), inv = leesWav(join(T, `${g.naam}.in.wav`));
    const m = new Motor(inv.sr);
    for (const [i, v] of Object.entries(g.waarden)) m.zet(+i, v);
    const f = bitVoorBit(verwacht.kanalen, m.render(inv.n, inv.kanalen.length));
    console.log(`zelftoets ${g.naam} (${inv.sr} Hz): ${f ? 'ANDERS — ' + f : 'bit voor bit gelijk'}`);
    if (f) fouten.push(g.naam);
  }
  {
    const verwacht = leesWav(join(T, 'automatie', 'verwacht.wav')), m = new Motor(verwacht.sr);
    const ev = readFileSync(join(T, 'automatie', 'events.txt'), 'utf8').trim().split('\n').map((r) => r.trim().split(/\s+/).map(Number));
    const acties = ev.map(([s, id, v]) => ({ sample: s, doe: () => m.zet(ID_NAAR_INDEX.get(id), v) }));
    const f = bitVoorBit(verwacht.kanalen, m.render(verwacht.n, verwacht.kanalen.length, acties));
    console.log(`zelftoets automatie (${verwacht.sr} Hz, ${ev.length} events): ${f ? 'ANDERS — ' + f : 'bit voor bit gelijk'}`);
    if (f) fouten.push('automatie');
  }
  {
    const verwacht = leesWav(join(T, 'noten', 'verwacht.wav')), m = new Motor(verwacht.sr);
    const f = bitVoorBit(verwacht.kanalen, m.render(verwacht.n, verwacht.kanalen.length, nootActies(m, leesNoten(join(T, 'noten', 'noten.txt')))));
    console.log(`zelftoets noten (${verwacht.sr} Hz): ${f ? 'ANDERS — ' + f : 'bit voor bit gelijk'}`);
    if (f) fouten.push('noten');
  }
  return fouten;
}

// --- 2. andere samplerates en mono ---------------------------------------------------------------------------------
// de noten van testsignalen/noten, op het dichtstbijzijnde veelvoud van 10 ms (dezelfde volgorde)
function notenInMs() {
  const n = leesNoten(join(T, 'noten', 'noten.txt'));
  return n.map((x) => ({ ms: Math.round(x.sample / 48000 * 100) * 10, aan: x.aan, noot: x.noot }));
}
// vaste knopwaarden met een open poort: het geval stukjes-laag uit gevallen.json
function knoppen() {
  const g = JSON.parse(readFileSync(join(T, 'gevallen.json'), 'utf8')).find((x) => x.naam === 'stukjes-laag');
  return g;
}

function maak(uitmap) {
  const noten = notenInMs(), kg = knoppen(), lijst = [];
  mkdirSync(uitmap, { recursive: true });
  schrijfMidi(join(uitmap, 'noten.mid'), noten);
  for (const sr of SAMPLERATES) {
    const d = join(uitmap, String(sr)), n = DUUR_SEC * sr;
    mkdirSync(d, { recursive: true });
    const opSample = noten.map((x) => ({ sample: x.ms * sr / 1000, aan: x.aan, noot: x.noot }));
    for (const x of opSample) if (!Number.isInteger(x.sample)) throw new Error(`noot niet op een hele sample bij ${sr} Hz`);
    writeFileSync(join(d, 'noten.txt'), opSample.map((x) => `${x.sample} ${x.aan ? 'aan' : 'uit'} ${x.noot}`).join('\n') + '\n');
    schrijfWav(join(d, 'stil.wav'), [new Float32Array(n), new Float32Array(n)], sr);
    for (const k of [2, 1]) {
      const naam = k === 2 ? 'stereo' : 'mono';
      let m = new Motor(sr);
      schrijfWav(join(d, `noten-${naam}.wav`), m.render(n, k, nootActies(m, opSample)), sr);
      m = new Motor(sr);
      for (const [i, v] of Object.entries(kg.waarden)) m.zet(+i, v);
      schrijfWav(join(d, `knoppen-${naam}.wav`), m.render(n, k), sr);
    }
    lijst.push({ sr, samples: n, noten: opSample.length });
  }
  writeFileSync(join(uitmap, 'sr.json'), JSON.stringify({ duurSec: DUUR_SEC, knoppen: kg, samplerates: lijst }, null, 2));
  return lijst;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const uitmap = process.argv[2];
  if (!uitmap) { console.error('gebruik: node gereedschap/maak-sr.mjs <uitmap>'); process.exit(2); }
  const fouten = zelftoets();
  if (fouten.length) { console.error(`FOUT: de WASM doet testsignalen/ niet na (${fouten.join(', ')}); geen verwachte uitvoer gemaakt`); process.exit(1); }
  for (const x of maak(uitmap)) console.log(`verwacht gemaakt: ${x.sr} Hz, ${x.samples} samples, ${x.noten} noot-events, stereo en mono`);
}
