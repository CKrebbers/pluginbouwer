// codegen.js — van een ontwerp naar de C++-header die de plugin bouwt (dsp/ontwerpen/keten_<id>.h).
// Puur: geeft tekst terug. Gebruikt door tools/bouwpakket.mjs (Node) en door de exportknop in de tool.
import { compileer, latentie } from './keten.js';

const cstr = (s) => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
const getal = (x) => {
  if (!Number.isFinite(x)) throw new Error(`geen eindig getal: ${x}`);
  // %.17g: elk double komt bit voor bit terug (de null-test hangt ervan af)
  const s = x.toPrecision(17).replace(/(\.\d*?)0+(e|$)/, '$1$2').replace(/\.(e|$)/, '$1');
  return Number(s) === x ? s : String(x);
};

// vier tekens uit een id, deterministisch (AU-subtype); de maker is altijd "Vrve"
export function auSubtype(id) {
  const schoon = id.replace(/[^A-Za-z0-9]/g, '');
  let h = 2166136261;
  for (const c of id) h = Math.imul(h ^ c.charCodeAt(0), 16777619) >>> 0;
  const abc = 'abcdefghijklmnopqrstuvwxyz';
  return ('P' + (schoon[0] || 'x').toUpperCase() + abc[h % 26] + abc[(h >>> 5) % 26]).slice(0, 4);
}

export function ketenHeader(ontwerp, { versie = '0.1.0', maker = 'Varve' } = {}) {
  const c = compileer(ontwerp);
  if (c.fouten.length) throw new Error('het ontwerp heeft fouten: ' + c.fouten.join('; '));
  const id = ontwerp.id, au = auSubtype(id);
  const vlag = (p) => 'AUTOMATEERBAAR' + (p.glijMs > 0 ? ' | pb::GLIJDEN' : '');
  const params = c.params.map((p, i) =>
    `    {${i + 1}, ${cstr(p.naam)}, ${cstr(p.uitleg || '')}, ${cstr(p.eenheid || '')}, ${getal(p.min)}, ${getal(p.max)}, ${getal(p.standaard)}, ${getal(p.glijMs)}, pb::${vlag(p)}}`);
  const rij = [];
  for (let i = 0; i < c.rij.length; i += 9) rij.push('  ' + Array.from(c.rij.slice(i, i + 9), getal).join(', '));
  // Een ontwerp zonder ingang is een instrument (AU-type aumu, geen audio-ingang). Met een toetsen-knoop waarvan
  // toon en poort plugin-parameters zijn, speelt hij MIDI: de schil zet noten om in die twee (plugin/src/plugin.cpp).
  const instrument = !ontwerp.knopen.some((k) => k.type === 'in');
  const toetsen = ontwerp.knopen.find((k) => k.type === 'toetsen');
  const pNoot = toetsen ? ontwerp.parameters.findIndex((p) => p.knoop === toetsen.id && p.knop === 'toon') : -1;
  const pPoort = toetsen ? ontwerp.parameters.findIndex((p) => p.knoop === toetsen.id && p.knop === 'poort') : -1;
  const noten = instrument && pNoot >= 0 && pPoort >= 0;
  const soortRegels = instrument
    ? `#define PB_INSTRUMENT 1\n#define PB_AU_TYPE "aumu"\n` + (noten ? `#define PB_NOTEN 1\n#define PB_NOOT_PARAM ${pNoot}\n#define PB_POORT_PARAM ${pPoort}\n` : '')
    : `#define PB_AU_TYPE "aufx"\n`;
  return `// keten_${id}.h — GEGENEREERD door tools/bouwpakket.mjs uit het ontwerp "${ontwerp.naam}". Niet met de hand wijzigen:
// verander het ontwerp in de tool en exporteer opnieuw. De rij hieronder is wat de ketenmotor (pb_graaf.h) leest,
// in de browser en in de plugin hetzelfde.
#pragma once
#include "../pb_core.h"

#define PB_NAAM ${cstr('PB ' + ontwerp.naam)}
#define PB_CLAP_ID ${cstr('nl.varve.pluginbouwer.' + id.replace(/_/g, '-'))}
#define PB_VERSIE ${cstr(versie)}
#define PB_MAKER ${cstr(maker)}
#define PB_UITLEG ${cstr(ontwerp.uitleg || '')}
#define PB_AU_MAKER "Vrve"
#define PB_AU_SUBTYPE ${cstr(au)}
${soortRegels}#define PB_LATENTIE ${latentie(ontwerp)}
#define PB_N_PARAMS ${c.params.length}
#define PB_PARAMS \\
${params.join(', \\\n')}

static constexpr double PB_KETEN[] = {
${rij.join(',\n')}
};
static constexpr int PB_KETEN_N = ${c.rij.length};

#include "../graaf_ontwerp.h"
`;
}
