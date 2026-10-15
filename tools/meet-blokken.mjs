// meet-blokken.mjs — CPU per blok, gemeten op de echte ketenmotor (WASM in Node, V8), voor de BIB.
//   node tools/meet-blokken.mjs   → tool/src/meting-blokken.json
// Per blok: ingang → blok → uitgang, 10 s stereo ruis op 48 kHz, blok 128; beste van 5 (minst gestoord).
// De ketenmotor zelf (lege keten ingang → uitgang) wordt eraf getrokken. Getal = % van één kern in realtime.
// Dit is V8/WASM op de meetmachine; native op een M1 is anders (meestal sneller). Zo staat het ook in de BIB.
import { writeFileSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import os from 'node:os';
import { compileer, BIB } from '../tool/src/keten.js';
import { ROOT } from '../tests/lib/pb.mjs';
import { ruis } from '../tests/lib/signalen.mjs';

const e = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(join(ROOT, 'dsp/wasm/motor/graaf.wasm'))), {}).exports;
const sr = 48000, sec = 10, x = [ruis(sr, sec, 0.3, 1), ruis(sr, sec, 0.3, 2)];
function tijd(ontwerp) {
  const c = compileer(ontwerp); if (c.fouten.length) throw new Error(c.fouten.join(';'));
  let best = Infinity;
  for (let r = 0; r < 5; r++) {
    const h = e.pg_maak(sr);
    new Float64Array(e.memory.buffer, e.pg_rij(), c.rij.length).set(c.rij);
    e.pg_laad(h, c.rij.length);
    const t0 = process.hrtime.bigint();
    for (let p = 0; p < x[0].length; p += 128) {
      for (let k = 0; k < 2; k++) new Float32Array(e.memory.buffer, e.pg_in(h, k), 128).set(x[k].subarray(p, p + 128));
      e.pg_verwerk(h, 2, 128);
    }
    best = Math.min(best, Number(process.hrtime.bigint() - t0) / 1e9);
    e.pg_weg(h);
  }
  return best;
}
const leeg = { knopen: [{ id: 'in', type: 'in' }, { id: 'uit', type: 'uit' }], kabels: [{ van: ['in', 'uit'], naar: ['uit', 'in'] }], parameters: [] };
const basis = tijd(leeg);
const uit = { gemeten: new Date().toISOString().slice(0, 10), machine: `${os.cpus()[0].model}, ${process.platform} ${process.arch}, Node ${process.version}`,
  methode: 'WASM-ketenmotor, 10 s stereo 48 kHz, blok 128, beste van 5, min de lege keten', motorProcent: (basis / sec) * 100, blokken: {} };
for (const type of Object.keys(BIB)) {
  if (type === 'in' || type === 'uit') continue;
  const def = BIB[type];
  const kn = [{ id: 'in', type: 'in' }, { id: 'b', type, vast: type === 'vertraging' ? { max: 1200 } : undefined }, { id: 'uit', type: 'uit' }];
  const kabels = [{ van: ['in', 'uit'], naar: ['uit', 'in'] }];
  if (def.in.length) kabels.push({ van: ['in', 'uit'], naar: ['b', def.in[0]] });
  // bewegende knop meemeten (de duurste stand: coëfficiënten elke sample) voor filters
  const t = tijd({ knopen: kn, kabels, parameters: [] });
  uit.blokken[type] = { cpuProcent: Math.max(0, ((t - basis) / sec) * 100) };
}
writeFileSync(join(ROOT, 'tool/src/meting-blokken.json'), JSON.stringify(uit, null, 2) + '\n');
console.log(JSON.stringify(uit, null, 1));
