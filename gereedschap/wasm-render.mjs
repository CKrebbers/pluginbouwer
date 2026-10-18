// wasm-render.mjs — de verwachte uitvoer uit de ketenmotor van de tool (wasm/graaf.wasm + wasm/keten.json), voor
// samplerates en kanaalaantallen die testsignalen/ niet heeft (fase 5). Staat los: alleen Node.
//
//   node gereedschap/wasm-render.mjs --na <noottabel>                  doet testsignalen/ eerst bit voor bit na
//                                                                      (alle gevallen, automatie, noten); exit 1 anders
//   node gereedschap/wasm-render.mjs --maak <map> <noottabel> <sr,…>   maakt per samplerate en per kanaalaantal (2 en 1)
//                                                                      invoer, verwachting en pb-render-opties in <map>
//
// <noottabel>: 128 regels "noot verhouding" van pb-noottabel (pb::nootVerhouding uit dsp/pb_noten.h, gebouwd met de
// DSP-vlaggen): de noten volgen zo precies ARCHITECTUUR.md "Noten", zonder de DSP in JS na te bouwen.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');
const T = join(PAKKET, 'testsignalen');
const NOOT = 0, POORT = 1;  // indexen in PARAMS (PB_NOOT_PARAM, PB_POORT_PARAM)
const BYPASS_ID = 5;        // Bypass: van de schil (plugin/src/plugin.cpp), niet van het Ontwerp
const OVERVLOEI_SEC = 0.010;
const FLT_MIN = 1.1754943508222875e-38;
const uitvoer = (y) => (y > -FLT_MIN && y < FLT_MIN ? 0 : Math.fround(y));  // pb::uitvoer (dsp/pb_core.h)

export function leesWav(pad) {
  const b = readFileSync(pad);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { tag: b.readUInt16LE(p + 8), k: b.readUInt16LE(p + 10), sr: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22) };
    if (id === 'data') data = { off: p + 8, len };
    p += 8 + len + (len & 1);
  }
  if (!fmt || (fmt.tag !== 3 && fmt.tag !== 0xfffe) || fmt.bits !== 32) throw new Error(`${pad}: alleen 32-bit float`);
  const n = Math.floor(data.len / (4 * fmt.k)), k = Array.from({ length: fmt.k }, () => new Float32Array(n));
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

// De ketenmotor één keer laden; elke render maakt een eigen instantie en ruimt hem op.
const WASM = new WebAssembly.Module(readFileSync(join(PAKKET, 'wasm', 'graaf.wasm')));
const { rij: RIJ } = JSON.parse(readFileSync(join(PAKKET, 'wasm', 'keten.json'), 'utf8'));

// render({ n, sr, kanalen, waarden: {index: v}, events: [{ sample, index, waarde } | { sample, noot, aan } |
//         { sample, bypass }] })
// Events op hun sample (het blok wordt daar gesplitst, zoals de schil doet); noten zoals ARCHITECTUUR.md "Noten".
// Bypass zoals de schil: de DSP rekent door (zijn uitvoer verandert niet), daarna per sample lineair overvloeien naar
// de droge invoer (stilte) in 10 ms, door pb::uitvoer; na de start springt hij meteen naar het doel.
export function render({ n, sr, kanalen, waarden = {}, events = [], noottabel }) {
  const { exports: e } = new WebAssembly.Instance(WASM, {});
  const h = e.pg_maak(sr);
  if (h < 0) throw new Error(`pg_maak(${sr}) mislukt`);
  new Float64Array(e.memory.buffer, e.pg_rij(), RIJ.length).set(RIJ);
  e.pg_laad(h, RIJ.length);
  for (const [i, v] of Object.entries(waarden)) e.pg_zet(h, Number(i), v);
  const ev = events.map((x, volgorde) => ({ ...x, volgorde })).sort((a, b) => a.sample - b.sample || a.volgorde - b.volgorde);
  const stapel = [];  // pb::NootStapel: monofoon, legato, hooguit 16
  let doel = 0, mix = 0, vers = true;
  const stap = 1.0 / (OVERVLOEI_SEC * sr);
  const pasToe = (x) => {
    if (x.bypass !== undefined) { doel = Math.min(1, Math.max(0, x.bypass)) >= 0.5 ? 1 : 0; return; }
    if (x.noot === undefined) { e.pg_zet(h, x.index, x.waarde); return; }
    if (x.noot < 0 || x.noot > 127) return;
    const j = stapel.indexOf(x.noot); if (j >= 0) stapel.splice(j, 1);
    if (x.aan) { if (stapel.length === 16) stapel.shift(); stapel.push(x.noot); }
    if (stapel.length) { e.pg_zet(h, NOOT, noottabel[stapel[stapel.length - 1]]); e.pg_zet(h, POORT, 1.0); }
    else e.pg_zet(h, POORT, 0.0);
  };
  const uit = Array.from({ length: kanalen }, () => new Float32Array(n));
  let p = 0, v = 0;
  while (p < n) {
    while (v < ev.length && ev[v].sample <= p) pasToe(ev[v++]);
    let m = Math.min(128, n - p);
    if (v < ev.length && ev[v].sample < p + m) m = ev[v].sample - p;
    for (let c = 0; c < kanalen; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).fill(0);  // instrument: stille invoer
    e.pg_verwerk(h, kanalen, m);
    for (let c = 0; c < kanalen; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), p);
    for (let i = p; i < p + m; i++) {
      if (vers) { mix = doel; vers = false; }
      if (mix < doel) mix = Math.min(doel, mix + stap); else if (mix > doel) mix = Math.max(doel, mix - stap);
      if (mix !== 0) for (let c = 0; c < kanalen; c++) uit[c][i] = uitvoer((1.0 - mix) * uit[c][i] + mix * 0.0);
    }
    p += m;
  }
  e.pg_weg(h);
  return uit;
}

export function leesNoottabel(pad) {
  const t = new Float64Array(128).fill(NaN);
  for (const r of readFileSync(pad, 'utf8').split('\n')) { const [k, v] = r.trim().split(/\s+/); if (v !== undefined) t[Number(k)] = Number(v); }
  if (t.some(Number.isNaN)) throw new Error(`${pad}: niet 128 noten`);
  return t;
}
const leesNoten = (pad, schaal = 1) => readFileSync(pad, 'utf8').trim().split('\n').map((r) => {
  const [s, wat, noot] = r.trim().split(/\s+/); return { sample: Math.round(Number(s) * schaal), noot: Number(noot), aan: wat === 'aan' };
});
const leesAutomatie = (pad, schaal = 1) => readFileSync(pad, 'utf8').trim().split('\n').map((r) => {
  const [s, id, w] = r.trim().split(/\s+/), sample = Math.round(Number(s) * schaal);
  return Number(id) === BYPASS_ID ? { sample, bypass: Number(w) } : { sample, index: Number(id) - 1, waarde: Number(w) };
});
// het bypass-geval (fase 5): noten, met Bypass aan en uit op momenten in seconden (ook korter dan het overvloeien,
// twee keer hetzelfde op één sample, en een waarde tussen de stappen); aan tot het eind, zodat de staart stil is
const BYPASS_EVENTS = [[1.25, 1], [1.26, 0], [1.3, 0.7], [1.3, 1], [2.08, 0], [3.0, 1], [3.004, 0], [3.75, 0], [5.2, 1]];
const gevallen = () => JSON.parse(readFileSync(join(T, 'gevallen.json'), 'utf8'));

function gelijk(a, b) {  // bit voor bit (ook −0 en NaN tellen)
  if (a.length !== b.length) return `${a.length} tegen ${b.length} kanalen`;
  for (let c = 0; c < a.length; c++) {
    if (a[c].length !== b[c].length) return `kanaal ${c}: ${a[c].length} tegen ${b[c].length} samples`;
    const x = new Uint32Array(a[c].buffer, a[c].byteOffset, a[c].length), y = new Uint32Array(b[c].buffer, b[c].byteOffset, b[c].length);
    for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return `kanaal ${c}, sample ${i}: ${a[c][i]} tegen ${b[c][i]}`;
  }
  return null;
}

// Een MIDI-bestand (SMF 0) met één tik per sample: ppq = sr/100 en 10 000 µs per kwart, zodat pluginval test
// (tijd = tik/ppq · 0,01 s, dan lround(tijd · sr)) elke noot op precies zijn sample zet. Noot-aan 0x90 met velocity
// 100, noot-uit 0x80.
function schrijfMidi(pad, noten, sr) {
  if (sr % 100 !== 0 || sr / 100 > 0x7fff) throw new Error(`geen MIDI-bestand voor ${sr} Hz`);
  const vlq = (n) => { const b = [n & 0x7f]; while ((n >>= 7) > 0) b.unshift((n & 0x7f) | 0x80); return b; };
  const spoor = [0, 0xff, 0x51, 3, 0x00, 0x27, 0x10];  // tempo 10 000 µs per kwart
  let t = 0;
  for (const x of [...noten].sort((a, b) => a.sample - b.sample)) {
    spoor.push(...vlq(x.sample - t), x.aan ? 0x90 : 0x80, x.noot, x.aan ? 100 : 0); t = x.sample;
  }
  spoor.push(0, 0xff, 0x2f, 0);
  const kop = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, (sr / 100) >> 8, (sr / 100) & 0xff];
  const l = spoor.length, trk = [0x4d, 0x54, 0x72, 0x6b, (l >>> 24) & 0xff, (l >>> 16) & 0xff, (l >>> 8) & 0xff, l & 0xff];
  writeFileSync(pad, Buffer.from([...kop, ...trk, ...spoor]));
}

// pluginval test (VST3 en AU) op deze samplerate: de gevallen (state.parameters zoals testsignalen/pluginval/) en de
// noten als MIDI-bestand; zonder invoer (een instrument), render_duration = de lengte. Referentie: de WASM-render.
function pluginvalConfigs(d, sr, noten, nNoten) {
  const pv = join(d, 'pluginval'); mkdirSync(pv, { recursive: true });
  const blok = { 44100: 64, 48000: 4096, 88200: 333, 96000: 512, 176400: 1024, 192000: 4096 }[sr] ?? 512;
  const config = (naam, n, state, midi) => {
    const c = { name: naam, reference: join(d, `${naam}.verwacht.wav`), ...(state ? { state } : {}), sample_rate: sr, block_size: blok,
      render_duration: n / sr, comparison: { sample: 0.0000316228 } };
    if (midi) c.input = { midi };
    writeFileSync(join(pv, `${naam}.json`), JSON.stringify(c, null, 2));
  };
  for (const g of gevallen()) {
    const { state } = JSON.parse(readFileSync(join(T, 'pluginval', `${g.naam}.json`), 'utf8'));
    config(g.naam, Math.round(leesWav(join(T, `${g.naam}.in.wav`)).kanalen[0].length * sr / g.sr), state);
  }
  schrijfMidi(join(pv, 'noten.mid'), noten, sr);
  config('noten', nNoten, undefined, join(pv, 'noten.mid'));
}

const [, , opdracht, ...rest] = process.argv;
if (opdracht === '--na') {
  // eerst bewijzen dat deze render precies is wat de tool deed: testsignalen/ bit voor bit
  const tabel = leesNoottabel(rest[0]);
  let mis = 0;
  const toets = (naam, verwachtPad, opties) => {
    const v = leesWav(verwachtPad);
    const r = render({ n: v.kanalen[0].length, sr: v.sr, kanalen: v.kanalen.length, noottabel: tabel, ...opties });
    const f = gelijk(v.kanalen, r);
    console.log(`wasm-render na: ${naam} (${v.sr} Hz, ${v.kanalen.length} kanalen): ${f ? 'ANDERS — ' + f : 'bit voor bit gelijk'}`);
    if (f) mis++;
  };
  for (const g of gevallen()) toets(g.naam, join(T, `${g.naam}.verwacht.wav`), { waarden: g.waarden });
  toets('automatie', join(T, 'automatie', 'verwacht.wav'), { events: leesAutomatie(join(T, 'automatie', 'events.txt')) });
  toets('noten', join(T, 'noten', 'verwacht.wav'), { events: leesNoten(join(T, 'noten', 'noten.txt')) });
  process.exit(mis ? 1 : 0);
} else if (opdracht === '--maak') {
  // per samplerate en kanaalaantal: <map>/<sr>-<k>/{<geval>,automatie,noten}.{in,verwacht}.wav, events op de
  // geschaalde sample (zelfde moment in seconden), en lijst.txt met per regel "geval sr kanalen pb-render-opties"
  const [map, tabelPad, srs] = rest;
  const tabel = leesNoottabel(tabelPad);
  const lijst = [];
  for (const sr of srs.split(',').map(Number)) for (const kanalen of [2, 1]) {
    const d = join(map, `${sr}-${kanalen}`); mkdirSync(d, { recursive: true });
    const schrijf = (naam, n, opties, pbOpties) => {
      const stil = Array.from({ length: kanalen }, () => new Float32Array(n));
      schrijfWav(join(d, `${naam}.in.wav`), stil, sr);
      schrijfWav(join(d, `${naam}.verwacht.wav`), render({ n, sr, kanalen, noottabel: tabel, ...opties }), sr);
      lijst.push(`${naam} ${sr} ${kanalen} ${pbOpties}`);
    };
    // de gevallen: dezelfde duur in seconden als in testsignalen/, op deze samplerate
    for (const g of gevallen()) {
      const n = Math.round(leesWav(join(T, `${g.naam}.in.wav`)).kanalen[0].length * sr / g.sr);
      schrijf(g.naam, n, { waarden: g.waarden }, g.pbRender.map((p) => '--p ' + p).join(' '));
    }
    const schaal = sr / 48000;
    const auto = leesAutomatie(join(T, 'automatie', 'events.txt'), schaal), autoPad = join(d, 'automatie.events.txt');
    writeFileSync(autoPad, auto.map((x) => `${x.sample} ${x.index + 1} ${x.waarde}`).join('\n') + '\n');
    schrijf('automatie', Math.round(192000 * schaal), { events: auto }, `--automatie ${autoPad}`);
    const noten = leesNoten(join(T, 'noten', 'noten.txt'), schaal), notenPad = join(d, 'noten.noten.txt');
    writeFileSync(notenPad, noten.map((x) => `${x.sample} ${x.aan ? 'aan' : 'uit'} ${x.noot}`).join('\n') + '\n');
    schrijf('noten', Math.round(288000 * schaal), { events: noten }, `--noten ${notenPad}`);
    const bypass = BYPASS_EVENTS.map(([t, w]) => ({ sample: Math.round(t * sr), bypass: w })), bypassPad = join(d, 'bypass.events.txt');
    writeFileSync(bypassPad, bypass.map((x) => `${x.sample} ${BYPASS_ID} ${x.bypass}`).join('\n') + '\n');
    schrijf('bypass', Math.round(288000 * schaal), { events: [...bypass, ...noten] }, `--automatie ${bypassPad} --noten ${notenPad}`);
    // bypass aan op een blokgrens (een veelvoud van 4096), voor een host die hem alleen tussen blokken zet (de AU:
    // kAudioUnitProperty_BypassEffect); daarna blijft hij aan
    const aan = [{ sample: Math.round(3.0 * sr / 4096) * 4096, bypass: 1 }], aanPad = join(d, 'bypassaan.events.txt');
    writeFileSync(aanPad, aan.map((x) => `${x.sample} ${BYPASS_ID} ${x.bypass}`).join('\n') + '\n');
    schrijf('bypassaan', Math.round(288000 * schaal), { events: [...aan, ...noten] }, `--automatie ${aanPad} --noten ${notenPad}`);
    if (kanalen === 2) pluginvalConfigs(d, sr, noten, Math.round(288000 * schaal));
  }
  writeFileSync(join(map, 'lijst.txt'), lijst.join('\n') + '\n');
  console.log(`wasm-render: ${lijst.length} verwachtingen in ${map}`);
} else if (opdracht !== undefined) {
  console.error('gebruik: wasm-render.mjs --na <noottabel> | --maak <map> <noottabel> <sr,…>');
  process.exit(2);
}
