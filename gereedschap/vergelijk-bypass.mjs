// vergelijk-bypass.mjs — de bypass van de plugin (fase 5) tegen de verwachte uitvoer en de onbewerkte invoer.
//   node gereedschap/vergelijk-bypass.mjs <soort> <render.wav> <verwacht.wav> <in.wav> [K]
// soort:
//   aan         bypass aan vóór activate: de uitgang is bit voor bit de ingang (mono naar stereo: op beide kanalen)
//   naar-aan    automatisering 0 → 1 (pb-render --automatie K --glij 9=0:1): tot de omslag bit voor bit de verwachte
//               uitvoer, na de omslag plus de overvloeiing (10 ms) bit voor bit de ingang
//   naar-uit    automatisering 1 → 0: tot de omslag de ingang, daarna de verwachte uitvoer (het Ontwerp rekende door)
// Tijdens de overvloeiing ligt elke sample tussen de natte en de droge waarde. exit 1 bij een verschil.
import { leesWav } from './maak-matrix.mjs';

const [soort, renderPad, verwachtPad, inPad, kArg] = process.argv.slice(2);
if (!inPad || !['aan', 'naar-aan', 'naar-uit'].includes(soort)) {
  console.error('gebruik: node gereedschap/vergelijk-bypass.mjs aan|naar-aan|naar-uit <render> <verwacht> <in> [K]');
  process.exit(2);
}
const r = leesWav(renderPad), v = leesWav(verwachtPad), inv = leesWav(inPad);
const sr = r.sr, n = r.kanalen[0].length, kan = r.kanalen.length;
const droog = (c) => inv.kanalen[Math.min(c, inv.kanalen.length - 1)];
const fout = (t) => { console.log(`FOUT bypass ${soort} (${sr} Hz, ${kan} kanalen): ${t}`); process.exit(1); };
if (kan !== v.kanalen.length || n !== v.kanalen[0].length) fout('vorm van de render verschilt van de verwachte uitvoer');

// [van, tot) moet bit voor bit gelijk zijn aan bron(c)
const gelijk = (van, tot, bron, wat) => {
  for (let c = 0; c < kan; c++) for (let i = van; i < tot; i++)
    if (!Object.is(r.kanalen[c][i], bron(c)[i])) fout(`sample ${i} kanaal ${c} is niet ${wat} (${r.kanalen[c][i]} ≠ ${bron(c)[i]})`);
};

if (soort === 'aan') {
  gelijk(0, n, droog, 'de ingang');
  console.log(`- bypass aan (${sr} Hz, ${kan} kanalen): ${n} samples bit voor bit de ingang`);
} else {
  // de omslag: het eerste event van pb-render --automatie K (op t = 0, K, 2K, …) met waarde ≥ 0,5 resp. < 0,5
  const K = Number(kArg || 64);
  const [van, naar] = soort === 'naar-aan' ? [0, 1] : [1, 0];
  const aan = (t) => van + (naar - van) * (t / (n - 1)) >= 0.5;  // zoals pb-render --glij de waarde uitrekent
  let t0 = 0;
  while (t0 < n && aan(t0) === aan(0)) t0 += K;
  if (t0 >= n) fout('geen omslag in de render');
  const L = Math.ceil(0.01 * sr) + 1;  // 10 ms overvloeiing, plus de sample waarop hij begint
  const nat = (c) => v.kanalen[c];
  const [voor, na] = soort === 'naar-aan' ? [nat, droog] : [droog, nat];
  gelijk(0, t0, voor, soort === 'naar-aan' ? 'de verwachte uitvoer' : 'de ingang');
  gelijk(t0 + L, n, na, soort === 'naar-aan' ? 'de ingang' : 'de verwachte uitvoer');
  let tussen = 0;
  for (let c = 0; c < kan; c++) for (let i = t0; i < t0 + L; i++) {
    const a = voor(c)[i], b = na(c)[i], x = r.kanalen[c][i];
    if (!(x >= Math.min(a, b) - 1e-6 && x <= Math.max(a, b) + 1e-6)) fout(`sample ${i} kanaal ${c} (${x}) ligt niet tussen nat en droog (${a}, ${b})`);
    if (!Object.is(x, a) && !Object.is(x, b)) tussen++;
  }
  if (tussen === 0) fout('geen overvloeiing te zien (springt hij?)');
  console.log(`- bypass ${soort} (${sr} Hz, ${kan} kanalen): omslag op sample ${t0}, ${L} samples overvloeiing ` +
              `(${tussen} tussenwaarden), daarvoor en daarna bit voor bit`);
}
