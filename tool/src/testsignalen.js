// testsignalen.js — de toetsgevallen van een ontwerp (invoer + genormaliseerde parameters) en WAV-bytes.
// Eén bron voor tools/null.mjs (Node) en de exportknop van de tool (browser): dezelfde invoer, dezelfde verwachting.
import { sinus, ruis, sweep, stukjes, impuls, stereo } from './signalen.js';
import { exp_ } from './wiskunde.js';

const lang = (x, sec, sr) => { const y = new Float32Array(Math.round(sec * sr)); y.set(x.subarray(0, Math.min(x.length, y.length))); return y; };
const MAKERS = {
  stukjes: (sr, sec) => stereo(lang(stukjes(sr, Math.min(sec, 2)), sec, sr)),
  'stukjes-kort': (sr, sec) => stereo(lang(stukjes(sr, 1), sec, sr)),
  'ruis-stukjes': (sr, sec) => stereo(lang(ruis(sr, 0.3, 0.5, 11), sec, sr), lang(stukjes(sr, 2), sec, sr)),
  ruis: (sr, sec) => stereo(ruis(sr, sec, 0.3, 1), ruis(sr, sec, 0.3, 2)),
  sweep: (sr, sec) => stereo(lang(sweep(30, Math.min(18000, 0.45 * sr), sr, Math.min(3, sec), 0.4), sec, sr)),
  'sweep-vol': (sr, sec) => stereo(sweep(20, 20000, sr, sec, 0.5)),
  sinus: (sr, sec) => stereo(sinus(997, sr, sec, 0.7)),
  impuls: (sr) => stereo(impuls(sr)),
};

// Een toetsgeval als data: { naam, signaal, sec, sr, norm: [n per parameter], statistisch? }
export function maakInvoer(g) { return MAKERS[g.signaal](g.sr, g.sec); }

// Standaardgevallen voor elk ontwerp zonder eigen lijst. Waarden n·(max−min)+min met n in achtsten: exact in float32.
// Heeft het ontwerp een terugkoppeling, dan komt er een geval "hoog" bij dat statistisch beoordeeld wordt
// (een lus kan dan zelf oscilleren: chaotisch, 1 ULP groeit).
export function toetsgevallen(ontwerp, nParams, { terugkoppeling = false } = {}) {
  if (ontwerp?.toetsgevallen) return ontwerp.toetsgevallen;
  const alle = (n) => Array(nParams).fill(n);
  const lijst = [
    { naam: 'ruis-half', signaal: 'ruis', sec: 2, sr: 48000, norm: alle(0.5) },
    { naam: 'sweep-kwart', signaal: 'sweep-vol', sec: 3, sr: 48000, norm: alle(0.25) },
    { naam: 'stukjes-laag', signaal: 'stukjes', sec: 2.5, sr: 48000, norm: Array.from({ length: nParams }, (_, i) => (i === 0 ? 0.0625 : 0.75)) },
    { naam: 'sinus-441', signaal: 'sinus', sec: 1, sr: 44100, norm: alle(0.5) },
    { naam: 'impuls-96k', signaal: 'impuls', sec: 1, sr: 96000, norm: alle(0.25) },
  ];
  if (terugkoppeling) lijst.push({ naam: 'hoog', signaal: 'stukjes-kort', sec: 8, sr: 48000, norm: alle(0.875), statistisch: true });
  return lijst;
}

// Automatisering op willekeurige samples (ook midden in een blok, en twee events op dezelfde sample): elke parameter
// beweegt tussen 1/8 en 6/8 van zijn bereik (exact in float32). Ids in events.txt zijn index + 1, zoals pbRender.
export function automatieGeval(params) {
  const sr = 48000, sec = 4, invoer = MAKERS['ruis-stukjes'](sr, sec), n = sr * sec, events = [];
  for (let k = 0; ; k++) {
    const sample = Math.floor(k * 293.37);
    if (sample >= n) break;
    const zet = (j) => { const i = j % params.length, nn = 0.125 + ((j * 3) % 6) * 0.125; events.push([sample, i, params[i].min + nn * (params[i].max - params[i].min)]); };
    zet(k);
    if (k % 10 === 9) zet(k + 3);   // een tweede event op dezelfde sample
  }
  const tekst = events.map(([t, i, v]) => `${t} ${i + 1} ${v}`).join('\n') + '\n';
  return { naam: 'automatie', sr, invoer, events, tekst };
}

export function wavBytes(kanalen, sr) {
  const n = kanalen[0].length, k = kanalen.length, data = n * k * 4;
  const b = new ArrayBuffer(44 + data), v = new DataView(b);
  const s = (o, t) => { for (let i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); };
  s(0, 'RIFF'); v.setUint32(4, 36 + data, true); s(8, 'WAVE'); s(12, 'fmt ');
  v.setUint32(16, 16, true); v.setUint16(20, 3, true); v.setUint16(22, k, true); v.setUint32(24, sr, true);
  v.setUint32(28, sr * k * 4, true); v.setUint16(32, k * 4, true); v.setUint16(34, 32, true);
  s(36, 'data'); v.setUint32(40, data, true);
  for (let i = 0; i < n; i++) for (let c = 0; c < k; c++) v.setFloat32(44 + (i * k + c) * 4, kanalen[c][i], true);
  return new Uint8Array(b);
}

// ---- instrument (18 okt): noten en het tempo van de host ----------------------------------------------------------------
// De noten van het notengeval (seconden, aan/uit, noot): legato (een tweede toets terwijl de eerste vastzit), loslaten
// naar de noot eronder, buiten het bereik van de toon-knop (30 en 90: de parameter klemt), en twee keer dezelfde toets.
export const NOTEN = [[0.1, 'aan', 57], [0.6, 'uit', 57], [0.8, 'aan', 60], [1.0, 'aan', 64], [1.3, 'uit', 64], [1.6, 'uit', 60],
  [1.9, 'aan', 30], [2.3, 'uit', 30], [2.5, 'aan', 90], [2.8, 'aan', 69], [3.0, 'uit', 90], [3.4, 'uit', 69],
  [3.7, 'aan', 62], [3.71, 'aan', 62], [4.2, 'uit', 62]];
export const HOSTTEMPO = 140;
// Wat de schil (plugin/src/plugin.cpp, noot/speel met pb::NootStapel uit dsp/pb_noten.h) bij elke noot doet, als
// parameter-events: bovenste noot → Noot = exp_((n − 57)/12 · ln 2) en Poort 1; geen noot meer → Poort 0. Een stapel
// van hooguit 16 (de oudste valt eraf). exp_ is bit voor bit pb_math.h (tests/math.test.mjs).
export function notenAlsEvents(noten, nootParam, poortParam) {
  const stapel = [], events = [];
  for (const [smp, wat, n] of noten) {
    const j = stapel.indexOf(n); if (j >= 0) stapel.splice(j, 1);
    if (wat === 'aan') { if (stapel.length === 16) stapel.shift(); stapel.push(n); }
    if (stapel.length) {
      const top = stapel[stapel.length - 1];
      events.push([smp, nootParam, exp_((top - 57) / 12 * Math.LN2)], [smp, poortParam, 1]);
    } else events.push([smp, poortParam, 0]);
  }
  return events;
}
// Welke parameters de noten krijgen (zoals tool/src/codegen.js PB_NOOT_PARAM/PB_POORT_PARAM), of null zonder noten.
export function notenParams(ontwerp) {
  if (ontwerp.knopen.some((k) => k.type === 'in')) return null;
  const t = ontwerp.knopen.find((k) => k.type === 'toetsen');
  if (!t) return null;
  const noot = ontwerp.parameters.findIndex((p) => p.knoop === t.id && p.knop === 'toon');
  const poort = ontwerp.parameters.findIndex((p) => p.knoop === t.id && p.knop === 'poort');
  return noot >= 0 && poort >= 0 ? { noot, poort } : null;
}
export const heeftSequencer = (ontwerp) => ontwerp.knopen.some((k) => k.type === 'sequencer');
// het notengeval: 6 s stilte als invoer, de noten op oneven samples (midden in blokken van 128, 512 en de wisselende reeks)
export function notenGeval(ontwerp) {
  const p = notenParams(ontwerp);
  if (!p) return null;
  const sr = 48000, n = sr * 6, invoer = [new Float32Array(n), new Float32Array(n)];
  const noten = NOTEN.map(([t, wat, nn]) => [Math.round(t * sr) + 37, wat, nn]);
  return { sr, invoer, noten, tekst: noten.map((x) => x.join(' ')).join('\n') + '\n', events: notenAlsEvents(noten, p.noot, p.poort) };
}

// pluginval-config voor één geval (docs/Acceptance testing.md van pluginval); de referentie is de WASM-render
export function pluginvalConfig(g, paramNamen, grensDb = -90) {
  return {
    name: g.naam, input: { audio: `../${g.naam}.in.wav` }, reference: `../${g.naam}.verwacht.wav`,
    state: { parameters: Object.fromEntries(paramNamen.map((naam, i) => [naam, g.norm[i]])) },
    sample_rate: g.sr, block_size: 512, comparison: { sample: Number(Math.pow(10, grensDb / 20).toPrecision(6)) },
  };
}

// Alle testsignaalbestanden van een ontwerp, gerekend op de ketenmotor (e = pg_*-exports, rij = compileer().rij).
// Zelfde vorm als tools/null.mjs "maak" schrijft (tests/pakket.test.mjs vergelijkt ze byte voor byte).
export function maakTestsignalen(e, rij, params, ontwerp, { terugkoppeling = false, render } = {}) {
  const enc = (o) => JSON.stringify(o, null, 2) + '\n';
  const bestanden = {}, index = [];
  for (const g of toetsgevallen(ontwerp, params.length, { terugkoppeling })) {
    const invoer = maakInvoer(g);
    const waarden = Object.fromEntries(g.norm.map((n, i) => [i, params[i].min + n * (params[i].max - params[i].min)]));
    const uit = render(e, rij, invoer, { sr: g.sr, waarden, blok: 128 }).uit;
    bestanden[`testsignalen/${g.naam}.in.wav`] = wavBytes(invoer, g.sr);
    bestanden[`testsignalen/${g.naam}.verwacht.wav`] = wavBytes(uit, g.sr);
    const norm = Object.fromEntries(g.norm.map((n, i) => [i, n]));
    index.push({ naam: g.naam, sr: g.sr, norm, waarden, pbRender: params.map((p, i) => `${i + 1}=${waarden[i]}`), statistisch: !!g.statistisch });
    bestanden[`testsignalen/pluginval/${g.naam}.json`] = enc(pluginvalConfig(g, params.map((p) => p.naam)));
  }
  bestanden['testsignalen/gevallen.json'] = enc(index);
  const a = automatieGeval(params);
  bestanden['testsignalen/automatie/in.wav'] = wavBytes(a.invoer, a.sr);
  bestanden['testsignalen/automatie/events.txt'] = a.tekst;
  bestanden['testsignalen/automatie/verwacht.wav'] = wavBytes(render(e, rij, a.invoer, { sr: a.sr, blok: 128, events: a.events }).uit, a.sr);
  const ng = notenGeval(ontwerp);
  if (ng) {
    bestanden['testsignalen/noten/in.wav'] = wavBytes(ng.invoer, ng.sr);
    bestanden['testsignalen/noten/noten.txt'] = ng.tekst;
    bestanden['testsignalen/noten/verwacht.wav'] = wavBytes(render(e, rij, ng.invoer, { sr: ng.sr, blok: 128, events: ng.events }).uit, ng.sr);
  }
  if (heeftSequencer(ontwerp) && !ontwerp.knopen.some((k) => k.type === 'in')) {
    const sr = 48000, stil = [new Float32Array(sr * 6), new Float32Array(sr * 6)];
    bestanden['testsignalen/hosttempo/in.wav'] = wavBytes(stil, sr);
    bestanden['testsignalen/hosttempo/tempo.txt'] = `${HOSTTEMPO}\n`;
    bestanden['testsignalen/hosttempo/verwacht.wav'] = wavBytes(render(e, rij, stil, { sr, blok: 128, tempo: HOSTTEMPO }).uit, sr);
  }
  return bestanden;
}
