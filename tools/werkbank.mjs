// werkbank.mjs — Claude aan de werkbank: lezen wat Clay bouwt (werkbank/nu uit de database van het artifact, als
// JSON-bestand) en er een voorstel van maken dat hij in de tool kan horen en overnemen (voorstellen/<id>).
//   node tools/werkbank.mjs lees <werkbank.json>
//       naam, de laatste wijzigingen, de weg van het geluid, de knoppen op het paneel en wat de motor meet
//   node tools/werkbank.mjs voorstel <werkbank.json> --titel "…" --waarom "…" [--erbij …] [--weg …] [--zet …] [--id v-…] [--uit pad]
//       zelfde --erbij/--weg/--zet als tools/template.mjs; toetst dat het klinkt, en schrijft { doc_id, data } met
//       basis = de klankafdruk van wat Clay had (zo ziet de tool of hij intussen verder ging)
import { readFileSync, writeFileSync } from 'node:fs';
import { normaliseerOntwerp, nieuwId } from '../tool/src/templates.js';
import { klankAfdruk, verschil, normaliseerVoorstel } from '../tool/src/werkbank.js';
import { beschrijf } from '../tool/src/rollen.js';
import { opties, pasAan, klinkt, stop } from './ontwerp-bouw.mjs';

const [wat, bestand, ...rest] = process.argv.slice(2);
if (!['lees', 'voorstel'].includes(wat) || !bestand) stop('gebruik: node tools/werkbank.mjs lees|voorstel <werkbank.json> …');
const ruw = JSON.parse(readFileSync(bestand, 'utf8'));
const doc = ruw.data ?? ruw;                    // een bestand van ArtifactData (out_dir) of het document zelf
if (doc.meekijken === false) stop('Clay heeft meekijken uitgezet: er staat geen ontwerp');
const o = normaliseerOntwerp(doc.ontwerp) ?? stop('geen bruikbaar ontwerp in ' + bestand);

if (wat === 'lees') {
  console.log(`${o.naam} · doorgegeven ${doc.wanneer} · afdruk ${klankAfdruk(o)}`);
  if (doc.geschiedenis?.length) console.log('laatste wijzigingen: ' + doc.geschiedenis.slice(-8).map((g) => g.wat).join(' · '));
  console.log(beschrijf(o));
  if (o.parameters.length) console.log('op het paneel: ' + o.parameters.map((p) => `${p.naam} ${p.standaard}`).join(', '));
  const k = klinkt(o);
  console.log(k.rmsDb === undefined ? 'meet niet: ' + k.waarom : `motor: rms ${k.rmsDb.toFixed(1)} dBFS, piek ${k.piekDb.toFixed(1)} dBFS${k.nan ? ', NaN' : ''}${k.ok ? '' : ' — ' + k.waarom}`);
} else {
  const { alle, een } = opties(rest);
  const r = pasAan(o, { erbij: alle('--erbij'), weg: alle('--weg'), zet: alle('--zet') });
  const k = klinkt(r.ontwerp);
  for (const z of verschil(o, r.ontwerp)) console.error('  ' + z);
  if (k.rmsDb !== undefined) console.error(`  klinkt: rms ${k.rmsDb.toFixed(1)} dBFS, piek ${k.piekDb.toFixed(1)} dBFS`);
  if (!k.ok) stop('voorstel: ' + k.waarom);
  const data = { titel: een('--titel') ?? 'Voorstel', waarom: een('--waarom') ?? '', ontwerp: r.ontwerp, basis: klankAfdruk(o), status: 'open', wanneer: new Date().toISOString() };
  const id = een('--id') ?? nieuwId('v');
  if (!normaliseerVoorstel(data, id)) stop('voorstel: haalt zijn eigen controle niet');
  const uit = JSON.stringify({ doc_id: id, data });
  if (een('--uit')) writeFileSync(een('--uit'), uit); else console.log(uit);
}
