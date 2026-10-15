// testsignalen.js — de toetsgevallen van een ontwerp (invoer + genormaliseerde parameters) en WAV-bytes.
// Eén bron voor tools/null.mjs (Node) en de exportknop van de tool (browser): dezelfde invoer, dezelfde verwachting.
import { sinus, ruis, sweep, stukjes, impuls, stereo } from './signalen.js';

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
  return bestanden;
}
