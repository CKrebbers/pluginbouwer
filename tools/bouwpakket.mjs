// bouwpakket.mjs — schrijft bouwpakket/<id>/ voor een archetype: wat de exportknop van de tool ook maakt.
//   node tools/bouwpakket.mjs dub_echo
// Vooraf (doet CI): sh dsp/bouw-wasm.sh keten_<id>, node tools/null.mjs maak keten_<id>, node --test tests/ (metingen).
// De map wordt eerst leeggemaakt (alleen bouwpakket/<id>, met een vaste naam).
import { writeFileSync, readFileSync, mkdirSync, existsSync, readdirSync, rmSync, chmodSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { ARCHETYPEN } from '../tool/src/archetypen.js';
import { bouwpakket, PAKKET_BRONNEN } from '../tool/src/bouwpakket.js';
import { ROOT } from '../tests/lib/pb.mjs';

const id = process.argv[2] || 'dub_echo';
const ontwerp = ARCHETYPEN.find((a) => a.id === id);
if (!ontwerp || !/^[a-z0-9_]+$/.test(id)) { console.error('onbekend archetype ' + id); process.exit(2); }
const lees = (p) => readFileSync(join(ROOT, p));
const bronnen = Object.fromEntries(Object.entries(PAKKET_BRONNEN).map(([van, naar]) => [naar, lees(van).toString('utf8')]));
const tsMap = join(ROOT, 'testsignalen', `keten_${id}`);
if (!existsSync(tsMap)) { console.error(`eerst: node tools/null.mjs maak keten_${id}`); process.exit(2); }
const testsignalen = {};
for (const f of readdirSync(tsMap)) if (f.endsWith('.wav') || f.endsWith('.json')) testsignalen[`testsignalen/${f}`] = readFileSync(join(tsMap, f));
for (const f of readdirSync(join(tsMap, 'pluginval'))) testsignalen[`testsignalen/pluginval/${f}`] = readFileSync(join(tsMap, 'pluginval', f));
const metingPad = join(ROOT, 'tests', 'uit', 'dubecho.json');
const metingen = existsSync(metingPad) && id === 'dub_echo' ? JSON.parse(readFileSync(metingPad, 'utf8')) : {};
const metingBlokken = JSON.parse(readFileSync(join(ROOT, 'tool', 'src', 'meting-blokken.json'), 'utf8'));
const extra = { [`wasm/keten_${id}.wasm`]: lees(`dsp/wasm/keten_${id}.wasm`) };
const pak = bouwpakket(ontwerp, { metingen, metingBlokken, bronnen, testsignalen, extra });
const doel = join(ROOT, 'bouwpakket', id);
rmSync(doel, { recursive: true, force: true });
for (const [pad, inhoud] of Object.entries(pak)) {
  mkdirSync(dirname(join(doel, pad)), { recursive: true });
  writeFileSync(join(doel, pad), inhoud);
  if (pad.endsWith('.sh')) chmodSync(join(doel, pad), 0o755);
}
console.log(`${Object.keys(pak).length} bestanden in ${doel}`);
