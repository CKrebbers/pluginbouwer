// bouwpakket.mjs — schrijft bouwpakket/<id>/ voor een archetype: wat de exportknop van de tool ook maakt.
//   node tools/bouwpakket.mjs dub_echo
// Vooraf (doet CI): sh dsp/bouw-wasm.sh graaf, node --test tests/ (de metingen in tests/uit/).
// De map wordt eerst leeggemaakt (alleen bouwpakket/<id>, met een vaste naam).
import { writeFileSync, readFileSync, mkdirSync, existsSync, rmSync, chmodSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { ARCHETYPEN } from '../tool/src/archetypen.js';
import { bouwpakket, PAKKET_BRONNEN } from '../tool/src/bouwpakket.js';
import { compileer, volgorde } from '../tool/src/keten.js';
import { maakTestsignalen } from '../tool/src/testsignalen.js';
import { renderKeten } from '../tool/src/meten.js';
import { ROOT } from '../tests/lib/pb.mjs';

const id = process.argv[2] || 'dub_echo';
const ontwerp = ARCHETYPEN.find((a) => a.id === id);
if (!ontwerp || !/^[a-z0-9_]+$/.test(id)) { console.error('onbekend archetype ' + id); process.exit(2); }
const lees = (p) => readFileSync(join(ROOT, p));
const bronnen = Object.fromEntries(Object.entries(PAKKET_BRONNEN).map(([van, naar]) => [naar, lees(van).toString('utf8')]));
// testsignalen op de ketenmotor, met dezelfde functie als de exportknop van de tool
const motorBytes = lees('dsp/wasm/motor/graaf.wasm');
const e = new WebAssembly.Instance(new WebAssembly.Module(motorBytes), {}).exports;
const c = compileer(ontwerp);
const testsignalen = maakTestsignalen(e, c.rij, c.params, ontwerp, { terugkoppeling: volgorde(ontwerp).terug.length > 0, render: renderKeten });
const metingPad = join(ROOT, 'tests', 'uit', 'dubecho.json');
const metingen = existsSync(metingPad) && id === 'dub_echo' ? JSON.parse(readFileSync(metingPad, 'utf8')) : {};
const metingBlokken = JSON.parse(readFileSync(join(ROOT, 'tool', 'src', 'meting-blokken.json'), 'utf8'));
const extra = { 'wasm/graaf.wasm': motorBytes, 'wasm/keten.json': JSON.stringify({ rij: Array.from(c.rij), params: c.params }, null, 1) + '\n' };
const uitslagPad = join(ROOT, 'tools', 'pakket', 'uitslagen.json');   // pass@3 per fase, uit de verse sessies van fase E
const uitslagen = existsSync(uitslagPad) ? JSON.parse(readFileSync(uitslagPad, 'utf8'))[id] || {} : {};
const pak = bouwpakket(ontwerp, { metingen, metingBlokken, bronnen, testsignalen, extra, uitslagen });
const doel = join(ROOT, 'bouwpakket', id);
rmSync(doel, { recursive: true, force: true });
for (const [pad, inhoud] of Object.entries(pak)) {
  mkdirSync(dirname(join(doel, pad)), { recursive: true });
  writeFileSync(join(doel, pad), inhoud);
  if (pad.endsWith('.sh')) chmodSync(join(doel, pad), 0o755);
}
console.log(`${Object.keys(pak).length} bestanden in ${doel}`);
