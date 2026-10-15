// tool.mjs — de Pluginbouwer (tool/index.html) in echte Chromium, zonder venster. Toetst: laadt zonder fouten, de
// readout meet, de keten compileert, horen start (AudioWorklet of de terugval), het bouwpakket exporteert als zip
// met dezelfde testsignalen als CI (byte voor byte), en de pagina scrolt niet opzij op telefoonbreedte.
//   node tools/headless/tool.mjs        (vooraf: node tool/bouw.mjs)
// Playwright: globaal geïnstalleerd (NODE_PATH) of in node_modules; Chromium uit PLAYWRIGHT_BROWSERS_PATH.
import { createRequire } from 'node:module';
import { readFileSync, existsSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { execFileSync } from 'node:child_process';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const require = createRequire(import.meta.url);
let pw;
try { pw = require('playwright'); } catch {
  const glob = execFileSync('npm', ['root', '-g']).toString().trim();
  pw = require(join(glob, 'playwright'));
}
const fouten = [];
const ok = (voorwaarde, tekst) => { console.log((voorwaarde ? 'ok   ' : 'FOUT ') + tekst); if (!voorwaarde) fouten.push(tekst); };

// PB_CHROME = een eigen Chrome (CI: die van de runner), anders de Chromium van Playwright
const browser = await pw.chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'], ...(process.env.PB_CHROME ? { executablePath: process.env.PB_CHROME } : {}) });
const UIT = join(ROOT, 'tools', 'headless', 'uit');
mkdirSync(UIT, { recursive: true });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, acceptDownloads: true });
const consoleFouten = [];
page.on('pageerror', (e) => consoleFouten.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error' && !/fonts\.g|net::ERR_FAILED/.test(m.text())) consoleFouten.push(m.text()); });
page.on('requestfailed', (q) => { if (!/fonts\.(googleapis|gstatic)\.com/.test(q.url())) consoleFouten.push('verzoek mislukt: ' + q.url()); });
await page.route(/fonts\.(googleapis|gstatic)\.com/, (r) => r.abort());   // geen netwerk nodig
await page.goto(pathToFileURL(join(ROOT, 'tool', 'index.html')).href);
await page.waitForFunction(() => globalThis.PB && document.querySelector('#readout .rv'), null, { timeout: 15000 });
await page.waitForFunction(() => globalThis.PB.S.readout && !globalThis.PB.S.readout.fout, null, { timeout: 30000 });
const r = await page.evaluate(() => ({ ...globalThis.PB.S.readout, fouten: globalThis.PB.compileer().fouten, tekst: document.querySelector('#readout').innerText }));
ok(r.fouten.length === 0, 'de dub-echo compileert zonder fouten');
ok(Number.isFinite(r.thdDb) && Number.isFinite(r.aliasingDb), `readout meet (thd ${r.thdDb?.toFixed(1)} dB, aliasing ${r.aliasingDb?.toFixed(1)} dB)`);
ok(/latency/i.test(r.tekst) && /staart/i.test(r.tekst), 'readout toont latency en staart');

// een knop draaien = de readout meet opnieuw, met een pijl
await page.evaluate(() => { const p = globalThis.PB.S.ontwerp.parameters.find((q) => q.naam === 'Verzadiging'); p.standaard = 18; globalThis.PB.gewijzigd('verzadiging 18 dB'); });
const pijl = await page.waitForFunction(() => !!document.querySelector('#readout .pijl'), null, { timeout: 30000 }).then(() => true, () => false);
ok(pijl, 'na een wijziging staat er een pijl in de readout');

// horen
await page.click('text=▶ speel');
await page.waitForTimeout(1200);
const modus = await page.evaluate(() => globalThis.PB.speler.modus);
ok(/AudioWorklet|hoofdthread/.test(modus), 'horen start: ' + modus);
const meter = await page.evaluate(() => parseFloat(document.querySelector('#meter')?.style.width || '0'));
ok(meter > 0, `er komt geluid uit de motor (meter ${meter.toFixed(0)} %)`);
await page.click('text=■ stop');

// tabs
for (const t of ['Frontpaneel', 'Meten', 'Verken', 'Schetsboek', 'ABX', 'Taaktoets']) {
  await page.click(`.tabs >> text=${t}`);
  await page.waitForTimeout(150);
}
await page.click('.tabs >> text=Verken');
await page.click('text=maak 6 varianten');
const nVar = await page.evaluate(() => globalThis.PB.S.varianten.length);
ok(nVar > 0, `verken maakt varianten (${nVar})`);

// bouwen-stand en export
await page.click('[data-stand="bouwen"]');
const [dl] = await Promise.all([page.waitForEvent('download'), page.click('text=exporteer bouwpakket')]);
const zipPad = await dl.path();
const zipBytes = readFileSync(zipPad);
ok(zipBytes.length > 100000, `zip gedownload (${(zipBytes.length / 1024).toFixed(0)} kB)`);
// de testsignalen uit de browser == die van CI (bouwpakket/dub_echo), na het terugzetten van het ontwerp
await page.evaluate(() => { const p = globalThis.PB.S.ontwerp.parameters.find((q) => q.naam === 'Verzadiging'); p.standaard = 6; globalThis.PB.gewijzigd('terug'); });
await page.waitForTimeout(200);
const lijst = await page.evaluate(() => { const pak = globalThis.PB.maakPakket(); const uit = {}; for (const [p, v] of Object.entries(pak)) if (p.startsWith('testsignalen/') && p.endsWith('.wav')) { let h = 0; const b = typeof v === 'string' ? new TextEncoder().encode(v) : v; for (let i = 0; i < b.length; i++) h = (h * 31 + b[i]) >>> 0; uit[p] = [b.length, h]; } return uit; });
let gelijk = 0, totaal = 0;
for (const [p, [n, h]] of Object.entries(lijst)) {
  totaal++;
  const ci = join(ROOT, 'bouwpakket', 'dub_echo', p);
  if (!existsSync(ci)) continue;
  const b = readFileSync(ci); let hh = 0; for (let i = 0; i < b.length; i++) hh = (hh * 31 + b[i]) >>> 0;
  if (b.length === n && hh === h) gelijk++;
}
ok(totaal > 0 && gelijk === totaal, `testsignalen uit Chromium = die van CI (${gelijk}/${totaal})`);

// telefoonbreedte: geen horizontale scroll van de pagina
await page.click('[data-stand="ontwerpen"]');
await page.setViewportSize({ width: 390, height: 844 });
await page.waitForTimeout(200);
const breed = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
ok(breed <= 0, `390 px breed: geen horizontale scroll (${breed} px over)`);
await page.screenshot({ path: join(UIT, 'telefoon.png'), fullPage: false });
await page.setViewportSize({ width: 1440, height: 900 });
await page.screenshot({ path: join(UIT, '1440.png') });

ok(consoleFouten.length === 0, 'geen fouten in de console' + (consoleFouten.length ? ': ' + consoleFouten.slice(0, 3).join(' | ') : ''));
await browser.close();
process.exit(fouten.length ? 1 : 0);
