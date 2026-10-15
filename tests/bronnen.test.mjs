// De blokken van 15 okt 2026 (dsp/pb_bronnen.h) tegen de theorie, en elk voorbeeld in de tool: klinkt, ontploft niet,
// hangt niet af van de blokgrootte. Referenties staan bij elke toets; de grenzen hebben een reden (CLAUDE.md regel 5).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { ROOT, nullTest } from './lib/pb.mjs';
import { render, renderOntwerp, stilte, niveau, groove, heeftIngang } from './lib/keten.mjs';
import { compileer, PATRONEN, BIB, SOORT } from '../tool/src/keten.js';
import { ARCHETYPEN, INSTRUMENTEN, EFFECTEN } from '../tool/src/archetypen.js';
import { spectrum } from './lib/fft.mjs';
import { aliasing } from '../tool/src/meten.js';

const SR = 48000, N = 1 << 16;
const db = (x) => 20 * Math.log10(x);
// klein ontwerp: blokken, kabels, parameters (in de volgorde van de waarden die render() zet)
const ontwerp = (knopen, kabels, parameters = [], modulaties = []) => ({ naam: 't', knopen, kabels, parameters, modulaties });
const kabel = (a, p, b, i) => ({ van: [a, p], naar: [b, i] });
function maak(o) { const c = compileer(o); assert.deepEqual(c.fouten, [], c.fouten.join('; ')); return c; }
const binFreq = (k) => k * SR / N;

test('de patronen in keten.js zijn die van pb_bronnen.h (aantal en volgorde van de bijzondere)', () => {
  const h = readFileSync(join(ROOT, 'dsp', 'pb_bronnen.h'), 'utf8');
  assert.equal(PATRONEN.length, Number(h.match(/N_PATRONEN = (\d+)/)[1]));
  assert.equal(PATRONEN.indexOf('aanhouden (drone)'), Number(h.match(/PATROON_AANHOUDEN = (\d+)/)[1]));
  assert.equal(PATRONEN.indexOf('toeval (pentatonisch)'), Number(h.match(/PATROON_TOEVAL = (\d+)/)[1]));
});

test('elk blok in de BIB heeft een soort in de motor en hooguit 4 knoppen, 2 ingangen en 3 uitgangen', () => {
  for (const [type, b] of Object.entries(BIB)) {
    assert.ok(type in SOORT, type);
    assert.ok(b.knoppen.length <= 4 && b.in.length <= 2 && b.uit.length <= 3, type);
    // exact in float32: dan mag elke knop met "in plugin" een parameter worden (een AU geeft float door)
    for (const k of b.knoppen) for (const v of [k.min, k.max, k.std]) assert.equal(Math.fround(v), v, `${type}.${k.id}: ${v} exact in float32`);
  }
});

// ---- oscillator ---------------------------------------------------------------------------------------------------
function osc(golf, f, { sec = (N + 4096) / SR, pw = 0.5 } = {}) {
  const c = maak(ontwerp([{ id: 'o', type: 'osc', waarden: { freq: f, vorm: pw }, vast: { golf } }, { id: 'uit', type: 'uit' }], [kabel('o', 'uit', 'uit', 'in')]));
  return render(c.rij, stilte(SR, sec, 1))[0].subarray(4096, 4096 + N);   // eerst inlopen
}
test('oscillator sinus: de toon ligt op zijn bin en heeft geen boventonen (< −100 dB)', () => {
  const k = 1365, p = spectrum(osc(0, binFreq(k)));
  let top = 0; for (let b = 1; b < p.length; b++) if (p[b] > p[top]) top = b;
  assert.equal(top, k);
  for (let h = 2; h <= 5; h++) assert.ok(10 * Math.log10(p[h * k] / p[k]) < -100, `H${h}`);
});
test('oscillator zaag en blok: boventonen volgen de Fourier-reeks (zaag 1/n, blok alleen oneven 1/n), ±0,3 dB tot H7', () => {
  const k = 137;   // ≈ 100 Hz: polyBLEP verandert de lage boventonen nauwelijks
  const zaag = spectrum(osc(1, binFreq(k))), blok = spectrum(osc(2, binFreq(k)));
  for (let n = 2; n <= 7; n++) {
    const z = 10 * Math.log10(zaag[n * k] / zaag[k]), verwacht = db(1 / n);
    assert.ok(Math.abs(z - verwacht) < 0.3, `zaag H${n}: ${z.toFixed(2)} dB, theorie ${verwacht.toFixed(2)}`);
    const b = 10 * Math.log10(blok[n * k] / blok[k]);
    if (n % 2) assert.ok(Math.abs(b - verwacht) < 0.3, `blok H${n}: ${b.toFixed(2)} dB, theorie ${verwacht.toFixed(2)}`);
    else assert.ok(b < -60, `blok H${n} (even): ${b.toFixed(1)} dB`);
  }
});
test('oscillator zaag op 5 kHz: polyBLEP haalt minstens 10 dB aliasing weg t.o.v. een naïeve zaag (zelfde meting)', () => {
  const k = 6827, f = binFreq(k), y = osc(1, f), naief = new Float32Array(N);
  let fase = 0; for (let i = 0; i < 4096 + N; i++) { if (i >= 4096) naief[i - 4096] = 2 * fase - 1; fase += f / SR; if (fase >= 1) fase -= 1; }
  const a = aliasing(y, k), b = aliasing(naief, k);
  assert.ok(a < b - 10, `polyBLEP ${a.toFixed(1)} dB, naïef ${b.toFixed(1)} dB`);
});

// ---- ruis -----------------------------------------------------------------------------------------------------------
test('ruis: gemiddelde 0, wit heeft rms 1/√3, donker dezelfde rms (±10 %), links en rechts verschillen', () => {
  for (const kleur of [0, 0.8]) {
    const c = maak(ontwerp([{ id: 'r', type: 'ruis', waarden: { kleur } }, { id: 'uit', type: 'uit' }], [kabel('r', 'uit', 'uit', 'in')]));
    const [L, R] = render(c.rij, stilte(SR, 4));
    let m = 0, q = 0, lr = 0; for (let i = 0; i < L.length; i++) { m += L[i]; q += L[i] * L[i]; lr += L[i] * R[i]; }
    m /= L.length; const rms = Math.sqrt(q / L.length);
    assert.ok(Math.abs(m) < 0.01, `gemiddelde ${m}`);
    assert.ok(Math.abs(rms / (1 / Math.sqrt(3)) - 1) < (kleur ? 0.1 : 0.02), `kleur ${kleur}: rms ${rms.toFixed(3)}`);
    assert.ok(Math.abs(lr / q) < 0.1, `correlatie L/R ${(lr / q).toFixed(3)}`);
  }
});

// ---- sequencer ----------------------------------------------------------------------------------------------------
function seq(patroon, { tempo = 120, lengte = 0.5, zwaai = 0, poort = 'poort', sec = 8, zaad = 1 } = {}) {
  const c = maak(ontwerp([{ id: 'osc', type: 'osc' }, { id: 's', type: 'sequencer', waarden: { tempo, lengte, zwaai }, vast: { patroon, zaad } }, { id: 'uit', type: 'uit' }],
    [kabel('s', poort, 'uit', 'in')]));
  return render(c.rij, stilte(SR, sec, 1))[0];
}
const flanken = (x) => { const u = []; let v = 0; for (let i = 0; i < x.length; i++) { if (x[i] > 0.5 && v <= 0.5) u.push(i); v = x[i]; } return u; };
test('sequencer: "vier op de vloer" op 120 BPM = 2 slagen per seconde, op de sample', () => {
  const f = flanken(seq(0));
  assert.equal(f.length, 16);
  for (let i = 0; i < f.length; i++) assert.ok(Math.abs(f[i] - i * SR / 2) <= 1, `slag ${i} op sample ${f[i]}`);
});
test('sequencer: zestienden = 8 per seconde; swing schuift de oneven stappen op', () => {
  assert.equal(flanken(seq(3, { sec: 4 })).length, 32);
  const f = flanken(seq(3, { sec: 1, zwaai: 0.25 })), stap = SR / 8;
  assert.ok(Math.abs(f[1] - 1.25 * stap) <= 1 && Math.abs(f[2] - 2 * stap) <= 1, `swing: ${f.slice(0, 3)}`);
});
test('sequencer: de toon van het arpeggio is 2^(st/12) (0, 3, 7, 12 …)', () => {
  const t = seq(6, { poort: 'toon', sec: 1 }), stap = SR / 8;
  [0, 3, 7, 12, 7, 3, 0, 3].forEach((st, i) => assert.equal(t[Math.round(i * stap + 10)], Math.fround(2 ** (st / 12)), `stap ${i}`));
});
test('sequencer: toeval is deterministisch per zaad, en een ander zaad geeft een ander patroon', () => {
  const a = seq(11, { poort: 'toon', sec: 2, zaad: 3 }), b = seq(11, { poort: 'toon', sec: 2, zaad: 3 }), c = seq(11, { poort: 'toon', sec: 2, zaad: 4 });
  assert.equal(nullTest([a], [b]).piekDb, -Infinity);
  assert.notEqual(nullTest([a], [c]).piekDb, -Infinity);
});

// ---- omhulling (met toetsen als poort: parameters 0 = toon, 1 = poort) --------------------------------------------
function omhulling(w, gebeurtenissen, sec) {
  const c = maak(ontwerp([{ id: 't', type: 'toetsen' }, { id: 'osc', type: 'osc' }, { id: 'e', type: 'omhulling', waarden: w }, { id: 'uit', type: 'uit' }],
    [kabel('t', 'poort', 'e', 'poort'), kabel('e', 'omhulling', 'uit', 'in')],
    [{ naam: 'Noot', knoop: 't', knop: 'toon', min: 0.25, max: 4, standaard: 1, glijMs: 5 }, { naam: 'Poort', knoop: 't', knop: 'poort', min: 0, max: 1, standaard: 0, glijMs: 0 }]));
  return render(c.rij, stilte(SR, sec, 1), { gebeurtenissen })[0];
}
test('omhulling: aanzet lineair in precies aanzet·sr samples, verval naar aanhouden, loslaten tot 1 % in de loslaattijd', () => {
  const y = omhulling({ aanzet: 10, verval: 100, aanhoud: 0.5, loslaten: 200 }, [[0, 1, 1], [SR, 1, 0]], 1.5);
  const n = 0.010 * SR;
  assert.ok(y[n - 2] < 1 && y[n - 1] === 1, `de top hoort op sample ${n - 1} te liggen: ${y[n - 2]}, ${y[n - 1]}`);
  assert.ok(Math.abs(y[n + 0.1 * SR] - 0.5) <= 0.01 * 0.5, `na het verval ${y[n + 0.1 * SR]}`);
  assert.ok(y[SR + 0.2 * SR] <= 0.5 * 0.0101 && y[SR + 0.2 * SR] > 0, `na het loslaten ${y[SR + 0.2 * SR]}`);
});

// ---- snaar -------------------------------------------------------------------------------------------------------
test('snaar: de toonhoogte klopt binnen 0,5 % (Jaffe & Smith: lusvertraging = sr/f)', () => {
  for (const f of [110, 220, 440, 880]) {
    const c = maak(ontwerp([{ id: 's', type: 'sequencer', vast: { patroon: 10 } }, { id: 'n', type: 'snaar', waarden: { freq: f, verval: 8, helder: 0.5 } }, { id: 'uit', type: 'uit' }],
      [kabel('s', 'poort', 'n', 'poort'), kabel('n', 'uit', 'uit', 'in')]));
    const x = render(c.rij, stilte(SR, 2, 1))[0].subarray(SR / 2, SR / 2 + N);
    // de grondtoon: de hoogste piek binnen ±10 % van f (de boventonen kunnen sterker zijn dan de grondtoon)
    const p = spectrum(x), b0 = Math.floor(0.9 * f * N / SR), b1 = Math.ceil(1.1 * f * N / SR);
    let top = b0; for (let b = b0; b <= b1; b++) if (p[b] > p[top]) top = b;
    const a = Math.log(p[top - 1]), b = Math.log(p[top]), g = Math.log(p[top + 1]);
    const fGemeten = (top + 0.5 * (a - g) / (a - 2 * b + g)) * SR / N;
    assert.ok(Math.abs(fGemeten / f - 1) < 0.005, `${f} Hz: gemeten ${fGemeten.toFixed(2)} Hz`);
  }
});

// ---- trommel -----------------------------------------------------------------------------------------------------
test('trommel: piek ≤ 1, na 5× het verval meer dan 60 dB stiller, en de toon zakt (eerst sneller door nul)', () => {
  const c = maak(ontwerp([{ id: 's', type: 'sequencer', waarden: { tempo: 40 }, vast: { patroon: 0 } }, { id: 't', type: 'trommel', waarden: { freq: 50, val: 3, verval: 200, ruis: 0 } }, { id: 'uit', type: 'uit' }],
    [kabel('s', 'poort', 't', 'poort'), kabel('t', 'uit', 'uit', 'in')]));
  const x = render(c.rij, stilte(SR, 1.4, 1))[0];
  const n = niveau([x]);
  assert.ok(n.piekDb <= 0, `piek ${n.piekDb}`);
  assert.ok(niveau([x.subarray(SR, SR + 0.1 * SR)]).piekDb < -60, 'na 1 s nog geluid');
  const nul = (a, b) => { let t = 0; for (let i = a + 1; i < b; i++) if ((x[i - 1] < 0) !== (x[i] < 0)) t++; return t; };
  assert.ok(nul(0, 2400) > nul(SR / 4, SR / 4 + 2400), 'de toon zakt niet');
});

// ---- vermenigvuldig, panner, vergruizer ---------------------------------------------------------------------------
test('vermenigvuldig: a × b (float32-afronding als enige verschil); zonder b gaat a ongewijzigd door', () => {
  const a = osc(0, 440, { sec: 1 });
  const c = maak(ontwerp([{ id: 'o', type: 'osc', waarden: { freq: 440 }, vast: { golf: 0 } }, { id: 'v', type: 'vermenigvuldig' }, { id: 'uit', type: 'uit' }],
    [kabel('o', 'uit', 'v', 'a'), kabel('o', 'uit', 'v', 'b'), kabel('v', 'uit', 'uit', 'in')]));
  const y = render(c.rij, stilte(SR, (N + 4096) / SR, 1))[0].subarray(4096, 4096 + N);
  for (let i = 0; i < 1000; i++) assert.ok(Math.abs(y[i] - a[i] * a[i]) < 1e-6, `sample ${i}`);
  const c2 = maak(ontwerp([{ id: 'in', type: 'in' }, { id: 'v', type: 'vermenigvuldig' }, { id: 'uit', type: 'uit' }], [kabel('in', 'uit', 'v', 'a'), kabel('v', 'uit', 'uit', 'in')]));
  const g = groove(SR, 1);
  assert.equal(nullTest(render(c2.rij, g), g).piekDb, -Infinity);
});
test('panner: midden = −3 dB per kant (gelijk vermogen), helemaal links = alleen links', () => {
  const ruisIn = [Float32Array.from({ length: 4800 }, (_, i) => Math.sin(i * 0.05) * 0.5)];
  const p = (positie) => render(maak(ontwerp([{ id: 'in', type: 'in' }, { id: 'p', type: 'panner', waarden: { positie } }, { id: 'uit', type: 'uit' }],
    [kabel('in', 'uit', 'p', 'in'), kabel('p', 'uit', 'uit', 'in')])).rij, [ruisIn[0], ruisIn[0]]);
  const [L, R] = p(0), [L2, R2] = p(-1);
  for (const i of [10, 100, 1000]) {
    assert.ok(Math.abs(L[i] - ruisIn[0][i] * Math.SQRT1_2) < 1e-6 && Math.abs(R[i] - ruisIn[0][i] * Math.SQRT1_2) < 1e-6, `midden ${i}`);
    assert.ok(Math.abs(L2[i] - ruisIn[0][i]) < 1e-6 && Math.abs(R2[i]) < 1e-6, `links ${i}`);
  }
});
test('vergruizer: 3 bits = treden van 1/4; samplerate ÷ 4 = hooguit elke 4 samples een nieuwe waarde', () => {
  const g = groove(SR, 1);
  const y = render(maak(ontwerp([{ id: 'in', type: 'in' }, { id: 'v', type: 'vergruizer', waarden: { bits: 3, deling: 4 } }, { id: 'uit', type: 'uit' }],
    [kabel('in', 'uit', 'v', 'in'), kabel('v', 'uit', 'uit', 'in')])).rij, g)[0];
  let wissel = 0;
  for (let i = 0; i < y.length; i++) {
    assert.equal(y[i] * 4, Math.round(y[i] * 4), `sample ${i}: ${y[i]}`);
    if (i && y[i] !== y[i - 1]) { assert.ok(i % 4 === 3, `wissel op ${i}`); wissel++; }   // de eerste nieuwe waarde op sample 3
  }
  assert.ok(wissel > 100);
});

// ---- chorus, galm, compressor -------------------------------------------------------------------------------------
test('chorus zonder diepte en terugkoppeling = een zuivere vertraging van tijd·sr + 1 samples (Lagrange op een hele sample is exact)', () => {
  const g = groove(SR, 1);
  const y = render(maak(ontwerp([{ id: 'in', type: 'in' }, { id: 'c', type: 'chorus', waarden: { tijd: 10, diepte: 0, snelheid: 1, terug: 0 } }, { id: 'uit', type: 'uit' }],
    [kabel('in', 'uit', 'c', 'in'), kabel('c', 'uit', 'uit', 'in')])).rij, g);
  for (let i = 481; i < SR; i += 97) assert.equal(y[0][i], g[0][i - 481], `sample ${i}`);
});
test('galm: de nagalmtijd (Schroeder-integraal, −5 tot −35 dB) ligt binnen 15 % van de knop', () => {
  for (const t60 of [1, 3]) {
    const x = stilte(SR, t60 * 1.6 + 0.2, 1); x[0][0] = 1;
    const y = render(maak(ontwerp([{ id: 'in', type: 'in' }, { id: 'g', type: 'galm', waarden: { tijd: t60, demping: 18000, grootte: 1 } }, { id: 'uit', type: 'uit' }],
      [kabel('in', 'uit', 'g', 'in'), kabel('g', 'uit', 'uit', 'in')])).rij, x)[0];
    const e = new Float64Array(y.length); let s = 0;
    for (let i = y.length - 1; i >= 0; i--) { s += y[i] * y[i]; e[i] = s; }
    const dbE = (i) => 10 * Math.log10(e[i] / e[0]);
    let a = 0; while (dbE(a) > -5) a++;
    let b = a; while (dbE(b) > -35) b++;
    const gemeten = 2 * (b - a) / SR;   // 30 dB in (b − a) samples → 60 dB
    assert.ok(Math.abs(gemeten / t60 - 1) < 0.15, `knop ${t60} s, gemeten ${gemeten.toFixed(2)} s`);
  }
});
test('compressor: statische kromme (drempel −20, 4:1): −10 dBFS in → −17,5 dBFS uit (±0,5 dB); zijketen stil = niets', () => {
  const k = 1365, x = Float32Array.from({ length: 2 * SR }, (_, i) => 10 ** (-10 / 20) * Math.sin(2 * Math.PI * binFreq(k) * i / SR));
  const comp = (zijketen) => maak(ontwerp([{ id: 'in', type: 'in' }, { id: 'osc', type: 'osc', waarden: { freq: 1 } }, { id: 'nul', type: 'versterker', waarden: { hoeveel: 0 } },
    { id: 'c', type: 'compressor', waarden: { drempel: -20, verhouding: 4, aanzet: 0.1, loslaten: 2000 } }, { id: 'uit', type: 'uit' }],
  [kabel('in', 'uit', 'c', 'in'), ...(zijketen ? [kabel('osc', 'uit', 'nul', 'in'), kabel('nul', 'uit', 'c', 'zijketen')] : []), kabel('c', 'uit', 'uit', 'in')]));
  const y = render(comp(false).rij, [x])[0];
  const piek = niveau([y.subarray(SR)]).piekDb;
  assert.ok(Math.abs(piek - -17.5) < 0.5, `uit ${piek.toFixed(2)} dBFS`);
  const z = render(comp(true).rij, [x])[0];
  assert.equal(nullTest([z], [x]).piekDb, -Infinity, 'met een stille zijketen hoort hij niets te doen');
});

// ---- de motor: modulatie en schaal --------------------------------------------------------------------------------
test('een parameter met schaal ("ook") stuurt een tweede knop: osc2 = 2 × de grondtoon', () => {
  const o = ontwerp([{ id: 'a', type: 'osc', vast: { golf: 0 } }, { id: 'b', type: 'osc', vast: { golf: 0 } }, { id: 'uit', type: 'uit' }],
    [kabel('b', 'uit', 'uit', 'in')], [{ naam: 'Toon', knoop: 'a', knop: 'freq', min: 20, max: 2000, standaard: 100, glijMs: 0, ook: [['b', 'freq', 2]] }]);
  const c = maak(o), k = 1365;
  const y = render(c.rij, stilte(SR, (N + 4096) / SR, 1), { waarden: { 0: binFreq(k) / 2 } })[0].subarray(4096, 4096 + N);
  const p = spectrum(y); let top = 0; for (let b = 1; b < p.length; b++) if (p[b] > p[top]) top = b;
  assert.equal(top, k);
});
test('exponentiële modulatie: een LFO met diepte d op een frequentieknop beweegt ± d·log2(max/min) octaaf', () => {
  // LFO 1 Hz op een oscillator van 1 kHz; diepte zo dat het 1 octaaf is. Meet de hoogste en laagste momentane frequentie.
  const oct = Math.log2(8000 / 20);
  const c = maak(ontwerp([{ id: 'l', type: 'lfo', waarden: { snelheid: 1 } }, { id: 'o', type: 'osc', waarden: { freq: 1000 }, vast: { golf: 0 } }, { id: 'uit', type: 'uit' }],
    [kabel('o', 'uit', 'uit', 'in')], [], [{ bron: ['l', 'golf'], doel: ['o', 'freq'], diepte: 1 / oct }]));
  const y = render(c.rij, stilte(SR, 1, 1))[0];
  // momentane frequentie: de periode tussen twee opgaande nuldoorgangen (lineair geïnterpoleerd) rond de top (0,25 s)
  // en het dal (0,75 s) van de sinus-LFO
  const tel = (m) => {
    const nul = []; for (let i = m - 2000; i < m + 2000 && nul.length < 64; i++) if (y[i - 1] < 0 && y[i] >= 0) nul.push(i - 1 + y[i - 1] / (y[i - 1] - y[i]));
    let best = 1;
    for (let j = 2; j < nul.length; j++) if (Math.abs((nul[j] + nul[j - 1]) / 2 - m) < Math.abs((nul[best] + nul[best - 1]) / 2 - m)) best = j;
    return SR / (nul[best] - nul[best - 1]);
  };
  assert.ok(Math.abs(tel(SR / 4) / 2000 - 1) < 0.03, `top ${tel(SR / 4)} Hz`);
  assert.ok(Math.abs(tel(3 * SR / 4) / 500 - 1) < 0.03, `dal ${tel(3 * SR / 4)} Hz`);
});
test('de uitgang beveiligt: een keten die ontploft geeft stilte, geen NaN of oneindig', () => {
  // terugkoppeling ×2 door een vertraging: groeit tot oneindig
  const c = maak(ontwerp([{ id: 'in', type: 'in' }, { id: 's', type: 'som' }, { id: 'l', type: 'vertraging', waarden: { tijd: 5 }, vast: { max: 50 } }, { id: 'v', type: 'versterker', waarden: { hoeveel: 1.25 } }, { id: 'uit', type: 'uit' }],
    [kabel('in', 'uit', 's', 'a'), kabel('v', 'uit', 's', 'b'), kabel('s', 'uit', 'l', 'in'), kabel('l', 'uit', 'v', 'in'), kabel('l', 'uit', 'uit', 'in')]));
  const y = render(c.rij, groove(SR, 8));
  const n = niveau(y);
  assert.ok(!n.nietEindig && n.piekDb < 20 * Math.log10(8), `piek ${n.piekDb}`);
});

// ---- elk voorbeeld ------------------------------------------------------------------------------------------------
// Toetsenpad wordt bespeeld: acht noten van een halve seconde (parameter 0 = Noot, 1 = Poort).
const SPEEL = Array.from({ length: 8 }, (_, i) => [[i * SR, 0, 2 ** ([0, 3, 7, 10, 12, 7, 3, 0][i] / 12)], [i * SR, 1, 1], [i * SR + SR / 2, 1, 0]]).flat();
function renderVoorbeeld(a, sec = 8, blok = 128) {
  if (!a.knopen.some((k) => k.type === 'toetsen')) return renderOntwerp(a, { sec, blok });
  const c = compileer(a);
  return render(c.rij, stilte(SR, sec), { blok, gebeurtenissen: SPEEL, waarden: Object.fromEntries(c.params.map((p, i) => [i, p.standaard])) });
}
test('er zijn minstens 10 instrumenten en 10 effecten om mee te beginnen', () => {
  assert.ok(INSTRUMENTEN.length >= 10 && EFFECTEN.length >= 10, `${INSTRUMENTEN.length} instrumenten, ${EFFECTEN.length} effecten`);
  for (const a of INSTRUMENTEN) assert.ok(!heeftIngang(a), `${a.naam} heeft geen ingang nodig`);
});
for (const a of ARCHETYPEN) {
  test(`voorbeeld ${a.naam}: klinkt (rms > −35 dBFS), piek ≤ 0 dBFS, geen NaN, en blokgrootte doet er niet toe`, () => {
    assert.deepEqual(compileer(a).fouten, []);
    const y = renderVoorbeeld(a);
    const n = niveau(y, 4 * SR);
    assert.ok(!n.nietEindig, 'NaN of oneindig');
    assert.ok(n.rmsDb > -35, `te stil: ${n.rmsDb.toFixed(1)} dBFS`);
    assert.ok(n.piekDb <= 0, `te hard: ${n.piekDb.toFixed(1)} dBFS`);
    const basis = renderVoorbeeld(a, 2, 128);
    for (const blok of [7, 512, [3, 128, 1, 77, 4096]]) {
      const r = nullTest(basis, renderVoorbeeld(a, 2, blok));
      assert.equal(r.piekDb, -Infinity, `blok ${blok}: verschil vanaf sample ${r.eersteVerschil}`);
    }
  });
}
