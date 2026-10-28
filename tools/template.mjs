// template.mjs — Claude maakt een template voor de gedeelde templates van de tool (de database van het artifact). Bouwt
// het ontwerp uit een voorbeeld of een JSON-bestand plus blokken erbij, toetst dat het klinkt op de echte motor (geen
// NaN, niet te stil, niet boven 0 dBFS) en schrijft het document dat in `templates/<id>` hoort.
//   node tools/template.mjs --van dub_echo --erbij galm --zet tape.drive=4 --naam "Dub echo + Reverb" --map m-dub \
//        [--notitie "…"] [--id t-…] [--plek 1] [--uit pad.json]
//   --van      id van een voorbeeld (tool/src/archetypen.js) of een JSON-bestand met een ontwerp (of een template)
//   --erbij    type (bv. galm) of recept:<id>, eventueel @<blok-id> = direct achter dat blok; mag vaker
//   --weg      <blok-id>: dat blok eruit (wat erin en eruit ging sluit weer aan); mag vaker
//   --zet      <blok-id>.<knop>=<getal>: de stand van een knop (of de standaard van de parameter op die knop); mag vaker
import { readFileSync, writeFileSync } from 'node:fs';
import { ARCHETYPEN } from '../tool/src/archetypen.js';
import { nieuweTemplate, bewaarVorm, normaliseerTemplate } from '../tool/src/templates.js';
import { opties, pasAan, klinkt, stop } from './ontwerp-bouw.mjs';

const { alle, een } = opties(process.argv.slice(2));
const van = een('--van') ?? stop('template: --van ontbreekt');
let o = ARCHETYPEN.find((a) => a.id === van);
if (!o) { const j = JSON.parse(readFileSync(van, 'utf8')); o = j.ontwerp ?? j; }
const r = pasAan(o, { erbij: alle('--erbij'), weg: alle('--weg'), zet: alle('--zet') });
for (const l of r.log) console.error('  ' + l);
const k = klinkt(r.ontwerp);
if (k.rmsDb !== undefined) console.error(`  klinkt: rms ${k.rmsDb.toFixed(1)} dBFS, piek ${k.piekDb.toFixed(1)} dBFS${k.nan ? ', NaN!' : ''}`);
if (!k.ok) stop('template: ' + k.waarom);

const map = een('--map') ?? 'los';
const t = nieuweTemplate(r.ontwerp, { naam: een('--naam') ?? r.ontwerp.naam, map, mappen: [{ id: map, naam: '', plek: 0 }], notitie: een('--notitie') ?? '', door: 'claude', ...(een('--id') ? { id: een('--id') } : {}) });
t.plek = +(een('--plek') ?? 1);                 // de map en de volgorde staan in de database, niet hier
if (!normaliseerTemplate(bewaarVorm(t), t.id)) stop('template: de template haalt zijn eigen controle niet');
const uit = JSON.stringify({ doc_id: t.id, data: bewaarVorm(t) });
if (een('--uit')) writeFileSync(een('--uit'), uit); else console.log(uit);
