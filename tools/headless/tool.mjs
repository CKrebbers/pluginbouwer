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
ok(eenvoudig.weergave === 'eenvoudig' && /tail/i.test(eenvoudig.tekst) && !/smp|latency|aliasing/i.test(eenvoudig.tekst), 'eenvoudig: de readout in gewone woorden (' + eenvoudig.tekst.replace(/\s+/g, ' ') + ')');
ok(/sources/i.test(eenvoudig.bib) && /combinaties/i.test(eenvoudig.bib) && !/% cpu|smp/.test(eenvoudig.bib), 'eenvoudig: blokken per familie en combinaties, zonder cpu en samples');
ok(/^Weg van het geluid: Input → /.test(eenvoudig.samenvatting), 'boven de keten staat wat het ontwerp doet');
// + bij Galm zet hem in de keten (vóór de uitgang), en het blijft klinken: daarna terug naar de dub-echo
const voor = await page.evaluate(() => globalThis.PB.S.ontwerp.knopen.length);
await page.click('#bib .kaart:has-text("Een ruimte om het geluid heen") .plus');
await page.waitForTimeout(300);
const na = await page.evaluate(() => ({ n: globalThis.PB.S.ontwerp.knopen.length, fouten: globalThis.PB.compileer().fouten, galm: globalThis.PB.S.ontwerp.kabels.some((k) => k.van[0].startsWith('galm') && k.naar[0] === 'uit'), melding: document.querySelector('#melding').innerText }));
ok(na.n === voor + 1 && na.fouten.length === 0 && na.galm && /Reverb tussen/.test(na.melding), '+ zet Reverb in de keten vóór de uitgang: ' + na.melding);
await page.click('[data-weergave="technisch"]');
await page.click('.voorbeelden button:text-is("Dub echo")').catch(() => {});
await page.evaluate(() => { localStorage.removeItem('pb.ontwerp'); });
await page.waitForTimeout(400);
if (await page.evaluate(() => globalThis.PB.speler.speelt())) await page.click('text=■ stop');
await page.waitForFunction(() => globalThis.PB.S.readout && !globalThis.PB.S.readout.fout && /latency/i.test(document.querySelector('#readout').innerText), null, { timeout: 30000 });
const r = await page.evaluate(() => ({ ...globalThis.PB.S.readout, fouten: globalThis.PB.compileer().fouten, tekst: document.querySelector('#readout').innerText }));
ok(r.fouten.length === 0, 'de dub-echo compileert zonder fouten');
ok(Number.isFinite(r.thdDb) && Number.isFinite(r.aliasingDb), `readout meet (thd ${r.thdDb?.toFixed(1)} dB, aliasing ${r.aliasingDb?.toFixed(1)} dB)`);
ok(/latency/i.test(r.tekst) && /tail/i.test(r.tekst), 'readout toont latency en tail');

// een knop draaien = de readout meet opnieuw, met een pijl
await page.evaluate(() => { const p = globalThis.PB.S.ontwerp.parameters.find((q) => q.naam === 'Saturation'); p.standaard = 18; globalThis.PB.gewijzigd('verzadiging 18 dB'); });
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
  if (naam === 'Keys pad') { await page.waitForTimeout(300); for (const k of ['a', 'f', 'h']) { await page.keyboard.down(k); await page.waitForTimeout(350); await page.keyboard.up(k); } }
  await page.waitForTimeout(1500);
  const r = await page.evaluate(() => ({ max: globalThis.__max, fouten: globalThis.PB.compileer().fouten, speelt: globalThis.PB.speler.speelt() }));
  if (r.fouten.length || !r.speelt || r.max < 0.03) stil.push(`${naam} (meter ${r.max.toFixed(3)}, ${r.fouten.length} fouten, speelt ${r.speelt})`);
}
await page.evaluate(() => clearInterval(globalThis.__peil));
ok(stil.length === 0, 'elk voorbeeld klinkt meteen na één klik' + (stil.length ? ': niet — ' + stil.join('; ') : ''));
await page.click('.voorbeelden button:text-is("Dub bass")');
await page.waitForFunction(() => globalThis.PB.S.readout?.instrument, null, { timeout: 30000 });
const ri = await page.evaluate(() => ({ ...globalThis.PB.S.readout, tekst: document.querySelector('#readout').innerText }));
ok(ri.rmsDb > -40 && ri.piekDb <= 0 && /level/i.test(ri.tekst), `readout van een instrument: niveau ${ri.rmsDb?.toFixed(1)} dB, piek ${ri.piekDb?.toFixed(1)} dB`);
await page.click('text=■ stop');
// stop is echt stil, ook voor een instrument (dat zonder invoer doorspeelt): de motor fadet uit en de audio slaapt
const stilNa = async () => { await page.waitForTimeout(500); return page.evaluate(() => ({ bevestigd: globalThis.PB.speler.stilBevestigd, staat: globalThis.PB.speler.ctx?.state })); };
const st1 = await stilNa();
ok(st1.bevestigd && st1.staat === 'suspended', `stop maakt een instrument echt stil (motor ${st1.bevestigd ? 'stil' : 'speelt door'}, audio ${st1.staat})`);
await page.click('.voorbeelden button:text-is("Dub echo")');
await page.waitForTimeout(400);
await page.click('text=■ stop');
await page.waitForFunction(() => globalThis.PB.S.readout && !globalThis.PB.S.readout.instrument && Number.isFinite(globalThis.PB.S.readout.thdDb), null, { timeout: 30000 });

// tabs
for (const t of ['Frontpaneel', 'Meten', 'Verken', 'Werkplaats', 'Schetsboek', 'ABX', 'Taaktoets']) {
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
await page.evaluate(() => { const p = globalThis.PB.S.ontwerp.parameters.find((q) => q.naam === 'Saturation'); p.standaard = 6; globalThis.PB.gewijzigd('terug'); });
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

// ---- templates: mappen maken, bewaren, ordenen, verplaatsen, openen; blijft na herladen (als losse pagina in de browser)
await page.click('[data-stand="ontwerpen"]');
await page.click('.tabs >> text=Werkplaats');
const tp = () => page.evaluate(() => ({ t: globalThis.PB.TP.templates.map((x) => ({ naam: x.naam, map: x.map, plek: x.plek })), m: globalThis.PB.TP.mappen.map((x) => ({ id: x.id, naam: x.naam, plek: x.plek })), bron: globalThis.PB.TP.bron }));
const mapErbij = async (n) => { await page.fill('#tpnieuwemap', n); await page.click('#tpmaperbij'); };
const bewaar = async (n, map) => { await page.fill('#tpnaam', n); await page.selectOption('#tpmap', { label: map }); await page.click('#tpbewaar'); await page.waitForTimeout(50); };
await mapErbij('Dub'); await mapErbij('Synth');
await bewaar('My echo', 'Dub');
await page.evaluate(() => { const p = globalThis.PB.S.ontwerp.parameters.find((q) => q.naam === 'Feedback'); p.standaard = 0.9; globalThis.PB.gewijzigd('meer feedback'); });
await page.waitForTimeout(150);
await bewaar('Long echo', 'Dub');
let st = await tp();
ok(st.bron === 'browser' && st.m.map((m) => m.naam).join() === 'Dub,Synth' && st.t.map((t) => t.naam).join() === 'My echo,Long echo', `templates: twee mappen en twee templates bewaard (${st.t.map((t) => t.naam)})`);
const kopNamen = () => page.$$eval('#tplijst .mapkop .mapnaam', (e) => e.map((x) => x.firstChild.textContent));
const kaartNamen = (map) => page.$$eval(`#tplijst .mapkop[data-map="${map}"] + .kaarten .kn`, (e) => e.map((x) => x.textContent));
const dub = st.m.find((m) => m.naam === 'Dub').id, synth = st.m.find((m) => m.naam === 'Synth').id;
ok((await kaartNamen(dub)).join() === 'My echo,Long echo', 'templates: in hun map, in volgorde van bewaren');
await page.click(`#tplijst .kaart:has(.kn:text-is("Long echo")) button[aria-label="omhoog"]`);
ok((await kaartNamen(dub)).join() === 'Long echo,My echo', 'templates: ▲ zet hem eerder');
await page.click(`#tplijst .mapkop[data-map="${synth}"] button[aria-label="map omhoog"]`);
ok((await kopNamen()).join() === 'Synth,Dub', 'templates: een map omhoog');
await page.selectOption(`#tplijst .kaart:has(.kn:text-is("My echo")) select`, synth);
ok((await kaartNamen(synth)).join() === 'My echo' && (await kaartNamen(dub)).join() === 'Long echo', 'templates: naar een andere map');
await page.click(`#tplijst .kaart:has(.kn:text-is("Long echo")) button:text-is("open")`);
await page.waitForTimeout(300);
const geopend = await page.evaluate(() => ({ naam: globalThis.PB.S.ontwerp.naam, fb: globalThis.PB.S.ontwerp.parameters.find((q) => q.naam === 'Feedback')?.standaard }));
ok(geopend.naam === 'Long echo' && geopend.fb === 0.9, `templates: openen laadt het bewaarde ontwerp (${geopend.naam}, feedback ${geopend.fb})`);
await page.reload();
await page.waitForFunction(() => globalThis.PB && globalThis.PB.TP.bron === 'browser', null, { timeout: 15000 });
st = await tp();
ok(st.t.length === 2 && st.m.length === 2, 'templates: na herladen nog alles (deze browser)');
await page.click('.tabs >> text=Werkplaats');
const [tdl] = await Promise.all([page.waitForEvent('download'), page.click('#onder button:text-is("exporteer")')]);
const tjson = JSON.parse(readFileSync(await tdl.path(), 'utf8'));
ok(tjson.soort === 'pluginbouwer-templates' && tjson.templates.length === 2 && tjson.mappen.length === 2, 'templates: exporteer geeft één JSON-bestand');
await page.click(`#tplijst .mapkop[data-map="${synth}"] button:text-is("weg")`);
await page.click(`#tplijst .mapkop[data-map="${synth}"] button:text-is("weg")`);
st = await tp();
ok(st.m.length === 1 && st.t.find((t) => t.naam === 'My echo').map === 'los' && (await kopNamen()).includes('Los'), 'templates: map weg (twee tikken), zijn templates gaan naar Los');
// terug naar de dub-echo voor wat volgt
await page.evaluate(() => { localStorage.clear(); });
await page.reload();
await page.waitForFunction(() => globalThis.PB && globalThis.PB.S.readout, null, { timeout: 30000 });

// ---- samen met Claude (met een nagebouwde database, zoals op claude.ai): meekijken, een voorstel horen, overnemen
{
  const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  await ctx.addInitScript({ path: join(ROOT, 'tools', 'headless', 'nep-db.js') });
  const p = await ctx.newPage();
  p.on('pageerror', (e) => consoleFouten.push('samen: ' + e));
  await p.route(/fonts\.(googleapis|gstatic)\.com/, (r) => r.abort());
  await p.goto(pathToFileURL(join(ROOT, 'tool', 'index.html')).href);
  await p.waitForFunction(() => globalThis.PB && globalThis.PB.TP.bron === 'gedeeld' && globalThis.__nepDb.store.has('werkbank/nu'), null, { timeout: 20000 });
  const wb = () => p.evaluate(() => globalThis.__nepDb.store.get('werkbank/nu'));
  ok((await wb()).meekijken === true && (await wb()).naam === 'Dub echo', 'samen: bij openen ziet Claude het ontwerp (standaard meekijken)');
  await p.evaluate(() => { const q = globalThis.PB.S.ontwerp.parameters.find((x) => x.naam === 'Time'); q.standaard = 500; globalThis.PB.gewijzigd('Time 500'); });
  await p.waitForFunction(() => globalThis.__nepDb.store.get('werkbank/nu').ontwerp.parameters.find((x) => x.naam === 'Time').standaard === 500, null, { timeout: 5000 }).catch(() => {});
  const na = await wb();
  ok(na.ontwerp.parameters.find((x) => x.naam === 'Time').standaard === 500 && na.wat === 'Time 500' && na.geschiedenis.at(-1).wat === 'Time 500', 'samen: een wijziging komt binnen 1,2 s door, met wat er gebeurde');
  // Claude doet een voorstel: de keten van nu plus een Reverb, Feedback hoger
  await p.evaluate(() => {
    const o = JSON.parse(JSON.stringify(globalThis.PB.S.ontwerp));
    o.knopen.push({ id: 'galm1', type: 'galm', x: 900, y: 320 });
    const k = o.kabels.find((x) => x.naar[0] === 'uit'); const van = k.van; k.van = ['galm1', 'uit'];
    o.kabels.push({ van, naar: ['galm1', 'in'] });
    o.parameters.find((x) => x.naam === 'Feedback').standaard = 0.875;   // exact in float32, anders weigert de compiler
    globalThis.__nepDb.schrijf('voorstellen/v-proef', { titel: 'Meer ruimte', waarom: 'Een galm achter de echo, en wat meer feedback.', ontwerp: o, basis: globalThis.__nepDb.store.get('werkbank/nu').afdruk, status: 'open', wanneer: new Date().toISOString() });
  });
  await p.waitForSelector('#voorstel:not([hidden])', { timeout: 5000 }).catch(() => {});
  const balk = await p.evaluate(() => ({ zicht: !document.querySelector('#voorstel').hidden, tekst: document.querySelector('#voorstel').innerText }));
  ok(balk.zicht && /Meer ruimte/.test(balk.tekst) && /Reverb erbij/.test(balk.tekst) && /Feedback|feedback/.test(balk.tekst), 'samen: het voorstel staat bovenin, met wat er anders is');
  await p.click('#voorstel [data-ab="B"]');
  await p.waitForTimeout(1200);
  const hoor = await p.evaluate(() => ({ ab: globalThis.PB.TP.ab, speelt: globalThis.PB.speler.speelt() }));
  ok(hoor.ab === 'B' && hoor.speelt, 'samen: "hoor voorstel" speelt het voorstel (slot B)');
  await p.click('#neemover');
  await p.waitForTimeout(300);
  const over = await p.evaluate(() => ({ galm: globalThis.PB.S.ontwerp.knopen.some((k) => k.type === 'galm'), fb: globalThis.PB.S.ontwerp.parameters.find((x) => x.naam === 'Feedback').standaard, status: globalThis.__nepDb.store.get('voorstellen/v-proef').status, versies: globalThis.PB.TP.versies.length, balk: document.querySelector('#voorstel').hidden, ab: globalThis.PB.TP.ab }));
  ok(over.galm && over.fb === 0.875 && over.status === 'aangenomen' && over.versies === 1 && over.balk && over.ab === 'A', `samen: overnemen zet het voorstel in de keten, bewaart de vorige versie, en meldt het terug (${over.status})`);
  await p.waitForFunction(() => globalThis.__nepDb.store.get('werkbank/nu').ontwerp.knopen.some((k) => k.type === 'galm'), null, { timeout: 5000 }).catch(() => {});
  ok((await wb()).ontwerp.knopen.some((k) => k.type === 'galm'), 'samen: en Claude ziet daarna het nieuwe ontwerp');
  // een voorstel dat niet klopt (0,85 is niet exact in float32): de balk zegt het en biedt alleen "nee"
  await p.evaluate(() => { const o = JSON.parse(JSON.stringify(globalThis.__nepDb.store.get('werkbank/nu').ontwerp)); o.parameters.find((x) => x.naam === 'Feedback').standaard = 0.85; globalThis.__nepDb.schrijf('voorstellen/v-kapot', { titel: 'Kapot', ontwerp: o, status: 'open', wanneer: new Date().toISOString() }); });
  await p.waitForSelector('#voorstel:not([hidden])', { timeout: 5000 }).catch(() => {});
  ok(await p.evaluate(() => /klopt nog niet/.test(document.querySelector('#voorstel').innerText) && !document.querySelector('#neemover')), 'samen: een voorstel dat niet klopt kun je niet overnemen');
  await p.click('#nietnu');
  await p.waitForTimeout(200);
  // een tweede voorstel afwijzen; dan meekijken uit; een versie terugzetten
  await p.evaluate(() => globalThis.__nepDb.schrijf('voorstellen/v-twee', { titel: 'Droger', waarom: '', ontwerp: globalThis.__nepDb.store.get('werkbank/nu').ontwerp, status: 'open', wanneer: new Date().toISOString() }));
  await p.waitForSelector('#voorstel:not([hidden])', { timeout: 5000 }).catch(() => {});
  await p.click('#nietnu');
  await p.waitForTimeout(200);
  ok(await p.evaluate(() => globalThis.__nepDb.store.get('voorstellen/v-twee').status === 'afgewezen' && document.querySelector('#voorstel').hidden), 'samen: "nee" sluit het voorstel en meldt het terug');
  await p.click('.tabs >> text=Werkplaats');
  await p.click('#meekijkenuit');
  await p.waitForTimeout(200);
  const uit = await wb();
  ok(uit.meekijken === false && !uit.ontwerp, 'samen: "even niet" haalt het ontwerp weg bij Claude');
  await p.click('#onder button:text-is("terug naar deze")');
  await p.waitForTimeout(200);
  ok(await p.evaluate(() => !globalThis.PB.S.ontwerp.knopen.some((k) => k.type === 'galm') && globalThis.PB.TP.versies.length === 2), 'samen: een versie terugzetten (en de huidige blijft ook bewaard)');
  await p.screenshot({ path: join(UIT, 'samen.png') });
  await ctx.close();
}

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
  ok(galm.scherm === 'keten' && galm.galm && galm.fouten === 0 && /Reverb tussen/.test(galm.melding), `${naam}: Reverb erin met één tik, terug in de keten: ${galm.melding}`);
  // een schuif in de lijst verzet de knop
  const voorV = await tel.evaluate(() => globalThis.PB.S.ontwerp.parameters.find((p) => p.naam === 'Time').standaard);
  await tel.locator('.blokkaart:has-text("Delay") input[type="range"]').first().fill('800');
  await tel.waitForTimeout(200);
  const naV = await tel.evaluate(() => globalThis.PB.S.ontwerp.parameters.find((p) => p.naam === 'Time').standaard);
  ok(naV !== voorV && naV > voorV, `${naam}: een schuif in de lijst verzet de tijd (${voorV} → ${naV} ms)`);
  // spelen met de knop bovenin, en een knop laten bewegen zonder slepen
  await tel.tap('#mini');
  await tel.waitForTimeout(1200);
  ok(await tel.evaluate(() => globalThis.PB.speler.speelt() && globalThis.PB.speler.motorDraait), `${naam}: ▶ bovenin speelt (motor draait)`);
  await tel.tap('.blokkaart:has-text("Filter") .bkkop');
  await tel.tap('#inspector .beweeg >> nth=0');
  await tel.waitForTimeout(200);
  ok(await tel.evaluate(() => (globalThis.PB.S.ontwerp.modulaties || []).some((m) => m.doel[1] === 'freq')), `${naam}: "laat bewegen" zet een LFO op de frequentie`);
  // templates op de telefoon: een map en een template met tikken, alles groot genoeg, niets opzij
  await tel.tap('#tabbalk [data-scherm="horen"]');
  await tel.tap('.tabs [data-tab="werkplaats"]');
  await tel.fill('#tpnieuwemap', 'Dub'); await tel.tap('#tpmaperbij');
  await tel.selectOption('#tpmap', { label: 'Dub' }); await tel.tap('#tpbewaar');
  await tel.waitForTimeout(150);
  const tt = await tel.evaluate(() => {
    const zicht = (e) => { const b = e.getBoundingClientRect(); return b.width > 0 && b.height > 0; };
    return { n: globalThis.PB.TP.templates.length, over: document.documentElement.scrollWidth - innerWidth,
      klein: [...document.querySelectorAll('#onder button, #onder select, #onder input:not([type="file"])')].filter(zicht).filter((e) => e.getBoundingClientRect().height < 35).map((e) => e.textContent || e.id) };
  });
  ok(tt.n === 1 && tt.over <= 0 && !tt.klein.length, `${naam}: templates met tikken (${tt.n} bewaard, ${tt.over} px opzij${tt.klein.length ? ', te klein: ' + tt.klein.join(', ') : ''})`);
  await tel.screenshot({ path: join(UIT, 'iphone-templates.png') });
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
