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
// eenvoudig (de standaard): gewone woorden, geen samples of dB in beeld
const eenvoudig = await page.evaluate(() => ({ weergave: globalThis.PB.S.weergave, tekst: document.querySelector('#readout').innerText, bib: document.querySelector('#bib').innerText, samenvatting: document.querySelector('.samenvatting')?.innerText || '' }));
ok(eenvoudig.weergave === 'eenvoudig' && /naklinken/i.test(eenvoudig.tekst) && !/smp|latency|aliasing/i.test(eenvoudig.tekst), 'eenvoudig: de readout in gewone woorden (' + eenvoudig.tekst.replace(/\s+/g, ' ') + ')');
ok(/maakt geluid/i.test(eenvoudig.bib) && /combinaties/i.test(eenvoudig.bib) && !/% cpu|smp/.test(eenvoudig.bib), 'eenvoudig: blokken per familie en combinaties, zonder cpu en samples');
ok(/^Weg van het geluid: Ingang → /.test(eenvoudig.samenvatting), 'boven de keten staat wat het ontwerp doet');
// + bij Galm zet hem in de keten (vóór de uitgang), en het blijft klinken: daarna terug naar de dub-echo
const voor = await page.evaluate(() => globalThis.PB.S.ontwerp.knopen.length);
await page.click('#bib .kaart:has-text("Een ruimte om het geluid heen") .plus');
await page.waitForTimeout(300);
const na = await page.evaluate(() => ({ n: globalThis.PB.S.ontwerp.knopen.length, fouten: globalThis.PB.compileer().fouten, galm: globalThis.PB.S.ontwerp.kabels.some((k) => k.van[0].startsWith('galm') && k.naar[0] === 'uit'), melding: document.querySelector('#melding').innerText }));
ok(na.n === voor + 1 && na.fouten.length === 0 && na.galm && /Galm tussen/.test(na.melding), '+ zet Galm in de keten vóór de uitgang: ' + na.melding);
await page.click('[data-weergave="technisch"]');
await page.click('.voorbeelden button:text-is("Dub-echo")').catch(() => {});
await page.evaluate(() => { localStorage.removeItem('pb.ontwerp'); });
await page.waitForTimeout(400);
if (await page.evaluate(() => globalThis.PB.speler.speelt())) await page.click('text=■ stop');
await page.waitForFunction(() => globalThis.PB.S.readout && !globalThis.PB.S.readout.fout && /latency/i.test(document.querySelector('#readout').innerText), null, { timeout: 30000 });
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
// zonder dit liep de worklet tot 15 okt 2026 ongemerkt droog door (de motor kwam er nooit aan)
ok(await page.evaluate(() => globalThis.PB.speler.motorDraait), 'de motor draait echt (hij meldde zich geladen)');
const meter = await page.evaluate(() => parseFloat(document.querySelector('#meter')?.style.width || '0'));
ok(meter > 0, `er komt geluid uit de motor (meter ${meter.toFixed(0)} %)`);
await page.click('text=■ stop');

// voorbeelden: elk knopje laadt het ontwerp en speelt meteen; instrumenten zonder ingang, effecten op de groove.
// Gemeten: de hoogste stand van de meter in 1,5 s na de klik (piek, lineair; 0,03 = −30 dBFS).
const namen = await page.evaluate(() => [...document.querySelectorAll('.voorbeelden button')].map((b) => b.textContent));
ok(namen.length >= 20, `${namen.length} voorbeelden onder Horen`);
const stil = [];
for (const naam of namen) {
  await page.evaluate(() => { globalThis.__max = 0; clearInterval(globalThis.__peil); globalThis.__peil = setInterval(() => { globalThis.__max = Math.max(globalThis.__max, parseFloat(document.querySelector('#meter')?.style.width || '0') / 100); }, 40); });
  await page.click(`.voorbeelden button:text-is("${naam}")`);
  if (naam === 'Toetsenpad') { await page.waitForTimeout(300); for (const k of ['a', 'f', 'h']) { await page.keyboard.down(k); await page.waitForTimeout(350); await page.keyboard.up(k); } }
  await page.waitForTimeout(1500);
  const r = await page.evaluate(() => ({ max: globalThis.__max, fouten: globalThis.PB.compileer().fouten, speelt: globalThis.PB.speler.speelt() }));
  if (r.fouten.length || !r.speelt || r.max < 0.03) stil.push(`${naam} (meter ${r.max.toFixed(3)}, ${r.fouten.length} fouten, speelt ${r.speelt})`);
}
await page.evaluate(() => clearInterval(globalThis.__peil));
ok(stil.length === 0, 'elk voorbeeld klinkt meteen na één klik' + (stil.length ? ': niet — ' + stil.join('; ') : ''));
await page.click('.voorbeelden button:text-is("Dubbas")');
await page.waitForFunction(() => globalThis.PB.S.readout?.instrument, null, { timeout: 30000 });
const ri = await page.evaluate(() => ({ ...globalThis.PB.S.readout, tekst: document.querySelector('#readout').innerText }));
ok(ri.rmsDb > -40 && ri.piekDb <= 0 && /niveau/i.test(ri.tekst), `readout van een instrument: niveau ${ri.rmsDb?.toFixed(1)} dB, piek ${ri.piekDb?.toFixed(1)} dB`);
await page.click('text=■ stop');
// stop is echt stil, ook voor een instrument (dat zonder invoer doorspeelt): de motor fadet uit en de audio slaapt
const stilNa = async () => { await page.waitForTimeout(500); return page.evaluate(() => ({ bevestigd: globalThis.PB.speler.stilBevestigd, staat: globalThis.PB.speler.ctx?.state })); };
const st1 = await stilNa();
ok(st1.bevestigd && st1.staat === 'suspended', `stop maakt een instrument echt stil (motor ${st1.bevestigd ? 'stil' : 'speelt door'}, audio ${st1.staat})`);
await page.click('.voorbeelden button:text-is("Dub-echo")');
await page.waitForTimeout(400);
await page.click('text=■ stop');
await page.waitForFunction(() => globalThis.PB.S.readout && !globalThis.PB.S.readout.instrument && Number.isFinite(globalThis.PB.S.readout.thdDb), null, { timeout: 30000 });

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
let gelijk = 0, totaal = 0; const anders = [];
for (const [p, [n, h]] of Object.entries(lijst)) {
  totaal++;
  const ci = join(ROOT, 'bouwpakket', 'dub_echo', p);
  if (!existsSync(ci)) { anders.push(p + ' (ontbreekt)'); continue; }
  const b = readFileSync(ci); let hh = 0; for (let i = 0; i < b.length; i++) hh = (hh * 31 + b[i]) >>> 0;
  if (b.length === n && hh === h) gelijk++; else anders.push(p);
}
const versie = await browser.version();
ok(totaal > 0 && gelijk === totaal, `testsignalen uit Chromium ${versie} = die van CI (${gelijk}/${totaal})${anders.length ? ' — anders: ' + anders.join(', ') : ''}`);

// telefoonbreedte: geen horizontale scroll van de pagina
await page.click('[data-stand="ontwerpen"]');
await page.setViewportSize({ width: 390, height: 844 });
await page.waitForTimeout(200);
const breed = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
ok(breed <= 0, `390 px breed: geen horizontale scroll (${breed} px over)`);
await page.screenshot({ path: join(UIT, 'telefoon.png'), fullPage: false });
await page.setViewportSize({ width: 1440, height: 900 });
await page.screenshot({ path: join(UIT, '1440.png') });

// ---- het spel: een level helemaal spelen (goede antwoorden), een luisterronde klinkt, Draai na geeft 3 sterren, en de
// opdracht wordt pas na het bouwen gehaald
await page.click('[data-stand="spel"]');
await page.waitForTimeout(150);
ok(await page.evaluate(() => !document.querySelector('#spel').hidden && document.querySelectorAll('.levelkaart').length === 7), 'spel: zeven levels');
await page.click('.levelkaart >> nth=0 >> text=speel');
let hoorde = false, fouten2 = [];
for (let i = 0; i < 8; i++) {
  const r = await page.evaluate(() => globalThis.PB.spel.rondes[globalThis.PB.spel.i]);
  if (r.soort === 'draai') {
    const t = await page.evaluate(({ r }) => globalThis.PB.spelStand(r.blok, r.knop, r.doel), { r });
    await page.click('.spelronde >> text=▶ doel'); await page.waitForTimeout(500);
    hoorde ||= await page.evaluate(() => globalThis.PB.speler.speelt() && globalThis.PB.speler.motorDraait);
    await page.locator('.draaischuif input').fill(String(Math.round(t * 1000)));
    await page.click('text=zo klinkt het');
  } else {
    if (r.soort === 'hoor') { await page.click('.spelronde >> text=▶ luister'); await page.waitForTimeout(500); hoorde ||= await page.evaluate(() => globalThis.PB.speler.speelt() && globalThis.PB.speler.motorDraait); }
    await page.locator('.antwoord').nth(r.keuzes.indexOf(r.blok)).click();
  }
  const fb = await page.evaluate(() => document.querySelector('.terugmelding')?.className || '');
  if (!/ok/.test(fb)) fouten2.push(`ronde ${i + 1} (${r.soort} ${r.blok}): ${fb}`);
  await page.click('.terugmelding .knopgroot');
}
const eind = await page.evaluate(() => ({ beeld: globalThis.PB.spel.beeld, sterren: globalThis.PB.spel.sterren, level: globalThis.PB.spel.stand.levels.bron, punten: globalThis.PB.spel.stand.punten }));
ok(fouten2.length === 0 && eind.beeld === 'einde' && eind.sterren === 3 && eind.level?.sterren === 3, `spel: acht rondes goed = 3 sterren, ${eind.punten} punten` + (fouten2.length ? ': ' + fouten2.join('; ') : ''));
ok(hoorde, 'spel: een luister- of Draai-na-ronde speelt echt geluid uit de motor');
await page.click('text=naar de levels');
ok(await page.evaluate(() => !!document.querySelectorAll('.levelkaart')[1].querySelector('.knopgroot')), 'spel: na een ster gaat level 2 open');
// de opdracht
await page.click('.levelkaart >> nth=0 >> text=opdracht');
await page.waitForTimeout(200);
await page.click('.opdracht >> text=controleer');
const nietGehaald = await page.evaluate(() => !document.querySelector('.opdracht.gehaald') && !!document.querySelector('.opdracht .bad'));
await page.click('#bib .kaart:has-text("Oscillator") .plus');
await page.waitForTimeout(300);
await page.click('.opdracht >> text=controleer');
const gehaald = await page.evaluate(() => ({ ui: !!document.querySelector('.opdracht.gehaald'), stand: !!globalThis.PB.spel.stand.levels.bron.opdracht }));
ok(nietGehaald && gehaald.ui && gehaald.stand, 'spel: de opdracht is niet gehaald op een leeg ontwerp, wel na een oscillator erbij');
await page.click('.opdracht >> text=terug naar het spel');
await page.click('text=alle kaarten');
ok(await page.evaluate(() => document.querySelectorAll('.spelkaart').length === 28), 'spel: 28 kaarten');
await page.click('.spelkaart:has-text("Oscillator") .bkkop');
await page.click('.spelkaart:has-text("Oscillator") >> text=▶ hoor');
await page.waitForTimeout(600);
const speeltSpel = await page.evaluate(() => globalThis.PB.speler.speelt() && globalThis.PB.speler.ctx.state === 'running');
await page.click('.spelkaart:has-text("Oscillator") >> text=■ stop');
const st2 = await stilNa();
ok(speeltSpel && st2.bevestigd && st2.staat === 'suspended', `spel: ▶ hoor speelt een toon en ■ stop maakt hem echt stil (audio ${st2.staat})`);
await page.click('[data-stand="ontwerpen"]');

// ---- de iPhone: één scherm tegelijk met een balk onderin, de keten als lijst, alles met een vinger te doen ----
for (const [w, h, naam] of [[390, 844, 'iPhone staand'], [844, 390, 'iPhone liggend']]) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: 3, isMobile: true, hasTouch: true,
    userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1' });
  const tel = await ctx.newPage();
  tel.on('pageerror', (e) => consoleFouten.push(naam + ': ' + e));
  await tel.route(/fonts\.(googleapis|gstatic)\.com/, (r) => r.abort());
  await tel.goto(pathToFileURL(join(ROOT, 'tool', 'index.html')).href);
  await tel.waitForFunction(() => globalThis.PB && document.body.classList.contains('smal'), null, { timeout: 15000 }).catch(() => {});
  ok(await tel.evaluate(() => document.body.classList.contains('smal') && getComputedStyle(document.querySelector('#tabbalk')).display !== 'none'), `${naam}: telefoonweergave met de balk onderin`);
  const fout = [];
  for (const scherm of ['keten', 'blokken', 'blok', 'horen', 'spel', 'bouwen']) {
    await tel.tap(`#tabbalk [data-scherm="${scherm}"]`);
    await tel.waitForTimeout(200);
    const r = await tel.evaluate(() => {
      document.querySelectorAll('#spel .levelkaart .knopgroot')[0]?.closest('#spel:not([hidden])');
      const zichtbaar = (e) => { const b = e.getBoundingClientRect(); return b.width > 0 && b.height > 0 && getComputedStyle(e).visibility !== 'hidden'; };
      const klein = [...document.querySelectorAll('button, select, input:not([type="range"]):not([type="file"])')].filter(zichtbaar)
        .filter((e) => !e.closest('.voorbeelden') || true).filter((e) => e.getBoundingClientRect().height < 35).map((e) => (e.textContent || e.placeholder || e.tagName).trim().slice(0, 20));
      const zoom = [...document.querySelectorAll('input:not([type="range"]), select')].filter(zichtbaar).filter((e) => parseFloat(getComputedStyle(e).fontSize) < 16).length;
      return { over: document.documentElement.scrollWidth - innerWidth, klein, zoom, panelen: ['#bib', '#canvas', '#inspector', '#onder', '#bouwen', '#spel'].filter((q) => zichtbaar(document.querySelector(q))) };
    });
    if (r.over > 0) fout.push(`${scherm}: ${r.over} px opzij`);
    if (r.klein.length) fout.push(`${scherm}: te klein om te tikken: ${r.klein.slice(0, 4).join(', ')}`);
    if (r.zoom) fout.push(`${scherm}: ${r.zoom} velden onder 16 px (Safari zoomt in)`);
    if (r.panelen.length !== 1) fout.push(`${scherm}: ${r.panelen.length} panelen tegelijk (${r.panelen.join(' ')})`);
  }
  ok(fout.length === 0, `${naam}: elk scherm past, één paneel tegelijk, alles groot genoeg` + (fout.length ? ': ' + fout.join('; ') : ''));
  if (h < w) { await ctx.close(); continue; }
  // de keten als lijst: "+ hier" → Blokken → Galm → terug, en Galm zit precies op die plek
  await tel.tap('#tabbalk [data-scherm="keten"]');
  const eerste = await tel.evaluate(() => ({ lijst: !!document.querySelector('.ketenlijst'), kaarten: document.querySelectorAll('.blokkaart').length }));
  ok(eerste.lijst && eerste.kaarten >= 10, `${naam}: de keten als lijst (${eerste.kaarten} blokken)`);
  await tel.tap('.tussen .hier >> nth=2');
  await tel.waitForTimeout(150);
  ok(await tel.evaluate(() => document.body.dataset.scherm === 'blokken' && !!document.querySelector('.doelregel')), `${naam}: "+ hier" opent Blokken en zegt waar het komt`);
  await tel.tap('#bib .kaart:has-text("Een ruimte om het geluid heen") .plus');
  await tel.waitForTimeout(300);
  const galm = await tel.evaluate(() => ({ scherm: document.body.dataset.scherm, fouten: globalThis.PB.compileer().fouten.length, galm: globalThis.PB.S.ontwerp.knopen.some((k) => k.type === 'galm'), melding: document.querySelector('#melding').innerText }));
  ok(galm.scherm === 'keten' && galm.galm && galm.fouten === 0 && /Galm tussen/.test(galm.melding), `${naam}: Galm erin met één tik, terug in de keten: ${galm.melding}`);
  // een schuif in de lijst verzet de knop
  const voorV = await tel.evaluate(() => globalThis.PB.S.ontwerp.parameters.find((p) => p.naam === 'Tijd').standaard);
  await tel.locator('.blokkaart:has-text("Vertraging") input[type="range"]').first().fill('800');
  await tel.waitForTimeout(200);
  const naV = await tel.evaluate(() => globalThis.PB.S.ontwerp.parameters.find((p) => p.naam === 'Tijd').standaard);
  ok(naV !== voorV && naV > voorV, `${naam}: een schuif in de lijst verzet de tijd (${voorV} → ${naV} ms)`);
  // spelen met de knop bovenin, en een knop laten bewegen zonder slepen
  await tel.tap('#mini');
  await tel.waitForTimeout(1200);
  ok(await tel.evaluate(() => globalThis.PB.speler.speelt() && globalThis.PB.speler.motorDraait), `${naam}: ▶ bovenin speelt (motor draait)`);
  await tel.tap('.blokkaart:has-text("Filter") .bkkop');
  await tel.tap('#inspector .beweeg >> nth=0');
  await tel.waitForTimeout(200);
  ok(await tel.evaluate(() => (globalThis.PB.S.ontwerp.modulaties || []).some((m) => m.doel[1] === 'freq')), `${naam}: "laat bewegen" zet een LFO op de frequentie`);
  // het spel op de telefoon: een ronde met grote antwoordknoppen, niets opzij
  await tel.tap('#tabbalk [data-scherm="spel"]');
  await tel.tap('.levelkaart >> nth=0 >> text=/speel|nog eens/');
  await tel.waitForTimeout(150);
  const sp = await tel.evaluate(() => ({ over: document.documentElement.scrollWidth - innerWidth, h: Math.min(...[...document.querySelectorAll('.antwoord')].map((b) => b.getBoundingClientRect().height)), n: document.querySelectorAll('.antwoord').length }));
  ok(sp.n >= 3 && sp.h >= 48 && sp.over <= 0, `${naam}: een spelronde past, antwoorden ${Math.round(sp.h)} px hoog`);
  await tel.screenshot({ path: join(UIT, 'iphone-spel.png') });
  await tel.screenshot({ path: join(UIT, 'iphone.png') });
  await ctx.close();
}

ok(consoleFouten.length === 0, 'geen fouten in de console' + (consoleFouten.length ? ': ' + consoleFouten.slice(0, 3).join(' | ') : ''));
await browser.close();
process.exit(fouten.length ? 1 : 0);
