// spel.js — de blokken spelenderwijs leren kennen. Zeven levels (de families en een meesterproef), elk met rondes van
// drie soorten en een bouwopdracht:
//   "Wie ben ik?"  een zin bij een blok, of een blok bij een zin;
//   "Wat hoor je?" een kort stuk door één blok (een proefopstelling op de echte motor): welk blok is het?
//   "Draai na"     hoor het doel en zet één knop zo dat het hetzelfde klinkt;
//   opdracht       bouw iets in de keten; de tool meet zelf of het klopt.
// Geen DOM: dezelfde code draait in de browser en in de toetsen (tests/spel.test.mjs). Toeval met een zaad (xorshift).
import { BIB, compileer } from './keten.js';
import { gewoneNaam, familieVan, hoofdweg } from './rollen.js';
import { renderKeten, gemiddeldSpectrum } from './meten.js';
import { ruis, stukjes, impuls } from './signalen.js';

// ---------------------------------------------------------------------------------------------------- proefopstellingen
// Per blok een kleine keten waarin je dat blok duidelijk hoort, met overdreven standen. `knop` = welke knop Draai na
// gebruikt (die wordt plugin-parameter 0, zodat hij tijdens het spelen kan draaien zonder herladen).
const effect = (type, waarden = {}, vast) => ({ knopen: [{ id: 'in', type: 'in' }, { id: 'x', type, waarden, ...(vast ? { vast } : {}) }, { id: 'uit', type: 'uit' }],
  kabels: [{ van: ['in', 'uit'], naar: ['x', 'in'] }, { van: ['x', BIB[type].uit[0]], naar: ['uit', 'in'] }] });
const gemengd = (type, waarden = {}, mix = 0.5, vast) => ({ knopen: [{ id: 'in', type: 'in' }, { id: 'x', type, waarden, ...(vast ? { vast } : {}) },
  { id: 'mix', type: 'mix', waarden: { mix } }, { id: 'uit', type: 'uit' }],
  kabels: [{ van: ['in', 'uit'], naar: ['x', 'in'] }, { van: ['in', 'uit'], naar: ['mix', 'droog'] }, { van: ['x', 'uit'], naar: ['mix', 'nat'] }, { van: ['mix', 'uit'], naar: ['uit', 'in'] }] });
const zacht = (knopen, kabels, bron) => ({ knopen: [...knopen, { id: 'vol', type: 'versterker', waarden: { hoeveel: 0.5 } }, { id: 'uit', type: 'uit' }],
  kabels: [...kabels, { van: [bron, 'uit'], naar: ['vol', 'in'] }, { van: ['vol', 'uit'], naar: ['uit', 'in'] }] });
// een filter met iets dat zijn frequentie beweegt
const bewogen = (type, waarden, diepte = 0.4) => ({ knopen: [{ id: 'in', type: 'in' }, { id: 'f', type: 'svf', waarden: { freq: 700, q: 3 } }, { id: 'x', type, waarden }, { id: 'uit', type: 'uit' }],
  kabels: [{ van: ['in', 'uit'], naar: ['f', 'in'] }, { van: ['f', 'band'], naar: ['uit', 'in'] }],
  modulaties: [{ bron: ['x', 'golf'], doel: ['f', 'freq'], diepte }] });

export const PROEF = {
  osc: { o: () => zacht([{ id: 'x', type: 'osc', waarden: { freq: 110 }, vast: { golf: 1 } }], [], 'x'), knop: 'freq' },
  ruis: { o: () => zacht([{ id: 'x', type: 'ruis', waarden: { kleur: 0.25 } }], [], 'x'), knop: 'kleur' },
  snaar: { o: () => zacht([{ id: 's', type: 'sequencer', waarden: { tempo: 120 }, vast: { patroon: 6 } }, { id: 'x', type: 'snaar', waarden: { freq: 147, verval: 2, helder: 0.5 } }],
    [{ van: ['s', 'poort'], naar: ['x', 'poort'] }, { van: ['s', 'toon'], naar: ['x', 'toon'] }], 'x'), knop: 'helder' },
  trommel: { o: () => zacht([{ id: 's', type: 'sequencer', waarden: { tempo: 120 }, vast: { patroon: 0 } }, { id: 'x', type: 'trommel', waarden: { freq: 55, val: 3, verval: 380 } }],
    [{ van: ['s', 'poort'], naar: ['x', 'poort'] }], 'x'), knop: 'freq' },
  tape: { o: () => effect('tape', { verzadiging: 18, asymmetrie: 0.25 }), knop: 'verzadiging' },
  hoogdoor1: { o: () => effect('hoogdoor1', { freq: 700 }), knop: 'freq' },
  laagdoor1: { o: () => effect('laagdoor1', { freq: 400 }), knop: 'freq' },
  svf: { o: () => { const o = effect('svf', { freq: 1500, q: 4 }); o.kabels[1].van = ['x', 'band']; return o; }, knop: 'freq' },
  biquad: { o: () => effect('biquad', { freq: 250, q: 8 }, { soort: 0 }), knop: 'freq' },
  vergruizer: { o: () => effect('vergruizer', { bits: 4, deling: 8 }), knop: 'bits' },
  vouwer: { o: () => effect('vouwer', { drive: 30 }), knop: 'drive' },
  // een compressor hoor je het best als hij pompt: een kick op "luister naar" duwt de groove op elke tel omlaag
  compressor: { o: () => { const o = effect('compressor', { drempel: -36, verhouding: 12, aanzet: 1, loslaten: 250 });
    o.knopen.push({ id: 's', type: 'sequencer', waarden: { tempo: 120 }, vast: { patroon: 0 } }, { id: 'k', type: 'trommel', waarden: { freq: 50, val: 3, verval: 300 } });
    o.kabels.push({ van: ['s', 'poort'], naar: ['k', 'poort'] }, { van: ['k', 'uit'], naar: ['x', 'zijketen'] }); return o; }, knop: 'drempel' },
  vertraging: { o: () => { const o = gemengd('vertraging', { tijd: 375 }, 0.5); o.knopen[1].vast = { max: 2000 }; return o; }, knop: 'tijd' },
  galm: { o: () => gemengd('galm', { tijd: 4 }, 0.5), knop: 'tijd' },
  chorus: { o: () => gemengd('chorus', { tijd: 12, diepte: 6, snelheid: 1.5 }, 0.5), knop: 'snelheid' },
  panner: { o: () => ({ ...effect('panner'), knopen: [...effect('panner').knopen, { id: 'l', type: 'lfo', waarden: { snelheid: 0.5 } }], modulaties: [{ bron: ['l', 'golf'], doel: ['x', 'positie'], diepte: 0.5 }] }),
    koptelefoon: true },
  lfo: { o: () => bewogen('lfo', { snelheid: 1 }), knop: 'snelheid' },
  willekeur: { o: () => bewogen('willekeur', { snelheid: 4, glad: 0 }), knop: 'snelheid' },
  bandsnelheid: { o: () => { const o = gemengd('vertraging', { tijd: 200 }, 0.6); o.knopen[1].vast = { max: 1200 };
    o.knopen.push({ id: 'b', type: 'bandsnelheid', waarden: { diepte: 1 } }); o.kabels.push({ van: ['b', 'snelheid'], naar: ['x', 'beweging'] }); return o; } },
  omhulling: { o: () => ({ knopen: [{ id: 's', type: 'sequencer', waarden: { tempo: 120, lengte: 0.25 }, vast: { patroon: 5 } }, { id: 'osc', type: 'osc', waarden: { freq: 110 }, vast: { golf: 1 } },
    { id: 'x', type: 'omhulling', waarden: { aanzet: 2, verval: 150, aanhoud: 0, loslaten: 80 } }, { id: 'vca', type: 'vermenigvuldig' }, { id: 'vol', type: 'versterker', waarden: { hoeveel: 0.6 } }, { id: 'uit', type: 'uit' }],
  kabels: [{ van: ['s', 'toon'], naar: ['osc', 'toon'] }, { van: ['s', 'poort'], naar: ['x', 'poort'] }, { van: ['osc', 'uit'], naar: ['vca', 'a'] }, { van: ['x', 'omhulling'], naar: ['vca', 'b'] },
    { van: ['vca', 'uit'], naar: ['vol', 'in'] }, { van: ['vol', 'uit'], naar: ['uit', 'in'] }] }), knop: 'verval' },
};
export const HOORBAAR = Object.keys(PROEF);

// Een proefopstelling als ontwerp; met `knop` wordt die knop parameter 0 (standaard = `waarde`, of wat er staat).
export function proefOntwerp(type, { knop = null, waarde } = {}) {
  const p = PROEF[type], o = { naam: 'Proef: ' + gewoneNaam(type), id: 'proef_' + type, modulaties: [], parameters: [], ...p.o() };
  o.knopen = o.knopen.map((k, i) => ({ x: 40 + i * 160, y: 160, waarden: {}, ...k }));
  if (knop) {
    const def = BIB[type].knoppen.find((k) => k.id === knop), kn = o.knopen.find((k) => k.id === 'x');
    const v = Math.fround(waarde ?? kn.waarden[knop] ?? def.std);
    o.parameters.push({ naam: def.naam, knoop: 'x', knop, min: def.min, max: def.max, standaard: v, glijMs: 30 });
  }
  return o;
}
// een knopstand t ∈ [0, 1] (zoals een schuif) naar de waarde in de eigen eenheid, en terug
export function knopWaarde(type, knop, t) { const d = BIB[type].knoppen.find((k) => k.id === knop); return d.exp ? d.min * Math.pow(d.max / d.min, t) : d.min + t * (d.max - d.min); }
export function knopStand(type, knop, v) { const d = BIB[type].knoppen.find((k) => k.id === knop); return d.exp ? Math.log(v / d.min) / Math.log(d.max / d.min) : (v - d.min) / (d.max - d.min); }

// ---------------------------------------------------------------------------------------------------- levels
const LEEG_EFFECT = () => ({ naam: 'Opdracht', id: 'opdracht', uitleg: '', knopen: [{ id: 'in', type: 'in', x: 40, y: 200 }, { id: 'uit', type: 'uit', x: 600, y: 200 }], kabels: [], parameters: [], modulaties: [] });
const LEEG_INSTRUMENT = () => ({ naam: 'Opdracht', id: 'opdracht', uitleg: '', knopen: [{ id: 'uit', type: 'uit', x: 600, y: 200 }], kabels: [], parameters: [], modulaties: [] });
const vanFamilie = (f) => Object.keys(BIB).filter((t) => !BIB[t].vast && familieVan(t) === f);

export const LEVELS = [
  { id: 'bron', naam: 'Maakt geluid', blokken: vanFamilie('bron'), opdracht: 'geluid' },
  { id: 'kleur', naam: 'Kleurt de klank', blokken: vanFamilie('kleur'), opdracht: 'donker' },
  { id: 'ruimte', naam: 'Ruimte en tijd', blokken: vanFamilie('ruimte'), opdracht: 'echo' },
  { id: 'beweging', naam: 'Beweegt een knop', blokken: vanFamilie('beweging'), opdracht: 'wiebel' },
  { id: 'spelen', naam: 'Speelt noten', blokken: vanFamilie('spelen'), opdracht: 'noot' },
  { id: 'verbinden', naam: 'Verbindt', blokken: vanFamilie('verbinden'), opdracht: 'twee' },
  { id: 'meester', naam: 'Meesterproef', blokken: Object.keys(BIB).filter((t) => !BIB[t].vast), opdracht: 'dub' },
];
export const level = (id) => LEVELS.find((l) => l.id === id);
export const RONDES_PER_LEVEL = 8;

// ---------------------------------------------------------------------------------------------------- opdrachten
// Elke opdracht: een begin, een doel in gewone woorden, een hint, en een controle die MEET (op de motor), niet alleen
// naar de blokken kijkt. `controleer(E, ontwerp)` geeft { gehaald, waarom }.
const SR = 48000;
const db = (x) => 20 * Math.log10(x + 1e-12);
function rms(y, van = 0, tot = y.length) { let s = 0; for (let i = van; i < tot; i++) s += y[i] * y[i]; return Math.sqrt(s / Math.max(1, tot - van)); }
function piek(y, van = 0, tot = y.length) { let p = 0; for (let i = van; i < tot; i++) p = Math.max(p, Math.abs(y[i])); return p; }
// de fout van de compiler in gewone woorden
export function foutInWoorden(f) {
  if (/geen ingang en geen bron/.test(f)) return 'Er zit nog niets in dat geluid maakt of doorgeeft.';
  if (/geen uitgang/.test(f)) return 'Er is geen uitgang meer.';
  if (/geen vertraging/.test(f)) return 'Een lus terug moet door een vertraging lopen, anders kan het niet.';
  return f;
}
function draai(E, o, invoer, gebeurtenissen = []) {
  const c = compileer(o);
  if (c.fouten.length) return { fout: foutInWoorden(c.fouten[0]) };
  const waarden = Object.fromEntries(c.params.map((p, i) => [i, p.standaard]));
  return { y: renderKeten(E, c.rij, invoer, { sr: SR, waarden, gebeurtenissen }).uit[0], c };
}
const stil = (sec) => [new Float32Array(SR * sec), new Float32Array(SR * sec)];
const hoog = (y) => { const l = gemiddeldSpectrum(y, SR, { punten: 120 }).filter((p) => p.f > 4000); return 10 * Math.log10(l.reduce((s, p) => s + 10 ** (p.db / 10), 0) / l.length + 1e-30); };
// de toetsen spelen: vier noten van een halve seconde, dan stil (zoals de meting in de tool)
export function notenProef(o, c) {
  const t = o.knopen.find((k) => k.type === 'toetsen'); if (!t) return [];
  const pn = c.params.findIndex((p) => p.knoop === t.id && p.knop === 'toon'), pp = c.params.findIndex((p) => p.knoop === t.id && p.knop === 'poort');
  if (pn < 0 || pp < 0) return [];
  return [0, 7, 12, 3].flatMap((st, i) => [[i * 24000, pn, 2 ** (st / 12)], [i * 24000, pp, 1], [i * 24000 + 16000, pp, 0]]);
}

export const OPDRACHTEN = {
  geluid: { naam: 'Maak een geluid', doel: 'Laat de plugin zelf geluid maken: zet iets dat geluid maakt op de uitgang.',
    hint: 'Kies een blok uit "Maakt geluid" en tik op +.', begin: LEEG_INSTRUMENT,
    controleer(E, o) {
      const r = draai(E, o, stil(2)); if (r.fout) return { gehaald: false, waarom: r.fout };
      const n = db(rms(r.y));
      return n > -40 ? { gehaald: true, waarom: `Er komt geluid uit (${Math.round(n)} dB).` } : { gehaald: false, waarom: 'Er komt nog (bijna) niets uit.' };
    } },
  donker: { naam: 'Maak het donkerder', doel: 'Haal het hoog uit de ruis, zodat hij dof en donker klinkt (minstens 12 dB minder hoog), maar wel hoorbaar blijft.',
    hint: 'Een filter dat het hoog weghaalt, en draai zijn frequentie omlaag.', begin: LEEG_EFFECT,
    controleer(E, o) {
      const x = ruis(SR, 2, 0.3, 7), r = draai(E, o, [x, x]); if (r.fout) return { gehaald: false, waarom: r.fout };
      const minder = hoog(x) - hoog(r.y), n = db(rms(r.y));
      if (n < -40) return { gehaald: false, waarom: 'Het is nu bijna stil: laat het geluid er wel door.' };
      return minder >= 12 ? { gehaald: true, waarom: `Het hoog is ${Math.round(minder)} dB zachter.` } : { gehaald: false, waarom: `Het hoog is maar ${Math.max(0, Math.round(minder))} dB zachter; het moet 12 dB. Draai de frequentie verder omlaag.` };
    } },
  echo: { naam: 'Een echo erbij', doel: 'Laat het origineel horen en daarna een echo, minstens 50 ms later.',
    hint: 'Een vertraging alleen geeft alleen de echo: meng hem met het origineel. "Echo met herhalingen" bij Combinaties kan ook.', begin: LEEG_EFFECT,
    controleer(E, o) {
      const x = impuls(SR * 2, 0.5), r = draai(E, o, [x, x]); if (r.fout) return { gehaald: false, waarom: r.fout };
      const nu = piek(r.y, 0, SR * 0.01), later = piek(r.y, SR * 0.05, SR * 2);
      if (nu < 0.05) return { gehaald: false, waarom: 'Het origineel hoor je niet meer: meng het droge geluid erbij.' };
      return later > 0.03 ? { gehaald: true, waarom: 'Origineel en echo zijn er allebei.' } : { gehaald: false, waarom: 'Na het origineel komt er nog geen echo.' };
    } },
  wiebel: { naam: 'Laat iets wiebelen', doel: 'Laat een knop vanzelf bewegen, zodat het geluid golft.',
    hint: 'Zet een filter in de keten, kies hem, en tik bij een knop op "laat bewegen".', begin: LEEG_EFFECT,
    controleer(E, o) {
      const c = compileer(o); if (c.fouten.length) return { gehaald: false, waarom: foutInWoorden(c.fouten[0]) };
      const { pad } = hoofdweg(o);
      const beweegt = (o.modulaties || []).some((m) => pad.includes(m.doel[0]) && m.diepte > 0);
      return beweegt ? { gehaald: true, waarom: 'Er beweegt een knop op de weg van het geluid.' } : { gehaald: false, waarom: 'Er beweegt nog geen knop van een blok waar het geluid doorheen gaat.' };
    } },
  noot: { naam: 'Speel een noot', doel: 'Maak een instrument dat je met je toetsenbord speelt, en dat stil wordt als je loslaat.',
    hint: '"Noot met vorm" bij Combinaties: toetsen, een toon en een omhulling.', begin: LEEG_INSTRUMENT,
    controleer(E, o) {
      const c = compileer(o); if (c.fouten.length) return { gehaald: false, waarom: foutInWoorden(c.fouten[0]) };
      const ev = notenProef(o, c); if (!ev.length) return { gehaald: false, waarom: 'Er zitten nog geen toetsen in (met Noot en Poort op het paneel).' };
      const r = draai(E, o, stil(4), ev);
      const aan = db(rms(r.y, 0, SR * 2)), na = db(rms(r.y, SR * 3.2, SR * 4));
      if (aan < -40) return { gehaald: false, waarom: 'Als je speelt hoor je nog niets.' };
      return na < -60 ? { gehaald: true, waarom: 'Hij speelt en wordt stil als je loslaat.' } : { gehaald: false, waarom: 'Hij klinkt door als je loslaat: geef elke noot een einde (een omhulling op het volume).' };
    } },
  twee: { naam: 'Twee tegelijk', doel: 'Laat twee dingen die geluid maken samen klinken.',
    hint: 'Zet twee keer iets uit "Maakt geluid" erin: de tweede wordt vanzelf opgeteld.', begin: LEEG_INSTRUMENT,
    controleer(E, o) {
      const r = draai(E, o, stil(2)); if (r.fout) return { gehaald: false, waarom: r.fout };
      const n = hoofdweg(o).bereikt.filter((k) => familieVan(k.type) === 'bron').length;
      if (n < 2) return { gehaald: false, waarom: n ? 'Er komt nog maar één bron bij de uitgang.' : 'Er komt nog geen bron bij de uitgang.' };
      return db(rms(r.y)) > -40 ? { gehaald: true, waarom: 'Twee bronnen samen, en je hoort ze.' } : { gehaald: false, waarom: 'Twee bronnen, maar het is (bijna) stil.' };
    } },
  dub: { naam: 'De dub-echo', doel: 'Bouw een echo die vanzelf doorgaat als je stopt met spelen, maar niet ontploft.',
    hint: 'Een echo met herhalingen, terugkoppeling boven 1, en een band (tapeverzadiging) in de lus die hem in toom houdt.', begin: LEEG_EFFECT,
    controleer(E, o) {
      const x = new Float32Array(SR * 4); for (let i = 0; i < 4800; i++) x[i] = 0.5 * Math.sin(2 * Math.PI * 440 * i / SR);
      const r = draai(E, o, [x, x]); if (r.fout) return { gehaald: false, waarom: r.fout };
      const na = db(rms(r.y, SR * 3, SR * 4));
      return na > -40 ? { gehaald: true, waarom: `Hij gaat door: na 3 s nog ${Math.round(na)} dB.` } : { gehaald: false, waarom: 'Na 3 seconden is hij uitgestorven: meer terugkoppeling (boven 1), met een band in de lus.' };
    } },
};

// ---------------------------------------------------------------------------------------------------- rondes
export function zaadVan(n) { let s = (n >>> 0) || 1; return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; }; }
const schud = (lijst, r) => { const a = [...lijst]; for (let i = a.length - 1; i > 0; i--) { const j = Math.floor(r() * (i + 1)); [a[i], a[j]] = [a[j], a[i]]; } return a; };
const kies = (lijst, r) => lijst[Math.floor(r() * lijst.length)];

// keuzes: het goede blok plus twee of drie anderen, liefst uit hetzelfde level (dat is het lastige en dus het leren)
function keuzes(goed, uit, alle, r, n) {
  const binnen = schud(uit.filter((t) => t !== goed), r), buiten = schud(alle.filter((t) => t !== goed && !uit.includes(t)), r);
  return schud([goed, ...[...binnen, ...buiten].slice(0, n - 1)], r);
}
// Maakt de rondes van een level. Elke ronde: { soort: 'wie' | 'zin' | 'hoor' | 'draai', blok, keuzes?, knop?, doel? }
export function maakRondes(levelId, zaad = 1) {
  const r = zaadVan(zaad), l = level(levelId), alle = Object.keys(BIB).filter((t) => !BIB[t].vast);
  const hoorbaar = l.blokken.filter((t) => HOORBAAR.includes(t));
  const draaibaar = hoorbaar.filter((t) => PROEF[t].knop);
  const soorten = hoorbaar.length >= 3 ? ['wie', 'hoor', 'zin', 'hoor', 'wie', 'draai', 'hoor', 'draai'] : ['wie', 'zin', 'wie', 'zin', 'wie', 'zin', 'wie', 'zin'];
  const rondes = [], gebruikt = new Map();
  let vorige = null;
  for (const soort of soorten.slice(0, RONDES_PER_LEVEL)) {
    // het minst gevraagde blok eerst (zo komt elk blok aan de beurt), nooit twee keer achter elkaar hetzelfde
    const pool = soort === 'hoor' ? hoorbaar : soort === 'draai' ? draaibaar : l.blokken;
    const minst = Math.min(...pool.map((t) => gebruikt.get(t) || 0));
    const kandidaten = pool.filter((t) => (gebruikt.get(t) || 0) === minst && (t !== vorige || pool.length === 1));
    const blok = kies(kandidaten.length ? kandidaten : pool.filter((t) => t !== vorige), r);
    gebruikt.set(blok, (gebruikt.get(blok) || 0) + 1);
    vorige = blok;
    if (soort === 'draai') {
      const t = 0.15 + 0.7 * r();
      rondes.push({ soort, blok, knop: PROEF[blok].knop, doel: Math.fround(knopWaarde(blok, PROEF[blok].knop, t)), begin: Math.fround(knopWaarde(blok, PROEF[blok].knop, t > 0.5 ? t - 0.4 : t + 0.4)) });
    } else rondes.push({ soort, blok, keuzes: keuzes(blok, soort === 'hoor' ? hoorbaar : l.blokken, soort === 'hoor' ? HOORBAAR : alle, r, l.id === 'meester' ? 4 : 3) });
  }
  return rondes;
}
// De zin van een blok zonder zijn eigen naam (anders verraadt "Eén trommel: kick, tom …" het antwoord). Elk woord van de
// naam van vier letters of meer, ook vervoegd (vouwer, vouwt; toetsen, toetsenbord), wordt ___. Wat tussen haakjes staat
// ("Hoog weg (zacht)") telt niet.
export function zinZonderNaam(type, zin) {
  const woorden = [gewoneNaam(type), BIB[type].naam.replace(/\(.*?\)/g, '')].join(' ').toLowerCase().split(/[^a-zà-ÿ]+/).filter((w) => w.length >= 4);
  let uit = zin;
  for (const w of new Set(woorden)) uit = uit.replace(new RegExp(`\\b${w.slice(0, Math.max(4, w.length - 2))}[a-zà-ÿ]*`, 'gi'), '___');
  return uit;
}
// Draai na: hoe dicht zit je erbij (op de schuif, dus in octaven voor frequentie en tijd)?
export function draaiScore(blok, knop, waarde, doel) {
  const d = Math.abs(knopStand(blok, knop, waarde) - knopStand(blok, knop, doel));
  return { afstand: d, sterren: d <= 0.05 ? 3 : d <= 0.12 ? 2 : d <= 0.25 ? 1 : 0 };
}

// ---------------------------------------------------------------------------------------------------- voortgang
// { gekend: { blok: aantal goed }, levels: { id: { sterren, opdracht } }, punten, reeks, besteReeks }
export const GEKEND = 3;   // zo vaak goed = dit blok ken je
export const nieuweStand = () => ({ gekend: {}, levels: {}, punten: 0, reeks: 0, besteReeks: 0 });
export function antwoord(stand, blok, goed, { sterren = goed ? 1 : 0 } = {}) {
  const s = structuredCloneVeilig(stand);
  if (goed) {
    s.gekend[blok] = (s.gekend[blok] || 0) + 1;
    s.reeks++; s.besteReeks = Math.max(s.besteReeks, s.reeks);
    s.punten += 10 * Math.max(1, sterren) + Math.min(5, s.reeks - 1) * 2;
  } else s.reeks = 0;
  return s;
}
export function levelKlaar(stand, id, goedAantal, totaal = RONDES_PER_LEVEL) {
  const s = structuredCloneVeilig(stand), f = goedAantal / totaal;
  const sterren = f >= 0.9 ? 3 : f >= 0.7 ? 2 : f >= 0.5 ? 1 : 0;
  s.levels[id] = { ...(s.levels[id] || {}), sterren: Math.max(sterren, s.levels[id]?.sterren || 0) };
  return { stand: s, sterren };
}
export function opdrachtGehaald(stand, id) { const s = structuredCloneVeilig(stand); s.levels[id] = { ...(s.levels[id] || {}), opdracht: true }; s.punten += 50; return s; }
// een level is open als het vorige minstens één ster heeft (het eerste altijd)
export const open = (stand, id) => { const i = LEVELS.findIndex((l) => l.id === id); return i <= 0 || (stand.levels[LEVELS[i - 1].id]?.sterren || 0) > 0; };
export const gekendAantal = (stand) => Object.keys(BIB).filter((t) => !BIB[t].vast && (stand.gekend[t] || 0) >= GEKEND).length;
export const totaalBlokken = () => Object.keys(BIB).filter((t) => !BIB[t].vast).length;
const structuredCloneVeilig = (o) => JSON.parse(JSON.stringify(o));
