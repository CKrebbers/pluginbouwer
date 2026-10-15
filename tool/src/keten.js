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
  bandbeweging: 8, mix: 9, biquad: 10, lfo: 11, laagdoor1: 12,
  osc: 13, ruis: 14, sequencer: 15, toetsen: 16, omhulling: 17, snaar: 18, trommel: 19, vermenigvuldig: 20,
  galm: 21, chorus: 22, compressor: 23, vergruizer: 24, vouwer: 25, panner: 26, willekeur: 27 };

// De patronen van de sequencer, in de volgorde van PATRONEN in dsp/pb_bronnen.h (tests/bronnen.test.mjs controleert
// het aantal). Zestien stappen van een zestiende.
export const PATRONEN = ['vier op de vloer', 'achterslag (2 en 4)', 'tegentijd (skank)', 'zestienden', 'dubbas', 'acid',
  'arpeggio', 'clave', 'halftijd kick', 'halftijd snare', 'aanhouden (drone)', 'toeval (pentatonisch)'];
export const GOLVEN = ['sinus', 'zaag', 'blok', 'driehoek'];
// blokken die zelf geluid maken: een ontwerp met een van deze heeft geen ingang nodig
export const BRONNEN = ['osc', 'ruis', 'snaar', 'trommel'];

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
    in: [], uit: ['golf'], knoppen: [{ id: 'snelheid', naam: 'snelheid', eenheid: 'Hz', min: 0.015625, max: 20, std: 0.5, glijMs: 20, exp: true }], bouw: 1, latency: 0, gedeeld: true },
  mix: { naam: 'Mix', groep: 'techniek', soort: 'utility', uitleg: 'Mengt het droge signaal en het effect.',
    in: ['droog', 'nat'], uit: ['uit'], knoppen: [{ id: 'mix', naam: 'mix', eenheid: '', min: 0, max: 1, std: 0.375, glijMs: 20 }], bouw: 1, latency: 0 },

  // ---- erbij op 15 okt 2026 (dsp/pb_bronnen.h): bronnen, bespelen, meer effecten ----
  osc: { naam: 'Oscillator', groep: 'klank', soort: 'instrument', cat: 'oscillatoren',
    uitleg: 'Maakt zelf een toon: sinus, zaag, blok of driehoek. Zaag en blok zonder hoorbare aliasing (polyBLEP). Sluit "toon" aan op een sequencer of toetsen om melodieën te spelen; "fm" laat een tweede oscillator de toon bewegen (bellen, metaal).',
    in: ['toon', 'fm'], uit: ['uit'], knoppen: [
      { id: 'freq', naam: 'toon', eenheid: 'Hz', min: 20, max: 8000, std: 220, glijMs: 20, exp: true },
      { id: 'vorm', naam: 'pulsbreedte', eenheid: '', min: 0.0625, max: 0.9375, std: 0.5, glijMs: 20 },
      { id: 'fm', naam: 'fm-diepte', eenheid: '', min: 0, max: 8, std: 1, glijMs: 20 },
      { id: 'fijn', naam: 'fijn', eenheid: 'cent', min: -50, max: 50, std: 0, glijMs: 20 }],
    vasteEig: [{ id: 'golf', naam: 'golfvorm', keuzes: GOLVEN, std: 1 }], bouw: 2, latency: 0,
    let: 'polyBLEP haalt de ergste aliasing weg, niet alle: hoog in het register (boven ~4 kHz) blijft er wat over. Pulsbreedte werkt alleen bij blok.' },
  ruis: { naam: 'Ruis', groep: 'klank', soort: 'instrument', cat: 'ruis',
    uitleg: 'Ruis: van wit (alles even hard, sissend) naar donker (meer laag, als wind of zee). Links en rechts verschillen, dus hij klinkt breed.',
    in: [], uit: ['uit'], knoppen: [{ id: 'kleur', naam: 'kleur', eenheid: '', min: 0, max: 1, std: 0, glijMs: 20 }], bouw: 1, latency: 0 },
  sequencer: { naam: 'Sequencer', groep: 'besturing', soort: 'modulatie', cat: 'tempo-sync',
    uitleg: 'Speelt een patroon van zestien stappen: "poort" gaat open bij elke noot, "toon" zegt welke noot (sluit aan op een oscillator, snaar of trommel), "accent" op de zware tellen. Eigen tempo (nog niet gekoppeld aan de host).',
    in: [], uit: ['poort', 'toon', 'accent'], knoppen: [
      { id: 'tempo', naam: 'tempo', eenheid: 'BPM', min: 40, max: 240, std: 120, glijMs: 20 },
      { id: 'lengte', naam: 'nootlengte', eenheid: '', min: 0.0625, max: 1, std: 0.5, glijMs: 20 },
      { id: 'zwaai', naam: 'swing', eenheid: '', min: 0, max: 0.375, std: 0, glijMs: 20 },
      { id: 'transponeer', naam: 'transponeer', eenheid: 'st', min: -24, max: 24, std: 0, glijMs: 0 }],
    vasteEig: [{ id: 'patroon', naam: 'patroon', keuzes: PATRONEN, std: 0 }, { id: 'zaad', naam: 'zaad (bij toeval)', eenheid: '', std: 1 }],
    bouw: 2, latency: 0, gedeeld: true },
  toetsen: { naam: 'Toetsen', groep: 'besturing', soort: 'modulatie', cat: 'MIDI en MPE',
    uitleg: 'Speel zelf: de middelste rij van je toetsenbord (a s d f …) of de knoppen onder Horen. "poort" is open zolang je een toets vasthoudt, "toon" is welke. In de plugin worden dit twee parameters (MIDI komt later).',
    in: [], uit: ['poort', 'toon'], knoppen: [
      { id: 'toon', naam: 'toon', eenheid: '×', min: 0.25, max: 4, std: 1, glijMs: 5 },
      { id: 'poort', naam: 'poort', eenheid: '', min: 0, max: 1, std: 0, glijMs: 0 }],
    bouw: 3, latency: 0, gedeeld: true, let: 'Nog geen echte MIDI-ingang: in de plugin zijn "Noot" en "Poort" automatiseerbare parameters. MIDI/MPE staat in de bibliotheek voor later.' },
  omhulling: { naam: 'Omhulling (ADSR)', groep: 'besturing', soort: 'modulatie', cat: 'envelopes',
    uitleg: 'Vormt elke noot: aanzet (hoe snel hij er is), verval, aanhouden (niveau zolang de poort open is) en loslaten. Sluit "poort" aan op een sequencer of toetsen, en de uitgang op een Vermenigvuldig (volume) of sleep hem op een filterknop.',
    in: ['poort'], uit: ['omhulling'], knoppen: [
      { id: 'aanzet', naam: 'aanzet', eenheid: 'ms', min: 0.5, max: 5000, std: 5, glijMs: 20, exp: true },
      { id: 'verval', naam: 'verval', eenheid: 'ms', min: 1, max: 10000, std: 250, glijMs: 20, exp: true },
      { id: 'aanhoud', naam: 'aanhouden', eenheid: '', min: 0, max: 1, std: 0.5, glijMs: 20 },
      { id: 'loslaten', naam: 'loslaten', eenheid: 'ms', min: 1, max: 20000, std: 300, glijMs: 20, exp: true }],
    bouw: 1, latency: 0 },
  snaar: { naam: 'Snaar', groep: 'klank', soort: 'instrument', cat: 'fysische modellen',
    uitleg: 'Een getokkelde snaar (Karplus–Strong): bij elke aanslag gaat er een tikje ruis een lus in die zo lang is als één trilling. Verval = hoe lang hij doorklinkt, helderheid = hoe metalig.',
    in: ['poort', 'toon'], uit: ['uit'], knoppen: [
      { id: 'freq', naam: 'toon', eenheid: 'Hz', min: 30, max: 2000, std: 110, glijMs: 20, exp: true },
      { id: 'verval', naam: 'verval', eenheid: 's', min: 0.0625, max: 20, std: 2, glijMs: 20, exp: true },
      { id: 'helder', naam: 'helderheid', eenheid: '', min: 0, max: 1, std: 0.5, glijMs: 20 }],
    bouw: 2, latency: 0 },
  trommel: { naam: 'Trommel', groep: 'klank', soort: 'instrument', cat: 'fysische modellen',
    uitleg: 'Eén slagwerkstem: een toon die snel zakt (kick, tom) met ruis erbij (snare, hihat). Sluit "poort" aan op een sequencer.',
    in: ['poort', 'toon'], uit: ['uit'], knoppen: [
      { id: 'freq', naam: 'toon', eenheid: 'Hz', min: 20, max: 2000, std: 55, glijMs: 20, exp: true },
      { id: 'val', naam: 'toonval', eenheid: 'oct', min: 0, max: 6, std: 2.5, glijMs: 20 },
      { id: 'verval', naam: 'verval', eenheid: 'ms', min: 10, max: 3000, std: 400, glijMs: 20, exp: true },
      { id: 'ruis', naam: 'ruis', eenheid: '', min: 0, max: 1, std: 0, glijMs: 20 }],
    bouw: 2, latency: 0 },
  vermenigvuldig: { naam: 'Vermenigvuldig', groep: 'techniek', soort: 'utility', cat: 'dynamiek',
    uitleg: 'a × b. Met een omhulling op b is dit het volume van een noot (VCA); met twee tonen is het ringmodulatie (metalig, robot).',
    in: ['a', 'b'], uit: ['uit'], knoppen: [], bouw: 1, latency: 0 },
  galm: { naam: 'Galm', groep: 'klank', soort: 'effect', cat: 'reverb',
    uitleg: 'Een ruimte: acht vertragingen die alles met alles mengen (feedback delay network). Tijd = hoe lang tot hij 60 dB stiller is, demping = hoe snel het hoog wegsterft, grootte = hoe groot de ruimte klinkt. Geeft alleen de galm: meng met een Mix.',
    in: ['in'], uit: ['uit'], knoppen: [
      { id: 'tijd', naam: 'galmtijd', eenheid: 's', min: 0.25, max: 30, std: 2.5, glijMs: 50, exp: true },
      { id: 'demping', naam: 'demping', eenheid: 'Hz', min: 500, max: 18000, std: 6000, glijMs: 20, exp: true },
      { id: 'grootte', naam: 'grootte', eenheid: '', min: 0.5, max: 2, std: 1, glijMs: 100 }],
    bouw: 3, latency: 0, let: 'Zonder diffusie vooraf: korte tikken klinken in het begin wat korrelig. Lange galmtijd + weinig demping = bijna oneindig.' },
  chorus: { naam: 'Chorus / flanger', groep: 'klank', soort: 'effect', cat: 'modulatie-effecten',
    uitleg: 'Een korte vertraging die heen en weer wiebelt: zwevend en breed (chorus). Met terugkoppeling en een korte tijd wordt het een straaljager (flanger). Geeft alleen het effect: meng met een Mix.',
    in: ['in'], uit: ['uit'], knoppen: [
      { id: 'tijd', naam: 'tijd', eenheid: 'ms', min: 0.5, max: 40, std: 12, glijMs: 50 },
      { id: 'diepte', naam: 'diepte', eenheid: 'ms', min: 0, max: 15, std: 3, glijMs: 50 },
      { id: 'snelheid', naam: 'snelheid', eenheid: 'Hz', min: 0.015625, max: 10, std: 0.375, glijMs: 20, exp: true },
      { id: 'terug', naam: 'terugkoppeling', eenheid: '', min: -0.875, max: 0.875, std: 0, glijMs: 20 }],
    bouw: 2, latency: 0 },
  compressor: { naam: 'Compressor', groep: 'klank', soort: 'effect', cat: 'dynamiek',
    uitleg: 'Maakt harde stukken zachter: boven de drempel gaat het niveau "verhouding" keer minder omhoog. Sluit iets op "zijketen" aan (bijvoorbeeld een kick) en hij pompt mee: sidechain.',
    in: ['in', 'zijketen'], uit: ['uit'], knoppen: [
      { id: 'drempel', naam: 'drempel', eenheid: 'dB', min: -60, max: 0, std: -18, glijMs: 20 },
      { id: 'verhouding', naam: 'verhouding', eenheid: ':1', min: 1, max: 20, std: 4, glijMs: 20, exp: true },
      { id: 'aanzet', naam: 'aanzet', eenheid: 'ms', min: 0.125, max: 200, std: 5, glijMs: 20, exp: true },
      { id: 'loslaten', naam: 'loslaten', eenheid: 'ms', min: 5, max: 2000, std: 150, glijMs: 20, exp: true }],
    bouw: 2, latency: 0, let: 'Harde knie en geen vooruitkijken: een heel snelle aanzet kan een tikje geven.' },
  vergruizer: { naam: 'Vergruizer', groep: 'klank', soort: 'effect', cat: 'saturatie en analoge modellen',
    uitleg: 'Lo-fi: minder bits (grover, ruizig) en een lagere samplerate (metalig, digitaal). Glitch.',
    in: ['in'], uit: ['uit'], knoppen: [
      { id: 'bits', naam: 'bits', eenheid: '', min: 1, max: 16, std: 8, glijMs: 20 },
      { id: 'deling', naam: 'samplerate ÷', eenheid: '', min: 1, max: 64, std: 4, glijMs: 0 }],
    bouw: 1, latency: 0, let: 'Aliast met opzet: dat is het geluid.' },
  vouwer: { naam: 'Vouwer', groep: 'klank', soort: 'effect', cat: 'saturatie en analoge modellen',
    uitleg: 'Wavefolder: duw de golf over de rand en hij vouwt terug. Weinig drive = warm, veel = scherpe, zingende boventonen. Scheef geeft even boventonen.',
    in: ['in'], uit: ['uit'], knoppen: [
      { id: 'drive', naam: 'drive', eenheid: 'dB', min: 0, max: 36, std: 12, glijMs: 20 },
      { id: 'scheef', naam: 'scheef', eenheid: '', min: -1, max: 1, std: 0, glijMs: 20 }],
    bouw: 1, latency: 0, let: 'Geen anti-aliasing: hoge tonen met veel drive aliasen hoorbaar.' },
  panner: { naam: 'Panner', groep: 'klank', soort: 'utility', cat: 'ruimtelijk',
    uitleg: 'Zet het geluid links of rechts (gelijk vermogen: in het midden −3 dB per kant). Sleep een LFO op positie en hij draait rond.',
    in: ['in'], uit: ['uit'], knoppen: [{ id: 'positie', naam: 'positie', eenheid: '', min: -1, max: 1, std: 0, glijMs: 20 }], bouw: 1, latency: 0 },
  willekeur: { naam: 'Willekeur', groep: 'besturing', soort: 'modulatie', cat: 'randomisatie',
    uitleg: 'Sample & hold: springt een paar keer per seconde naar een willekeurige waarde (glad = glijdt erheen). Sleep hem op een knop.',
    in: [], uit: ['golf'], knoppen: [
      { id: 'snelheid', naam: 'snelheid', eenheid: 'Hz', min: 0.0625, max: 40, std: 2, glijMs: 20, exp: true },
      { id: 'glad', naam: 'glad', eenheid: '', min: 0, max: 1, std: 0, glijMs: 20 }],
    bouw: 1, latency: 0, gedeeld: true },
};

// Wat er in de volledige bibliotheek komt maar nog niet gebouwd is (TASKS.md "Bibliotheek later"). De tool toont ze
// grijs, zodat de catalogus al de vorm heeft waar alles in past.
export const BIB_LATER = [
  ['sampler', 'klank', 'instrument', 'sampler'],
  ['granulaire wolk', 'klank', 'instrument', 'granulair'], ['ladderfilter', 'klank', 'effect', 'filters'],
  ['kamfilter', 'klank', 'effect', 'filters'], ['limiter', 'klank', 'effect', 'dynamiek'],
  ['gate', 'klank', 'effect', 'dynamiek'], ['buis', 'klank', 'effect', 'saturatie en analoge modellen'],
  ['tape-hysterese (Jiles–Atherton)', 'klank', 'effect', 'saturatie en analoge modellen'], ['ping-pong', 'klank', 'effect', 'delay en echo'],
  ['plaatgalm (Dattorro)', 'klank', 'effect', 'reverb'], ['veergalm', 'klank', 'effect', 'reverb'], ['convolutiegalm', 'klank', 'effect', 'reverb'],
  ['phaser', 'klank', 'effect', 'modulatie-effecten'],
  ['pitchshift', 'klank', 'effect', 'pitch en tijd'], ['spectraal bevriezen', 'klank', 'effect', 'spectraal'], ['mid/side', 'klank', 'utility', 'ruimtelijk'],
  ['MSEG', 'besturing', 'modulatie', 'MSEG'], ['macro', 'besturing', 'modulatie', "macro's"],
  ['MIDI/MPE', 'besturing', 'MIDI-effect', 'MIDI en MPE'], ['tempo-sync', 'besturing', 'modulatie', 'tempo-sync'],
  ['plaatgalm met diffusie (Dattorro)', 'klank', 'effect', 'reverb'], ['oversampling 2×/4×', 'techniek', 'utility', 'oversampling'], ['presets', 'techniek', 'utility', 'presets en state'],
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
  if (!ontwerp.knopen.some((k) => k.type === 'in' || BRONNEN.includes(k.type))) fouten.push('er is geen ingang en geen bron (oscillator, ruis, snaar of trommel)');
  if (!ontwerp.knopen.some((k) => k.type === 'uit')) fouten.push('er is geen uitgang');
  const plek = new Map(orde.map((id, i) => [id, i]));
  const params = ontwerp.parameters.map((p) => {
    const kn = ontwerp.knopen.find((k) => k.id === p.knoop), def = knop(kn.type, p.knop);
    return { naam: p.naam, eenheid: p.eenheid ?? def.eenheid, min: p.min ?? def.min, max: p.max ?? def.max,
      standaard: p.standaard ?? kn.waarden?.[p.knop] ?? def.std, glijMs: p.glijMs ?? def.glijMs, uitleg: p.uitleg ?? '',
      knoop: p.knoop, knop: p.knop, glijden: p.glijden ?? 'glijden', ...(p.ook ? { ook: p.ook } : {}) };
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
      // een parameter stuurt zijn eigen knop, en eventueel meer ("ook": [[knoop, knop, schaal]])
      let pi = params.findIndex((p) => p.knoop === id && p.knop === def.id), schaal = 1;
      if (pi < 0) {
        pi = params.findIndex((p) => (p.ook || []).some(([k, kn2]) => k === id && kn2 === def.id));
        if (pi >= 0) schaal = params[pi].ook.find(([k, kn2]) => k === id && kn2 === def.id)[2] ?? 1;
      }
      const mod = (ontwerp.modulaties || []).find((m) => m.doel[0] === id && m.doel[1] === def.id);
      const modKnoop = mod ? plek.get(mod.bron[0]) : -1;
      let modPoort = mod ? Math.max(0, BIB[typeVan(ontwerp, mod.bron[0])].uit.indexOf(mod.bron[1])) : 0;
      // diepte 1 = het hele bereik van de knop. Knoppen op een log-schaal (frequentie, tijd) bewegen in octaven
      // (poort + 8 = exponentieel in pb_graaf.h); lineaire knoppen: een plugin-parameter schaalt de motor zelf
      // met zijn bereik, een vaste knop krijgt het bereik uit de BIB mee.
      let diepte = mod ? mod.diepte : 0;
      if (mod) {
        const p = pi >= 0 ? params[pi] : null, lo = p ? p.min : def.min, hi = p ? p.max : def.max;
        if (def.exp && lo > 0) { modPoort += 8; diepte *= Math.log2(hi / lo); }
        else if (pi < 0) diepte *= hi - lo;
      }
      if (pi >= 0 && schaal !== 1) rij.push(pi + 1000, schaal, modKnoop, modPoort, diepte);
      else rij.push(pi, kn.waarden?.[def.id] ?? def.std, modKnoop, modPoort, diepte);
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
    else if (kn.type === 'biquad') v0 = kn.vast?.soort ?? 0;
    else if (bib.vasteEig) {
      v0 = kn.vast?.[bib.vasteEig[0].id] ?? bib.vasteEig[0].std;
      if (bib.vasteEig[1]) v1 = kn.vast?.[bib.vasteEig[1].id] ?? bib.vasteEig[1].std;
    }
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
