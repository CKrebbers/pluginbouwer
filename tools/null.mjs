// null.mjs — de null-test tussen de WASM (browser) en het gebouwde binary.
//   node tools/null.mjs maak [ontwerp]          testsignalen/<ontwerp>/: invoer, verwachte uitvoer (WASM) en pluginval-configs
//   node tools/null.mjs vergelijk <map> [ontwerp] vergelijkt <map>/<geval>.wav met de verwachting; exit 1 boven de grens
// Grens: piek van het verschil ≤ −90 dBFS (PROMPT.md). We melden ook RMS en of het bit voor bit gelijk is.
import { mkdirSync, writeFileSync, existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, render, uitNorm, schrijfWav, leesWav, nullTest, fmtDb, ROOT } from '../tests/lib/pb.mjs';
import { sinus, ruis, sweep, stukjes, impuls, stereo } from '../tests/lib/signalen.mjs';
import * as SPEC from '../tests/lib/fft.mjs';

const GRENS_DB = -90;
const [, , opdracht, a1, a2] = process.argv;

// Elk geval: invoer + parameters (genormaliseerd, zoals een host ze zet; waarden exact in float32).
// statistisch: zelfoscillatie is chaotisch (1 ULP groeit). Dan geldt naast de strenge toets ook de statistische:
// spectrum per terts en omhullende per 100 ms binnen 1 dB (PROMPT.md).
function gevallen(o, naam) {
  const sr = 48000;
  const norm = (lijst) => Object.fromEntries(lijst.map((n, i) => [i, n]));
  const lang = (x, sec, sr2 = sr) => { const y = new Float32Array(Math.round(sec * sr2)); y.set(x.subarray(0, Math.min(x.length, y.length))); return y; };
  if (naam === 'dub_echo') {
    //          Tijd    Terug   Toon    Laag    Reso   Verz   Haper  Mix
    return [
      { naam: 'echo-standaard', sr, in: stereo(lang(stukjes(sr, 2), 4)), norm: norm([0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25]) },
      { naam: 'echo-heet', sr, in: stereo(lang(ruis(sr, 0.3, 0.5, 11), 8), lang(stukjes(sr, 2), 8)), norm: norm([0.125, 0.75, 0.125, 0.0625, 0.25, 0.5, 0.5, 0.75]) },
      { naam: 'zelfoscillatie', sr, statistisch: true, in: stereo(lang(stukjes(sr, 1), 8)), norm: norm([0.0625, 0.875, 0.25, 0.0625, 0.125, 0.25, 0.25, 1]) },
      { naam: 'sweep-441', sr: 44100, in: stereo(lang(sweep(30, 18000, 44100, 3, 0.4), 5, 44100)), norm: norm([0.5, 0.5, 0.5, 0.125, 0.125, 0.75, 0.75, 0.5]) },
    ];
  }
  const half = o.params.map(() => 0.5), kwart = o.params.map(() => 0.25), laag = o.params.map((_, i) => (i === 0 ? 0.0625 : 0.75));
  return [
    { naam: 'ruis-half', sr, in: stereo(ruis(sr, 2, 0.3, 1), ruis(sr, 2, 0.3, 2)), norm: norm(half) },
    { naam: 'sweep-kwart', sr, in: stereo(sweep(20, 20000, sr, 3, 0.5)), norm: norm(kwart) },
    { naam: 'stukjes-laag', sr, in: stereo(stukjes(sr, 2.5)), norm: norm(laag) },
    { naam: 'sinus-441', sr: 44100, in: stereo(sinus(997, 44100, 1, 0.7)), norm: norm(half) },
    { naam: 'impuls-96k', sr: 96000, in: stereo(impuls(96000)), norm: norm(kwart) },
  ];
}

// statistische vergelijking: energie per terts (20 Hz – 20 kHz) en RMS per 100 ms, waar er iets klinkt
export function statistisch(a, b, sr) {
  const { spectrum, volgendeMacht } = SPEC;
  let slechtstBand = 0, slechtstOmh = 0;
  for (let c = 0; c < Math.min(a.length, b.length); c++) {
    const n = volgendeMacht(a[c].length) / 2;
    const pa = spectrum(a[c].subarray(0, n)), pb = spectrum(b[c].subarray(0, n));
    const totaal = pa.reduce((s, v) => s + v, 0);
    for (let f = 20; f < 20000; f *= Math.pow(2, 1 / 3)) {
      const lo = Math.floor(f / sr * n), hi = Math.max(lo + 1, Math.floor(f * Math.pow(2, 1 / 3) / sr * n));
      let ea = 0, eb = 0; for (let k = lo; k < hi; k++) { ea += pa[k]; eb += pb[k]; }
      if (ea < totaal * 1e-8) continue;   // banden 80 dB onder het totaal tellen niet
      slechtstBand = Math.max(slechtstBand, Math.abs(10 * Math.log10(ea / eb)));
    }
    const venster = Math.round(sr / 10);
    for (let i = 0; i + venster <= a[c].length; i += venster) {
      let ea = 0, eb = 0; for (let k = i; k < i + venster; k++) { ea += a[c][k] ** 2; eb += b[c][k] ** 2; }
      if (ea / venster < 1e-6) continue;   // onder −60 dBFS RMS telt niet
      slechtstOmh = Math.max(slechtstOmh, Math.abs(10 * Math.log10(ea / eb)));
    }
  }
  return { bandDb: slechtstBand, omhullendeDb: slechtstOmh };
}

const ontwerpNaam = (opdracht === 'vergelijk' ? a2 : a1) || 'pijplijn_biquad';
const o = laadOntwerp(ontwerpNaam);
const map = join(ROOT, 'testsignalen', ontwerpNaam);

if (opdracht === 'maak') {
  mkdirSync(join(map, 'pluginval'), { recursive: true });
  const index = [];
  for (const g of gevallen(o, ontwerpNaam)) {
    const waarden = Object.fromEntries(Object.entries(g.norm).map(([i, n]) => [i, uitNorm(o.params[i], n)]));
    const uit = render(o, g.in, { sr: g.sr, waarden, blok: 128 });
    schrijfWav(join(map, `${g.naam}.in.wav`), g.in, g.sr);
    schrijfWav(join(map, `${g.naam}.verwacht.wav`), uit, g.sr);
    const plain = o.params.map((p, i) => `${p.id}=${waarden[i]}`);
    index.push({ naam: g.naam, sr: g.sr, norm: g.norm, waarden, pbRender: plain, statistisch: !!g.statistisch });
    // pluginval test: één config per geval (zie pluginval docs/Acceptance testing.md). De referentie is de WASM-render.
    // Ook de statistische gevallen krijgen een config (pluginval neemt hun render op); tools/ci-null.sh slaat ze
    // over in de strenge poort, want pluginval vergelijkt alleen per sample.
    writeFileSync(join(map, 'pluginval', `${g.naam}.json`), JSON.stringify({
      name: g.naam,
      input: { audio: `../${g.naam}.in.wav` },
      reference: `../${g.naam}.verwacht.wav`,
      state: { parameters: Object.fromEntries(o.params.map((p, i) => [p.naam, g.norm[i]])) },
      sample_rate: g.sr, block_size: 512,
      comparison: { sample: Number(Math.pow(10, GRENS_DB / 20).toPrecision(6)) },
    }, null, 2) + '\n');
  }
  writeFileSync(join(map, 'gevallen.json'), JSON.stringify(index, null, 2) + '\n');
  console.log(`${index.length} gevallen in ${map}`);
} else if (opdracht === 'vergelijk') {
  if (!a1) throw new Error('gebruik: vergelijk <map met renders> [ontwerp]');
  const index = JSON.parse(readFileSync(join(map, 'gevallen.json'), 'utf8'));
  let mis = 0;
  console.log(`| geval | piek | RMS | bit voor bit |\n|---|---|---|---|`);
  for (const g of index) {
    const pad = join(a1, `${g.naam}.wav`);
    if (!existsSync(pad)) { console.log(`| ${g.naam} | ontbreekt | | |`); mis++; continue; }
    const verwacht = leesWav(join(map, `${g.naam}.verwacht.wav`)).kanalen, gekregen = leesWav(pad).kanalen;
    const r = nullTest(verwacht, gekregen);
    let ok = r.piekDb <= GRENS_DB, stat = '';
    if (g.statistisch) {
      const st = statistisch(verwacht, gekregen, g.sr);
      const statOk = st.bandDb <= 1 && st.omhullendeDb <= 1;
      stat = ` statistisch: terts ${st.bandDb.toFixed(3)} dB, omhullende ${st.omhullendeDb.toFixed(3)} dB${statOk ? '' : ' **BOVEN 1 dB**'}`;
      ok = statOk;   // bij zelfoscillatie beslist de statistische toets; de strenge staat er ter informatie bij
    }
    if (!ok) mis++;
    let uitleg = '';
    if (!ok) {
      // Diagnose: klinkt de render als de STANDAARDwaarden? Dan heeft de host de parameters niet bij de DSP gekregen.
      const inv = leesWav(join(map, `${g.naam}.in.wav`));
      const std = render(o, inv.kanalen, { sr: inv.sr, waarden: {}, blok: 128 });
      const rs = nullTest(std, leesWav(pad).kanalen);
      uitleg = rs.piekDb <= GRENS_DB ? ' — gelijk aan de STANDAARDwaarden: de parameters kwamen niet aan' : '';
    }
    console.log(`| ${g.naam} | ${fmtDb(r.piekDb)} | ${fmtDb(r.rmsDb)} | ${r.piekDb === -Infinity ? 'ja' : 'nee'} |${stat}${ok ? '' : ' **BOVEN DE GRENS**' + uitleg}`);
  }
  process.exit(mis ? 1 : 0);
} else {
  console.error('gebruik: node tools/null.mjs maak|vergelijk …'); process.exit(2);
}
