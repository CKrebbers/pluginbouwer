// cv-eis.mjs — clap-validator met een eis per toets: niets mislukt, en elke toets die op een patroon past is PASSED
// (niet SKIPPED). Leest de JSON-uitvoer (`--json`), dus geen last van kleuren of afgebroken namen in de boom.
//   node gereedschap/cv-eis.mjs <clap-validator> <plugin.clap> [patroon …]
//   bijvoorbeeld: node gereedschap/cv-eis.mjs clap-validator build/plugins/X.clap '^state-reproducibility' '^param-fuzz' '^preset-discovery'
// Een patroon is een reguliere expressie op de toetsnaam; past er geen enkele toets op, dan faalt het ook (een
// verkeerd gespelde naam mag niet stilletjes groen zijn). Exit 0 = alles in orde, 1 = een eis niet gehaald. De eigen
// logregels van clap-validator (stderr) gaan weg: ze duwen het bewijs uit de staart van het CI-log.
import { execFileSync } from 'node:child_process';

const [, , cv, plugin, ...patronen] = process.argv;
if (!cv || !plugin) { console.error('gebruik: node cv-eis.mjs <clap-validator> <plugin.clap> [patroon …]'); process.exit(2); }
let uit;
try { uit = execFileSync(cv, ['validate', '--json', plugin], { encoding: 'utf8', maxBuffer: 64 << 20, stdio: ['ignore', 'pipe', 'pipe'] }); }
catch (e) { uit = e.stdout || ''; }   // clap-validator geeft exit 1 als er iets mislukt; de JSON staat er dan toch
const begin = uit.indexOf('{');
if (begin < 0) { console.error('geen JSON van clap-validator'); process.exit(1); }
const { results = [] } = JSON.parse(uit.slice(begin));
const toetsen = results.map((r) => ({ naam: Object.values(r.test)[0]?.test ?? '?', code: r.status?.code ?? '?', details: r.status?.details ?? '' }));
let fout = 0;
const telling = {};
for (const t of toetsen) {
  telling[t.code] = (telling[t.code] || 0) + 1;
  if (t.code === 'failed' || t.code === 'crashed') { console.log(`MISLUKT: ${t.naam} (${t.code}) ${t.details}`); fout++; }
}
for (const p of patronen) {
  const re = new RegExp(p, 'i'), passend = toetsen.filter((t) => re.test(t.naam));
  if (!passend.length) { console.log(`GEEN TOETS past op /${p}/`); fout++; continue; }
  for (const t of passend) {
    const ok = t.code === 'success';
    console.log(`${ok ? 'PASSED' : 'NIET PASSED'} ${t.naam} (${t.code})${ok ? '' : ' ' + t.details}`);
    if (!ok) fout++;
  }
}
console.log(`clap-validator: ${toetsen.length} toetsen, ${Object.entries(telling).map(([k, v]) => `${v} ${k}`).join(', ')}; eisen ${fout ? 'NIET gehaald' : 'gehaald'}`);
process.exit(fout ? 1 : 0);
