// matrix.mjs — de null-test van fase 5: elke samplerate (44,1–192 kHz), elke kanaalindeling (stereo 2 → 2, mono 1 → 1,
// mono → stereo 1 → 2), blokgrootte 1..4096, in-place, en de bypass. De verwachte uitvoer komt uit de ketenmotor van
// het pakket (wasm/graaf.wasm + wasm/keten.json); die moet eerst testsignalen/ bit voor bit nadoen, anders stopt alles.
//
//   node gereedschap/matrix.mjs maak <map>                                   invoer en verwachte uitvoer schrijven
//   node gereedschap/matrix.mjs clap <map> <pb-render> <plugin.clap>          alle renders door het .clap en vergelijken
//   node gereedschap/matrix.mjs pluginval <map> <pluginval> <plugin> <label>  pluginval test per samplerate (stereo)
//   node gereedschap/matrix.mjs vergelijk <map> <rendermap> <label>           renders van een ander gereedschap (pb-aurender)
//
// Namen: <map>/in/<geval>@<sr>.wav (stereo) en <geval>@<sr>@mono.wav (kanaal 0), <map>/verwacht/<geval>@<sr>@<indeling>.wav.
// Het geval "bypass" is echo-standaard met bypass-events (<map>/bypass@<sr>.txt, "sample 10 waarde", veelvouden van 64).
// Grens zoals vergelijk.mjs: piek ≤ −90 dBFS; zelfoscillatie statistisch (terts en omhullende binnen 1 dB).
import { readFileSync, writeFileSync, mkdirSync, existsSync, appendFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');
const T = join(PAKKET, 'testsignalen');
const GRENS_DB = -90;
export const SAMPLERATES = [44100, 48000, 88200, 96000, 176400, 192000];
export const INDELINGEN = ['stereo', 'mono', 'mono-stereo'];
// pluginval test: één blokgrootte per samplerate (samen 1..4096)
const PV_BLOK = { 44100: 4096, 48000: 512, 88200: 1, 96000: 77, 176400: 4096, 192000: 3 };
const BYPASS_ID = 10, OVERVLOEI_SEC = 0.010;

// --- WAV ----------------------------------------------------------------------------------------------------
function leesWav(pad) {
  const b = readFileSync(pad);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4), len = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { tag: b.readUInt16LE(p + 8), k: b.readUInt16LE(p + 10), sr: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22) };
    if (id === 'data') data = { off: p + 8, len };
    p += 8 + len + (len & 1);
  }
  const tag = fmt.tag === 0xfffe ? 3 : fmt.tag;
  if (tag !== 3 || fmt.bits !== 32) throw new Error(`${pad}: alleen 32-bit float`);
  const n = Math.floor(data.len / (4 * fmt.k));
  const k = Array.from({ length: fmt.k }, () => new Float32Array(n));
  for (let i = 0; i < n; i++) for (let c = 0; c < fmt.k; c++) k[c][i] = b.readFloatLE(data.off + (i * fmt.k + c) * 4);
  return { kanalen: k, sr: fmt.sr };
}
function schrijfWav(pad, kanalen, sr) {
  const K = kanalen.length, n = kanalen[0].length, data = n * K * 4, b = Buffer.alloc(44 + data);
  b.write('RIFF', 0); b.writeUInt32LE(36 + data, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(3, 20); b.writeUInt16LE(K, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * K * 4, 28);
  b.writeUInt16LE(K * 4, 32); b.writeUInt16LE(32, 34); b.write('data', 36); b.writeUInt32LE(data, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < K; c++) b.writeFloatLE(kanalen[c][i], 44 + (i * K + c) * 4);
  mkdirSync(dirname(pad), { recursive: true });
  writeFileSync(pad, b);
}

// --- de ketenmotor van het pakket ----------------------------------------------------------------------------
const MODULE = new WebAssembly.Module(readFileSync(join(PAKKET, 'wasm', 'graaf.wasm')));
const { rij: RIJ } = JSON.parse(readFileSync(join(PAKKET, 'wasm', 'keten.json'), 'utf8'));
// invoer: DSP-kanalen (1 of 2) als Float32Array; waarden: { index: waarde } (gevallen.json "waarden")
function wasmRender(invoer, sr, waarden) {
  const { exports: e } = new WebAssembly.Instance(MODULE, {});
  const h = e.pg_maak(sr);
  new Float64Array(e.memory.buffer, e.pg_rij(), RIJ.length).set(RIJ);
  e.pg_laad(h, RIJ.length);
  for (const [i, v] of Object.entries(waarden)) e.pg_zet(h, +i, v);
  const K = invoer.length, n = invoer[0].length, uit = invoer.map(() => new Float32Array(n));
  for (let p = 0; p < n; p += 128) {
    const m = Math.min(128, n - p);
    for (let c = 0; c < K; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), m).set(invoer[c].subarray(p, p + m));
    e.pg_verwerk(h, K, m);
    for (let c = 0; c < K; c++) uit[c].set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), m), p);
  }
  e.pg_weg(h);
  return uit;
}
// pb::uitvoer: onder FLT_MIN wordt 0, anders naar float
const FLT_MIN = 1.1754943508222875e-38;
const uitvoer = (y) => (y > -FLT_MIN && y < FLT_MIN ? 0 : Math.fround(y));
// de bypass zoals de plugin hem rekent (dub_echo.cpp verwerkStuk): per sample eerst het doel van de events op die
// sample, dan g een stap van 1 / (10 ms · sr) naar het doel, dan nat · (1 − g) + droog · g door pb::uitvoer
function metBypass(nat, droog, sr, events) {
  const stap = 1.0 / (OVERVLOEI_SEC * sr), n = nat[0].length, uit = nat.map(() => new Float32Array(n));
  for (let c = 0; c < nat.length; c++) {
    let g = 0, doel = 0, e = 0;
    for (let s = 0; s < n; s++) {
      while (e < events.length && events[e][0] <= s) doel = events[e++][1] >= 0.5 ? 1 : 0;
      if (doel === 0 && g === 0) { uit[c][s] = nat[c][s]; continue; }
      if (g < doel) { g += stap; if (g > doel) g = doel; }
      else if (g > doel) { g -= stap; if (g < doel) g = doel; }
      uit[c][s] = uitvoer(nat[c][s] * (1.0 - g) + droog[c][s] * g);
    }
  }
  return uit;
}
function bypassEvents(sr) {
  const op = (t) => 64 * Math.round((t * sr) / 64);
  const uit = op(1.0);
  // aan; uit; midden in die overvloei weer aan (omkeren); uit
  return [[op(0.5), 1], [uit, 0], [uit + 192, 1], [op(2.0), 0]];
}

// --- vergelijken (zoals vergelijk.mjs) -----------------------------------------------------------------------
function nullTest(a, b) {
  if (a.length !== b.length) return { piekDb: Infinity, rmsDb: Infinity, reden: `${b.length} kanalen, verwacht ${a.length}` };
  let piek = 0, som = 0, n = 0;
  for (let c = 0; c < a.length; c++) {
    if (a[c].length !== b[c].length) return { piekDb: Infinity, rmsDb: Infinity, reden: `${b[c].length} samples, verwacht ${a[c].length}` };
    for (let i = 0; i < a[c].length; i++) {
      const d = a[c][i] - b[c][i];
      if (!Number.isFinite(d)) return { piekDb: Infinity, rmsDb: Infinity, reden: 'NaN/Inf' };
      piek = Math.max(piek, Math.abs(d)); som += d * d; n++;
    }
  }
  const db = (x) => (x === 0 ? -Infinity : 20 * Math.log10(x));
  return { piekDb: db(piek), rmsDb: db(Math.sqrt(som / Math.max(1, n))) };
}
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= n; len <<= 1) {
    const h = -2 * Math.PI / len, wr = Math.cos(h), wi = Math.sin(h);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2, tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}
function spectrum(x) {
  const n = x.length, re = Float64Array.from(x), im = new Float64Array(n);
  fft(re, im);
  const p = new Float64Array(n / 2 + 1);
  for (let k = 0; k <= n / 2; k++) p[k] = re[k] ** 2 + im[k] ** 2;
  return p;
}
function statistisch(a, b, sr) {
  let band = 0, omh = 0;
  for (let c = 0; c < Math.min(a.length, b.length); c++) {
    let n = 1; while (n < a[c].length) n <<= 1; n >>= 1;
    const pa = spectrum(a[c].subarray(0, n)), pb = spectrum(b[c].subarray(0, n)), tot = pa.reduce((s, v) => s + v, 0);
    for (let f = 20; f < Math.min(20000, sr / 2); f *= Math.pow(2, 1 / 3)) {
      const lo = Math.floor(f / sr * n), hi = Math.max(lo + 1, Math.floor(f * Math.pow(2, 1 / 3) / sr * n));
      let ea = 0, eb = 0; for (let k = lo; k < hi; k++) { ea += pa[k]; eb += pb[k]; }
      if (ea >= tot * 1e-8) band = Math.max(band, Math.abs(10 * Math.log10(ea / eb)));
    }
    const v = Math.round(sr / 10);
    for (let i = 0; i + v <= a[c].length; i += v) {
      let ea = 0, eb = 0; for (let k = i; k < i + v; k++) { ea += a[c][k] ** 2; eb += b[c][k] ** 2; }
      if (ea / v >= 1e-6) omh = Math.max(omh, Math.abs(10 * Math.log10(ea / eb)));
    }
  }
  return { band, omh };
}

const gevallen = JSON.parse(readFileSync(join(T, 'gevallen.json'), 'utf8'));
const f = (x) => (x === -Infinity ? '−∞ (bit voor bit gelijk)' : `${x.toFixed(1)} dBFS`);
const som = (t) => { if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, t + '\n'); };
function regel(t) { console.log(t); som(t); }

// één render tegen zijn verwachting; geeft true als binnen de grens
function vergelijkEen(map, naam, renderPad, label) {
  const [geval, sr] = naam.split('@');
  const g = gevallen.find((x) => x.naam === geval);
  const verwacht = leesWav(join(map, 'verwacht', `${naam}.wav`)).kanalen;
  if (!existsSync(renderPad)) { regel(`| ${label} | ${naam} | ontbreekt | | | **ONTBREEKT** |`); return false; }
  const gekregen = leesWav(renderPad).kanalen;
  const r = nullTest(verwacht, gekregen);
  let ok = r.piekDb <= GRENS_DB, extra = r.reden ? ` ${r.reden}` : '';
  if (g?.statistisch && !r.reden) {
    const s = statistisch(verwacht, gekregen, +sr);
    ok = ok || (s.band <= 1 && s.omh <= 1);
    extra += ` statistisch: terts ${s.band.toFixed(3)} dB, omhullende ${s.omh.toFixed(3)} dB`;
  }
  regel(`| ${label} | ${naam} | ${f(r.piekDb)} | ${f(r.rmsDb)} | ${r.piekDb === -Infinity ? 'ja' : 'nee'} |${extra}${ok ? '' : ' **BOVEN DE GRENS**'}`);
  return ok;
}
const kop = (titel) => { regel(`### ${titel}`); regel(''); regel('| render | naam | piek | RMS | bit voor bit |'); regel('|---|---|---|---|---|'); };

// --- maak ----------------------------------------------------------------------------------------------------
function maak(map) {
  // eerst: doet de ketenmotor testsignalen/ bit voor bit na?
  kop('graaf.wasm tegen testsignalen/ (moet bit voor bit)');
  let goed = true;
  for (const g of gevallen) {
    const inv = leesWav(join(T, `${g.naam}.in.wav`)), verw = leesWav(join(T, `${g.naam}.verwacht.wav`));
    const r = nullTest(verw.kanalen, wasmRender(inv.kanalen, inv.sr, g.waarden));
    regel(`| graaf.wasm | ${g.naam}@${inv.sr} | ${f(r.piekDb)} | ${f(r.rmsDb)} | ${r.piekDb === -Infinity ? 'ja' : 'nee'} |`);
    goed = goed && r.piekDb === -Infinity;
  }
  if (!goed) { console.error('graaf.wasm doet testsignalen/ niet bit voor bit na: geen verwachte uitvoer gemaakt'); process.exit(1); }
  for (const g of gevallen) {
    const inv = leesWav(join(T, `${g.naam}.in.wav`)).kanalen;
    const mono = [inv[0]];
    for (const sr of SAMPLERATES) {
      schrijfWav(join(map, 'in', `${g.naam}@${sr}.wav`), inv, sr);
      schrijfWav(join(map, 'in', `${g.naam}@${sr}@mono.wav`), mono, sr);
      schrijfWav(join(map, 'verwacht', `${g.naam}@${sr}@stereo.wav`), wasmRender(inv, sr, g.waarden), sr);
      schrijfWav(join(map, 'verwacht', `${g.naam}@${sr}@mono.wav`), wasmRender(mono, sr, g.waarden), sr);
      schrijfWav(join(map, 'verwacht', `${g.naam}@${sr}@mono-stereo.wav`), wasmRender([inv[0], inv[0]], sr, g.waarden), sr);
    }
  }
  // bypass: echo-standaard met events
  const g = gevallen.find((x) => x.naam === 'echo-standaard');
  const inv = leesWav(join(T, `${g.naam}.in.wav`)).kanalen;
  for (const sr of SAMPLERATES) {
    const ev = bypassEvents(sr);
    writeFileSync(join(map, `bypass@${sr}.txt`), ev.map(([s, v]) => `${s} ${BYPASS_ID} ${v}`).join('\n') + '\n');
    schrijfWav(join(map, 'in', `bypass@${sr}.wav`), inv, sr);
    schrijfWav(join(map, 'in', `bypass@${sr}@mono.wav`), [inv[0]], sr);
    const dsp = { stereo: inv, mono: [inv[0]], 'mono-stereo': [inv[0], inv[0]] };
    for (const ind of INDELINGEN)
      schrijfWav(join(map, 'verwacht', `bypass@${sr}@${ind}.wav`), metBypass(wasmRender(dsp[ind], sr, g.waarden), dsp[ind], sr, ev), sr);
  }
  // de lijst voor pb-aurender (macOS): per regel naam, sr, kanalen in en uit, invoer, events (of -), en id=waarde
  const KAN = { stereo: [2, 2], mono: [1, 1], 'mono-stereo': [1, 2] }, lijst = [];
  for (const sr of SAMPLERATES) for (const ind of INDELINGEN) for (const geval of [...gevallen.map((x) => x.naam), 'bypass'])
    lijst.push([`${geval}@${sr}@${ind}`, sr, ...KAN[ind], invoerVoor(map, geval, sr, ind),
                geval === 'bypass' ? join(map, `bypass@${sr}.txt`) : '-', ...params(geval)].join(' '));
  writeFileSync(join(map, 'au-lijst.txt'), lijst.join('\n') + '\n');
  console.log(`matrix: invoer en verwachte uitvoer in ${map} (${SAMPLERATES.length} samplerates × ${INDELINGEN.length} indelingen × ${gevallen.length + 1} gevallen)`);
}

const params = (geval) => gevallen.find((x) => x.naam === (geval === 'bypass' ? 'echo-standaard' : geval)).pbRender;
const invoerVoor = (map, geval, sr, ind) => join(map, 'in', `${geval}@${sr}${ind === 'stereo' ? '' : '@mono'}.wav`);
function draai(cmd, args) {
  const r = spawnSync(cmd, args, { stdio: ['ignore', 'inherit', 'inherit'] });
  if (r.status !== 0) { console.error(`mislukt (${r.status}): ${cmd} ${args.join(' ')}`); return false; }
  return true;
}

// --- clap: pb-render door het .clap --------------------------------------------------------------------------
function clap(map, render, plugin) {
  const varianten = [['blok 512', ['--blok', '512']], ['blok 1,4096,77,3 in-place', ['--blok', '1,4096,77,3', '--in-place']]];
  let mis = 0;
  for (const [vnaam, vopt] of varianten) {
    kop(`Null-test CLAP matrix, ${vnaam} (${process.arch})`);
    for (const sr of SAMPLERATES) for (const ind of INDELINGEN) for (const geval of [...gevallen.map((g) => g.naam), 'bypass']) {
      const naam = `${geval}@${sr}@${ind}`, uit = join(map, 'clap', vnaam.replace(/[^a-z0-9]+/gi, '-'), `${naam}.wav`);
      mkdirSync(dirname(uit), { recursive: true });
      const args = [plugin, invoerVoor(map, geval, sr, ind), uit, '--sr', String(sr), '--indeling', ind, ...vopt,
                    ...params(geval).flatMap((p) => ['--p', p])];
      if (geval === 'bypass') args.push('--automatie', join(map, `bypass@${sr}.txt`));
      if (!draai(render, args) || !vergelijkEen(map, naam, uit, `CLAP ${vnaam}`)) mis++;
    }
  }
  // bypass aan vanaf het begin met subnormale invoer: de droge invoer gaat door pb::uitvoer, dus geen subnormale float
  // naar de host (de fuzzer van clap-validator vond dat eens); de rest komt ongewijzigd door
  kop('Bypass: subnormale invoer wordt 0, de rest komt ongewijzigd door');
  const n = 4800, x = [new Float32Array(n), new Float32Array(n)];
  for (let i = 0; i < n; i++) {
    x[0][i] = (i % 3 === 0 ? 1e-40 : i % 3 === 1 ? -3e-39 : Math.sin(i * 0.01) * 0.5);
    x[1][i] = (i % 2 ? 1.4e-45 : -0.25 * Math.cos(i * 0.003));
  }
  for (const ind of INDELINGEN) {
    const inPad = join(map, 'in', `subnormaal@${ind}.wav`), uit = join(map, 'clap', `subnormaal@${ind}.wav`);
    schrijfWav(inPad, ind === 'stereo' ? x : [x[0]], 48000);
    const ok = draai(render, [plugin, inPad, uit, '--indeling', ind, '--in-place', '--blok', '1,4096,77,3', '--p', '10=1']);
    let sub = 0, anders = 0;
    if (ok) {
      const k = leesWav(uit).kanalen, bron = ind === 'stereo' ? x : [x[0], x[0]];
      for (let c = 0; c < k.length; c++) for (let i = 0; i < n; i++) {
        const v = k[c][i];
        if (v !== 0 && Math.abs(v) < FLT_MIN) sub++;
        if (v !== uitvoer(bron[c][i])) anders++;
      }
    }
    const goed = ok && sub === 0 && anders === 0;
    regel(`| CLAP bypass aan | subnormaal@48000@${ind} | ${sub} subnormaal | ${anders} anders dan de invoer | ${goed ? 'ja' : 'nee'} |${goed ? '' : ' **FOUT**'}`);
    if (!goed) mis++;
  }
  return mis;
}

// --- pluginval test per samplerate (stereo; pluginval kiest zelf de standaardindeling) --------------------------
function pluginval(map, pv, plugin, label) {
  kop(`Null-test ${label} matrix (pluginval test, stereo)`);
  let mis = 0;
  for (const sr of SAMPLERATES) for (const g of gevallen) {
    const naam = `${g.naam}@${sr}@stereo`, dir = join(map, `pluginval-${label}`), cfgPad = join(dir, 'cfg', `${naam}.json`);
    const basis = JSON.parse(readFileSync(join(T, 'pluginval', `${g.naam}.json`), 'utf8'));
    const cfg = { ...basis, name: naam, input: { audio: invoerVoor(map, g.naam, sr, 'stereo') }, reference: join(dir, `${naam}.wav`),
                  sample_rate: sr, block_size: PV_BLOK[sr] };
    mkdirSync(dirname(cfgPad), { recursive: true });
    writeFileSync(cfgPad, JSON.stringify(cfg, null, 2));
    for (const x of [cfg.reference, cfg.reference + '.json']) if (existsSync(x)) writeFileSync(x, ''), spawnSync('rm', ['-f', x]);
    // eerst opnemen (de render zelf, voor de tabel), dan tegen de verwachting van graaf.wasm met de grens van pluginval
    const ok1 = draai(pv, ['test', '--record-missing', '--plugin', plugin, cfgPad]);
    const ok2 = ok1 && vergelijkEen(map, naam, cfg.reference, `${label} blok ${PV_BLOK[sr]}`);
    let ok3 = true;
    if (ok2 && !g.statistisch) {
      const tegen = { ...cfg, reference: join(map, 'verwacht', `${naam}.wav`) }, tegenPad = join(dir, 'cfg', `${naam}.tegen.json`);
      writeFileSync(tegenPad, JSON.stringify(tegen, null, 2));
      ok3 = draai(pv, ['test', '--plugin', plugin, tegenPad]);
    }
    if (!(ok1 && ok2 && ok3)) mis++;
  }
  return mis;
}

// --- vergelijk: renders van een ander gereedschap (map met <naam>.wav) -----------------------------------------
function vergelijk(map, renders, label) {
  kop(`Null-test ${label} matrix`);
  let mis = 0, gezien = 0;
  for (const sr of SAMPLERATES) for (const ind of INDELINGEN) for (const geval of [...gevallen.map((g) => g.naam), 'bypass']) {
    const naam = `${geval}@${sr}@${ind}`;
    gezien++;
    if (!vergelijkEen(map, naam, join(renders, `${naam}.wav`), label)) mis++;
  }
  return mis;
}

const [wat, map, ...rest] = process.argv.slice(2);
if (!wat || !map) {
  console.error('gebruik: node gereedschap/matrix.mjs maak|clap|pluginval|vergelijk <map> …'); process.exit(2);
}
let mis = 0;
if (wat === 'maak') maak(map);
else if (wat === 'clap') mis = clap(map, rest[0], rest[1]);
else if (wat === 'pluginval') mis = pluginval(map, rest[0], rest[1], rest[2]);
else if (wat === 'vergelijk') mis = vergelijk(map, rest[0], rest[1] || 'render');
else { console.error(`onbekend: ${wat}`); process.exit(2); }
if (wat !== 'maak') regel(`\nmatrix ${wat}: ${mis === 0 ? 'alles binnen de grens' : `${mis} buiten de grens of mislukt`}`);
process.exit(mis ? 1 : 0);
