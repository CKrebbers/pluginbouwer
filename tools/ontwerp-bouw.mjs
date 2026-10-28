// ontwerp-bouw.mjs — gedeeld door tools/template.mjs en tools/werkbank.mjs: een ontwerp aanpassen vanaf de opdrachtregel
// (blokken erbij of eruit, knoppen zetten) en toetsen dat het klinkt op de echte motor.
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { BIB, compileer } from '../tool/src/keten.js';
import { GROOVE } from '../tool/src/archetypen.js';
import { voegIn, ordenen, overlap, blokMaat } from '../tool/src/rollen.js';
import { motorInstantie, renderKeten } from '../tool/src/meten.js';
import { notenProef } from '../tool/src/spel.js';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
export const stop = (t) => { console.error(t); process.exit(1); };
export function opties(args) {
  const alle = (n) => args.flatMap((a, i) => (a === n ? [args[i + 1]] : [])), een = (n) => alle(n)[0];
  return { alle, een };
}

// --erbij <type|recept:id>[@blok], --weg <blok>, --zet <blok>.<knop>=<getal>; geeft het nieuwe ontwerp en wat er gebeurde
export function pasAan(ontwerp, { erbij = [], weg = [], zet = [] }) {
  let o = JSON.parse(JSON.stringify(ontwerp));
  const log = [];
  for (const id of weg) {
    const kn = o.knopen.find((k) => k.id === id) ?? stop('geen blok ' + id);
    if (BIB[kn.type].vast) stop(id + ' is de in- of uitgang');
    // wat erin en eruit ging, sluit weer op elkaar aan (de eerste geluidskabel erin op elke kabel eruit)
    const erin = o.kabels.find((k) => k.naar[0] === id), eruit = o.kabels.filter((k) => k.van[0] === id);
    o.knopen = o.knopen.filter((k) => k.id !== id);
    o.kabels = o.kabels.filter((k) => k.van[0] !== id && k.naar[0] !== id);
    if (erin) for (const k of eruit) if (!o.kabels.some((x) => x.naar.join() === k.naar.join())) o.kabels.push({ van: erin.van, naar: k.naar });
    o.parameters = o.parameters.filter((p) => p.knoop !== id);
    o.modulaties = (o.modulaties || []).filter((m) => m.bron[0] !== id && m.doel[0] !== id);
    log.push(BIB[kn.type].naam + ' eruit');
  }
  for (const e of erbij) {
    const [wat, na] = e.split('@');
    const r = voegIn(o, wat.startsWith('recept:') ? { recept: wat.slice(7) } : { type: wat }, { na: na || null });
    o = r.ontwerp; log.push(r.wat);
  }
  for (const z of zet) {
    const m = /^([\w-]+)\.([\w-]+)=(-?[\d.]+)$/.exec(z) ?? stop('--zet ' + z + ': verwacht blok.knop=getal');
    const kn = o.knopen.find((k) => k.id === m[1]) ?? stop('geen blok ' + m[1]);
    const v = Math.fround(+m[3]);   // de plugin geeft float door: alles exact in float32
    const p = o.parameters.find((q) => q.knoop === m[1] && q.knop === m[2]);
    if (p) p.standaard = v; else (kn.waarden ??= {})[m[2]] = v;
    log.push(`${m[1]}.${m[2]} = ${v}`);
  }
  if (overlap(o, blokMaat)) o = ordenen(o, blokMaat);
  return { ontwerp: o, log };
}

// Klinkt het? Effecten op de groove, instrumenten met vier noten (of vanzelf, met een eigen ritme). 8 s op 48 kHz.
let E = null;
export function klinkt(o, { sec = 8 } = {}) {
  const c = compileer(o);
  if (c.fouten.length) return { ok: false, waarom: c.fouten.join('; ') };
  const SR = 48000;
  E ??= motorInstantie(new WebAssembly.Module(readFileSync(join(ROOT, 'dsp/wasm/motor/graaf.wasm'))));
  const leeg = () => [new Float32Array(SR * sec), new Float32Array(SR * sec)];
  const invoer = o.knopen.some((k) => k.type === 'in') ? renderKeten(E, compileer(GROOVE).rij, leeg()).uit : leeg();
  const waarden = Object.fromEntries(c.params.map((p, i) => [i, p.standaard]));
  const y = renderKeten(E, c.rij, invoer, { sr: SR, waarden, gebeurtenissen: notenProef(o, c) }).uit;
  let som = 0, piek = 0, nan = false;
  for (const k of y) for (const v of k) { if (!Number.isFinite(v)) nan = true; som += v * v; piek = Math.max(piek, Math.abs(v)); }
  const rmsDb = 10 * Math.log10(som / (y[0].length * y.length) + 1e-30), piekDb = 20 * Math.log10(piek + 1e-30);
  const ok = !nan && rmsDb >= -40 && piekDb <= 0;
  return { ok, rmsDb, piekDb, nan, waarom: ok ? '' : 'klinkt niet goed genoeg (rms > −40, piek ≤ 0 dBFS, geen NaN)' };
}
