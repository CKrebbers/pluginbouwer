// pb_math.h tegen Math.* van V8. Grens: ≤ 4 ULP relatief (sin/cos/exp/log), ≤ 1e-15 absoluut voor tanh/logcosh rond 0.
// Waarom niet 0 ULP: Math.* is zelf niet correct afgerond; we willen alleen weten dat onze benadering op
// fdlibm-niveau zit. Gelijkheid tussen WASM en native is een andere toets (de null-test).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { laadOntwerp } from './lib/pb.mjs';

const { e } = laadOntwerp();
const ulp = (x) => { const b = new Float64Array([Math.abs(x)]); const u = new BigUint64Array(b.buffer); u[0] += 1n; return b[0] - Math.abs(x) || Number.MIN_VALUE; };

function vergelijk(naam, f, g, xs, maxUlp, absTol = 0) {
  let slechtst = 0, waar = 0;
  for (const x of xs) {
    const a = f(x), b = g(x);
    const fout = Math.abs(a - b);
    if (fout <= absTol) continue;
    const u = fout / ulp(b);
    if (u > slechtst) { slechtst = u; waar = x; }
  }
  assert.ok(slechtst <= maxUlp, `${naam}: ${slechtst.toFixed(1)} ULP bij x=${waar}`);
  return slechtst;
}

const reeks = (a, b, n) => Array.from({ length: n }, (_, i) => a + (b - a) * (i + 0.5) / n);

test('sin en cos op [-4π, 4π]', () => {
  const xs = reeks(-4 * Math.PI, 4 * Math.PI, 20001);
  vergelijk('sin', e.pb_t_sin, Math.sin, xs, 4, 1e-300);
  vergelijk('cos', e.pb_t_cos, Math.cos, xs, 4, 1e-300);
});
test('exp op [-60, 60] en log op [1e-12, 1e6]', () => {
  vergelijk('exp', e.pb_t_exp, Math.exp, reeks(-60, 60, 20001), 4);
  vergelijk('log', e.pb_t_log, Math.log, reeks(-12, 6, 20001).map((t) => 10 ** t), 4);
  assert.equal(e.pb_t_exp(0), 1);
  assert.equal(e.pb_t_log(1), 0);
});
test('tanh en logcosh op [-30, 30]', () => {
  const xs = reeks(-30, 30, 20001).concat(reeks(-1e-3, 1e-3, 2001));
  vergelijk('tanh', e.pb_t_tanh, Math.tanh, xs, 8, 1e-16);
  vergelijk('logcosh', e.pb_t_logcosh, (x) => Math.log(Math.cosh(x)), xs.filter((x) => Math.abs(x) < 300), 64, 1e-15);
});
