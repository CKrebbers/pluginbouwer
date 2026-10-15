// null.mjs — de null-test tussen de WASM (browser) en het gebouwde binary.
//   node tools/null.mjs maak [ontwerp]          testsignalen/<ontwerp>/: invoer, verwachte uitvoer (WASM) en pluginval-configs
//   node tools/null.mjs vergelijk <map> [ontwerp] vergelijkt <map>/<geval>.wav met de verwachting; exit 1 boven de grens
// Grens: piek van het verschil ≤ −90 dBFS (PROMPT.md). We melden ook RMS en of het bit voor bit gelijk is.
import { mkdirSync, writeFileSync, existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, render, uitNorm, schrijfWav, leesWav, nullTest, fmtDb, ROOT } from '../tests/lib/pb.mjs';
import { sinus, ruis, sweep, stukjes, impuls, stereo } from '../tests/lib/signalen.mjs';

const GRENS_DB = -90;
const [, , opdracht, a1, a2] = process.argv;

// Elk geval: invoer + parameters (genormaliseerd, zoals een host ze zet; waarden exact in float32).
function gevallen(o) {
  const sr = 48000;
  const norm = (lijst) => Object.fromEntries(lijst.map((n, i) => [i, n]));
  const half = o.params.map(() => 0.5), kwart = o.params.map(() => 0.25), laag = o.params.map((_, i) => (i === 0 ? 0.0625 : 0.75));
  return [
    { naam: 'ruis-half', sr, in: stereo(ruis(sr, 2, 0.3, 1), ruis(sr, 2, 0.3, 2)), norm: norm(half) },
    { naam: 'sweep-kwart', sr, in: stereo(sweep(20, 20000, sr, 3, 0.5)), norm: norm(kwart) },
    { naam: 'stukjes-laag', sr, in: stereo(stukjes(sr, 2.5)), norm: norm(laag) },
    { naam: 'sinus-441', sr: 44100, in: stereo(sinus(997, 44100, 1, 0.7)), norm: norm(half) },
    { naam: 'impuls-96k', sr: 96000, in: stereo(impuls(96000)), norm: norm(kwart) },
  ];
}

const ontwerpNaam = (opdracht === 'vergelijk' ? a2 : a1) || 'pijplijn_biquad';
const o = laadOntwerp(ontwerpNaam);
const map = join(ROOT, 'testsignalen', ontwerpNaam);

if (opdracht === 'maak') {
  mkdirSync(join(map, 'pluginval'), { recursive: true });
  const index = [];
  for (const g of gevallen(o)) {
    const waarden = Object.fromEntries(Object.entries(g.norm).map(([i, n]) => [i, uitNorm(o.params[i], n)]));
    const uit = render(o, g.in, { sr: g.sr, waarden, blok: 128 });
    schrijfWav(join(map, `${g.naam}.in.wav`), g.in, g.sr);
    schrijfWav(join(map, `${g.naam}.verwacht.wav`), uit, g.sr);
    const plain = o.params.map((p, i) => `${p.id}=${waarden[i]}`);
    index.push({ naam: g.naam, sr: g.sr, norm: g.norm, waarden, pbRender: plain });
    // pluginval test: één config per geval (zie pluginval docs/Acceptance testing.md). De referentie is de WASM-render.
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
    const r = nullTest(leesWav(join(map, `${g.naam}.verwacht.wav`)).kanalen, leesWav(pad).kanalen);
    const ok = r.piekDb <= GRENS_DB;
    if (!ok) mis++;
    console.log(`| ${g.naam} | ${fmtDb(r.piekDb)} | ${fmtDb(r.rmsDb)} | ${r.piekDb === -Infinity ? 'ja' : 'nee'} |${ok ? '' : ' **BOVEN DE GRENS**'}`);
  }
  process.exit(mis ? 1 : 0);
} else {
  console.error('gebruik: node tools/null.mjs maak|vergelijk …'); process.exit(2);
}
