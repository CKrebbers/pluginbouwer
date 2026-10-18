// au-proef.mjs — de AU zoals een host (Logic) hem gebruikt, gemeten door host/au-proef (macOS, AudioToolbox).
//   node tools/au-proef.mjs maak [ontwerp]           testsignalen/<ontwerp>/au/: invoer, parameterstappen, verwachting
//   node tools/au-proef.mjs toets <uitmap> [ontwerp] leest wat au-proef schreef; exit 1 als een poort faalt
// Poorten (fase 7 van PROMPTS.md, zo dicht als het zonder Logic kan):
//   automatie   alle parameters per blok van 64 samples gezet (zoals een host automatiseert): null ≤ −90 dBFS tegen de WASM
//   zelfosc     terugkoppeling boven 1: na 6 s stilte nog geluid (> −40 dBFS RMS), begrensd (piek ≤ 2), eindig
//   staart      na een stoot valt de uitgang onder −90 dBFS binnen de TailTime die de AU meldt
//   state       ClassInfo bewaren, nieuwe instantie, terugzetten: zelfde parameterwaarden en bit voor bit zelfde render
//   bypass      met BypassEffect aan is de uitgang de ingang (na 10 ms overvloeien)
// Een instrument (au-proef aumu …, 18 okt) heeft andere poorten; zijn testsignalen maakt tools/null.mjs maak:
//   ingang      de AU heeft geen audio-ingang
//   noten       de noten van testsignalen/<ontwerp>/noten als MIDI, blok 512 en 64: null ≤ −90 dBFS tegen de WASM
//   hosttempo   het tempo via de host-callbacks: null ≤ −90 dBFS tegen de WASM met hetzelfde transport
//   state       als hierboven
import { mkdirSync, writeFileSync, readFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { laadOntwerp, schrijfWav, leesWav, nullTest, fmtDb, ROOT } from '../tests/lib/pb.mjs';
import { stukjes } from '../tool/src/signalen.js';

const SR = 48000, BLOK = 64, GRENS_DB = -90;
const [, , opdracht, a1, a2] = process.argv;
const ontwerpNaam = (opdracht === 'toets' ? a2 : a1) || 'keten_dub_echo';
const map = join(ROOT, 'testsignalen', ontwerpNaam, 'au');

// parameterstappen: per blok de waarde van elke parameter, in eigen eenheid en exact in float32 (een AU geeft float)
function stappen(o, nBlokken) {
  const rijen = [];
  for (let b = 0; b < nBlokken; b++) {
    const t = b / (nBlokken - 1);
    rijen.push(o.params.map((p) => {
      const span = p.max - p.min;
      // elke parameter zijn eigen langzame beweging tussen 20 % en 80 % van zijn bereik, afgerond op 1/64 van het bereik
      const fase = (p.index + 1) * 0.7;
      const n = 0.5 + 0.3 * Math.sin(2 * Math.PI * t * (1 + p.index * 0.25) + fase);
      return Math.fround(p.min + Math.round(n * 64) / 64 * span);
    }));
  }
  return rijen;
}

function renderStappen(o, invoer, rijen) {
  const { e } = o, h = e.pb_maak(SR), n = invoer[0].length, uit = invoer.map(() => new Float32Array(n));
  for (let pos = 0, b = 0; pos < n; pos += BLOK, b++) {
    rijen[b].forEach((v, i) => e.pb_zet(h, i, v));
    const m = Math.min(BLOK, n - pos);
    for (let c = 0; c < 2; c++) new Float32Array(e.memory.buffer, e.pb_in(h, c), m).set(invoer[c].subarray(pos, pos + m));
    e.pb_verwerk(h, 2, m);
    for (let c = 0; c < 2; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pb_uit(h, c), m), pos);
  }
  e.pb_weg(h);
  return uit;
}

const dB = (x) => (x > 0 ? 20 * Math.log10(x) : -Infinity);
const rms = (x, a, b) => { let s = 0; for (let i = a; i < b; i++) s += x[i] * x[i]; return Math.sqrt(s / Math.max(1, b - a)); };

if (opdracht === 'maak') {
  const o = laadOntwerp(ontwerpNaam);
  mkdirSync(map, { recursive: true });
  const x = stukjes(SR, 6, 0.4), invoer = [x, Float32Array.from(x, (v) => -0.5 * v)];
  const rijen = stappen(o, Math.ceil(x.length / BLOK));
  schrijfWav(join(map, 'automatie.in.wav'), invoer, SR);
  schrijfWav(join(map, 'automatie.verwacht.wav'), renderStappen(o, invoer, rijen), SR);
  // stappen.txt: eerste regel de parameternamen (tab), dan per blok de waarden; %.9g is exact genoeg voor een float32
  writeFileSync(join(map, 'stappen.txt'), [o.params.map((p) => p.naam).join('\t'), ...rijen.map((r) => r.map((v) => v.toPrecision(9)).join('\t'))].join('\n') + '\n');
  // stoot (staart en zelfoscillatie): 50 ms toon, daarna stilte
  const stoot = new Float32Array(SR * 8); for (let i = 0; i < SR * 0.05; i++) stoot[i] = 0.5 * Math.sin(2 * Math.PI * 330 * i / SR);
  schrijfWav(join(map, 'stoot.in.wav'), [stoot, stoot], SR);
  writeFileSync(join(map, 'instellingen.json'), JSON.stringify({
    blok: BLOK, zelfosc: { Terugkoppeling: 1.1875, Mix: 1, Verzadiging: 12 },
    state: Object.fromEntries(o.params.map((p, i) => [p.naam, Math.fround(p.min + [0.125, 0.75, 0.125, 0.0625, 0.25, 0.5, 0.5, 0.75][i % 8] * (p.max - p.min))])),
  }, null, 2) + '\n');
  console.log(`${rijen.length} blokken parameterstappen in ${map}`);
} else if (opdracht === 'toets') {
  if (!a1) throw new Error('gebruik: toets <uitmap> [ontwerp]');
  const info = JSON.parse(readFileSync(join(a1, 'info.json'), 'utf8'));
  const poorten = [];
  const poort = (naam, ok, wat) => { poorten.push({ naam, ok, wat }); console.log(`| ${naam} | ${ok ? 'groen' : '**ROOD**'} | ${wat} |`); };
  if (info.soort === 'instrument') {
    const tmap = join(ROOT, 'testsignalen', ontwerpNaam);
    console.log(`AU (instrument): latency ${info.latencySec} s, TailTime ${info.tailSec} s, ${info.ingangen} audio-ingangen, ${info.params.length} parameters\n`);
    console.log('| poort | uitslag | gemeten |\n|---|---|---|');
    poort('geen audio-ingang', info.ingangen === 0, `${info.ingangen} ingangen`);
    const tegen = (naam, verwacht, pad) => {
      if (!existsSync(pad)) return poort(naam, false, 'render ontbreekt');
      const r = nullTest(leesWav(verwacht).kanalen, leesWav(pad).kanalen);
      poort(naam, r.piekDb <= GRENS_DB, `piek ${fmtDb(r.piekDb)}`);
    };
    if (existsSync(join(tmap, 'noten', 'noten.txt')))
      for (const blok of [512, 64]) tegen(`noten als MIDI (blok ${blok})`, join(tmap, 'noten', 'verwacht.wav'), join(a1, `noten-${blok}.wav`));
    if (existsSync(join(tmap, 'hosttempo', 'tempo.txt')))
      tegen('hosttempo (host-callbacks)', join(tmap, 'hosttempo', 'verwacht.wav'), join(a1, 'hosttempo.wav'));
    poort('state (ClassInfo terug in een nieuwe instantie)', info.state.parametersGelijk && info.state.renderGelijk, `parameters ${info.state.parametersGelijk ? 'gelijk' : 'ANDERS: ' + info.state.verschil}, render ${info.state.renderGelijk ? 'bit voor bit' : 'anders'}`);
    process.exit(poorten.every((p) => p.ok) ? 0 : 1);
  }
  console.log(`AU: latency ${info.latencySec} s, TailTime ${info.tailSec} s, ${info.params.length} parameters (${info.params.map((p) => `${p.naam} ${p.min}..${p.max}`).join(', ')})\n`);
  console.log('| poort | uitslag | gemeten |\n|---|---|---|');
  const verwacht = leesWav(join(map, 'automatie.verwacht.wav')).kanalen, kreeg = leesWav(join(a1, 'automatie.wav')).kanalen;
  const r = nullTest(verwacht, kreeg);
  poort('automatie (alle parameters per 64 samples)', r.piekDb <= GRENS_DB, `piek ${fmtDb(r.piekDb)}`);
  const z = leesWav(join(a1, 'zelfosc.wav')).kanalen[0];
  let piek = 0, eindig = true; for (const v of z) { if (!Number.isFinite(v)) eindig = false; piek = Math.max(piek, Math.abs(v)); }
  const laat = dB(rms(z, z.length - 2 * SR, z.length));
  poort('zelfoscillatie', eindig && laat > -40 && piek <= 2, `RMS laatste 2 s ${laat.toFixed(1)} dBFS, piek ${piek.toFixed(3)}${eindig ? '' : ', NIET EINDIG'}`);
  const s = leesWav(join(a1, 'staart.wav')).kanalen[0];
  let laatsteLuid = 0; for (let i = 0; i < s.length; i++) if (Math.abs(s[i]) > Math.pow(10, -90 / 20)) laatsteLuid = i;
  const stilNa = (laatsteLuid + 1) / SR - 0.05;   // vanaf het einde van de stoot (50 ms)
  poort('staart', info.tailSec > 0 && stilNa <= info.tailSec, `onder −90 dBFS ${stilNa.toFixed(2)} s na de stoot, AU meldt TailTime ${Number(info.tailSec).toFixed(2)} s`);
  poort('state (ClassInfo terug in een nieuwe instantie)', info.state.parametersGelijk && info.state.renderGelijk, `parameters ${info.state.parametersGelijk ? 'gelijk' : 'ANDERS: ' + info.state.verschil}, render ${info.state.renderGelijk ? 'bit voor bit' : 'anders'}`);
  poort('bypass (BypassEffect)', info.bypass.startsWith('uitgang = ingang'), info.bypass);
  console.log(`\nClassInfo vóór Initialize (alleen gemeld; clap-wrapper eist Initialize): ${info.state.voorInitialize}`);
  process.exit(poorten.every((p) => p.ok) ? 0 : 1);
} else {
  console.error('gebruik: node tools/au-proef.mjs maak|toets …'); process.exit(2);
}
