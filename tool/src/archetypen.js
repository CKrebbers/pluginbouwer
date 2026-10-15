// archetypen.js — ontwerpen om mee te beginnen. De dub-echo is het ontwerp dat de hele keten doorloopt (PROMPT.md);
// de andere twee laten zien dat de catalogus meer kan, en zijn (nog) niet tot plugin gebouwd.
// Namen naar wat ze doen, niet naar een merk (design.md).

export const DUB_ECHO = {
  naam: 'Dub-echo', id: 'dub_echo',
  uitleg: 'Tape-echo voor dub: saturatie en een filter in de terugkoppeling. Boven terugkoppeling 1 gaat hij vanzelf door, en de band houdt hem in toom.',
  knopen: [
    { id: 'in', type: 'in', x: 20, y: 200 },
    { id: 'band', type: 'bandbeweging', x: 360, y: 60, waarden: { diepte: 0.25 } },
    { id: 'som', type: 'som', x: 130, y: 200 },
    { id: 'tape', type: 'tape', x: 240, y: 200, waarden: { verzadiging: 6 } },
    { id: 'lijn', type: 'vertraging', x: 360, y: 200, waarden: { tijd: 375 }, vast: { max: 1200 } },
    { id: 'laag', type: 'hoogdoor1', x: 480, y: 200, waarden: { freq: 100 } },
    { id: 'toon', type: 'svf', x: 600, y: 200, waarden: { freq: 3000, q: 0.70703125 } },
    { id: 'terug', type: 'versterker', x: 380, y: 360, waarden: { hoeveel: 0.5 } },
    { id: 'mix', type: 'mix', x: 790, y: 200, waarden: { mix: 0.375 } },
    { id: 'uit', type: 'uit', x: 910, y: 200 },
  ],
  kabels: [
    { van: ['in', 'uit'], naar: ['som', 'a'] },
    { van: ['terug', 'uit'], naar: ['som', 'b'] },
    { van: ['som', 'uit'], naar: ['tape', 'in'] },
    { van: ['tape', 'uit'], naar: ['lijn', 'in'] },
    { van: ['band', 'beweging'], naar: ['lijn', 'beweging'] },
    { van: ['lijn', 'uit'], naar: ['laag', 'in'] },
    { van: ['laag', 'uit'], naar: ['toon', 'in'] },
    { van: ['toon', 'laag'], naar: ['terug', 'in'] },
    { van: ['in', 'uit'], naar: ['mix', 'droog'] },
    { van: ['toon', 'laag'], naar: ['mix', 'nat'] },
    { van: ['mix', 'uit'], naar: ['uit', 'in'] },
  ],
  // in deze volgorde ook in de plugin (en dezelfde grenzen als dsp/ontwerpen/dub_echo.h)
  parameters: [
    { naam: 'Tijd', knoop: 'lijn', knop: 'tijd', min: 20, max: 1200, standaard: 375, glijMs: 250, glijden: 'glijden',
      uitleg: 'Hoe lang tot de echo komt. Draaien laat de toon glijden, zoals een band die sneller gaat' },
    { naam: 'Terugkoppeling', knoop: 'terug', knop: 'hoeveel', min: 0, max: 1.25, standaard: 0.5, glijMs: 30,
      uitleg: 'Hoeveel van de echo weer de band op gaat. Boven 1 gaat hij vanzelf door (zelfoscillatie)' },
    { naam: 'Toon', knoop: 'toon', knop: 'freq', min: 300, max: 12000, standaard: 3000, glijMs: 20,
      uitleg: 'Hoe donker elke herhaling wordt: het filter in de lus haalt hoog weg' },
    { naam: 'Laag weg', knoop: 'laag', knop: 'freq', min: 20, max: 1000, standaard: 100, glijMs: 20,
      uitleg: 'Haalt laag uit de lus zodat de herhalingen niet dichtslibben' },
    { naam: 'Resonantie', knoop: 'toon', knop: 'q', min: 0.5, max: 4, standaard: 0.70703125, glijMs: 20,
      uitleg: 'Een piek op de toonfrequentie in de lus; hoger = fluitender herhalingen' },
    { naam: 'Verzadiging', knoop: 'tape', knop: 'verzadiging', min: 0, max: 24, standaard: 6, glijMs: 20,
      uitleg: 'Hoe vroeg de band dichtloopt: warmer, en het remt de zelfoscillatie' },
    { naam: 'Haperen', knoop: 'band', knop: 'diepte', min: 0, max: 1, standaard: 0.25, glijMs: 20,
      uitleg: 'Wow en flutter: hoe onrustig de band loopt' },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.375, glijMs: 20,
      uitleg: 'Hoeveel echo je hoort naast het origineel' },
  ],
  modulaties: [],
  // toetsgevallen voor de null-test (genormaliseerd; in deze volgorde: Tijd, Terug, Toon, Laag, Reso, Verz, Haper, Mix)
  toetsgevallen: [
    { naam: 'echo-standaard', signaal: 'stukjes', sec: 4, sr: 48000, norm: [0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25] },
    { naam: 'echo-heet', signaal: 'ruis-stukjes', sec: 8, sr: 48000, norm: [0.125, 0.75, 0.125, 0.0625, 0.25, 0.5, 0.5, 0.75] },
    { naam: 'zelfoscillatie', signaal: 'stukjes-kort', sec: 8, sr: 48000, norm: [0.0625, 0.875, 0.25, 0.0625, 0.125, 0.25, 0.25, 1], statistisch: true },
    { naam: 'sweep-441', signaal: 'sweep', sec: 5, sr: 44100, norm: [0.5, 0.5, 0.5, 0.125, 0.125, 0.75, 0.75, 0.5] },
  ],
};

export const SIDECHAIN_POMP = {
  naam: 'Pomp (zonder sidechain)', id: 'pomp',
  uitleg: 'Een LFO laat het volume pompen op een vast tempo. Echte sidechain (een tweede ingang) staat in de bibliotheek voor later.',
  knopen: [
    { id: 'in', type: 'in', x: 40, y: 200 }, { id: 'lfo', type: 'lfo', x: 120, y: 60, waarden: { snelheid: 2 } },
    { id: 'vol', type: 'versterker', x: 240, y: 200, waarden: { hoeveel: 0.7 } }, { id: 'uit', type: 'uit', x: 400, y: 200 }],
  kabels: [{ van: ['in', 'uit'], naar: ['vol', 'in'] }, { van: ['vol', 'uit'], naar: ['uit', 'in'] }],
  parameters: [{ naam: 'Diepte', knoop: 'vol', knop: 'hoeveel', min: 0, max: 1, standaard: 0.75, glijMs: 20 },
    { naam: 'Snelheid', knoop: 'lfo', knop: 'snelheid', min: 0.125, max: 16, standaard: 2, glijMs: 20 }],
  modulaties: [{ bron: ['lfo', 'golf'], doel: ['vol', 'hoeveel'], diepte: 0.25 }],
};

export const VEERGALM_SCHETS = {
  naam: 'Veergalm (schets)', id: 'veer',
  uitleg: 'Een grove schets: korte vertraging met veel terugkoppeling en een banddoorlaat geeft het boing van een veer. Een echte veer (dispersie) staat in de bibliotheek voor later.',
  knopen: [
    { id: 'in', type: 'in', x: 40, y: 200 }, { id: 'som', type: 'som', x: 160, y: 200 },
    { id: 'lijn', type: 'vertraging', x: 260, y: 200, waarden: { tijd: 37 }, vast: { max: 200 } },
    { id: 'kleur', type: 'svf', x: 390, y: 200, waarden: { freq: 1800, q: 2 } },
    { id: 'terug', type: 'versterker', x: 280, y: 360, waarden: { hoeveel: 0.8 } },
    { id: 'mix', type: 'mix', x: 580, y: 200, waarden: { mix: 0.3 } }, { id: 'uit', type: 'uit', x: 700, y: 200 }],
  kabels: [
    { van: ['in', 'uit'], naar: ['som', 'a'] }, { van: ['terug', 'uit'], naar: ['som', 'b'] },
    { van: ['som', 'uit'], naar: ['lijn', 'in'] }, { van: ['lijn', 'uit'], naar: ['kleur', 'in'] },
    { van: ['kleur', 'band'], naar: ['terug', 'in'] }, { van: ['in', 'uit'], naar: ['mix', 'droog'] },
    { van: ['kleur', 'band'], naar: ['mix', 'nat'] }, { van: ['mix', 'uit'], naar: ['uit', 'in'] }],
  parameters: [{ naam: 'Galm', knoop: 'terug', knop: 'hoeveel', min: 0, max: 0.9375, standaard: 0.8125, glijMs: 30 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.3125, glijMs: 20 }],
  modulaties: [],
};

export const ARCHETYPEN = [DUB_ECHO, VEERGALM_SCHETS, SIDECHAIN_POMP];
