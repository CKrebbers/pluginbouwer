// keten-header.mjs — schrijft dsp/ontwerpen/keten_<id>.h voor een archetype (of een ontwerp-JSON).
//   node tools/keten-header.mjs dub_echo          (een archetype uit tool/src/archetypen.js)
//   node tools/keten-header.mjs pad/naar/ontwerp.json
import { writeFileSync, readFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { ketenHeader } from '../tool/src/codegen.js';
import { ARCHETYPEN } from '../tool/src/archetypen.js';
import { ROOT } from '../tests/lib/pb.mjs';
const arg = process.argv[2];
const ontwerp = existsSync(arg || '') ? JSON.parse(readFileSync(arg, 'utf8')) : ARCHETYPEN.find((a) => a.id === arg);
if (!ontwerp) { console.error('onbekend ontwerp: ' + arg); process.exit(2); }
const pad = join(ROOT, 'dsp', 'ontwerpen', `keten_${ontwerp.id}.h`);
writeFileSync(pad, ketenHeader(ontwerp));
console.log(pad);
