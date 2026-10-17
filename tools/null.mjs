// null.mjs — de null-test tussen de WASM (browser) en het gebouwde binary.
//   node tools/null.mjs maak [ontwerp]          testsignalen/<ontwerp>/: invoer, verwachte uitvoer (WASM) en pluginval-configs
//   node tools/null.mjs vergelijk <map> [ontwerp] vergelijkt <map>/<geval>.wav met de verwachting; exit 1 boven de grens
// Grens: piek van het verschil ≤ −90 dBFS (PROMPT.md). We melden ook RMS en of het bit voor bit gelijk is.
import { mkdirSync, writeFileSync, existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, render, uitNorm, schrijfWav, leesWav, nullTest, fmtDb, ROOT } from '../tests/lib/pb.mjs';
import { toetsgevallen, maakInvoer, automatieGeval } from '../tool/src/testsignalen.js';
import { ARCHETYPEN } from '../tool/src/archetypen.js';
import * as SPEC from '../tests/lib/fft.mjs';

const GRENS_DB = -90;
const [, , opdracht, a1, a2] = process.argv;

// De toetsgevallen komen uit tool/src/testsignalen.js (zelfde bron als de exportknop van de tool). Voor de
// handgeschreven dub-echo de gevallen van het archetype, zodat hij en de keten-dub-echo dezelfde verwachting hebben.
function gevallen(o, naam) {
  const ontwerp = ARCHETYPEN.find((a) => a.id === naam.replace(/^keten_/, ''));
  return toetsgevallen(ontwerp, o.params.length).map((g) => ({ ...g, in: maakInvoer(g), norm: Object.fromEntries(g.norm.map((n, i) => [i, n])) }));
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

const ontwerpNaam = (opdracht === 'vergelijk' || opdracht === 'automatie' ? a2 : a1) || 'pijplijn_biquad';
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
  // automatisering op willekeurige samples (pb-render --automatie): bewijst het splitsen bij events, op de bits
  const a = automatieGeval(o.params);
  mkdirSync(join(map, 'automatie'), { recursive: true });
  schrijfWav(join(map, 'automatie', 'in.wav'), a.invoer, a.sr);
  writeFileSync(join(map, 'automatie', 'events.txt'), a.tekst);
  schrijfWav(join(map, 'automatie', 'verwacht.wav'), render(o, a.invoer, { sr: a.sr, blok: 128, events: a.events }), a.sr);
  console.log(`${index.length} gevallen en automatie (${a.events.length} events) in ${map}`);
} else if (opdracht === 'automatie') {
  // node tools/null.mjs automatie <render.wav> [ontwerp]: de render van pb-render --automatie tegen de WASM
  if (!a1) throw new Error('gebruik: automatie <render.wav> [ontwerp]');
  const r = nullTest(leesWav(join(map, 'automatie', 'verwacht.wav')).kanalen, leesWav(a1).kanalen);
  const ok = r.piekDb <= GRENS_DB;
  console.log(`| automatie (events midden in blokken) | ${fmtDb(r.piekDb)} | ${fmtDb(r.rmsDb)} | ${r.piekDb === -Infinity ? 'ja' : 'nee'} |${ok ? '' : ' **BOVEN DE GRENS**'}`);
  process.exit(ok ? 0 : 1);
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
