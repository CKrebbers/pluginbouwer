// null-matrix.mjs — de null-test van fase 5 over samplerates, kanaalindelingen en blokgroottes. De referentie komt
// per geval uit de WASM van de tool (gereedschap/wasm-render.mjs; bij 48 en 44,1 kHz bit voor bit gelijk aan
// testsignalen/*.verwacht.wav), de render uit het gebouwde binary.
//   node gereedschap/null-matrix.mjs clap <pb-render> <plugin.clap>              pb-render: sr x indeling x blokken
//   node gereedschap/null-matrix.mjs pluginval <pluginval> <plugin> <label>      pluginval test (VST3/AU, stereo): sr x blok,
//                                                                                 en de parameter Bypass aan (= de invoer)
//   node gereedschap/null-matrix.mjs au <au-render>                                de geïnstalleerde AU (macOS): sr x indeling
//                                                                                 x blokken, en kAudioUnitProperty_BypassEffect
// Grens: piek ≤ −90 dBFS (verwacht: bit voor bit). Zelfoscillatie (statistisch in gevallen.json): terts-spectrum en
// omhullende binnen 1 dB, zoals vergelijk.mjs. Exit 1 als één geval boven de grens komt.
import { execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync, appendFileSync, rmSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { leesWav, schrijfWav, wasmRender, invoerVoor } from './wasm-render.mjs';
import { nullTest, statistisch } from './vergelijk.mjs';

const PAKKET = join(dirname(fileURLToPath(import.meta.url)), '..');
const T = join(PAKKET, 'testsignalen');
const GRENS_DB = -90;
const SAMPLERATES = [44100, 48000, 88200, 96000, 176400, 192000];
const gevallen = JSON.parse(readFileSync(join(T, 'gevallen.json'), 'utf8'));

const [, , soort, ...arg] = process.argv;
const regels = [];
let mis = 0, aantal = 0;
const f = (x) => (x === -Infinity ? '−∞' : `${x.toFixed(1)} dBFS`);
// een render draaien; de uitvoer (clap-wrapper praat veel op stderr) alleen laten zien als het misgaat
function draai(prog, args) {
  try {
    execFileSync(prog, args, { stdio: ['ignore', 'pipe', 'pipe'], maxBuffer: 1 << 28 });
  } catch (e) {
    process.stderr.write(`${prog} ${args.join(' ')}\n${e.stdout || ''}${e.stderr || ''}`);
  }
}

// referenties (per sr en indeling één keer)
const refCache = new Map();
function referentie(g, sr, indeling) {
  const sleutel = `${g.naam}|${sr}|${indeling}`;
  if (!refCache.has(sleutel)) {
    const inv = leesWav(join(T, `${g.naam}.in.wav`));
    refCache.set(sleutel, wasmRender(invoerVoor(inv.kanalen, indeling), sr, g.waarden));
  }
  return refCache.get(sleutel);
}
// het droge signaal (bypass): de invoer zoals hij per uitgang klinkt
function droog(g, indeling) {
  return invoerVoor(leesWav(join(T, `${g.naam}.in.wav`)).kanalen, indeling);
}
function vergelijk(wat, g, sr, ref, pad) {
  aantal++;
  if (!existsSync(pad)) { regels.push(`| ${wat} | ${g.naam} | geen render | | **MISLUKT**`); mis++; return; }
  const kregen = leesWav(pad).kanalen;
  let ok = kregen.length === ref.length && kregen[0].length === ref[0].length;
  const r = nullTest(ref, kregen);
  let extra = ok ? '' : ` kanalen/lengte ${kregen.length}x${kregen[0].length}, verwacht ${ref.length}x${ref[0].length}`;
  ok = ok && r.piekDb <= GRENS_DB;
  if (g.statistisch && kregen.length === ref.length && r.piekDb > GRENS_DB) {
    const s = statistisch(ref, kregen, sr);
    ok = s.band <= 1 && s.omh <= 1;
    extra += ` statistisch: terts ${s.band.toFixed(3)} dB, omhullende ${s.omh.toFixed(3)} dB`;
  }
  if (!ok) mis++;
  regels.push(`| ${wat} | ${g.naam} | ${f(r.piekDb)} | ${r.piekDb === -Infinity ? 'ja' : 'nee'} |${extra}${ok ? '' : ' **BOVEN DE GRENS**'}`);
}

if (soort === 'clap') {
  const [render, clap] = arg;
  if (!render || !clap) { console.error('gebruik: null-matrix.mjs clap <pb-render> <plugin.clap>'); process.exit(2); }
  const UIT = join(PAKKET, 'uit', 'matrix-clap');
  mkdirSync(UIT, { recursive: true });
  // elke sr en indeling: blok 512 (apart), en wisselend in-place (3,128,1,4096,77); bij de uitersten ook blok 1 en 4096
  for (const sr of SAMPLERATES) {
    for (const indeling of ['stereo', 'mono', 'mono-stereo']) {
      const varianten = [['blok 512', ['--blok', '512']], ['wisselend, in-place', ['--blok', '3,128,1,4096,77', '--in-place']]];
      if (sr === 44100 || sr === 192000) varianten.push(['blok 1, in-place', ['--blok', '1', '--in-place']], ['blok 4096', ['--blok', '4096']]);
      for (const [naam, opties] of varianten) {
        for (const g of gevallen) {
          const pad = join(UIT, `${g.naam}-${sr}-${indeling}-${naam.replace(/[^a-z0-9]+/gi, '_')}.wav`);
          const params = g.pbRender.flatMap((p) => ['--p', p]);
          rmSync(pad, { force: true });
          draai(render, [clap, join(T, `${g.naam}.in.wav`), pad, '--sr', String(sr), '--kanalen', indeling, ...opties, ...params]);
          vergelijk(`${sr} Hz, ${indeling}, ${naam}`, g, sr, referentie(g, sr, indeling), pad);
        }
      }
    }
  }
} else if (soort === 'pluginval') {
  const [pv, plugin, label] = arg;
  if (!pv || !plugin || !label) { console.error('gebruik: null-matrix.mjs pluginval <pluginval> <plugin> <label>'); process.exit(2); }
  const UIT = join(PAKKET, 'uit', `matrix-pluginval-${label}`);
  mkdirSync(UIT, { recursive: true });
  // stereo (de standaardindeling van de plugin; pluginval test kiest zelf geen andere). Elke sr met blok 512, en bij
  // 44,1 en 192 kHz ook blok 1 en 4096. De invoer is dezelfde reeks samples, met de sr van de render in de WAV.
  const varianten = [];
  for (const sr of SAMPLERATES) {
    varianten.push([sr, 512]);
    if (sr === 44100 || sr === 192000) varianten.push([sr, 1], [sr, 4096]);
  }
  for (const [sr, blok] of varianten) {
    for (const g of gevallen) {
      const basis = JSON.parse(readFileSync(join(T, 'pluginval', `${g.naam}.json`), 'utf8'));
      const inv = leesWav(join(T, `${g.naam}.in.wav`));
      const stam = `${g.naam}-${sr}-blok${blok}`;
      const inPad = join(UIT, `${stam}.in.wav`), uitPad = join(UIT, `${stam}.wav`), cfg = join(UIT, `${stam}.json`);
      schrijfWav(inPad, inv.kanalen, sr);
      rmSync(uitPad, { force: true });
      rmSync(`${uitPad}.json`, { force: true });
      writeFileSync(cfg, JSON.stringify({ name: stam, input: { audio: inPad }, reference: uitPad, state: basis.state,
                                          sample_rate: sr, block_size: blok, comparison: basis.comparison }, null, 2));
      draai(pv, ['test', '--record-missing', '--plugin', plugin, cfg]);
      vergelijk(`${sr} Hz, blok ${blok}`, g, sr, referentie(g, sr, 'stereo'), uitPad);
    }
  }
  // de parameter Bypass aan (genormaliseerd 1; CLAP_PARAM_IS_BYPASS -> VST3 kIsBypass / AU-parameter): de invoer
  for (const g of gevallen) {
    const basis = JSON.parse(readFileSync(join(T, 'pluginval', `${g.naam}.json`), 'utf8'));
    const stam = `${g.naam}-bypass`, uitPad = join(UIT, `${stam}.wav`), cfg = join(UIT, `${stam}.json`);
    rmSync(uitPad, { force: true });
    rmSync(`${uitPad}.json`, { force: true });
    writeFileSync(cfg, JSON.stringify({ name: stam, input: { audio: join(T, `${g.naam}.in.wav`) }, reference: uitPad,
                                        state: { parameters: { ...basis.state.parameters, Bypass: 1 } },
                                        sample_rate: g.sr, block_size: 512, comparison: basis.comparison }, null, 2));
    draai(pv, ['test', '--record-missing', '--plugin', plugin, cfg]);
    vergelijk(`${g.sr} Hz, blok 512, Bypass aan (= de invoer)`, { ...g, statistisch: false }, g.sr, droog(g, 'stereo'), uitPad);
  }
} else if (soort === 'au') {
  const [render] = arg;
  if (!render) { console.error('gebruik: null-matrix.mjs au <au-render>'); process.exit(2); }
  const UIT = join(PAKKET, 'uit', 'matrix-au');
  mkdirSync(UIT, { recursive: true });
  for (const sr of SAMPLERATES) {
    for (const indeling of ['stereo', 'mono', 'mono-stereo']) {
      const varianten = [['blok 512', ['--blok', '512']]];
      if (sr === 44100 || sr === 192000) varianten.push(['wisselend', ['--blok', '3,128,1,4096,77']]);
      for (const [naam, opties] of varianten) {
        for (const g of gevallen) {
          const pad = join(UIT, `${g.naam}-${sr}-${indeling}-${naam.replace(/[^a-z0-9]+/gi, '_')}.wav`);
          const params = g.pbRender.flatMap((p) => ['--p', p]);
          rmSync(pad, { force: true });
          draai(render, [join(T, `${g.naam}.in.wav`), pad, '--sr', String(sr), '--kanalen', indeling, ...opties, ...params]);
          vergelijk(`${sr} Hz, ${indeling}, ${naam}`, g, sr, referentie(g, sr, indeling), pad);
        }
      }
    }
  }
  // de bypass van de host (kAudioUnitProperty_BypassEffect, vóór Initialize): de invoer, in elke indeling
  for (const indeling of ['stereo', 'mono', 'mono-stereo']) {
    for (const g of gevallen) {
      const pad = join(UIT, `${g.naam}-${indeling}-bypass.wav`);
      const params = g.pbRender.flatMap((p) => ['--p', p]);
      rmSync(pad, { force: true });
      draai(render, [join(T, `${g.naam}.in.wav`), pad, '--kanalen', indeling, '--blok', '512', '--bypass', ...params]);
      vergelijk(`${g.sr} Hz, ${indeling}, BypassEffect (= de invoer)`, { ...g, statistisch: false }, g.sr, droog(g, indeling), pad);
    }
  }
} else {
  console.error('gebruik: null-matrix.mjs clap|pluginval|au …');
  process.exit(2);
}

const kop = `### Null-matrix ${soort} ${arg[arg.length - 1]} (${process.platform} ${process.arch})\n\n| variant | geval | piek | bit voor bit |\n|---|---|---|---|`;
const tekst = `${kop}\n${regels.join('\n')}\n\n${aantal - mis} van ${aantal} binnen de grens${mis ? `, ${mis} erboven` : ''}\n`;
console.log(tekst);
if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, tekst + '\n');
process.exit(mis ? 1 : 0);
