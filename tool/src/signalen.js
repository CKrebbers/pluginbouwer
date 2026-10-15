// signalen.js — testsignalen, deterministisch (eigen zaad, geen Math.random). Gedeeld door de tool, de toetsen en de export.
export function sinus(f, sr, sec, amp = 0.5) {
  const n = Math.round(sr * sec), x = new Float32Array(n);
  for (let i = 0; i < n; i++) x[i] = amp * Math.sin(2 * Math.PI * f * i / sr);
  return x;
}
export function impuls(n, amp = 1) { const x = new Float32Array(n); x[0] = amp; return x; }
export function ruis(sr, sec, amp = 0.25, zaad = 12345) {
  const n = Math.round(sr * sec), x = new Float32Array(n);
  let s = zaad >>> 0 || 1;
  for (let i = 0; i < n; i++) { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; x[i] = amp * ((s / 4294967296) * 2 - 1); }
  return x;
}
// exponential sine sweep (Farina 2000): x(t) = A sin(2π f1 L (e^{t/L} − 1)), L = T / ln(f2/f1)
export function sweep(f1, f2, sr, sec, amp = 0.5) {
  const n = Math.round(sr * sec), x = new Float32Array(n), L = sec / Math.log(f2 / f1);
  for (let i = 0; i < n; i++) x[i] = amp * Math.sin(2 * Math.PI * f1 * L * (Math.exp(i / sr / L) - 1));
  return x;
}
// een "muziekachtig" stuk: tonen met omhullenden en stiltes, om een echo-staart te zien
export function stukjes(sr, sec, amp = 0.4) {
  const n = Math.round(sr * sec), x = new Float32Array(n);
  const noten = [220, 330, 262, 392, 196];
  for (let k = 0; k < noten.length; k++) {
    const begin = Math.round(k * 0.35 * sr), len = Math.round(0.12 * sr);
    for (let i = 0; i < len && begin + i < n; i++) {
      const env = Math.exp(-i / (0.03 * sr));
      x[begin + i] += amp * env * Math.sin(2 * Math.PI * noten[k] * i / sr);
    }
  }
  return x;
}
export const stereo = (x, y = x) => [x, y];
