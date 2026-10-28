// rollen.js — de tool in gewone woorden: elk blok hoort bij een familie (wat het met geluid doet), heeft één zin uitleg
// zonder vaktaal, een uitleg per knop en per aansluiting, en zegt waar het goed mee samengaat. Daarnaast het rekenwerk
// om blokken te combineren: een blok (of een kant-en-klare combinatie) op de juiste plek in de keten zetten, en de keten
// netjes neerleggen. Geen DOM: draait in de browser en in Node (tests/rollen.test.mjs).
import { BIB, volgorde } from './keten.js';

// ---------------------------------------------------------------------------------------------------- families
// De volgorde is die van de bibliotheek. De kleuren staan in tool/stijl.css (.f-<id>) en design.md.
export const FAMILIES = [
  { id: 'bron', naam: 'Maakt geluid', zin: 'Hier begint het geluid: een toon, een snaar, een trommel of ruis.' },
  { id: 'kleur', naam: 'Kleurt de klank', zin: 'Maakt het geluid donkerder, warmer, vuiler of strakker.' },
  { id: 'ruimte', naam: 'Ruimte en tijd', zin: 'Echo, galm, zweven, links en rechts.' },
  { id: 'beweging', naam: 'Beweegt een knop', zin: 'Draait zelf aan een knop: langzaam golvend, willekeurig of per noot.' },
  { id: 'spelen', naam: 'Speelt noten', zin: 'Zegt wanneer en welke noot: een patroon of je toetsenbord.' },
  { id: 'verbinden', naam: 'Verbindt', zin: 'Optellen, mengen, harder en zachter.' },
  { id: 'vast', naam: 'In en uit', zin: 'Waar het geluid de plugin in en uit gaat.' },
];

// Per blok: familie, één zin, wat elke knop doet, en per aansluiting een gewone naam en de soort: 'geluid' (wat je hoort)
// of 'sturing' (een noot, een tel, een beweging; je hoort het niet zelf, het stuurt iets anders aan). Aansluitingen die
// hier niet staan heten zoals in de BIB en zijn geluid.
export const ROL = {
  in: { familie: 'vast', kort: 'Het geluid dat het muziekprogramma de plugin geeft.', knoppen: {} },
  uit: { familie: 'vast', kort: 'Wat hier aankomt, hoor je.', knoppen: {} },
  som: { familie: 'verbinden', kort: 'Telt twee geluiden bij elkaar op.', knoppen: {},
    poorten: { a: { naam: 'in 1' }, b: { naam: 'in 2' } } },
  versterker: { familie: 'verbinden', kort: 'Harder of zachter.', knoppen: { hoeveel: 'hoe hard: 0,5 is half zo hard, boven 1 harder' },
    goedMet: [['lfo', 'een LFO op "hoeveel" laat het pompen']] },
  mix: { familie: 'verbinden', kort: 'Mengt het origineel met het effect.', knoppen: { mix: 'hoeveel effect: 0 = alleen het origineel, 1 = alleen het effect' },
    poorten: { droog: { naam: 'origineel' }, nat: { naam: 'effect' } } },
  vermenigvuldig: { familie: 'verbinden', kort: 'Een geluid keer iets anders: met een omhulling is dat het volume van elke noot.', knoppen: {},
    poorten: { a: { naam: 'geluid' }, b: { naam: 'keer', soort: 'beide' } },
    goedMet: [['omhulling', 'op "keer" krijgt elke noot een begin en een einde']] },
  tape: { familie: 'kleur', kort: 'Warm en rond, zoals een bandrecorder die je hard aanstuurt.',
    knoppen: { verzadiging: 'hoe hard je de band aanstuurt: meer = warmer en ronder', asymmetrie: 'een beetje scheef maakt het voller, zoals een echte band (0,1)' },
    goedMet: [['laagdoor1', 'erachter haalt het scherpe randje weg'], ['recept:echo', 'in de lus van een echo houdt hij de herhalingen in toom']] },
  vertraging: { familie: 'ruimte', kort: 'Speelt het geluid later nog eens: een echo.',
    knoppen: { tijd: 'hoe lang tot de echo komt' },
    poorten: { beweging: { naam: 'band', soort: 'sturing' } },
    goedMet: [['meng', 'meng de echo met het origineel'], ['bandsnelheid', 'op "band" jankt de echo zoals een echte band'], ['tape', 'erachter: warmer, zoals een bandecho']] },
  hoogdoor1: { familie: 'kleur', kort: 'Haalt het laag weg: dunner, minder dreun.', knoppen: { freq: 'onder deze toonhoogte wordt het zachter' } },
  laagdoor1: { familie: 'kleur', naam: 'Hoog weg', kort: 'Haalt het hoog weg: zachter, minder scherp.', knoppen: { freq: 'boven deze toonhoogte wordt het zachter' } },
  svf: { familie: 'kleur', naam: 'Filter', kort: 'Een filter: houdt laag, midden of hoog over. Mag snel bewegen.',
    knoppen: { freq: 'waar het filter afsnijdt', q: 'een piek op die plek: hoger = fluitender' },
    poorten: { laag: { naam: 'laag' }, band: { naam: 'midden' }, hoog: { naam: 'hoog' } },
    goedMet: [['lfo', 'sleep een LFO op de frequentie en hij wiebelt'], ['omhulling', 'op de frequentie: elke noot gaat open en dicht (acid)']] },
  biquad: { familie: 'kleur', naam: 'Vast filter', kort: 'Een klassiek filter dat stilstaat: laag, hoog, midden of een gat.',
    knoppen: { freq: 'waar het filter werkt', q: 'hoe smal of scherp' } },
  vergruizer: { familie: 'kleur', kort: 'Lo-fi: grof, ruizig en digitaal.',
    knoppen: { bits: 'minder = grover en ruiziger', deling: 'hoger = metaliger en dunner' } },
  vouwer: { familie: 'kleur', kort: 'Vouwt de golf terug: van warm tot scherp zingend.',
    knoppen: { drive: 'hoe ver je de golf over de rand duwt', scheef: 'naar één kant: voller, holler' } },
  compressor: { familie: 'kleur', kort: 'Maakt harde stukken zachter, zodat alles strakker en dichter klinkt.',
    knoppen: { drempel: 'vanaf hoe hard hij ingrijpt', verhouding: 'hoe hard hij ingrijpt', aanzet: 'hoe snel hij ingrijpt', loslaten: 'hoe snel hij weer loslaat' },
    poorten: { zijketen: { naam: 'luister naar', luister: true } } },
  galm: { familie: 'ruimte', kort: 'Een ruimte om het geluid heen, van kamer tot kathedraal.',
    knoppen: { tijd: 'hoe lang de ruimte nagalmt', demping: 'lager = de galm wordt sneller donker', grootte: 'hoe groot de ruimte klinkt' },
    goedMet: [['meng', 'de galm geeft alleen de ruimte: meng hem met het origineel'], ['laagdoor1', 'erachter: een donkerdere, zachtere ruimte']] },
  chorus: { familie: 'ruimte', naam: 'Chorus', kort: 'Zwevend en breed; met veel terug wordt het een straaljager.',
    knoppen: { tijd: 'kort = straaljager, lang = zweven', diepte: 'hoe ver hij wiebelt', snelheid: 'hoe snel hij wiebelt', terug: 'hoeveel hij terugvoert: meer = metaliger' },
    goedMet: [['meng', 'meng hem met het origineel'], ['lfo', 'op de tijd: nog meer beweging']] },
  panner: { familie: 'ruimte', kort: 'Zet het geluid links of rechts.', knoppen: { positie: '−1 = links, 0 = midden, 1 = rechts' },
    goedMet: [['lfo', 'sleep een LFO op positie en hij draait rond']] },
  osc: { familie: 'bron', kort: 'Maakt een toon: zacht (sinus), scherp (zaag), hol (blok) of fluitend (driehoek).',
    knoppen: { freq: 'hoe hoog de toon is', vorm: 'alleen bij blok: smaller = neuziger', fm: 'hoeveel de fm-aansluiting de toon laat trillen', fijn: 'een klein beetje hoger of lager (zweven met een tweede)' },
    poorten: { toon: { naam: 'noot', soort: 'sturing' }, fm: { naam: 'fm' } },
    goedMet: [['toetsen', 'speel hem met je toetsenbord'], ['sequencer', 'of laat hem een patroon spelen'], ['svf', 'een filter erachter maakt hem donkerder']] },
  ruis: { familie: 'bron', kort: 'Ruis: van sissend tot wind en zee.', knoppen: { kleur: '0 = sissend, 1 = donker' },
    goedMet: [['recept:wiebel', 'een filter dat langzaam open en dicht gaat: golven'], ['recept:ruimte', 'met galm wordt het wind in een hal']] },
  bandruis: { familie: 'bron', kort: 'De zachte sis van een echte band.', knoppen: { niveau: '1 = zo hard als een echte bandrecorder' } },
  snaar: { familie: 'bron', kort: 'Een getokkelde snaar.',
    knoppen: { freq: 'welke noot', verval: 'hoe lang hij doorklinkt', helder: 'hoger = metaliger' },
    poorten: { poort: { naam: 'tokkel', soort: 'sturing' }, toon: { naam: 'noot', soort: 'sturing' } },
    goedMet: [['sequencer', 'die tokkelt hem in een patroon'], ['recept:ruimte', 'met galm klinkt hij als harp in een kerk']] },
  trommel: { familie: 'bron', kort: 'Eén trommel: kick, tom, snare of hihat.',
    knoppen: { freq: 'hoe hoog hij klinkt', val: 'hoe ver de toon zakt (veel = kick)', verval: 'hoe lang hij klinkt', ruis: 'meer = snare of hihat' },
    poorten: { poort: { naam: 'slag', soort: 'sturing' }, toon: { naam: 'noot', soort: 'sturing' } },
    goedMet: [['sequencer', 'die slaat hem in een patroon']] },
  bandbeweging: { familie: 'beweging', kort: 'Een oudere versie van Bandsnelheid; gebruik liever die.', knoppen: { diepte: 'hoe onrustig de band loopt' },
    poorten: { beweging: { naam: 'beweging', soort: 'sturing' } } },
  bandsnelheid: { familie: 'beweging', kort: 'Laat een echo janken zoals een echte bandrecorder.', knoppen: { diepte: '0,25 = een goede bandrecorder, 1 = versleten' },
    poorten: { snelheid: { naam: 'band', soort: 'sturing' } },
    goedMet: [['vertraging', 'op "band" van een vertraging']] },
  lfo: { familie: 'beweging', kort: 'Draait langzaam heen en weer aan een knop.', knoppen: { snelheid: 'hoe snel hij heen en weer gaat' },
    poorten: { golf: { naam: 'beweging', soort: 'sturing' } } },
  willekeur: { familie: 'beweging', kort: 'Zet een knop steeds op een andere, toevallige plek.',
    knoppen: { snelheid: 'hoe vaak hij springt', glad: 'hoger = glijdt erheen in plaats van te springen' },
    poorten: { golf: { naam: 'beweging', soort: 'sturing' } } },
  omhulling: { familie: 'beweging', naam: 'Omhulling', kort: 'Geeft elke noot een begin en een einde (aanzet, verval, aanhouden, loslaten).',
    knoppen: { aanzet: 'hoe snel de noot er is', verval: 'hoe snel hij daarna zakt', aanhoud: 'hoe hard hij blijft zolang je de noot vasthoudt', loslaten: 'hoe lang hij uitklinkt als je loslaat' },
    poorten: { poort: { naam: 'noot aan', soort: 'sturing' }, omhulling: { naam: 'vorm', soort: 'sturing' } },
    goedMet: [['vermenigvuldig', 'op "keer" wordt het het volume van de noot'], ['svf', 'op de frequentie gaat het filter per noot open']] },
  sequencer: { familie: 'spelen', kort: 'Speelt een patroon van zestien stappen.',
    knoppen: { tempo: 'hoe snel (tellen per minuut)', lengte: 'hoe lang elke noot duurt', zwaai: 'meer = swingender', transponeer: 'alles hoger of lager' },
    poorten: { poort: { naam: 'slag', soort: 'sturing' }, toon: { naam: 'noot', soort: 'sturing' }, accent: { naam: 'accent', soort: 'sturing' } },
    goedMet: [['trommel', 'een trommel op "slag" en je hebt een ritme'], ['snaar', 'of een snaar die tokkelt']] },
  toetsen: { familie: 'spelen', kort: 'Speel zelf op je toetsenbord (a s d f …).',
    knoppen: { toon: 'welke toets je indrukt', poort: 'of er een toets ingedrukt is' },
    poorten: { poort: { naam: 'aan', soort: 'sturing' }, toon: { naam: 'noot', soort: 'sturing' } },
    goedMet: [['recept:noot', 'met een oscillator en een omhulling heb je een instrument']] },
};

export const familieVan = (type) => ROL[type]?.familie ?? 'verbinden';
// de naam zonder vaktaal ("Filter" in plaats van "Filter (SVF)"); de BIB-naam blijft voor de technische weergave
export const gewoneNaam = (type) => ROL[type]?.naam ?? BIB[type]?.naam ?? type;
export const familie = (id) => FAMILIES.find((f) => f.id === id);
export function poortInfo(type, poort) {
  const p = ROL[type]?.poorten?.[poort];
  return { naam: p?.naam ?? poort, soort: p?.soort ?? 'geluid', luister: !!p?.luister };
}
const isGeluid = (type, poort) => poortInfo(type, poort).soort !== 'sturing';
const isSturing = (type, poort) => poortInfo(type, poort).soort !== 'geluid';
// een blok dat alleen stuurt (geen geluid uit): LFO, sequencer, omhulling …
export const alleenSturing = (type) => BIB[type].uit.length > 0 && BIB[type].uit.every((p) => !isGeluid(type, p));
// past een uitgang op een ingang? (geluid op geluid, sturing op sturing; "keer" neemt beide)
export function past(vanType, vanPoort, naarType, naarPoort) {
  const a = poortInfo(vanType, vanPoort).soort, b = poortInfo(naarType, naarPoort).soort;
  return b === 'beide' || a === b;
}

// De maat van een blok zoals de tool het tekent (kop, knoppen in twee kolommen; breed genoeg voor de naam).
export const KOP = 24, RIJ = 66, KB = 132;
export function blokMaat(kn) {
  const b = BIB[kn.type], n = b.knoppen.length;
  const w = Math.max(n >= 2 ? KB : n === 1 ? 76 : 70, Math.ceil(b.naam.length * 7.4 + 30));
  return { w, h: KOP + 18 + Math.ceil(Math.max(1, n) / 2) * RIJ };
}

// ---------------------------------------------------------------------------------------------------- combinaties
// Kant-en-klare combinaties: een paar blokken die samen één ding doen. `ingang` = waar het geluid erin gaat (leeg =
// het maakt zelf geluid), `uitgang` = waar het eruit komt. `deel: true` = een bestaand blok van die soort hergebruiken
// (één toetsenbord per ontwerp). Waarden staan exact in float32.
export const RECEPTEN = [
  { id: 'echo', naam: 'Echo met herhalingen', kort: 'Een echo die zichzelf herhaalt, gemengd met het origineel.',
    knopen: [{ id: 'som', type: 'som' }, { id: 'lijn', type: 'vertraging', waarden: { tijd: 375 }, vast: { max: 1200 } },
      { id: 'terug', type: 'versterker', waarden: { hoeveel: 0.5 } }, { id: 'mix', type: 'mix', waarden: { mix: 0.375 } }],
    kabels: [[['som', 'uit'], ['lijn', 'in']], [['lijn', 'uit'], ['terug', 'in']], [['terug', 'uit'], ['som', 'b']], [['lijn', 'uit'], ['mix', 'nat']]],
    ingang: [['som', 'a'], ['mix', 'droog']], uitgang: ['mix', 'uit'] },
  { id: 'ruimte', naam: 'Ruimte erbij', kort: 'Galm, gemengd met het origineel.',
    knopen: [{ id: 'galm', type: 'galm', waarden: { tijd: 2.5 } }, { id: 'mix', type: 'mix', waarden: { mix: 0.3125 } }],
    kabels: [[['galm', 'uit'], ['mix', 'nat']]], ingang: [['galm', 'in'], ['mix', 'droog']], uitgang: ['mix', 'uit'] },
  { id: 'zweven', naam: 'Breed zweven', kort: 'Chorus, gemengd met het origineel.',
    knopen: [{ id: 'chorus', type: 'chorus' }, { id: 'mix', type: 'mix', waarden: { mix: 0.5 } }],
    kabels: [[['chorus', 'uit'], ['mix', 'nat']]], ingang: [['chorus', 'in'], ['mix', 'droog']], uitgang: ['mix', 'uit'] },
  { id: 'wiebel', naam: 'Wiebelend filter', kort: 'Een filter dat een LFO langzaam open en dicht draait.',
    knopen: [{ id: 'filter', type: 'svf', waarden: { freq: 1200, q: 2 } }, { id: 'lfo', type: 'lfo', waarden: { snelheid: 0.5 } }],
    kabels: [], modulaties: [[['lfo', 'golf'], ['filter', 'freq'], 0.25]], ingang: [['filter', 'in']], uitgang: ['filter', 'laag'] },
  { id: 'warm', naam: 'Warme band', kort: 'Bandverzadiging met het scherpe randje eraf.',
    knopen: [{ id: 'tape', type: 'tape', waarden: { verzadiging: 9 } }, { id: 'zacht', type: 'laagdoor1', waarden: { freq: 8000 } }],
    kabels: [[['tape', 'uit'], ['zacht', 'in']]], ingang: [['tape', 'in']], uitgang: ['zacht', 'uit'] },
  { id: 'noot', naam: 'Noot met vorm', kort: 'Speel een toon op je toetsenbord; elke noot zwelt aan en sterft weg.',
    knopen: [{ id: 'toetsen', type: 'toetsen', deel: true }, { id: 'osc', type: 'osc', vast: { golf: 1 } },
      { id: 'omh', type: 'omhulling' }, { id: 'vca', type: 'vermenigvuldig' }],
    kabels: [[['toetsen', 'toon'], ['osc', 'toon']], [['toetsen', 'poort'], ['omh', 'poort']], [['osc', 'uit'], ['vca', 'a']], [['omh', 'omhulling'], ['vca', 'b']]],
    ingang: [], uitgang: ['vca', 'uit'] },
  { id: 'kick', naam: 'Kick op de tel', kort: 'Een kick op elke tel.',
    knopen: [{ id: 'seq', type: 'sequencer', vast: { patroon: 0 } }, { id: 'kick', type: 'trommel', waarden: { freq: 50, val: 3, verval: 380 } }],
    kabels: [[['seq', 'poort'], ['kick', 'poort']]], ingang: [], uitgang: ['kick', 'uit'] },
];
export const recept = (id) => RECEPTEN.find((r) => r.id === id);

// ---------------------------------------------------------------------------------------------------- invoegen
const kloon = (o) => JSON.parse(JSON.stringify(o));
const naamVan = (o, id) => { const t = o.knopen.find((k) => k.id === id)?.type; return t ? gewoneNaam(t) : id; };
function vrijeId(o, basis) { let n = 1; while (o.knopen.some((k) => k.id === basis + n)) n++; return basis + n; }
const TOETS_PARAMS = (id) => [
  { naam: 'Noot', knoop: id, knop: 'toon', min: 0.25, max: 4, standaard: 1, glijMs: 5, uitleg: 'welke toets (verhouding t.o.v. de grondtoon)' },
  { naam: 'Poort', knoop: id, knop: 'poort', min: 0, max: 1, standaard: 0, glijMs: 0, uitleg: 'toets ingedrukt (1) of los (0)' }];

// Een nieuw blok zoals de tool het neerzet (dezelfde regels als voegToe in app.js).
export function nieuwBlok(o, type) {
  const kn = { id: vrijeId(o, type), type, x: 0, y: 0, waarden: {} };
  if (type === 'vertraging') kn.vast = { max: 1200 };
  o.knopen.push(kn);
  if (type === 'toetsen') o.parameters.push(...TOETS_PARAMS(kn.id));
  return kn;
}
const vrij = (o, id, poort) => !o.kabels.some((k) => k.naar[0] === id && k.naar[1] === poort);
const verbind = (o, van, naar) => { o.kabels = o.kabels.filter((k) => !(k.naar[0] === naar[0] && k.naar[1] === naar[1])); o.kabels.push({ van, naar }); };

// De kabel waar iets tussen moet: na het gekozen blok (de eerste kabel met geluid die vooruit loopt), anders de kabel
// naar de uitgang.
function doelKabel(o, na, kabel) {
  if (kabel) return o.kabels.find((k) => k.van.join() === kabel.van.join() && k.naar.join() === kabel.naar.join()) ?? null;
  const { terug } = volgorde(o);
  const vooruit = o.kabels.filter((k) => !terug.includes(k) && isGeluid(o.knopen.find((x) => x.id === k.van[0]).type, k.van[1])
    && isGeluid(o.knopen.find((x) => x.id === k.naar[0]).type, k.naar[1]));
  const kn = na && o.knopen.find((k) => k.id === na);
  if (kn && kn.type !== 'uit') { const k = vooruit.find((c) => c.van[0] === na); if (k) return k; }
  return vooruit.find((c) => c.naar[0] === 'uit' || o.knopen.find((x) => x.id === c.naar[0])?.type === 'uit') ?? null;
}
const uitKnoop = (o) => o.knopen.find((k) => k.type === 'uit');
const inKnoop = (o) => o.knopen.find((k) => k.type === 'in');

// Zet een stuk keten (ingangen → … → uitgang) in de keten. Geeft een zin terug over wat er gebeurde.
function zetInKeten(o, ingang, uitgang, na, naam, gekozenKabel) {
  const kabel = doelKabel(o, na, gekozenKabel), uit = uitKnoop(o);
  if (ingang.length) {
    if (kabel) {
      o.kabels = o.kabels.filter((k) => k !== kabel);
      for (const i of ingang) o.kabels.push({ van: kabel.van, naar: i });
      o.kabels.push({ van: uitgang, naar: kabel.naar });
      return `${naam} tussen ${naamVan(o, kabel.van[0])} en ${naamVan(o, kabel.naar[0])} gezet`;
    }
    const bron = inKnoop(o);
    if (bron) for (const i of ingang) o.kabels.push({ van: [bron.id, 'uit'], naar: i });
    if (uit && vrij(o, uit.id, 'in')) {
      o.kabels.push({ van: uitgang, naar: [uit.id, 'in'] });
      return bron ? `${naam} tussen Ingang en Uitgang gezet` : `${naam} op de uitgang gezet; er moet nog iets in dat geluid maakt`;
    }
    return `${naam} erbij gezet: sluit hem nog aan`;
  }
  // maakt zelf geluid: op de uitgang, of opgeteld bij wat er al klonk
  if (!kabel) {
    if (uit && vrij(o, uit.id, 'in')) { o.kabels.push({ van: uitgang, naar: [uit.id, 'in'] }); return `${naam} op de uitgang gezet`; }
    return `${naam} erbij gezet: sluit hem nog aan`;
  }
  const som = nieuwBlok(o, 'som');
  o.kabels = o.kabels.filter((k) => k !== kabel);
  o.kabels.push({ van: kabel.van, naar: [som.id, 'a'] }, { van: uitgang, naar: [som.id, 'b'] }, { van: [som.id, 'uit'], naar: kabel.naar });
  return `${naam} opgeteld bij wat er al klonk`;
}

// Stuurblokken sluiten zich aan op wat er al is: een LFO op een knop van het gekozen blok, een sequencer of toetsen
// op elke vrije noot- of slag-aansluiting, een bandsnelheid op een vertraging.
function sluitSturingAan(o, kn, na) {
  const type = kn.type, gekozen = na && o.knopen.find((k) => k.id === na && k.id !== kn.id);
  if (['lfo', 'willekeur', 'bandbeweging'].includes(type) || (type === 'omhulling' && gekozen && gekozen.type !== 'vermenigvuldig')) {
    if (type === 'omhulling') verbindSpelen(o, kn);
    if (!gekozen || !BIB[gekozen.type].knoppen.length || BIB[gekozen.type].vast) return `${gewoneNaam(type)} erbij: sleep zijn uitgang op een knop`;
    const knoppen = BIB[gekozen.type].knoppen, knop = knoppen.find((k) => k.exp) ?? knoppen[0];
    o.modulaties = (o.modulaties || []).filter((m) => !(m.doel[0] === gekozen.id && m.doel[1] === knop.id));
    o.modulaties.push({ bron: [kn.id, BIB[type].uit[0]], doel: [gekozen.id, knop.id], diepte: 0.25 });
    return `${gewoneNaam(type)} beweegt nu ${knop.naam} van ${gewoneNaam(gekozen.type)}`;
  }
  if (type === 'omhulling') {
    verbindSpelen(o, kn);
    const vca = (gekozen?.type === 'vermenigvuldig' && vrij(o, gekozen.id, 'b') ? gekozen : null)
      ?? o.knopen.find((k) => k.type === 'vermenigvuldig' && vrij(o, k.id, 'b'));
    if (vca) { o.kabels.push({ van: [kn.id, 'omhulling'], naar: [vca.id, 'b'] }); return 'Omhulling bepaalt nu het volume van elke noot'; }
    return 'Omhulling erbij: sleep zijn uitgang op een knop, of op "keer" van een Vermenigvuldig';
  }
  if (type === 'bandsnelheid') {
    const lijn = (gekozen?.type === 'vertraging' ? gekozen : null) ?? o.knopen.find((k) => k.type === 'vertraging' && vrij(o, k.id, 'beweging'));
    if (lijn) { verbind(o, [kn.id, 'snelheid'], [lijn.id, 'beweging']); return 'Bandsnelheid laat nu de echo janken'; }
    return 'Bandsnelheid erbij: hij werkt op "band" van een vertraging';
  }
  if (type === 'sequencer' || type === 'toetsen') {
    const n = verbindSpelen(o, null, kn, gekozen);
    return n ? `${gewoneNaam(type)} speelt nu ${n}` : `${gewoneNaam(type)} erbij: sluit "${poortInfo(type, 'poort').naam}" aan op een trommel, snaar of omhulling`;
  }
  return `${gewoneNaam(type)} erbij gezet`;
}
// Sluit noten aan. Met `ontvanger`: alleen op dat blok, van het eerste speelblok. Met `speler`: van dat speelblok op elk
// vrij blok dat noten hoort (eerst het gekozen blok). Geeft terug wie er nu speelt.
const LUISTERT = { osc: ['toon'], snaar: ['poort', 'toon'], trommel: ['poort'], omhulling: ['poort'] };
function verbindSpelen(o, ontvanger, speler, gekozen) {
  speler = speler ?? o.knopen.find((k) => k.type === 'toetsen' || k.type === 'sequencer');
  if (!speler) return '';
  const doelen = ontvanger ? [ontvanger] : [...(gekozen ? [gekozen] : []), ...o.knopen.filter((k) => k !== gekozen)];
  const wie = [];
  for (const d of doelen) for (const p of LUISTERT[d.type] || []) if (vrij(o, d.id, p)) { o.kabels.push({ van: [speler.id, p], naar: [d.id, p] }); if (!wie.includes(gewoneNaam(d.type))) wie.push(gewoneNaam(d.type)); }
  return wie.join(' en ');
}

// voegIn: een blok ({ type }) of een combinatie ({ recept }) in het ontwerp zetten, na het gekozen blok (of vóór de
// uitgang), of op een gegeven kabel ({ van, naar }: een blok dat op een kabel wordt losgelaten). Geeft
// { ontwerp, nieuw: [id's], wat } terug; het ontwerp zelf blijft onaangeroerd.
export function voegIn(ontwerp, wat, { na = null, kabel = null } = {}) {
  const o = kloon(ontwerp);
  o.modulaties = o.modulaties || [];
  if (wat.meng) return mengRond(o, na);
  if (wat.type) {
    const kn = nieuwBlok(o, wat.type), b = BIB[wat.type];
    const ingang = familieVan(wat.type) === 'bron' ? [] : b.in.filter((p) => poortInfo(wat.type, p).soort === 'geluid').slice(0, 1).map((p) => [kn.id, p]);
    const uit = b.uit.find((p) => isGeluid(wat.type, p));
    let zin;
    if (uit) {
      zin = zetInKeten(o, ingang, [kn.id, uit], na, gewoneNaam(wat.type), kabel);
      if (!ingang.length) verbindSpelen(o, kn);   // een bron hoort de noten als er al iemand speelt
    } else zin = sluitSturingAan(o, kn, na);
    return { ontwerp: o, nieuw: [kn.id], wat: zin };
  }
  const r = recept(wat.recept);
  const ids = {};
  for (const k of r.knopen) {
    const bestaand = k.deel && o.knopen.find((x) => x.type === k.type);
    if (bestaand) { ids[k.id] = bestaand.id; continue; }
    const kn = nieuwBlok(o, k.type);
    if (k.waarden) kn.waarden = { ...k.waarden };
    if (k.vast) kn.vast = { ...(kn.vast || {}), ...k.vast };
    ids[k.id] = kn.id;
  }
  const m = ([id, p]) => [ids[id], p];
  for (const [van, naar] of r.kabels) o.kabels.push({ van: m(van), naar: m(naar) });
  for (const [bron, doel, diepte] of r.modulaties || []) o.modulaties.push({ bron: m(bron), doel: m(doel), diepte });
  const zin = zetInKeten(o, r.ingang.map(m), m(r.uitgang), na, r.naam, kabel);
  return { ontwerp: o, nieuw: Object.values(ids), wat: zin };
}

// Een Mix om het gekozen blok: wat erin gaat wordt het origineel, wat eruit komt het effect. Voor een galm, chorus of
// echo, die alleen het effect geven.
function mengRond(o, na) {
  const kn = o.knopen.find((k) => k.id === na);
  const inP = kn && BIB[kn.type].in.find((p) => poortInfo(kn.type, p).soort === 'geluid');
  const voor = inP && o.kabels.find((k) => k.naar[0] === kn.id && k.naar[1] === inP);
  const erna = kn && doelKabel(o, kn.id);
  const mix = nieuwBlok(o, 'mix');
  if (!voor || !erna || erna.van[0] !== kn.id) return { ontwerp: o, nieuw: [mix.id], wat: 'Mix erbij: sluit het origineel aan op "origineel" en het effect op "effect"' };
  mix.waarden = { mix: 0.375 };
  o.kabels = o.kabels.filter((k) => k !== erna);
  o.kabels.push({ van: voor.van, naar: [mix.id, 'droog'] }, { van: erna.van, naar: [mix.id, 'nat'] }, { van: [mix.id, 'uit'], naar: erna.naar });
  return { ontwerp: o, nieuw: [mix.id], wat: `${gewoneNaam(kn.type)} wordt nu gemengd met het origineel` };
}

// ---------------------------------------------------------------------------------------------------- netjes neerleggen
// Kolommen van links naar rechts in de volgorde waarin het geluid loopt (de langste weg vanaf het begin); blokken die
// alleen sturen staan in een rij erboven, boven het blok dat ze aansturen. `maat(knoop)` geeft { w, h } (de tool weet
// hoe groot een blok getekend wordt). Geeft een nieuw ontwerp terug.
export function ordenen(ontwerp, maat, { gatX = 44, gatY = 30, boven = 26 } = {}) {
  const o = kloon(ontwerp);
  const { terug } = volgorde(o);
  const stuur = new Set(o.knopen.filter((k) => alleenSturing(k.type)).map((k) => k.id));
  const kabels = o.kabels.filter((k) => !terug.includes(k) && !stuur.has(k.van[0]));
  const rang = new Map(o.knopen.map((k) => [k.id, 0]));
  // langste weg (de keten heeft geen kringen meer zonder de terugkabels)
  for (let ronde = 0; ronde < o.knopen.length; ronde++) {
    let veranderd = false;
    for (const k of kabels) if (rang.get(k.naar[0]) < rang.get(k.van[0]) + 1) { rang.set(k.naar[0], rang.get(k.van[0]) + 1); veranderd = true; }
    if (!veranderd) break;
  }
  // een bron die halverwege bij komt (bandruis op de band, een trommel op de zijketen) staat vlak vóór waar hij heen gaat
  for (const k of o.knopen) {
    if (k.type === 'in' || stuur.has(k.id) || kabels.some((c) => c.naar[0] === k.id)) continue;
    const doelen = kabels.filter((c) => c.van[0] === k.id).map((c) => rang.get(c.naar[0]));
    if (doelen.length) rang.set(k.id, Math.max(0, Math.min(...doelen) - 1));
  }
  const hoofd = o.knopen.filter((k) => !stuur.has(k.id));
  const maxRang = Math.max(0, ...hoofd.map((k) => rang.get(k.id)));
  for (const k of hoofd) if (k.type === 'uit') rang.set(k.id, maxRang + (hoofd.some((x) => x !== k && rang.get(x.id) === maxRang) ? 1 : 0));
  const kolommen = [];
  for (const k of hoofd) (kolommen[rang.get(k.id)] ??= []).push(k);
  // volgorde in een kolom: het gemiddelde plekje van wat erin gaat (minder kruisende kabels), de oude volgorde als gelijk
  const plek = new Map();
  kolommen.forEach((kol = [], ci) => {
    const gewicht = (k) => { const v = kabels.filter((c) => c.naar[0] === k.id && plek.has(c.van[0])).map((c) => plek.get(c.van[0])); return v.length ? v.reduce((a, b) => a + b, 0) / v.length : Infinity; };
    kol.sort((a, b) => gewicht(a) - gewicht(b) || a.y - b.y || o.knopen.indexOf(a) - o.knopen.indexOf(b));
    kol.forEach((k, i) => plek.set(k.id, i + ci * 0.001));
  });
  // de sturing-rij
  const stuurKnopen = o.knopen.filter((k) => stuur.has(k.id));
  const bovenH = stuurKnopen.length ? Math.max(...stuurKnopen.map((k) => maat(k).h)) + boven : 0;
  let x = 20;
  const kolX = [];
  kolommen.forEach((kol = [], ci) => {
    kolX[ci] = x;
    let y = 30 + bovenH;
    for (const k of kol) { k.x = x; k.y = y; y += maat(k).h + gatY; }
    x += Math.max(0, ...kol.map((k) => maat(k).w)) + gatX;
  });
  let vrijVanaf = 20;
  const doelX = (id) => {
    const k = o.kabels.find((c) => c.van[0] === id) ?? (o.modulaties || []).map((m) => ({ naar: m.doel, van: m.bron })).find((c) => c.van[0] === id);
    const d = k && o.knopen.find((n) => n.id === k.naar[0]);
    return d && !stuur.has(d.id) ? d.x : 20;
  };
  for (const k of stuurKnopen.sort((a, b) => doelX(a.id) - doelX(b.id) || o.knopen.indexOf(a) - o.knopen.indexOf(b))) {
    k.x = Math.max(doelX(k.id), vrijVanaf); k.y = 30; vrijVanaf = k.x + maat(k).w + 24;
  }
  return o;
}
// overlappen twee blokken? (voor de toets en om te beslissen of een voorbeeld opnieuw neergelegd moet worden)
export function overlap(o, maat, marge = 8) {
  const ks = o.knopen;
  for (let i = 0; i < ks.length; i++) for (let j = i + 1; j < ks.length; j++) {
    const a = ks[i], b = ks[j], ma = maat(a), mb = maat(b);
    if (a.x < b.x + mb.w + marge && b.x < a.x + ma.w + marge && a.y < b.y + mb.h + marge && b.y < a.y + ma.h + marge) return [a.id, b.id];
  }
  return null;
}

// ---------------------------------------------------------------------------------------------------- beschrijven
// Wat dit ontwerp doet, in één of twee zinnen: de weg van het geluid, de lus terug, en wat er beweegt.
export function beschrijf(o) {
  if (!o.knopen.length) return 'Nog leeg.';
  const { terug } = volgorde(o);
  const vooruit = o.kabels.filter((k) => !terug.includes(k));
  const uit = uitKnoop(o);
  if (!uit) return 'Er is nog geen uitgang: zonder uitgang hoor je niets.';
  // de langste weg naar de uitgang
  const lang = new Map(), volg = new Map();
  const weg = (id, pad = new Set()) => {
    if (id === uit.id) return 0;
    if (lang.has(id)) return lang.get(id);
    if (pad.has(id)) return -Infinity;
    pad.add(id);
    let best = -Infinity, naar = null;
    for (const k of vooruit.filter((c) => c.van[0] === id && isGeluid(o.knopen.find((x) => x.id === c.naar[0]).type, c.naar[1]) && !poortInfo(o.knopen.find((x) => x.id === c.naar[0]).type, c.naar[1]).luister)) {
      const d = weg(k.naar[0], pad) + 1; if (d > best) { best = d; naar = k.naar[0]; }
    }
    pad.delete(id); lang.set(id, best); volg.set(id, naar);
    return best;
  };
  const beginners = o.knopen.filter((k) => k.type === 'in' || familieVan(k.type) === 'bron');
  let start = null, best = -Infinity;
  for (const b of beginners) { const d = weg(b.id); if (d > best) { best = d; start = b; } }
  const delen = [];
  if (!start || best === -Infinity) delen.push('Er komt nog niets bij de uitgang aan.');
  else {
    const pad = [start.id]; while (pad.at(-1) !== uit.id && volg.get(pad.at(-1))) pad.push(volg.get(pad.at(-1)));
    const namen = pad.filter((id) => !['som', 'mix'].includes(o.knopen.find((k) => k.id === id).type)).map((id) => naamVan(o, id));
    delen.push('Weg van het geluid: ' + namen.join(' → ') + '.');
    const andere = beginners.filter((b) => b !== start && lang.get(b.id) > -Infinity && b.type !== 'in');
    if (andere.length) delen.push('Er komt ook geluid uit: ' + andere.map((b) => gewoneNaam(b.type)).join(', ') + '.');
    for (const k of o.kabels.filter((c) => poortInfo(o.knopen.find((x) => x.id === c.naar[0]).type, c.naar[1]).luister)) delen.push(`${naamVan(o, k.naar[0])} luistert naar ${naamVan(o, k.van[0])}.`);
  }
  if (terug.length) delen.push('Een deel gaat terug (' + terug.map((k) => naamVan(o, k.van[0])).join(', ') + ' → ' + terug.map((k) => naamVan(o, k.naar[0])).join(', ') + '): zo herhaalt het zich.');
  const beweegt = [
    ...(o.modulaties || []).map((m) => `${naamVan(o, m.bron[0])} beweegt ${BIB[o.knopen.find((k) => k.id === m.doel[0])?.type]?.knoppen.find((k) => k.id === m.doel[1])?.naam ?? m.doel[1]} van ${naamVan(o, m.doel[0])}`),
  ];
  const stuurt = new Map();
  for (const k of o.kabels.filter((c) => alleenSturing(o.knopen.find((x) => x.id === c.van[0]).type))) {
    const l = stuurt.get(k.van[0]) ?? []; if (!l.includes(naamVan(o, k.naar[0]))) l.push(naamVan(o, k.naar[0])); stuurt.set(k.van[0], l);
  }
  for (const [van, l] of stuurt) beweegt.push(`${naamVan(o, van)} stuurt ${l.length > 1 ? l.slice(0, -1).join(', ') + ' en ' + l.at(-1) : l[0]}`);
  if (beweegt.length) delen.push([...new Set(beweegt)].join('; ') + '.');
  return delen.join(' ');
}
