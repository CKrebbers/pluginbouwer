// proef.mjs — fase B: dezelfde keten in Faust, één .dsp naar WASM (faustwasm) en naar C++ (native), en de null-test.
//   node stackproef/faust/proef.mjs        schrijft stackproef/faust/uitslag.md
// Varianten: Faust standaard (float) en -double. Native = clang met dezelfde vlaggen als onze stack, en zonder.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { laadOntwerp, render, schrijfWav, leesWav, nullTest, fmtDb, ROOT } from '../../tests/lib/pb.mjs';
import { ruis, stukjes, sweep, stereo } from '../../tests/lib/signalen.mjs';

const HIER = dirname(fileURLToPath(import.meta.url));
const FAUSTWASM = process.env.FAUSTWASM || '/home/user/bronnen/faustnpm/node_modules/@grame/faustwasm';
const { instantiateFaustModuleFromFile, LibFaust, FaustCompiler, FaustMonoDspGenerator } = await import(join(FAUSTWASM, 'dist/esm/index.js'));
const mod = await instantiateFaustModuleFromFile(join(FAUSTWASM, 'libfaust-wasm/libfaust-wasm.js'));
const compiler = new FaustCompiler(new LibFaust(mod));
const code = readFileSync(join(HIER, 'proef_delay.dsp'), 'utf8');
const uit = join(ROOT, 'uit', 'stackproef'); mkdirSync(join(HIER, 'gen'), { recursive: true }); mkdirSync(uit, { recursive: true });

// Parametersets (exact in float32). De eerste geeft een GEHEEL aantal samples (5925): dan doet de interpolatie niets.
// De andere geven een fractie, ook bij 44,1 kHz, zodat Lagrange-3 echt rekent.
const SETS = [
  { naam: 'geheel', sr: 48000, w: { Frequentie: 4000, Resonantie: 2.5, Tijd: 123.4375, Mix: 0.5 } },
  { naam: 'fractie', sr: 48000, w: { Frequentie: 7000, Resonantie: 0.70703125, Tijd: 10.0078125, Mix: 0.75 } },
  { naam: 'fractie-441', sr: 44100, w: { Frequentie: 1500, Resonantie: 6, Tijd: 333.3125, Mix: 0.5 } },
];
const waarden = SETS[0].w;
const gevallen = [
  { naam: 'ruis', maak: (sr) => stereo(ruis(sr, 2, 0.3, 3), ruis(sr, 2, 0.3, 4)) },
  { naam: 'stukjes', maak: (sr) => stereo(stukjes(sr, 2)) },
  { naam: 'sweep', maak: (sr) => stereo(sweep(20, 20000, sr, 2, 0.5)) },
];
let fouten = 0;
const regels = [`# Uitslag proefbouw B (${new Date().toISOString().slice(0, 10)})`, '',
  `Faust ${compiler.version()} (WASM, @grame/faustwasm) en ${execFileSync(process.env.FAUST || 'faust', ['--version']).toString().split('\n')[0]} (native), clang ${execFileSync('clang', ['--version']).toString().split('\n')[0]}.`,
  'Keten: laagdoor → Lagrange-3 delay → droog/nat. Sets: ' + SETS.map((s) => `${s.naam} (${s.sr} Hz, ${JSON.stringify(s.w)})`).join('; ') + '.', ''];

// onze stack als ijkpunt
const o = laadOntwerp('proef_delay');

const CLAP = process.env.PB_FAUST_CLAP, RENDER = process.env.PB_RENDER;
regels.push('| variant | set | geval | browser (WASM) vs native | WASM vs gebouwd .clap | Faust vs onze stack |', '|---|---|---|---|---|---|');
for (const variant of ['float', 'double']) {
  const args = `-I libraries/ -ftz 0 ${variant === 'double' ? '-double' : ''}`.trim();
  const gen = new FaustMonoDspGenerator();
  await gen.compile(compiler, 'proef', code, args);
  // de WASM-build van de Faust-compiler heeft geen C++-backend: de native compiler (zelfde bron, zie KEUZE-STACK.md)
  const FAUST = process.env.FAUST || 'faust';
  const cpp = join(HIER, 'gen', `proef-${variant}.cpp`);
  execFileSync(FAUST, ['-I', process.env.FAUST_LIB || '/usr/share/faust', ...args.replace('-I libraries/', '').split(' ').filter(Boolean),
    '-lang', 'cpp', '-cn', 'FaustProef', '-o', cpp, join(HIER, 'proef_delay.dsp')], { stdio: 'inherit' });
  const bin = join(uit, `rend-${variant}`);
  const vlag = variant === 'double' ? ['-DFAUSTFLOAT=double'] : [];
  execFileSync(process.env.CXX || 'clang++', ['-std=c++17', '-O2', '-ffp-contract=off', ...vlag, `-DPB_FAUST_GEN="gen/proef-${variant}.cpp"`,
    '-o', bin, join(HIER, 'rend.cpp')], { cwd: HIER, stdio: 'inherit' });
  for (const set of SETS) {
    const gevallenSr = gevallen.map((g) => ({ naam: g.naam, in: g.maak(set.sr) }));
    const pbW = { 0: set.w.Frequentie, 1: set.w.Resonantie, 2: set.w.Tijd, 3: set.w.Mix };
    for (const g of gevallenSr) {
      const p = await gen.createOfflineProcessor(set.sr, 128);
      for (const [k, v] of Object.entries(set.w)) p.setParamValue(`/proef/${k}`, v);
      const w = p.render(g.in, g.in[0].length);
      const stam = `${set.naam}-${g.naam}`;
      const inPad = join(uit, `${stam}.in.wav`), natPad = join(uit, `${stam}.faust-${variant}.native.wav`);
      schrijfWav(inPad, g.in, set.sr);
      schrijfWav(join(uit, `${stam}.faust-${variant}.wasm.wav`), w, set.sr);
      execFileSync(bin, [inPad, natPad, '512', ...Object.entries(set.w).map(([k, v]) => `${k}=${v}`)]);
      const nat = leesWav(natPad).kanalen;
      const ons = render(o, g.in, { sr: set.sr, waarden: pbW, blok: 128 });
      const r1 = nullTest(w, nat), r2 = nullTest(w, ons);
      if (r1.piekDb > -90) fouten++;
      let r3 = '—';
      if (CLAP && RENDER && variant === 'double') {
        // door het gebouwde binary: de Faust-C++ in onze CLAP-schil (dsp/ontwerpen/faust_proef_delay.h)
        const clapPad = join(uit, `${stam}.faust-clap.wav`);
        execFileSync(RENDER, [CLAP, inPad, clapPad, '--blok', '512', ...[1, 2, 3, 4].flatMap((id, i) => ['--p', `${id}=${Object.values(set.w)[i]}`])]);
        const r = nullTest(w, leesWav(clapPad).kanalen);
        if (r.piekDb > -90) fouten++;
        r3 = fmtDb(r.piekDb);
      }
      regels.push(`| Faust ${variant} | ${set.naam} | ${g.naam} | ${fmtDb(r1.piekDb)} | ${r3} | ${fmtDb(r2.piekDb)} |`);
    }
  }
}
regels.push('', `Platform: ${process.platform} ${process.arch}, Node ${process.version}.`);
writeFileSync(join(HIER, 'uitslag.md'), regels.join('\n') + '\n');
console.log(regels.join('\n'));
if (fouten) { console.error(`${fouten} gevallen boven −90 dBFS tussen Faust-WASM en Faust-native`); process.exit(1); }
