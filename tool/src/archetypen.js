// archetypen.js — ontwerpen om mee te beginnen. De dub-echo is het ontwerp dat de hele keten doorloopt (PROMPT.md);
// de andere twee laten zien dat de catalogus meer kan, en zijn (nog) niet tot plugin gebouwd.
// Namen naar wat ze doen, niet naar een merk (design.md).

export const DUB_ECHO = {
  naam: 'Dub echo', id: 'dub_echo', voorbeeld: 'effect',
  uitleg: 'Tape echo voor dub: saturation en een filter in de feedbacklus. Boven feedback 1 gaat hij vanzelf door, en de tape houdt hem in toom.',
  knopen: [
    { id: 'in', type: 'in', x: 20, y: 200 },
    { id: 'band', type: 'bandsnelheid', x: 360, y: 60, waarden: { diepte: 0.25 } },
    { id: 'som', type: 'som', x: 130, y: 200 },
    { id: 'tape', type: 'tape', x: 240, y: 200, waarden: { verzadiging: 6, asymmetrie: 0.099609375 } },
    { id: 'lijn', type: 'vertraging', x: 360, y: 200, waarden: { tijd: 375 }, vast: { max: 1200 } },
    { id: 'sis', type: 'bandruis', x: 420, y: 60, waarden: { niveau: 0 } },
    { id: 'opband', type: 'som', x: 420, y: 200 },
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
    { van: ['band', 'snelheid'], naar: ['lijn', 'beweging'] },
    { van: ['lijn', 'uit'], naar: ['opband', 'a'] },
    { van: ['sis', 'uit'], naar: ['opband', 'b'] },
    { van: ['opband', 'uit'], naar: ['laag', 'in'] },
    { van: ['laag', 'uit'], naar: ['toon', 'in'] },
    { van: ['toon', 'laag'], naar: ['terug', 'in'] },
    { van: ['in', 'uit'], naar: ['mix', 'droog'] },
    { van: ['toon', 'laag'], naar: ['mix', 'nat'] },
    { van: ['mix', 'uit'], naar: ['uit', 'in'] },
  ],
  // in deze volgorde ook in de plugin (en dezelfde grenzen als dsp/ontwerpen/dub_echo.h)
  parameters: [
    { naam: 'Time', knoop: 'lijn', knop: 'tijd', min: 20, max: 1200, standaard: 375, glijMs: 250, glijden: 'glijden',
      uitleg: 'Hoe lang tot de echo komt. Draaien laat de toon glijden, zoals een band die sneller gaat' },
    { naam: 'Feedback', kort: 'Fdbk', knoop: 'terug', knop: 'hoeveel', min: 0, max: 1.25, standaard: 0.5, glijMs: 30,
      uitleg: 'Hoeveel van de echo weer de band op gaat. Boven 1 gaat hij vanzelf door (zelfoscillatie)' },
    { naam: 'Tone', knoop: 'toon', knop: 'freq', min: 300, max: 12000, standaard: 3000, glijMs: 20,
      uitleg: 'Hoe donker elke herhaling wordt: het filter in de lus haalt hoog weg' },
    { naam: 'Low cut', knoop: 'laag', knop: 'freq', min: 20, max: 1000, standaard: 100, glijMs: 20,
      uitleg: 'Haalt laag uit de lus zodat de herhalingen niet dichtslibben' },
    { naam: 'Resonance', knoop: 'toon', knop: 'q', min: 0.5, max: 4, standaard: 0.70703125, glijMs: 20,
      uitleg: 'Een piek op de toonfrequentie in de lus; hoger = fluitender herhalingen' },
    { naam: 'Saturation', knoop: 'tape', knop: 'verzadiging', min: 0, max: 24, standaard: 6, glijMs: 20,
      uitleg: 'Hoe vroeg de band dichtloopt: warmer, en het remt de zelfoscillatie' },
    { naam: 'Flutter', knoop: 'band', knop: 'diepte', min: 0, max: 1, standaard: 0.25, glijMs: 20,
      uitleg: 'Wow en flutter: hoe onrustig de band loopt. 0,25 is een goede bandrecorder, 1 een versleten band' },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.375, glijMs: 20,
      uitleg: 'Hoeveel echo je hoort naast het origineel' },
    { naam: 'Hiss', knoop: 'sis', knop: 'niveau', min: 0, max: 1, standaard: 0, glijMs: 20,
      uitleg: 'De sis van de band in de echo; bij 1 zoals een echte bandrecorder, en hij stapelt in de herhalingen' },
  ],
  modulaties: [],
  // fabriekspresets (fase 4): waarden per parameternaam, elk exact in float32 en binnen het bereik
  presets: [
    { naam: 'Default', uitleg: 'De beginstand van de plugin.',
      waarden: { Time: 375, Feedback: 0.5, Tone: 3000, 'Low cut': 100, Resonance: 0.70703125, Saturation: 6, Flutter: 0.25, Mix: 0.375, Hiss: 0 } },
    { naam: 'Slapback', uitleg: 'Eén korte, heldere tik achter het origineel.',
      waarden: { Time: 110, Feedback: 0.1875, Tone: 6000, 'Low cut': 150, Resonance: 0.70703125, Saturation: 4, Flutter: 0.125, Mix: 0.3125, Hiss: 0 } },
    { naam: 'Long dub', uitleg: 'Lange, donker wordende herhalingen.',
      waarden: { Time: 500, Feedback: 0.75, Tone: 2200, 'Low cut': 250, Resonance: 1, Saturation: 9, Flutter: 0.375, Mix: 0.4375, Hiss: 0.25 } },
    { naam: 'Worn tape', uitleg: 'Onrustige band, vroeg verzadigd, dof.',
      waarden: { Time: 375, Feedback: 0.625, Tone: 1500, 'Low cut': 300, Resonance: 1.5, Saturation: 14, Flutter: 0.8125, Mix: 0.375, Hiss: 1 } },
    { naam: 'Self-oscillation', uitleg: 'Boven 1: gaat vanzelf door en blijft begrensd door de band.',
      waarden: { Time: 250, Feedback: 1.125, Tone: 2500, 'Low cut': 200, Resonance: 1.25, Saturation: 12, Flutter: 0.3125, Mix: 0.5, Hiss: 0 } },
  ],
  // toetsgevallen voor de null-test (genormaliseerd; in deze volgorde: Tijd, Terug, Toon, Laag, Reso, Verz, Haper, Mix, Ruis)
  toetsgevallen: [
    { naam: 'echo-standaard', signaal: 'stukjes', sec: 4, sr: 48000, norm: [0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25] },
    { naam: 'echo-heet', signaal: 'ruis-stukjes', sec: 8, sr: 48000, norm: [0.125, 0.75, 0.125, 0.0625, 0.25, 0.5, 0.5, 0.75, 0.5] },
    { naam: 'zelfoscillatie', signaal: 'stukjes-kort', sec: 8, sr: 48000, norm: [0.0625, 0.875, 0.25, 0.0625, 0.125, 0.25, 0.25, 1, 0], statistisch: true },
    { naam: 'sweep-441', signaal: 'sweep', sec: 5, sr: 44100, norm: [0.5, 0.5, 0.5, 0.125, 0.125, 0.75, 0.75, 0.5, 0] },
  ],
};

export const SIDECHAIN_POMP = {
  naam: 'LFO pump', id: 'pomp', voorbeeld: 'effect',
  uitleg: 'Een LFO laat het volume pompen op een vast tempo. Echte sidechain: zie Sidechain pump.',
  knopen: [
    { id: 'in', type: 'in', x: 40, y: 200 }, { id: 'lfo', type: 'lfo', x: 120, y: 60, waarden: { snelheid: 2 } },
    { id: 'vol', type: 'versterker', x: 240, y: 200, waarden: { hoeveel: 0.7 } }, { id: 'uit', type: 'uit', x: 400, y: 200 }],
  kabels: [{ van: ['in', 'uit'], naar: ['vol', 'in'] }, { van: ['vol', 'uit'], naar: ['uit', 'in'] }],
  parameters: [{ naam: 'Depth', knoop: 'vol', knop: 'hoeveel', min: 0, max: 1, standaard: 0.75, glijMs: 20 },
    { naam: 'Rate', knoop: 'lfo', knop: 'snelheid', min: 0.125, max: 16, standaard: 2, glijMs: 20 }],
  modulaties: [{ bron: ['lfo', 'golf'], doel: ['vol', 'hoeveel'], diepte: 0.25 }],
};

export const VEERGALM_SCHETS = {
  naam: 'Spring reverb (sketch)', id: 'veer', voorbeeld: 'effect',
  uitleg: 'Een grove schets: korte vertraging met veel terugkoppeling en een banddoorlaat geeft het boing van een veer. Een echte veer (dispersie) staat in de bibliotheek voor later. (15 okt: de banddoorlaat is nu een biquad met 0 dB piek; de SVF-band versterkte Q keer en liet de lus ontploffen.)',
  knopen: [
    { id: 'in', type: 'in', x: 40, y: 200 }, { id: 'som', type: 'som', x: 160, y: 200 },
    { id: 'lijn', type: 'vertraging', x: 260, y: 200, waarden: { tijd: 37 }, vast: { max: 200 } },
    { id: 'kleur', type: 'biquad', x: 390, y: 200, waarden: { freq: 1800, q: 2 }, vast: { soort: 2 } },
    { id: 'terug', type: 'versterker', x: 280, y: 360, waarden: { hoeveel: 0.8 } },
    { id: 'mix', type: 'mix', x: 580, y: 200, waarden: { mix: 0.3 } }, { id: 'uit', type: 'uit', x: 700, y: 200 }],
  kabels: [
    { van: ['in', 'uit'], naar: ['som', 'a'] }, { van: ['terug', 'uit'], naar: ['som', 'b'] },
    { van: ['som', 'uit'], naar: ['lijn', 'in'] }, { van: ['lijn', 'uit'], naar: ['kleur', 'in'] },
    { van: ['kleur', 'uit'], naar: ['terug', 'in'] }, { van: ['in', 'uit'], naar: ['mix', 'droog'] },
    { van: ['kleur', 'uit'], naar: ['mix', 'nat'] }, { van: ['mix', 'uit'], naar: ['uit', 'in'] }],
  parameters: [{ naam: 'Decay', knoop: 'terug', knop: 'hoeveel', min: 0, max: 0.9375, standaard: 0.8125, glijMs: 30 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.3125, glijMs: 20 }],
  modulaties: [],
};

// ---- voorbeelden met de blokken van 15 okt 2026 (dsp/pb_bronnen.h) ------------------------------------------------
// Instrumenten maken zelf geluid (geen ingang nodig); effecten bewerken wat je erin stopt (bij Horen: de groove of
// eigen audio). tests/bronnen.test.mjs rendert ze allemaal: niet stil, geen NaN, niet ontploft, blokgrootte-onafhankelijk.
const kabel = (van, poort, naar, ingang) => ({ van: [van, poort], naar: [naar, ingang] });

export const DUBSIRENE = {
  naam: 'Dub siren', id: 'dubsirene', voorbeeld: 'instrument',
  uitleg: 'De sirene van een soundsystem: een blokgolf die een LFO laat jammeren, door een tape-echo met terugkoppeling.',
  knopen: [
    { id: 'lfo', type: 'lfo', x: 20, y: 40, waarden: { snelheid: 3 } },
    { id: 'osc', type: 'osc', x: 20, y: 200, waarden: { freq: 520, vorm: 0.5 }, vast: { golf: 2 } },
    { id: 'vol', type: 'versterker', x: 180, y: 200, waarden: { hoeveel: 0.3 } },
    { id: 'som', type: 'som', x: 300, y: 200 },
    { id: 'tape', type: 'tape', x: 400, y: 200, waarden: { verzadiging: 6 } },
    { id: 'lijn', type: 'vertraging', x: 510, y: 200, waarden: { tijd: 333 }, vast: { max: 1200 } },
    { id: 'toon', type: 'svf', x: 630, y: 200, waarden: { freq: 2500, q: 0.70703125 } },
    { id: 'terug', type: 'versterker', x: 520, y: 380, waarden: { hoeveel: 0.6 } },
    { id: 'mix', type: 'mix', x: 800, y: 200, waarden: { mix: 0.4 } },
    { id: 'uit', type: 'uit', x: 920, y: 200 },
  ],
  kabels: [kabel('osc', 'uit', 'vol', 'in'), kabel('vol', 'uit', 'som', 'a'), kabel('terug', 'uit', 'som', 'b'), kabel('som', 'uit', 'tape', 'in'),
    kabel('tape', 'uit', 'lijn', 'in'), kabel('lijn', 'uit', 'toon', 'in'), kabel('toon', 'laag', 'terug', 'in'),
    kabel('vol', 'uit', 'mix', 'droog'), kabel('toon', 'laag', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Pitch', knoop: 'osc', knop: 'freq', min: 100, max: 2000, standaard: 520, glijMs: 30 },
    { naam: 'Wail rate', knoop: 'lfo', knop: 'snelheid', min: 0.125, max: 16, standaard: 3, glijMs: 20 },
    { naam: 'Echo time', knoop: 'lijn', knop: 'tijd', min: 20, max: 1200, standaard: 333, glijMs: 250 },
    { naam: 'Feedback', knoop: 'terug', knop: 'hoeveel', min: 0, max: 1.125, standaard: 0.625, glijMs: 30 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.375, glijMs: 20 },
  ],
  modulaties: [{ bron: ['lfo', 'golf'], doel: ['osc', 'freq'], diepte: 0.0625 }],
};

export const DUBBAS = {
  naam: 'Dub bass', id: 'dubbas', voorbeeld: 'instrument',
  uitleg: 'Een diepe roots-baslijn: sinus plus driehoek een octaaf hoger, een omhulling per noot en een laagdoorlaat. De sequencer speelt het patroon.',
  knopen: [
    { id: 'seq', type: 'sequencer', x: 20, y: 40, waarden: { tempo: 76, lengte: 0.75 }, vast: { patroon: 4 } },
    { id: 'osc', type: 'osc', x: 200, y: 160, waarden: { freq: 55 }, vast: { golf: 0 } },
    { id: 'osc2', type: 'osc', x: 200, y: 330, waarden: { freq: 110 }, vast: { golf: 3 } },
    { id: 'zacht', type: 'versterker', x: 370, y: 330, waarden: { hoeveel: 0.25 } },
    { id: 'som', type: 'som', x: 380, y: 160 },
    { id: 'omh', type: 'omhulling', x: 200, y: 520, waarden: { aanzet: 4, verval: 600, aanhoud: 0.75, loslaten: 120 } },
    { id: 'vca', type: 'vermenigvuldig', x: 500, y: 160 },
    { id: 'filter', type: 'svf', x: 620, y: 160, waarden: { freq: 500, q: 0.8 } },
    { id: 'vol', type: 'versterker', x: 790, y: 160, waarden: { hoeveel: 0.6 } },
    { id: 'uit', type: 'uit', x: 920, y: 160 },
  ],
  kabels: [kabel('seq', 'toon', 'osc', 'toon'), kabel('seq', 'toon', 'osc2', 'toon'), kabel('seq', 'poort', 'omh', 'poort'),
    kabel('osc', 'uit', 'som', 'a'), kabel('osc2', 'uit', 'zacht', 'in'), kabel('zacht', 'uit', 'som', 'b'),
    kabel('som', 'uit', 'vca', 'a'), kabel('omh', 'omhulling', 'vca', 'b'), kabel('vca', 'uit', 'filter', 'in'),
    kabel('filter', 'laag', 'vol', 'in'), kabel('vol', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Tempo', knoop: 'seq', knop: 'tempo', min: 60, max: 180, standaard: 76, glijMs: 20 },
    { naam: 'Root', knoop: 'osc', knop: 'freq', min: 30, max: 120, standaard: 55, glijMs: 20, ook: [['osc2', 'freq', 2]] },
    { naam: 'Gate length', knoop: 'seq', knop: 'lengte', min: 0.125, max: 1, standaard: 0.75, glijMs: 20 },
    { naam: 'Filter', knoop: 'filter', knop: 'freq', min: 80, max: 4000, standaard: 500, glijMs: 20 },
  ],
  modulaties: [],
};

export const ACIDLIJN = {
  naam: 'Acid line', id: 'acidlijn', voorbeeld: 'instrument',
  uitleg: 'Zaagtand door een resonerend filter dat per noot opengaat (de omhulling beweegt de afsnijding), en dan de band in.',
  knopen: [
    { id: 'seq', type: 'sequencer', x: 20, y: 40, waarden: { tempo: 128, lengte: 0.5 }, vast: { patroon: 5 } },
    { id: 'osc', type: 'osc', x: 200, y: 180, waarden: { freq: 55 }, vast: { golf: 1 } },
    { id: 'omh', type: 'omhulling', x: 200, y: 360, waarden: { aanzet: 1, verval: 180, aanhoud: 0.125, loslaten: 60 } },
    { id: 'vca', type: 'vermenigvuldig', x: 370, y: 180 },
    { id: 'filter', type: 'svf', x: 490, y: 180, waarden: { freq: 300, q: 6 } },
    { id: 'tape', type: 'tape', x: 660, y: 180, waarden: { verzadiging: 9 } },
    { id: 'vol', type: 'versterker', x: 780, y: 180, waarden: { hoeveel: 1 } },
    { id: 'uit', type: 'uit', x: 920, y: 180 },
  ],
  kabels: [kabel('seq', 'toon', 'osc', 'toon'), kabel('seq', 'poort', 'omh', 'poort'), kabel('osc', 'uit', 'vca', 'a'),
    kabel('omh', 'omhulling', 'vca', 'b'), kabel('vca', 'uit', 'filter', 'in'), kabel('filter', 'laag', 'tape', 'in'),
    kabel('tape', 'uit', 'vol', 'in'), kabel('vol', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Cutoff', knoop: 'filter', knop: 'freq', min: 60, max: 4000, standaard: 300, glijMs: 20 },
    { naam: 'Resonance', knoop: 'filter', knop: 'q', min: 0.5, max: 8, standaard: 6, glijMs: 20 },
    { naam: 'Tempo', knoop: 'seq', knop: 'tempo', min: 80, max: 180, standaard: 128, glijMs: 20 },
    { naam: 'Drive', knoop: 'tape', knop: 'verzadiging', min: 0, max: 24, standaard: 9, glijMs: 20 },
  ],
  modulaties: [{ bron: ['omh', 'omhulling'], doel: ['filter', 'freq'], diepte: 0.5 }],
};

export const RITMEBOX = {
  naam: 'Drum box', id: 'ritmebox', voorbeeld: 'instrument',
  uitleg: 'Drie trommels op drie patronen: kick op elke tel, snare op 2 en 4, hihat op de tegentijd. Eén tempoknop stuurt alle drie.',
  knopen: [
    { id: 'seqK', type: 'sequencer', x: 20, y: 40, waarden: { tempo: 120 }, vast: { patroon: 0 } },
    { id: 'seqS', type: 'sequencer', x: 20, y: 220, waarden: { tempo: 120 }, vast: { patroon: 1 } },
    { id: 'seqH', type: 'sequencer', x: 20, y: 400, waarden: { tempo: 120, lengte: 0.25 }, vast: { patroon: 2 } },
    { id: 'kick', type: 'trommel', x: 220, y: 40, waarden: { freq: 50, val: 3, verval: 380, ruis: 0 } },
    { id: 'snare', type: 'trommel', x: 220, y: 220, waarden: { freq: 190, val: 1, verval: 180, ruis: 0.75 } },
    { id: 'hat', type: 'trommel', x: 220, y: 400, waarden: { freq: 900, val: 0, verval: 45, ruis: 1 } },
    { id: 's1', type: 'som', x: 420, y: 120 },
    { id: 'hatvol', type: 'versterker', x: 420, y: 400, waarden: { hoeveel: 0.4 } },
    { id: 's2', type: 'som', x: 540, y: 200 },
    { id: 'vol', type: 'versterker', x: 660, y: 200, waarden: { hoeveel: 0.4 } },
    { id: 'uit', type: 'uit', x: 820, y: 200 },
  ],
  kabels: [kabel('seqK', 'poort', 'kick', 'poort'), kabel('seqS', 'poort', 'snare', 'poort'), kabel('seqH', 'poort', 'hat', 'poort'),
    kabel('kick', 'uit', 's1', 'a'), kabel('snare', 'uit', 's1', 'b'), kabel('hat', 'uit', 'hatvol', 'in'),
    kabel('s1', 'uit', 's2', 'a'), kabel('hatvol', 'uit', 's2', 'b'), kabel('s2', 'uit', 'vol', 'in'), kabel('vol', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Tempo', knoop: 'seqK', knop: 'tempo', min: 60, max: 180, standaard: 120, glijMs: 20, ook: [['seqS', 'tempo'], ['seqH', 'tempo']] },
    { naam: 'Kick pitch', knoop: 'kick', knop: 'freq', min: 30, max: 120, standaard: 50, glijMs: 20 },
    { naam: 'Snare noise', knoop: 'snare', knop: 'ruis', min: 0, max: 1, standaard: 0.75, glijMs: 20 },
    { naam: 'Hi-hat decay', knoop: 'hat', knop: 'verval', min: 10, max: 400, standaard: 45, glijMs: 20 },
  ],
  modulaties: [],
};

export const SNARENREGEN = {
  naam: 'String rain', id: 'snarenregen', voorbeeld: 'instrument',
  uitleg: 'Getokkelde snaren (Karplus–Strong) op een arpeggio, in een grote galm.',
  knopen: [
    { id: 'seq', type: 'sequencer', x: 20, y: 40, waarden: { tempo: 100, lengte: 0.3 }, vast: { patroon: 6 } },
    { id: 'snaar', type: 'snaar', x: 200, y: 160, waarden: { freq: 147, verval: 3, helder: 0.6 } },
    { id: 'galm', type: 'galm', x: 400, y: 300, waarden: { tijd: 4, demping: 5000, grootte: 1.25 } },
    { id: 'mix', type: 'mix', x: 620, y: 160, waarden: { mix: 0.35 } },
    { id: 'vol', type: 'versterker', x: 740, y: 160, waarden: { hoeveel: 0.6 } },
    { id: 'uit', type: 'uit', x: 880, y: 160 },
  ],
  kabels: [kabel('seq', 'poort', 'snaar', 'poort'), kabel('seq', 'toon', 'snaar', 'toon'), kabel('snaar', 'uit', 'galm', 'in'),
    kabel('snaar', 'uit', 'mix', 'droog'), kabel('galm', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'vol', 'in'), kabel('vol', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Pitch', knoop: 'snaar', knop: 'freq', min: 55, max: 880, standaard: 147, glijMs: 20 },
    { naam: 'Decay', knoop: 'snaar', knop: 'verval', min: 0.125, max: 12, standaard: 3, glijMs: 20 },
    { naam: 'Brightness', knoop: 'snaar', knop: 'helder', min: 0, max: 1, standaard: 0.625, glijMs: 20 },
    { naam: 'Reverb', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.375, glijMs: 20 },
  ],
  modulaties: [],
};

export const DRONEWOLK = {
  naam: 'Drone cloud', id: 'dronewolk', voorbeeld: 'instrument',
  uitleg: 'Drie zaagtanden (grondtoon, iets ontstemd, kwint) door een filter dat heel langzaam ademt, in een lange galm. Gaat vanzelf door.',
  knopen: [
    { id: 'o1', type: 'osc', x: 20, y: 40, waarden: { freq: 55 }, vast: { golf: 1 } },
    { id: 'o2', type: 'osc', x: 20, y: 200, waarden: { freq: 55, fijn: 9 }, vast: { golf: 1 } },
    { id: 'o3', type: 'osc', x: 20, y: 360, waarden: { freq: 82.5 }, vast: { golf: 1 } },
    { id: 's1', type: 'som', x: 200, y: 120 },
    { id: 's2', type: 'som', x: 310, y: 220 },
    { id: 'adem', type: 'lfo', x: 430, y: 40, waarden: { snelheid: 0.0625 } },
    { id: 'filter', type: 'svf', x: 430, y: 220, waarden: { freq: 700, q: 1.5 } },
    { id: 'zacht', type: 'versterker', x: 600, y: 220, waarden: { hoeveel: 0.2 } },
    { id: 'galm', type: 'galm', x: 600, y: 400, waarden: { tijd: 9, demping: 3000, grootte: 1.75 } },
    { id: 'mix', type: 'mix', x: 780, y: 220, waarden: { mix: 0.5 } },
    { id: 'uit', type: 'uit', x: 910, y: 220 },
  ],
  kabels: [kabel('o1', 'uit', 's1', 'a'), kabel('o2', 'uit', 's1', 'b'), kabel('s1', 'uit', 's2', 'a'), kabel('o3', 'uit', 's2', 'b'),
    kabel('s2', 'uit', 'filter', 'in'), kabel('filter', 'laag', 'zacht', 'in'), kabel('zacht', 'uit', 'galm', 'in'),
    kabel('zacht', 'uit', 'mix', 'droog'), kabel('galm', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Root', knoop: 'o1', knop: 'freq', min: 27.5, max: 220, standaard: 55, glijMs: 200, ook: [['o2', 'freq'], ['o3', 'freq', 1.5]] },
    { naam: 'Brightness', knoop: 'filter', knop: 'freq', min: 100, max: 6000, standaard: 700, glijMs: 50 },
    { naam: 'Breath', knoop: 'adem', knop: 'snelheid', min: 0.015625, max: 2, standaard: 0.0625, glijMs: 20 },
    { naam: 'Reverb', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.5, glijMs: 20 },
  ],
  modulaties: [{ bron: ['adem', 'golf'], doel: ['filter', 'freq'], diepte: 0.125 }],
};

export const RUISZEE = {
  naam: 'Noise sea', id: 'ruiszee', voorbeeld: 'instrument',
  uitleg: 'Donkere ruis door een banddoorlaat die golft, een volume dat aanzwelt en een plek die langzaam van links naar rechts gaat.',
  knopen: [
    { id: 'ruis', type: 'ruis', x: 20, y: 200, waarden: { kleur: 0.5 } },
    { id: 'golf', type: 'lfo', x: 200, y: 20, waarden: { snelheid: 0.11 } },
    { id: 'vlaag', type: 'willekeur', x: 380, y: 20, waarden: { snelheid: 0.4, glad: 1 } },
    { id: 'band', type: 'svf', x: 200, y: 200, waarden: { freq: 700, q: 1.2 } },
    { id: 'vol', type: 'versterker', x: 380, y: 200, waarden: { hoeveel: 0.5 } },
    { id: 'zwerf', type: 'lfo', x: 560, y: 20, waarden: { snelheid: 0.05 } },
    { id: 'plek', type: 'panner', x: 560, y: 200, waarden: { positie: 0 } },
    { id: 'galm', type: 'galm', x: 560, y: 380, waarden: { tijd: 3, demping: 4000, grootte: 1.5 } },
    { id: 'mix', type: 'mix', x: 740, y: 200, waarden: { mix: 0.3 } },
    { id: 'uit', type: 'uit', x: 880, y: 200 },
  ],
  kabels: [kabel('ruis', 'uit', 'band', 'in'), kabel('band', 'band', 'vol', 'in'), kabel('vol', 'uit', 'plek', 'in'),
    kabel('plek', 'uit', 'galm', 'in'), kabel('plek', 'uit', 'mix', 'droog'), kabel('galm', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Color', knoop: 'band', knop: 'freq', min: 150, max: 4000, standaard: 700, glijMs: 50 },
    { naam: 'Waves', knoop: 'golf', knop: 'snelheid', min: 0.03125, max: 2, standaard: 0.109375, glijMs: 20 },
    { naam: 'Volume', knoop: 'vol', knop: 'hoeveel', min: 0, max: 1, standaard: 0.5, glijMs: 30 },
  ],
  modulaties: [{ bron: ['golf', 'golf'], doel: ['band', 'freq'], diepte: 0.125 }, { bron: ['vlaag', 'golf'], doel: ['vol', 'hoeveel'], diepte: 0.375 },
    { bron: ['zwerf', 'golf'], doel: ['plek', 'positie'], diepte: 0.375 }],
};

export const GLITCHMACHINE = {
  naam: 'Glitch machine', id: 'glitchmachine', voorbeeld: 'instrument',
  uitleg: 'Toevalsnoten in een blokgolf, vergruisd tot een paar bits, met een samplerate die willekeurig verspringt en een korte echo.',
  knopen: [
    { id: 'seq', type: 'sequencer', x: 20, y: 40, waarden: { tempo: 150, lengte: 0.35 }, vast: { patroon: 11, zaad: 7 } },
    { id: 'osc', type: 'osc', x: 200, y: 160, waarden: { freq: 220, vorm: 0.25 }, vast: { golf: 2 } },
    { id: 'omh', type: 'omhulling', x: 200, y: 360, waarden: { aanzet: 1, verval: 90, aanhoud: 0.25, loslaten: 40 } },
    { id: 'vca', type: 'vermenigvuldig', x: 370, y: 160 },
    { id: 'spring', type: 'willekeur', x: 370, y: 20, waarden: { snelheid: 3, glad: 0 } },
    { id: 'gruis', type: 'vergruizer', x: 500, y: 160, waarden: { bits: 4, deling: 8 } },
    { id: 'som', type: 'som', x: 640, y: 160 },
    { id: 'lijn', type: 'vertraging', x: 750, y: 160, waarden: { tijd: 90 }, vast: { max: 400 } },
    { id: 'terug', type: 'versterker', x: 760, y: 330, waarden: { hoeveel: 0.55 } },
    { id: 'vol', type: 'versterker', x: 900, y: 160, waarden: { hoeveel: 0.4 } },
    { id: 'uit', type: 'uit', x: 1030, y: 160 },
  ],
  kabels: [kabel('seq', 'toon', 'osc', 'toon'), kabel('seq', 'poort', 'omh', 'poort'), kabel('osc', 'uit', 'vca', 'a'),
    kabel('omh', 'omhulling', 'vca', 'b'), kabel('vca', 'uit', 'gruis', 'in'), kabel('gruis', 'uit', 'som', 'a'),
    kabel('terug', 'uit', 'som', 'b'), kabel('som', 'uit', 'lijn', 'in'), kabel('lijn', 'uit', 'terug', 'in'),
    kabel('som', 'uit', 'vol', 'in'), kabel('vol', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Bits', knoop: 'gruis', knop: 'bits', min: 1, max: 16, standaard: 4, glijMs: 20 },
    { naam: 'Tempo', knoop: 'seq', knop: 'tempo', min: 60, max: 240, standaard: 150, glijMs: 20 },
    { naam: 'Echo', knoop: 'terug', knop: 'hoeveel', min: 0, max: 0.875, standaard: 0.5, glijMs: 30 },
  ],
  modulaties: [{ bron: ['spring', 'golf'], doel: ['gruis', 'deling'], diepte: 0.125 }],
};

export const FMBEL = {
  naam: 'FM bell', id: 'fmbel', voorbeeld: 'instrument',
  uitleg: 'Frequentiemodulatie: een tweede sinus (1,4× de toon) laat de eerste trillen. Een snelle omhulling op de modulator geeft de klank van een bel.',
  knopen: [
    { id: 'seq', type: 'sequencer', x: 20, y: 40, waarden: { tempo: 84, lengte: 0.25 }, vast: { patroon: 6 } },
    { id: 'mod', type: 'osc', x: 200, y: 20, waarden: { freq: 308 }, vast: { golf: 0 } },
    { id: 'omhM', type: 'omhulling', x: 200, y: 200, waarden: { aanzet: 1, verval: 900, aanhoud: 0, loslaten: 400 } },
    { id: 'vcaM', type: 'vermenigvuldig', x: 380, y: 60 },
    { id: 'drager', type: 'osc', x: 500, y: 160, waarden: { freq: 220, fm: 3 }, vast: { golf: 0 } },
    { id: 'omh', type: 'omhulling', x: 380, y: 360, waarden: { aanzet: 2, verval: 2500, aanhoud: 0, loslaten: 1500 } },
    { id: 'vca', type: 'vermenigvuldig', x: 660, y: 160 },
    { id: 'galm', type: 'galm', x: 660, y: 340, waarden: { tijd: 3.5, demping: 7000, grootte: 1 } },
    { id: 'mix', type: 'mix', x: 800, y: 160, waarden: { mix: 0.3 } },
    { id: 'vol', type: 'versterker', x: 920, y: 160, waarden: { hoeveel: 0.375 } },
    { id: 'uit', type: 'uit', x: 1050, y: 160 },
  ],
  kabels: [kabel('seq', 'toon', 'mod', 'toon'), kabel('seq', 'toon', 'drager', 'toon'), kabel('seq', 'poort', 'omhM', 'poort'),
    kabel('seq', 'poort', 'omh', 'poort'), kabel('mod', 'uit', 'vcaM', 'a'), kabel('omhM', 'omhulling', 'vcaM', 'b'),
    kabel('vcaM', 'uit', 'drager', 'fm'), kabel('drager', 'uit', 'vca', 'a'), kabel('omh', 'omhulling', 'vca', 'b'),
    kabel('vca', 'uit', 'galm', 'in'), kabel('vca', 'uit', 'mix', 'droog'), kabel('galm', 'uit', 'mix', 'nat'),
    kabel('mix', 'uit', 'vol', 'in'), kabel('vol', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Pitch', knoop: 'drager', knop: 'freq', min: 55, max: 880, standaard: 220, glijMs: 20, ook: [['mod', 'freq', 1.4]] },
    { naam: 'Brightness', knoop: 'drager', knop: 'fm', min: 0, max: 8, standaard: 3, glijMs: 20 },
    { naam: 'Reverb', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.3125, glijMs: 20 },
  ],
  modulaties: [],
};

export const TOETSENPAD = {
  naam: 'Keys pad', id: 'toetsenpad', voorbeeld: 'instrument',
  uitleg: 'Zelf spelen: twee zaagtanden een octaaf uit elkaar, een filter dat per noot opent, chorus en galm. Speel met a s d f g h j k l ; \' of met de knoppen onder Horen.',
  knopen: [
    { id: 'toetsen', type: 'toetsen', x: 20, y: 40 },
    { id: 'o1', type: 'osc', x: 200, y: 120, waarden: { freq: 220 }, vast: { golf: 1 } },
    { id: 'o2', type: 'osc', x: 200, y: 290, waarden: { freq: 110, fijn: 6 }, vast: { golf: 1 } },
    { id: 'som', type: 'som', x: 370, y: 200 },
    { id: 'omh', type: 'omhulling', x: 200, y: 460, waarden: { aanzet: 12, verval: 400, aanhoud: 0.625, loslaten: 450 } },
    { id: 'filter', type: 'svf', x: 480, y: 200, waarden: { freq: 900, q: 1.5 } },
    { id: 'vca', type: 'vermenigvuldig', x: 650, y: 200 },
    { id: 'chorus', type: 'chorus', x: 650, y: 380, waarden: { tijd: 12, diepte: 4, snelheid: 0.5, terug: 0 } },
    { id: 'mix', type: 'mix', x: 830, y: 200, waarden: { mix: 0.5 } },
    { id: 'galm', type: 'galm', x: 830, y: 380, waarden: { tijd: 2.5, demping: 6000, grootte: 1 } },
    { id: 'mix2', type: 'mix', x: 980, y: 200, waarden: { mix: 0.25 } },
    { id: 'vol', type: 'versterker', x: 1100, y: 200, waarden: { hoeveel: 0.35 } },
    { id: 'uit', type: 'uit', x: 1230, y: 200 },
  ],
  kabels: [kabel('toetsen', 'toon', 'o1', 'toon'), kabel('toetsen', 'toon', 'o2', 'toon'), kabel('toetsen', 'poort', 'omh', 'poort'),
    kabel('o1', 'uit', 'som', 'a'), kabel('o2', 'uit', 'som', 'b'), kabel('som', 'uit', 'filter', 'in'),
    kabel('filter', 'laag', 'vca', 'a'), kabel('omh', 'omhulling', 'vca', 'b'), kabel('vca', 'uit', 'chorus', 'in'),
    kabel('vca', 'uit', 'mix', 'droog'), kabel('chorus', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'galm', 'in'),
    kabel('mix', 'uit', 'mix2', 'droog'), kabel('galm', 'uit', 'mix2', 'nat'), kabel('mix2', 'uit', 'vol', 'in'), kabel('vol', 'uit', 'uit', 'in')],
  parameters: [
    { naam: 'Note', knoop: 'toetsen', knop: 'toon', min: 0.25, max: 4, standaard: 1, glijMs: 5 },
    { naam: 'Gate', knoop: 'toetsen', knop: 'poort', min: 0, max: 1, standaard: 0, glijMs: 0 },
    { naam: 'Filter', knoop: 'filter', knop: 'freq', min: 100, max: 8000, standaard: 900, glijMs: 20 },
    { naam: 'Release', knoop: 'omh', knop: 'loslaten', min: 10, max: 4000, standaard: 450, glijMs: 20 },
  ],
  modulaties: [{ bron: ['omh', 'omhulling'], doel: ['filter', 'freq'], diepte: 0.25 }],
  // fabriekspresets: alleen de klank, nooit Note en Gate (die spelen de toetsen; Gate 1 zou zonder toets klinken)
  presets: [
    { naam: 'Soft pad', uitleg: 'Donker en lang uitklinkend.', waarden: { Filter: 600, Release: 1500 } },
    { naam: 'Bright', uitleg: 'Open filter, kort loslaten.', waarden: { Filter: 3000, Release: 300 } },
    { naam: 'Long tail', uitleg: 'De galm krijgt de tijd.', waarden: { Filter: 1200, Release: 4000 } },
    { naam: 'Pluck', uitleg: 'Kort en scherp.', waarden: { Filter: 2000, Release: 60 } },
  ],
};

// ---- effecten (bewerken de ingang) ----
const effect = (naam, id, uitleg, midden, kabelsMidden, parameters, modulaties = [], extra = []) => ({
  naam, id, uitleg, voorbeeld: 'effect',
  knopen: [{ id: 'in', type: 'in', x: 20, y: 200 }, ...midden, ...extra, { id: 'uit', type: 'uit', x: 900, y: 200 }],
  kabels: kabelsMidden, parameters, modulaties,
});
export const GALMKAMER = effect('Reverb room', 'galmkamer', 'Een ruimte om je geluid heen: acht vertragingen die alles mengen. Van kleine kamer tot kathedraal.',
  [{ id: 'galm', type: 'galm', x: 300, y: 320, waarden: { tijd: 2.5, demping: 6000, grootte: 1 } }, { id: 'mix', type: 'mix', x: 640, y: 200, waarden: { mix: 0.3 } }],
  [kabel('in', 'uit', 'galm', 'in'), kabel('in', 'uit', 'mix', 'droog'), kabel('galm', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  [{ naam: 'Decay time', knoop: 'galm', knop: 'tijd', min: 0.25, max: 20, standaard: 2.5, glijMs: 50 },
    { naam: 'Damping', knoop: 'galm', knop: 'demping', min: 500, max: 16000, standaard: 6000, glijMs: 20 },
    { naam: 'Size', knoop: 'galm', knop: 'grootte', min: 0.5, max: 2, standaard: 1, glijMs: 100 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.3125, glijMs: 20 }]);
export const BREEDCHORUS = effect('Wide chorus', 'breedchorus', 'Zweven en breed maken: een korte vertraging die links en rechts verschillend wiebelt.',
  [{ id: 'ch', type: 'chorus', x: 300, y: 320, waarden: { tijd: 14, diepte: 4, snelheid: 0.375, terug: 0 } }, { id: 'mix', type: 'mix', x: 640, y: 200, waarden: { mix: 0.5 } }],
  [kabel('in', 'uit', 'ch', 'in'), kabel('in', 'uit', 'mix', 'droog'), kabel('ch', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  [{ naam: 'Rate', knoop: 'ch', knop: 'snelheid', min: 0.03125, max: 8, standaard: 0.375, glijMs: 20 },
    { naam: 'Depth', knoop: 'ch', knop: 'diepte', min: 0, max: 12, standaard: 4, glijMs: 50 },
    { naam: 'Feedback', knoop: 'ch', knop: 'terug', min: -0.875, max: 0.875, standaard: 0, glijMs: 20 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.5, glijMs: 20 }]);
export const STRAALJAGER = effect('Jet flanger', 'straaljager', 'Flanger: een heel korte vertraging met veel terugkoppeling die langzaam heen en weer gaat.',
  [{ id: 'ch', type: 'chorus', x: 300, y: 320, waarden: { tijd: 1.5, diepte: 5, snelheid: 0.15, terug: 0.75 } }, { id: 'mix', type: 'mix', x: 640, y: 200, waarden: { mix: 0.5 } }],
  [kabel('in', 'uit', 'ch', 'in'), kabel('in', 'uit', 'mix', 'droog'), kabel('ch', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  [{ naam: 'Rate', knoop: 'ch', knop: 'snelheid', min: 0.03125, max: 4, standaard: 0.15625, glijMs: 20 },
    { naam: 'Feedback', knoop: 'ch', knop: 'terug', min: -0.875, max: 0.875, standaard: 0.75, glijMs: 20 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.5, glijMs: 20 }]);
export const LOFIKRAKER = effect('Lo-fi crusher', 'lofikraker', 'Minder bits, lagere samplerate, het hoog eraf en een beetje ruis eronder: een kapotte sampler.',
  [{ id: 'gruis', type: 'vergruizer', x: 180, y: 200, waarden: { bits: 6, deling: 6 } }, { id: 'dof', type: 'laagdoor1', x: 330, y: 200, waarden: { freq: 4500 } },
    { id: 'ruis', type: 'ruis', x: 330, y: 380, waarden: { kleur: 0.25 } }, { id: 'stil', type: 'versterker', x: 480, y: 380, waarden: { hoeveel: 0.03 } },
    { id: 'som', type: 'som', x: 520, y: 200 }, { id: 'mix', type: 'mix', x: 680, y: 200, waarden: { mix: 0.8 } }],
  [kabel('in', 'uit', 'gruis', 'in'), kabel('gruis', 'uit', 'dof', 'in'), kabel('dof', 'uit', 'som', 'a'), kabel('ruis', 'uit', 'stil', 'in'),
    kabel('stil', 'uit', 'som', 'b'), kabel('in', 'uit', 'mix', 'droog'), kabel('som', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  [{ naam: 'Bits', knoop: 'gruis', knop: 'bits', min: 1, max: 16, standaard: 6, glijMs: 20 },
    { naam: 'Downsample', knoop: 'gruis', knop: 'deling', min: 1, max: 32, standaard: 6, glijMs: 0 },
    { naam: 'Tone', knoop: 'dof', knop: 'freq', min: 500, max: 16000, standaard: 4500, glijMs: 20 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.8125, glijMs: 20 }]);
export const RINGROBOT = effect('Ring robot', 'ringrobot', 'Ringmodulatie: je geluid keer een sinus. Stemmen worden robots, drums worden bellen.',
  [{ id: 'osc', type: 'osc', x: 180, y: 380, waarden: { freq: 120 }, vast: { golf: 0 } }, { id: 'ring', type: 'vermenigvuldig', x: 360, y: 260 },
    { id: 'mix', type: 'mix', x: 640, y: 200, waarden: { mix: 0.6 } }],
  [kabel('in', 'uit', 'ring', 'a'), kabel('osc', 'uit', 'ring', 'b'), kabel('in', 'uit', 'mix', 'droog'), kabel('ring', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  [{ naam: 'Frequency', knoop: 'osc', knop: 'freq', min: 20, max: 2000, standaard: 120, glijMs: 30 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.625, glijMs: 20 }]);
export const ECHTE_POMP = effect('Sidechain pump', 'sidechainpomp', 'Echte sidechain: een stille kick op elke tel drukt de compressor in, je geluid pompt mee. De kick zelf hoor je niet.',
  [{ id: 'seq', type: 'sequencer', x: 20, y: 20, waarden: { tempo: 120, lengte: 0.25 }, vast: { patroon: 0 } },
    { id: 'kick', type: 'trommel', x: 200, y: 20, waarden: { freq: 50, val: 2, verval: 250, ruis: 0 } },
    { id: 'comp', type: 'compressor', x: 360, y: 200, waarden: { drempel: -24, verhouding: 10, aanzet: 2, loslaten: 220 } },
    { id: 'vol', type: 'versterker', x: 600, y: 200, waarden: { hoeveel: 1.25 } }],
  [kabel('seq', 'poort', 'kick', 'poort'), kabel('in', 'uit', 'comp', 'in'), kabel('kick', 'uit', 'comp', 'zijketen'), kabel('comp', 'uit', 'vol', 'in'), kabel('vol', 'uit', 'uit', 'in')],
  [{ naam: 'Tempo', knoop: 'seq', knop: 'tempo', min: 60, max: 180, standaard: 120, glijMs: 20 },
    { naam: 'Depth', knoop: 'comp', knop: 'drempel', min: -60, max: 0, standaard: -24, glijMs: 20 },
    { naam: 'Release', knoop: 'comp', knop: 'loslaten', min: 20, max: 1000, standaard: 220, glijMs: 20 }]);
export const VOUWWARM = effect('Wavefolder', 'vouwvervormer', 'Wavefolder met een filter erachter: van warm tot zingend en scheef.',
  [{ id: 'vouw', type: 'vouwer', x: 200, y: 200, waarden: { drive: 12, scheef: 0 } }, { id: 'dof', type: 'svf', x: 380, y: 200, waarden: { freq: 5000, q: 0.70703125 } },
    { id: 'vol', type: 'versterker', x: 540, y: 200, waarden: { hoeveel: 0.5 } }, { id: 'mix', type: 'mix', x: 700, y: 200, waarden: { mix: 0.75 } }],
  [kabel('in', 'uit', 'vouw', 'in'), kabel('vouw', 'uit', 'dof', 'in'), kabel('dof', 'laag', 'vol', 'in'), kabel('in', 'uit', 'mix', 'droog'),
    kabel('vol', 'uit', 'mix', 'nat'), kabel('mix', 'uit', 'uit', 'in')],
  [{ naam: 'Drive', knoop: 'vouw', knop: 'drive', min: 0, max: 36, standaard: 12, glijMs: 20 },
    { naam: 'Bias', knoop: 'vouw', knop: 'scheef', min: -1, max: 1, standaard: 0, glijMs: 20 },
    { naam: 'Tone', knoop: 'dof', knop: 'freq', min: 300, max: 16000, standaard: 5000, glijMs: 20 },
    { naam: 'Mix', knoop: 'mix', knop: 'mix', min: 0, max: 1, standaard: 0.75, glijMs: 20 }]);

// De groove waar effecten standaard op draaien (Horen → "groove"): ritmebox plus dubbas, in de motor gerenderd.
export const GROOVE = {
  naam: 'Groove (source)', id: 'groove',
  knopen: [...RITMEBOX.knopen.filter((k) => k.id !== 'uit').map((k) => ({ ...k, waarden: { ...(k.waarden || {}), ...(k.type === 'sequencer' ? { tempo: 96 } : {}) } })),
    ...DUBBAS.knopen.filter((k) => k.id !== 'uit').map((k) => ({ ...k, id: 'b_' + k.id, waarden: { ...(k.waarden || {}), ...(k.type === 'sequencer' ? { tempo: 96 } : {}) } })),
    { id: 'som', type: 'som', x: 0, y: 0 }, { id: 'zacht', type: 'versterker', x: 0, y: 0, waarden: { hoeveel: 0.5 } }, { id: 'uit', type: 'uit', x: 0, y: 0 }],
  kabels: [...RITMEBOX.kabels.filter((k) => k.naar[0] !== 'uit'), ...DUBBAS.kabels.filter((k) => k.naar[0] !== 'uit').map((k) => ({ van: ['b_' + k.van[0], k.van[1]], naar: ['b_' + k.naar[0], k.naar[1]] })),
    kabel('vol', 'uit', 'som', 'a'), kabel('b_vol', 'uit', 'som', 'b'), kabel('som', 'uit', 'zacht', 'in'), kabel('zacht', 'uit', 'uit', 'in')],
  parameters: [], modulaties: [],
};

export const INSTRUMENTEN = [DUBSIRENE, DUBBAS, ACIDLIJN, RITMEBOX, SNARENREGEN, DRONEWOLK, RUISZEE, GLITCHMACHINE, FMBEL, TOETSENPAD];
export const EFFECTEN = [DUB_ECHO, VEERGALM_SCHETS, SIDECHAIN_POMP, ECHTE_POMP, GALMKAMER, BREEDCHORUS, STRAALJAGER, LOFIKRAKER, RINGROBOT, VOUWWARM];
export const ARCHETYPEN = [...EFFECTEN, ...INSTRUMENTEN];
