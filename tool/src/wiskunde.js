// wiskunde.js — sin, cos, exp en log zonder Math.*: dezelfde algoritmen als dsp/pb_math.h, regel voor regel, met alleen
// + − × ÷ en floor (in IEEE 754 exact afgerond). Waarom: Math.sin en Math.exp liggen niet vast; de Chrome van de
// CI-runner gaf andere laatste bits dan Node, en dan maakt de tool andere testsignalen dan CI. tests/math.test.mjs
// vergelijkt deze functies bit voor bit met de WASM van pb_math.h.
const buf = new DataView(new ArrayBuffer(8));
const vanBits = (hoog, laag = 0) => { buf.setUint32(0, hoog >>> 0); buf.setUint32(4, laag >>> 0); return buf.getFloat64(0); };
const tweeMacht = (k) => vanBits((k + 1023) << 20);   // 2^k voor −1022 ≤ k ≤ 1023 (exponent-bits, zoals pb_math.h)

function kernSin(x) {
  const S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
    S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
    S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
  const z = x * x, v = z * x;
  const r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  return x + v * (S1 + z * r);
}
function kernCos(x) {
  const C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
    C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
    C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
  const z = x * x;
  const r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  const hz = 0.5 * z, w = 1.0 - hz;
  return w + (((1.0 - w) - hz) + z * r);
}
function reduceer(x) {
  const invpio2 = 6.36619772367581382433e-01;
  const pio2_1 = 1.57079632673412561417e+00, pio2_2 = 6.07710050630396597660e-11, pio2_3 = 2.02226624871116645580e-21;
  const k = Math.floor(x * invpio2 + 0.5);
  const r = ((x - k * pio2_1) - k * pio2_2) - k * pio2_3;
  return [r, ((k % 4) + 4) % 4];   // = ki & 3 voor een geheel getal (ook negatief, twee-complement)
}
export function sin_(x) {
  const [r, q] = reduceer(x);
  return q === 0 ? kernSin(r) : q === 1 ? kernCos(r) : q === 2 ? -kernSin(r) : -kernCos(r);
}
export function cos_(x) {
  const [r, q] = reduceer(x);
  return q === 0 ? kernCos(r) : q === 1 ? -kernSin(r) : q === 2 ? -kernCos(r) : kernSin(r);
}
export function exp_(x) {
  if (x !== x) return x;
  if (x > 709.78) return Infinity;
  if (x < -745.13) return 0.0;
  const ln2hi = 6.93147180369123816490e-01, ln2lo = 1.90821492927058770002e-10;
  const invln2 = 1.44269504088896338700e+00;
  const P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03,
    P3 = 6.61375632143793436117e-05, P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08;
  const k = Math.floor(x * invln2 + 0.5);
  const hi = x - k * ln2hi, lo = k * ln2lo, r = hi - lo;
  const t = r * r;
  const c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  let y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
  let ki = k;
  if (ki < -1021) { y *= tweeMacht(ki + 1000); return y * tweeMacht(-1000); }
  if (ki > 1023) { y *= 2.0; ki -= 1; }
  return y * tweeMacht(ki);
}
export function log_(x) {
  if (x !== x || x < 0) return NaN;
  if (x === 0) return -Infinity;
  if (x === Infinity) return x;
  const Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01, Lg3 = 2.857142874366239149e-01,
    Lg4 = 2.222219843214978396e-01, Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
    Lg7 = 1.479819860511658591e-01;
  const ln2hi = 6.93147180369123816490e-01, ln2lo = 1.90821492927058770002e-10;
  let k = 0;
  if (x < 2.2250738585072014e-308) { x *= 18014398509481984.0; k -= 54; }
  buf.setFloat64(0, x);
  const hoog = buf.getUint32(0), laag = buf.getUint32(4);
  k += ((hoog >>> 20) & 0x7ff) - 1023;
  let m = vanBits((hoog & 0x000fffff) | 0x3ff00000, laag);
  if (m > 1.41421356237309504880) { m *= 0.5; k += 1; }
  const f = m - 1.0;
  const s = f / (2.0 + f), z = s * s, w = z * z;
  const t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
  const t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
  const R = t2 + t1, hfsq = 0.5 * f * f;
  const dk = k;
  return dk * ln2hi - ((hfsq - (s * (hfsq + R) + dk * ln2lo)) - f);
}
