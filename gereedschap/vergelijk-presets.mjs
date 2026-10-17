// vergelijk-presets.mjs — de fabriekspresets in het binary tegen presets/*.json. Staat los: alleen Node.
//   node gereedschap/vergelijk-presets.mjs <tabel.tsv>        (CLAP: de tabel komt uit gereedschap/pb-preset-toets)
//   node gereedschap/vergelijk-presets.mjs --au <tabel.tsv>   (AU: de tabel komt uit gereedschap/pb-au-presets)
// Elk JSON-bestand is precies één preset in de plugin (sleutel = bestandsnaam zonder .json), met dezelfde naam en
// uitleg, en elke parameter die de plugin kent heeft na het laden exact (bit voor bit) de waarde uit de JSON.
// De parameter Bypass (fase 5) hoort niet bij een preset: na het laden moet hij uit (0) staan.
// De AU kent geen sleutel en geen uitleg: daar gaat het op naam, en een AU-parameter is een float32 (Math.fround).
// exit 1 bij een verschil.
import { readFileSync, readdirSync } from 'node:fs';
import { join, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');
const MAP = join(PAKKET, 'presets');

const AU = process.argv[2] === '--au';
const geladen = new Map();  // sleutel (AU: naam) → { naam, uitleg, waarden: Map(param → getal) }
for (const regel of readFileSync(process.argv[AU ? 3 : 2], 'utf8').split('\n')) {
  if (!regel) continue;
  const [sleutel, naam, param, waarde] = regel.split('\t');
  if (!geladen.has(sleutel)) geladen.set(sleutel, { naam, uitleg: '', waarden: new Map() });
  const p = geladen.get(sleutel);
  if (param === '#uitleg') p.uitleg = waarde;
  else p.waarden.set(param, Number(waarde));
}

const fouten = [];
const rijen = [];
const bestanden = readdirSync(MAP).filter(f => f.endsWith('.json')).sort();
for (const f of bestanden) {
  const json = JSON.parse(readFileSync(join(MAP, f), 'utf8'));
  const sleutel = AU ? json.naam : basename(f, '.json');
  const p = geladen.get(sleutel);
  if (!p) { fouten.push(`${f}: niet in de plugin`); continue; }
  if (p.naam !== json.naam) fouten.push(`${f}: naam "${p.naam}" in de plugin, "${json.naam}" in de JSON`);
  if (!AU && p.uitleg !== (json.uitleg ?? '')) fouten.push(`${f}: uitleg verschilt`);
  // de bypass (fase 5) is de schakelaar van de host, geen klank: hij hoort niet in een preset, en een preset laden
  // laat hem staan (de toetsen laden in een verse instantie: uit = 0)
  if (p.waarden.has('Bypass')) {
    if (p.waarden.get('Bypass') !== 0) fouten.push(`${f}: Bypass = ${p.waarden.get('Bypass')} na laden, verwacht 0 (onveranderd)`);
    p.waarden.delete('Bypass');
  }
  if ('Bypass' in json.waarden) fouten.push(`${f}: een preset hoort geen Bypass te zetten`);
  const namen = new Set([...Object.keys(json.waarden), ...p.waarden.keys()]);
  let gelijk = 0;
  for (const n of namen) {
    const j = json.waarden[n], b = p.waarden.get(n);
    if (j === undefined) fouten.push(`${f}: de plugin kent "${n}", de JSON noemt hem niet`);
    else if (b === undefined) fouten.push(`${f}: "${n}" uit de JSON is geen parameter van de plugin`);
    else if (!Object.is(AU ? Math.fround(j) : j, AU ? Math.fround(b) : b)) fouten.push(`${f}: "${n}" = ${b} na laden, ${j} in de JSON`);
    else gelijk++;
  }
  rijen.push(`| ${sleutel} | ${p.naam} | ${gelijk}/${namen.size} |`);
}
const verwacht = bestanden.map(f => AU ? JSON.parse(readFileSync(join(MAP, f), 'utf8')).naam : basename(f, '.json'));
for (const s of geladen.keys())
  if (!verwacht.includes(s)) fouten.push(`preset "${s}" zit in de plugin maar niet in presets/`);

console.log('| sleutel | naam | waarden gelijk |\n|---|---|---|\n' + rijen.join('\n'));
if (fouten.length) {
  for (const f of fouten) console.error('FOUT: ' + f);
  process.exit(1);
}
console.log(`presets${AU ? ' (AU)' : ''}: alle ${bestanden.length} gelijk aan presets/*.json`);
