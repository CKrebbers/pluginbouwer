// keten.js — het model van de ontwerper: de bibliotheek (BIB), een ontwerp als data, en de compiler naar de platte
// beschrijving die de ketenmotor (dsp/pb_graaf.h) leest. Geen DOM: draait in de browser en in Node (toetsen, export).
//
// Een ontwerp:
//   { naam, uitleg, knopen: [{ id, type, x, y, waarden: { knop: getal }, vast: {…} }],
//     kabels: [{ van: [id, poort], naar: [id, ingang] }],
//     parameters: [{ naam, knoop, knop, min, max, standaard, glijMs, eenheid, uitleg, glijden }],   // wat de plugin toont
//     modulaties: [{ bron: [id, poort], doel: [id, knop], diepte }] }
// Een knop die in `parameters` staat, wordt een plugin-parameter (automatiseerbaar); anders een vaste waarde.

// Blok-soorten: dezelfde nummers als enum Blok in dsp/pb_graaf.h (tests/graaf.test.mjs controleert de volgorde).
export const SOORT = { in: 0, uit: 1, som: 2, versterker: 3, tape: 4, vertraging: 5, hoogdoor1: 6, svf: 7,
  bandbeweging: 8, mix: 9, biquad: 10, lfo: 11, laagdoor1: 12 };

// De bibliotheek. Per blok wat het doet in gewone woorden, poorten, knoppen (in hun eigen eenheid), en wat het kost.
// CPU komt uit tools/meet-blokken.mjs (meting-blokken.json); latency en aliasing uit de toetsen. "bouw" = hoeveel
// werk het is om dit blok goed te bouwen en te toetsen (1 = een middag, 5 = weken), een schatting.
export const BIB = {
  in: { naam: 'Ingang', groep: 'techniek', soort: 'utility', uitleg: 'Het geluid dat de host de plugin geeft.',
    in: [], uit: ['uit'], knoppen: [], bouw: 1, latency: 0, vast: true },
  uit: { naam: 'Uitgang', groep: 'techniek', soort: 'utility', uitleg: 'Wat de plugin teruggeeft aan de host.',
    in: ['in'], uit: [], knoppen: [], bouw: 1, latency: 0, vast: true },
  som: { naam: 'Optellen', groep: 'techniek', soort: 'utility', uitleg: 'Telt twee signalen op. Nodig om een terugkoppeling terug in de keten te brengen.',
    in: ['a', 'b'], uit: ['uit'], knoppen: [], bouw: 1, latency: 0 },
  versterker: { naam: 'Volume', groep: 'techniek', soort: 'utility', uitleg: 'Maakt een signaal harder of zachter. In een terugkoppeling is dit hoeveel er terug gaat.',
    in: ['in'], uit: ['uit'], knoppen: [{ id: 'hoeveel', naam: 'hoeveel', eenheid: '', min: 0, max: 1.25, std: 0.5, glijMs: 30 }], bouw: 1, latency: 0 },
  tape: { naam: 'Tapeverzadiging', groep: 'klank', soort: 'effect', cat: 'saturatie en analoge modellen',
    uitleg: 'De band loopt warm dicht als het hard wordt: zacht afgeronde pieken (tanh), met anti-aliasing (ADAA, Parker 2016). Remt een terugkoppeling die doorslaat.',
    in: ['in'], uit: ['uit'], knoppen: [{ id: 'verzadiging', naam: 'verzadiging', eenheid: 'dB', min: 0, max: 24, std: 6, glijMs: 20 }],
    bouw: 3, latency: 0.5, let: 'Een halve sample vertraging en −3 dB op ¼ van de samplerate (ADAA voor kleine signalen). Met de toon open aliast hij hoorbaar bij veel verzadiging: 2× oversampling is dan de volgende stap.' },
  vertraging: { naam: 'Vertraging', groep: 'klank', soort: 'effect', cat: 'delay en echo',
    uitleg: 'Speelt het signaal later terug. De tijd mag bewegen; tussen samples wordt geïnterpoleerd (Lagrange, 3e orde).',
    in: ['in', 'beweging'], uit: ['uit'], knoppen: [{ id: 'tijd', naam: 'tijd', eenheid: 'ms', min: 1, max: 2000, std: 375, glijMs: 250 }],
    vasteEig: [{ id: 'max', naam: 'langste tijd', eenheid: 'ms', std: 1200 }], bouw: 2, latency: 0,
    let: 'Lagrange-3 dempt hoog: −0,03 dB op 0,1·fs, −1,07 dB op 0,25·fs bij een halve sample fractie. Draaien aan de tijd laat de toon glijden (zoals een band).' },
  hoogdoor1: { naam: 'Laag weg', groep: 'klank', soort: 'effect', cat: 'filters', uitleg: 'Haalt laag weg, zacht (6 dB per octaaf).',
    in: ['in'], uit: ['uit'], knoppen: [{ id: 'freq', naam: 'frequentie', eenheid: 'Hz', min: 20, max: 1000, std: 100, glijMs: 20, exp: true }], bouw: 1, latency: 0 },
  laagdoor1: { naam: 'Hoog weg (zacht)', groep: 'klank', soort: 'effect', cat: 'filters', uitleg: 'Haalt hoog weg, zacht (6 dB per octaaf).',
    in: ['in'], uit: ['uit'], knoppen: [{ id: 'freq', naam: 'frequentie', eenheid: 'Hz', min: 100, max: 20000, std: 4000, glijMs: 20, exp: true }], bouw: 1, latency: 0 },
  svf: { naam: 'Filter (SVF)', groep: 'klank', soort: 'effect', cat: 'filters',
    uitleg: 'Een filter dat je snel mag bewegen zonder tikken (TPT, Zavalishin). Laagdoor, banddoor en hoogdoor tegelijk.',
    in: ['in'], uit: ['laag', 'band', 'hoog'], knoppen: [
      { id: 'freq', naam: 'frequentie', eenheid: 'Hz', min: 30, max: 18000, std: 3000, glijMs: 20, exp: true },
      { id: 'q', naam: 'resonantie', eenheid: 'Q', min: 0.5, max: 8, std: 0.70703125, glijMs: 20 }],
    bouw: 2, latency: 0, let: 'Resonantie in een terugkoppeling verhoogt de lusversterking op die frequentie: daar gaat hij het eerst fluiten.' },
  biquad: { naam: 'Biquad', groep: 'klank', soort: 'effect', cat: 'filters', uitleg: 'Klassiek filter (RBJ-cookbook). Prima als hij stilstaat; snel moduleren kan tikken (gebruik dan de SVF).',
    in: ['in'], uit: ['uit'], knoppen: [
      { id: 'freq', naam: 'frequentie', eenheid: 'Hz', min: 20, max: 20000, std: 1000, glijMs: 20, exp: true },
      { id: 'q', naam: 'resonantie', eenheid: 'Q', min: 0.25, max: 10, std: 0.70703125, glijMs: 20 }],
    vasteEig: [{ id: 'soort', naam: 'soort', keuzes: ['laagdoor', 'hoogdoor', 'banddoor', 'kerf'], std: 0 }], bouw: 1, latency: 0 },
  bandbeweging: { naam: 'Bandbeweging', groep: 'besturing', soort: 'modulatie', cat: 'randomisatie',
    uitleg: 'Wow en flutter: de band loopt nooit helemaal gelijk. Sluit aan op "beweging" van een vertraging.',
    in: [], uit: ['beweging'], knoppen: [{ id: 'diepte', naam: 'haperen', eenheid: '', min: 0, max: 1, std: 0.25, glijMs: 20 }], bouw: 2, latency: 0, gedeeld: true },
  lfo: { naam: 'LFO', groep: 'besturing', soort: 'modulatie', cat: 'LFO', uitleg: 'Een langzame golf die een knop laat bewegen. Sleep hem op een knop.',
    in: [], uit: ['golf'], knoppen: [{ id: 'snelheid', naam: 'snelheid', eenheid: 'Hz', min: 0.02, max: 20, std: 0.5, glijMs: 20, exp: true }], bouw: 1, latency: 0, gedeeld: true },
  mix: { naam: 'Mix', groep: 'techniek', soort: 'utility', uitleg: 'Mengt het droge signaal en het effect.',
    in: ['droog', 'nat'], uit: ['uit'], knoppen: [{ id: 'mix', naam: 'mix', eenheid: '', min: 0, max: 1, std: 0.375, glijMs: 20 }], bouw: 1, latency: 0 },
};

// Wat er in de volledige bibliotheek komt maar nog niet gebouwd is (TASKS.md "Bibliotheek later"). De tool toont ze
// grijs, zodat de catalogus al de vorm heeft waar alles in past.
export const BIB_LATER = [
  ['oscillator (polyBLEP)', 'klank', 'instrument', 'oscillatoren'], ['sampler', 'klank', 'instrument', 'sampler'],
  ['granulaire wolk', 'klank', 'instrument', 'granulair'], ['ruis', 'klank', 'instrument', 'ruis'],
  ['snaar (Karplus-Strong)', 'klank', 'instrument', 'fysische modellen'], ['ladderfilter', 'klank', 'effect', 'filters'],
  ['kamfilter', 'klank', 'effect', 'filters'], ['compressor', 'klank', 'effect', 'dynamiek'], ['limiter', 'klank', 'effect', 'dynamiek'],
  ['gate', 'klank', 'effect', 'dynamiek'], ['buis', 'klank', 'effect', 'saturatie en analoge modellen'],
  ['tape-hysterese (Jiles–Atherton)', 'klank', 'effect', 'saturatie en analoge modellen'], ['ping-pong', 'klank', 'effect', 'delay en echo'],
  ['plaatgalm (Dattorro)', 'klank', 'effect', 'reverb'], ['veergalm', 'klank', 'effect', 'reverb'], ['convolutiegalm', 'klank', 'effect', 'reverb'],
  ['chorus', 'klank', 'effect', 'modulatie-effecten'], ['flanger', 'klank', 'effect', 'modulatie-effecten'], ['phaser', 'klank', 'effect', 'modulatie-effecten'],
  ['pitchshift', 'klank', 'effect', 'pitch en tijd'], ['spectraal bevriezen', 'klank', 'effect', 'spectraal'], ['mid/side', 'klank', 'utility', 'ruimtelijk'],
  ['envelope', 'besturing', 'modulatie', 'envelopes'], ['MSEG', 'besturing', 'modulatie', 'MSEG'], ['macro', 'besturing', 'modulatie', "macro's"],
  ['sidechain', 'besturing', 'effect', 'sidechain'], ['MIDI/MPE', 'besturing', 'MIDI-effect', 'MIDI en MPE'], ['tempo-sync', 'besturing', 'modulatie', 'tempo-sync'],
  ['oversampling 2×/4×', 'techniek', 'utility', 'oversampling'], ['presets', 'techniek', 'utility', 'presets en state'],
  ['XY-vlak', 'UI', 'utility', 'XY-vlak'], ['meter', 'UI', 'analyser', 'meters'], ['scope', 'UI', 'analyser', 'scopes'],
];

const knop = (type, id) => BIB[type].knoppen.find((k) => k.id === id);

// De uitvoervolgorde: diepte-eerst vanaf de ingang; een kabel naar een knoop die nog op de stapel ligt is een
// terugkoppeling (die leest de vorige sample). Elke terugkoppeling moet door een vertraging lopen.
export function volgorde(ontwerp) {
  const fouten = [], ids = ontwerp.knopen.map((k) => k.id), uitKabels = new Map(ids.map((i) => [i, []]));
  for (const k of ontwerp.kabels) uitKabels.get(k.van[0])?.push(k);
  const staat = new Map(), post = [], terug = [];
  const bezoek = (id, pad) => {
    staat.set(id, 1);
    for (const k of uitKabels.get(id) || []) {
      const doel = k.naar[0];
      if (staat.get(doel) === 1) {
        terug.push(k);
        const lus = pad.slice(pad.indexOf(doel));
        if (!lus.some((n) => typeVan(ontwerp, n) === 'vertraging'))
          fouten.push(`terugkoppeling ${k.van[0]} → ${doel} heeft geen vertraging: hij zou in nul tijd rond moeten`);
      } else if (!staat.get(doel)) bezoek(doel, [...pad, doel]);
    }
    staat.set(id, 2); post.push(id);
  };
  const begin = ontwerp.knopen.filter((k) => k.type === 'in' || BIB[k.type].gedeeld).map((k) => k.id);
  for (const b of begin) if (!staat.get(b)) bezoek(b, [b]);
  for (const id of ids) if (!staat.get(id)) bezoek(id, [id]);   // losse knopen achteraan
  const orde = post.reverse();
  // gedeelde knopen vooraan (de motor rekent ze toch eerst), de rest in topologische volgorde
  const gedeeld = orde.filter((id) => BIB[typeVan(ontwerp, id)].gedeeld);
  return { orde: [...gedeeld, ...orde.filter((id) => !gedeeld.includes(id))], terug, fouten };
}
const typeVan = (o, id) => o.knopen.find((k) => k.id === id).type;

// compileer: ontwerp → { rij (Float64Array voor pg_laad / de header), params, orde, fouten }
export function compileer(ontwerp) {
  const fouten = [];
  for (const k of ontwerp.knopen) if (!BIB[k.type]) fouten.push(`onbekend blok ${k.type}`);
  if (fouten.length) return { fouten };
  const { orde, fouten: f2 } = volgorde(ontwerp);
  fouten.push(...f2);
  if (!ontwerp.knopen.some((k) => k.type === 'in')) fouten.push('er is geen ingang');
  if (!ontwerp.knopen.some((k) => k.type === 'uit')) fouten.push('er is geen uitgang');
  const plek = new Map(orde.map((id, i) => [id, i]));
  const params = ontwerp.parameters.map((p) => {
    const kn = ontwerp.knopen.find((k) => k.id === p.knoop), def = knop(kn.type, p.knop);
    return { naam: p.naam, eenheid: p.eenheid ?? def.eenheid, min: p.min ?? def.min, max: p.max ?? def.max,
      standaard: p.standaard ?? kn.waarden?.[p.knop] ?? def.std, glijMs: p.glijMs ?? def.glijMs, uitleg: p.uitleg ?? '',
      knoop: p.knoop, knop: p.knop, glijden: p.glijden ?? 'glijden' };
  });
  const rij = [orde.length, params.length];
  for (const id of orde) {
    const kn = ontwerp.knopen.find((k) => k.id === id), bib = BIB[kn.type];
    rij.push(SOORT[kn.type]);
    for (let i = 0; i < 2; i++) {
      const kabel = ontwerp.kabels.find((k) => k.naar[0] === id && k.naar[1] === (bib.in[i] ?? '\u0000'));
      if (kabel) {
        const van = ontwerp.knopen.find((k) => k.id === kabel.van[0]);
        rij.push(plek.get(kabel.van[0]), Math.max(0, BIB[van.type].uit.indexOf(kabel.van[1])));
      } else rij.push(-1, 0);
    }
    for (let j = 0; j < 4; j++) {
      const def = bib.knoppen[j];
      if (!def) { rij.push(-1, 0, -1, 0, 0); continue; }
      const pi = params.findIndex((p) => p.knoop === id && p.knop === def.id);
      const mod = (ontwerp.modulaties || []).find((m) => m.doel[0] === id && m.doel[1] === def.id);
      const modKnoop = mod ? plek.get(mod.bron[0]) : -1;
      const modPoort = mod ? Math.max(0, BIB[typeVan(ontwerp, mod.bron[0])].uit.indexOf(mod.bron[1])) : 0;
      rij.push(pi, kn.waarden?.[def.id] ?? def.std, modKnoop, modPoort, mod ? mod.diepte : 0);
    }
    let v0 = 0, v1 = 0;
    if (kn.type === 'vertraging') {
      v0 = kn.vast?.max ?? 1200;
      // compensatie: de fractionele latency van wat er vlak vóór de vertraging zit (de tape: ½ sample)
      const voor = ontwerp.kabels.find((k) => k.naar[0] === id && k.naar[1] === 'in');
      v1 = voor ? (BIB[typeVan(ontwerp, voor.van[0])].latency || 0) : 0;
      const pTijd = params.find((p) => p.knoop === id && p.knop === 'tijd');
      if (pTijd && pTijd.max > v0) fouten.push(`de tijd van ${id} gaat tot ${pTijd.max} ms, de lijn maar tot ${v0} ms`);
    }
    if (kn.type === 'biquad') v0 = kn.vast?.soort ?? 0;
    rij.push(v0, v1);
  }
  for (const p of params) rij.push(p.min, p.max, p.standaard, p.glijMs);
  for (const p of params) for (const [k, v] of [['min', p.min], ['max', p.max], ['standaard', p.standaard]])
    if (Math.fround(v) !== v) fouten.push(`${p.naam}: ${k} ${v} past niet exact in float32 (een AU geeft float door)`);
  return { rij: Float64Array.from(rij), params, orde, fouten };
}

// Latency van het ontwerp in hele samples (die moet de plugin melden). Fractionele delen worden gecompenseerd.
export function latentie(ontwerp) {
  return ontwerp.knopen.some((k) => BIB[k.type].latency >= 1) ? Math.max(...ontwerp.knopen.map((k) => Math.floor(BIB[k.type].latency))) : 0;
}
