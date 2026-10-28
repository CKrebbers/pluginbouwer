// template.mjs — Claude maakt een template voor de gedeelde templates van de tool (de database van het artifact). Bouwt
// het ontwerp uit een voorbeeld of een JSON-bestand plus blokken erbij, toetst dat het klinkt op de echte motor (geen
// NaN, niet te stil, niet boven 0 dBFS) en schrijft het document dat in `templates/<id>` hoort.
//   node tools/template.mjs --van dub_echo --erbij galm --zet tape.drive=4 --naam "Dub echo + Reverb" --map m-dub \
//        [--notitie "…"] [--id t-…] [--uit pad.json]
//   --van      id van een voorbeeld (tool/src/archetypen.js) of een JSON-bestand met een ontwerp (of een template)
//   --erbij    type (bv. galm) of recept:<id>, eventueel @<blok-id> = direct achter dat blok; mag vaker
//   --zet      <blok-id>.<knop>=<getal>: de stand van een knop (of de standaard van de parameter op die knop); mag vaker
import { readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compileer } from '../tool/src/keten.js';
import { ARCHETYPEN, GROOVE } from '../tool/src/archetypen.js';
import { voegIn, ordenen, overlap, blokMaat } from '../tool/src/rollen.js';
import { motorInstantie, renderKeten } from '../tool/src/meten.js';
import { notenProef } from '../tool/src/spel.js';
import { nieuweTemplate, bewaarVorm, normaliseerTemplate } from '../tool/src/templates.js';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2), alle = (n) => args.flatMap((a, i) => (a === n ? [args[i + 1]] : [])), een = (n) => alle(n)[0];
const stop = (t) => { console.error('template: ' + t); process.exit(1); };

const van = een('--van') ?? stop('--van ontbreekt');
let o = ARCHETYPEN.find((a) => a.id === van);
if (!o) { const j = JSON.parse(readFileSync(van, 'utf8')); o = j.ontwerp ?? j; }
o = JSON.parse(JSON.stringify(o));
for (const e of alle('--erbij')) {
  const [wat, na] = e.split('@');
  const r = voegIn(o, wat.startsWith('recept:') ? { recept: wat.slice(7) } : { type: wat }, { na: na || null });
  o = r.ontwerp; console.error('  ' + r.wat);
}
for (const z of alle('--zet')) {
  const m = /^([\w-]+)\.([\w-]+)=(-?[\d.]+)$/.exec(z) ?? stop('--zet ' + z + ': verwacht blok.knop=getal');
  const kn = o.knopen.find((k) => k.id === m[1]) ?? stop('geen blok ' + m[1]);
  const p = o.parameters.find((q) => q.knoop === m[1] && q.knop === m[2]);
  if (p) p.standaard = +m[3]; else (kn.waarden ??= {})[m[2]] = +m[3];
}
if (overlap(o, blokMaat)) o = ordenen(o, blokMaat);
const c = compileer(o);
if (c.fouten.length) stop('het ontwerp compileert niet: ' + c.fouten.join('; '));

// klinkt het? effecten op de groove, instrumenten met vier noten (of vanzelf, als ze een eigen ritme hebben)
const SR = 48000, sec = 8;
const E = motorInstantie(new WebAssembly.Module(readFileSync(join(ROOT, 'dsp/wasm/motor/graaf.wasm'))));
const ingang = o.knopen.some((k) => k.type === 'in');
const invoer = ingang ? renderKeten(E, compileer(GROOVE).rij, [new Float32Array(SR * sec), new Float32Array(SR * sec)]).uit : [new Float32Array(SR * sec), new Float32Array(SR * sec)];
const waarden = Object.fromEntries(c.params.map((p, i) => [i, p.standaard]));
const y = renderKeten(E, c.rij, invoer, { sr: SR, waarden, gebeurtenissen: notenProef(o, c) }).uit;
let som = 0, piek = 0, nan = false;
for (const k of y) for (const v of k) { if (!Number.isFinite(v)) nan = true; som += v * v; piek = Math.max(piek, Math.abs(v)); }
const rmsDb = 10 * Math.log10(som / (y[0].length * y.length) + 1e-30), piekDb = 20 * Math.log10(piek + 1e-30);
console.error(`  klinkt: rms ${rmsDb.toFixed(1)} dBFS, piek ${piekDb.toFixed(1)} dBFS${nan ? ', NaN!' : ''}`);
if (nan || rmsDb < -40 || piekDb > 0) stop('klinkt niet goed genoeg voor een template (rms > −40, piek ≤ 0 dBFS, geen NaN)');

const t = nieuweTemplate(o, { naam: een('--naam') ?? o.naam, map: een('--map') ?? 'los', mappen: [{ id: een('--map') ?? 'los', naam: '', plek: 0 }], notitie: een('--notitie') ?? '', door: 'claude', ...(een('--id') ? { id: een('--id') } : {}) });
t.map = een('--map') ?? 'los';                  // de map bestaat in de database, niet hier
t.plek = +(een('--plek') ?? 1);
if (!normaliseerTemplate(bewaarVorm(t), t.id)) stop('de template haalt zijn eigen controle niet');
const uit = JSON.stringify({ doc_id: t.id, data: bewaarVorm(t) });
if (een('--uit')) writeFileSync(een('--uit'), uit); else console.log(uit);
