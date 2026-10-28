// app.js — de Pluginbouwer in de browser. Ontwerpen (bibliotheek, keten, knoppen, horen, meten, verken) en Bouwen
// (stack, formaten, kosten, het bouwpakket). Alles wat klinkt en gemeten wordt draait op de ketenmotor (WASM): dezelfde
// code als de plugin en als de CI-toetsen.
import { BIB, BIB_LATER, compileer, volgorde, latentie } from './keten.js';
import { ARCHETYPEN, DUB_ECHO, INSTRUMENTEN, EFFECTEN, GROOVE } from './archetypen.js';
import { motorInstantie, renderKeten, respons, readout as meetReadout, instrumentReadout, harmonischen, sinusBin, midden, gemiddeldSpectrum } from './meten.js';
import { Speler } from './audio.js';
import { varianten } from './verken.js';
import { bouwpakket, cpuSchatting, spec } from './bouwpakket.js';
import { maakTestsignalen } from './testsignalen.js';
import { zip } from './zip.js';
import { FAMILIES, ROL, RECEPTEN, recept, familieVan, familie, gewoneNaam, poortInfo, past, alleenSturing, voegIn, ordenen, overlap, blokMaat, beschrijf, hoofdweg, KOP, RIJ } from './rollen.js';

// ---------------------------------------------------------------------------------------------------- data van de bouw
const DATA = globalThis.PB_DATA;   // door tool/bouw.mjs ingebakken: wasm (base64), bronnen voor het pakket, metingen
const b64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
const MOTOR_BYTES = b64(DATA.motor);
const MOTOR = new WebAssembly.Module(MOTOR_BYTES);
const E = motorInstantie(MOTOR);

// ---------------------------------------------------------------------------------------------------- toestand
const kloon = (o) => JSON.parse(JSON.stringify(o));
const opslag = { lees(k, d) { try { const v = localStorage.getItem('pb.' + k); return v ? JSON.parse(v) : d; } catch { return d; } },
  schrijf(k, v) { try { localStorage.setItem('pb.' + k, JSON.stringify(v)); } catch { /* privévenster: niet bewaren */ } } };
const S = {
  ontwerp: opslag.lees('ontwerp', null) || kloon(DUB_ECHO),
  stand: 'ontwerpen', selectie: null, tab: 'horen',
  readout: null, vorige: null, laatsteWijziging: '',
  schetsboek: opslag.lees('schetsboek', []), vergelijk: [],
  filter: { familie: 'alle', soort: 'alle', zoek: '' },
  wildheid: 'subtiel', zaad: 1, varianten: [],
  abx: null, taak: opslag.lees('taak', null), bron: 'groove',
  // eenvoudig (standaard): gewone woorden, families, combineren; technisch: cpu, samples, dB, vaktaal
  weergave: opslag.lees('weergave', 'eenvoudig'),
};
const tech = () => S.weergave === 'technisch';
// De telefoon (iPhone staand, of liggend met weinig hoogte): één scherm tegelijk met een balk onderin, de keten als lijst
// van boven naar beneden, en grote knoppen. Dezelfde ontwerpen en dezelfde motor.
const SMAL = matchMedia('(max-width: 700px), (max-height: 500px) and (pointer: coarse)');
const smal = () => SMAL.matches;
S.scherm = 'keten';                                   // keten · blokken · blok · horen · bouwen
S.ketenWeergave = opslag.lees('ketenweergave', 'lijst');   // op de telefoon: lijst of tekening
S.doelKabel = null;                                   // "+ hier" in de lijst: de kabel waar het nieuwe blok op komt
const SCHERMEN = [['keten', 'Keten'], ['blokken', 'Blokken'], ['blok', 'Blok'], ['horen', 'Horen'], ['bouwen', 'Bouwen']];
function naarScherm(w) { S.scherm = w; S.stand = w === 'bouwen' ? 'bouwen' : 'ontwerpen'; teken(); scrollTo(0, 0); }
if (overlap(S.ontwerp, blokMaat)) S.ontwerp = ordenen(S.ontwerp, blokMaat);   // ook een bewaard ontwerp uit de tijd van smallere blokken
let C = compileer(S.ontwerp);
const speler = new Speler(MOTOR, MOTOR_BYTES);

// ---------------------------------------------------------------------------------------------------- hulp
const $ = (s, r = document) => r.querySelector(s);
const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v; else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (v !== false && v !== undefined && v !== null) e.setAttribute(k, v === true ? '' : v);
  }
  for (const k of kids.flat()) if (k !== null && k !== undefined && k !== false) e.append(k.nodeType ? k : document.createTextNode(String(k)));
  return e;
};
const svg = (tag, attrs = {}, ...kids) => {
  const e = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const [k, v] of Object.entries(attrs)) if (k.startsWith('on')) e.addEventListener(k.slice(2), v); else if (v !== undefined && v !== null) e.setAttribute(k, v);
  for (const k of kids.flat()) if (k) e.append(k.nodeType ? k : document.createTextNode(String(k)));
  return e;
};
const nl = (x, d = 1) => (Math.abs(x) === Infinity ? (x < 0 ? '−∞' : '∞') : Number(x).toFixed(d)).replace('-', '−').replace('.', ',');
const waardeTekst = (v, eenheid) => {
  // grenzen op de afgeronde tekst, zoals tool/src/bouwpakket.js waardeTekst (999,7 Hz = "1,00 kHz")
  if (eenheid === 'Hz') return v >= 999.5 ? nl(v / 1000, v >= 9995 ? 1 : 2) + ' kHz' : Math.round(v) + ' Hz';
  if (eenheid === 'ms') return v >= 99.95 ? Math.round(v) + ' ms' : nl(v, 1) + ' ms';
  if (eenheid === 'dB') return nl(v, 1) + ' dB';
  if (eenheid === 'Q') return 'Q ' + nl(v, 2);
  if (eenheid === 'BPM') return Math.round(v) + ' BPM';
  if (eenheid === 's') return nl(v, v < 10 ? 2 : 1) + ' s';
  if (eenheid === 'st') return (v > 0 ? '+' : '') + nl(v, 0) + ' st';
  if (eenheid === 'cent') return (v > 0 ? '+' : '') + nl(v, 0) + ' cent';
  if (eenheid === 'oct') return nl(v, 1) + ' oct';
  if (eenheid === ':1') return nl(v, 1) + ':1';
  if (eenheid === '×') return '×' + nl(v, 2);
  if (Math.abs(v) > 2) return nl(v, Math.abs(v) < 10 ? 1 : 0);   // een telling (bits, deling), geen deel van het geheel
  return Math.round(v * 100) + ' %';
};
const dbTekst = (x) => (x === null || x === undefined ? 'n.v.t.' : nl(x, 0) + ' dB');
const knoopVan = (id) => S.ontwerp.knopen.find((k) => k.id === id);
const knopDef = (type, id) => BIB[type].knoppen.find((k) => k.id === id);
const paramVan = (knoopId, knop) => S.ontwerp.parameters.findIndex((p) => p.knoop === knoopId && p.knop === knop);
// een knop die meeloopt met de parameter van een andere knop ("ook": [[knoop, knop, schaal]]), bv. een octaaf hoger
function ookVan(knoopId, knop) {
  const pi = S.ontwerp.parameters.findIndex((p) => (p.ook || []).some(([k, kn2]) => k === knoopId && kn2 === knop));
  if (pi < 0) return null;
  return { pi, schaal: S.ontwerp.parameters[pi].ook.find(([k, kn2]) => k === knoopId && kn2 === knop)[2] ?? 1 };
}
function knopWaarde(kn, def) {
  const pi = paramVan(kn.id, def.id);
  if (pi >= 0) return S.ontwerp.parameters[pi].standaard;
  const o = ookVan(kn.id, def.id);
  if (o) return S.ontwerp.parameters[o.pi].standaard * o.schaal;
  return kn.waarden?.[def.id] ?? def.std;
}
function knopBereik(kn, def) {
  const pi = paramVan(kn.id, def.id);
  if (pi >= 0) return { min: S.ontwerp.parameters[pi].min, max: S.ontwerp.parameters[pi].max };
  const o = ookVan(kn.id, def.id);
  if (o) { const p = S.ontwerp.parameters[o.pi]; return { min: p.min * o.schaal, max: p.max * o.schaal }; }
  return { min: def.min, max: def.max };
}
const heeftIngang = (o = S.ontwerp) => o.knopen.some((k) => k.type === 'in');
const standaardWaarden = () => C.params.map((p) => p.standaard);
const exactF32 = (v) => (Math.fround(v) === v ? v : Math.round(v * 64) / 64);

// ---------------------------------------------------------------------------------------------------- wijzigen
let wachtCompile = 0, wachtMeten = 0;
function gewijzigd(wat, { live = null, alGeladen = false } = {}) {
  S.laatsteWijziging = wat;
  opslag.schrijf('ontwerp', S.ontwerp);
  taakLog('wijziging', wat);
  if (live) { speler.zet(live.i, live.v); }
  clearTimeout(wachtCompile);
  wachtCompile = setTimeout(() => {
    C = compileer(S.ontwerp);
    if (!live && !alGeladen && !C.fouten.length) speler.laad(C.rij, standaardWaarden());
    teken();
    clearTimeout(wachtMeten);
    wachtMeten = setTimeout(meetNu, 250);
  }, live ? 0 : 60);
}
// Meten gebeurt met de bandbeweging en modulaties stil: die maken zijbanden rond elke toon, en dat is beweging, geen
// aliasing of vervorming. Bandruis ook uit: dat is ruis, geen vervorming. De staart komt uit dezelfde render.
const STIL = { bandbeweging: 'diepte', bandsnelheid: 'diepte', bandruis: 'niveau' };
function stilOntwerp(o) {
  const v = kloon(o);
  for (const kn of v.knopen) if (STIL[kn.type]) {
    const knop = STIL[kn.type];
    kn.waarden = { ...(kn.waarden || {}), [knop]: 0 };
    for (const p of v.parameters) if (p.knoop === kn.id && p.knop === knop) p.standaard = 0;
  }
  for (const m of v.modulaties || []) m.diepte = 0;
  return v;
}
function meetOntwerp(o) {
  const c = compileer(heeftIngang(o) ? stilOntwerp(o) : o);
  if (c.fouten.length) throw new Error(c.fouten.join('; '));
  const waarden = Object.fromEntries(c.params.map((p, i) => [i, p.standaard]));
  if (!heeftIngang(o)) return instrumentReadout(E, c.rij, { waarden, gebeurtenissen: speelProef(o, c) });
  return meetReadout(E, c.rij, { waarden });
}
// Een instrument met toetsen meet je terwijl het speelt: vier noten van een halve seconde (Noot en Poort).
function speelProef(o, c) {
  const t = o.knopen.find((k) => k.type === 'toetsen'); if (!t) return [];
  const pn = c.params.findIndex((p) => p.knoop === t.id && p.knop === 'toon'), pp = c.params.findIndex((p) => p.knoop === t.id && p.knop === 'poort');
  if (pn < 0 || pp < 0) return [];
  return [0, 7, 12, 3].flatMap((st, i) => [[i * 24000, pn, 2 ** (st / 12)], [i * 24000, pp, 1], [i * 24000 + 16000, pp, 0]]);
}
function meetNu() {
  if (C.fouten.length) { S.readout = null; tekenReadout(); return; }
  try {
    S.vorige = S.readout;
    const r = meetOntwerp(S.ontwerp);
    r.cpuSchatting = cpuSchatting(S.ontwerp, DATA.metingBlokken);
    S.readout = r;
  } catch (f) { S.readout = { fout: String(f.message || f) }; }
  tekenReadout();
  if (S.tab === 'meten') tekenOnder();
  if (S.taak?.bezig) controleerTaak();
}

// ---------------------------------------------------------------------------------------------------- opbouw
function bouw() {
  document.body.append(
    el('header', { class: 'kop' },
      el('div', { class: 'logo' }, 'PLUGINBOUWER'),
      el('div', { class: 'stand standkeuze', role: 'tablist' },
        ...['ontwerpen', 'bouwen'].map((s) => el('button', { class: 'seg', 'data-stand': s, onclick: () => { S.stand = s; teken(); } }, s === 'ontwerpen' ? 'Ontwerpen' : 'Bouwen'))),
      el('div', { class: 'stand', role: 'group', 'aria-label': 'weergave', title: 'eenvoudig = gewone woorden; technisch = cpu, samples, dB en de vaktermen' },
        ...['eenvoudig', 'technisch'].map((w) => el('button', { class: 'seg klein', 'data-weergave': w, onclick: () => { S.weergave = w; opslag.schrijf('weergave', w); teken(); } }, w))),
      el('button', { id: 'mini', class: 'knopgroot mini', 'aria-label': 'speel of stop', onclick: async () => { if (speler.speelt()) speler.stopBron(); else await speelNu(); tekenMini(); if (S.tab === 'horen') tekenOnder(); } }, '▶'),
      el('div', { id: 'readout', class: 'readout' })),
    el('main', { id: 'werk' },
      el('aside', { id: 'bib', class: 'paneel' }),
      el('section', { id: 'midden' }, el('div', { id: 'canvas', class: 'paneel' }), el('div', { id: 'onder', class: 'paneel' })),
      el('aside', { id: 'inspector', class: 'paneel' })),
    el('section', { id: 'bouwen', class: 'paneel', hidden: true }),
    el('div', { id: 'melding', class: 'melding', role: 'status', hidden: true }),
    el('nav', { id: 'tabbalk', class: 'tabbalk', 'aria-label': 'schermen' },
      ...SCHERMEN.map(([w, n]) => el('button', { 'data-scherm': w, onclick: () => naarScherm(w) }, n))));
  SMAL.addEventListener?.('change', () => teken());
  // een schuif die je vasthoudt wordt niet opnieuw getekend (anders breekt het slepen af, vooral op een telefoon)
  const los = () => { if (actieveSchuif) { actieveSchuif = null; setTimeout(teken, 0); } };
  document.addEventListener('pointerup', los); document.addEventListener('touchend', los);
  document.addEventListener('keydown', (ev) => {
    if (ev.target.closest('input,textarea,select')) return;
    if ((ev.key === 'Delete' || ev.key === 'Backspace') && S.selectie) { verwijderSelectie(); ev.preventDefault(); }
    if (ev.key === 'Escape') { S.selectie = null; teken(); }
    const t = TOETSEN.find(([c]) => c === ev.code);
    if (t && !ev.repeat && !ev.metaKey && !ev.ctrlKey && S.stand === 'ontwerpen' && S.ontwerp.knopen.some((k) => k.type === 'toetsen')) { noot(t[0], t[1], true); ev.preventDefault(); }
  });
  document.addEventListener('keyup', (ev) => { const t = TOETSEN.find(([c]) => c === ev.code); if (t) noot(t[0], t[1], false); });
  speler.opMeter = (p) => { const m = $('#meter'); if (m) m.style.width = Math.min(100, p * 100) + '%'; if (p > 1e-4) taakLog('geluid'); };
  speler.opFout = (t) => melding(t);
}
function tekenMini() { const m = $('#mini'); if (m) { m.textContent = speler.speelt() ? '■' : '▶'; m.title = speler.speelt() ? 'stop' : 'speel'; } }
let actieveSchuif = null;
const schuifVrij = (box) => !(actieveSchuif && box.contains(actieveSchuif));
function melding(t) { const m = $('#melding'); if (m) { m.textContent = t; m.hidden = false; clearTimeout(melding.t); melding.t = setTimeout(() => (m.hidden = true), 4000); } }

function teken() {
  document.body.classList.toggle('smal', smal());
  if (smal()) { if (S.scherm === 'bouwen') S.stand = 'bouwen'; else if (S.stand === 'bouwen') S.scherm = 'bouwen'; document.body.dataset.scherm = S.scherm; }
  for (const b of document.querySelectorAll('[data-scherm]')) if (b.tagName === 'BUTTON') b.classList.toggle('aan', b.dataset.scherm === S.scherm);
  tekenMini();
  for (const b of document.querySelectorAll('[data-stand]')) b.classList.toggle('aan', b.dataset.stand === S.stand);
  for (const b of document.querySelectorAll('[data-weergave]')) b.classList.toggle('aan', b.dataset.weergave === S.weergave);
  document.body.classList.toggle('technisch', tech());
  $('#werk').hidden = S.stand !== 'ontwerpen';
  $('#bouwen').hidden = S.stand !== 'bouwen';
  if (S.stand === 'ontwerpen') { tekenBib(); tekenCanvas(); tekenInspector(); tekenOnder(); } else tekenBouwen();
  tekenReadout();
}

// ---------------------------------------------------------------------------------------------------- readout
function tekenReadout() {
  const r = S.readout, v = S.vorige, box = $('#readout');
  box.replaceChildren();
  const pijl = (nu, toen, beterLager) => {
    if (!toen || nu === undefined || toen === undefined || !Number.isFinite(nu) || !Number.isFinite(toen) || Math.abs(nu - toen) < 0.05) return null;
    const omhoog = nu > toen, beter = beterLager ? !omhoog : omhoog;
    return el('span', { class: 'pijl ' + (beter ? 'ok' : 'bad'), title: 'laatste wijziging: ' + S.laatsteWijziging }, omhoog ? '▲' : '▼');
  };
  const vak = (label, waarde, p, uitleg) => el('div', { class: 'rv', title: uitleg, onclick: () => { if (smal() && uitleg) melding(uitleg); } }, el('span', { class: 'rl' }, label), el('span', { class: 'rw' }, waarde, p));
  const toetsen = el('div', { class: 'rv', title: 'wat er nu al gecontroleerd wordt; de rest doet CI' }, el('span', { class: 'rl' }, tech() ? 'toetsen' : 'klopt het'),
    el('span', { class: 'blokjes' }, ...toetsBlokjes()));
  if (!r || r.fout) {
    box.append(vak('keten', C.fouten.length ? C.fouten.length + (tech() ? ' fout' + (C.fouten.length > 1 ? 'en' : '') : ' probleem') : (r?.fout || 'meten…'), null, C.fouten.join('\n')), toetsen);
    return;
  }
  const staart = r.staart < 0 ? '∞' : nl(r.staart, 1) + ' s';
  if (!tech()) {
    // in gewone woorden; de getallen staan in de tooltip en in de technische weergave
    const naklank = r.staart < 0 ? 'blijft gaan' : r.staart < 0.05 ? 'meteen stil' : nl(r.staart, 1) + ' s';
    const zwaar = r.cpuSchatting < 1 ? 'licht' : r.cpuSchatting < 5 ? 'gemiddeld' : 'zwaar';
    const vakken = [];
    if (r.instrument) {
      const volume = r.piekDb > 0 ? 'te hard' : r.rmsDb < -40 ? 'zacht' : 'goed';
      vakken.push(vak('volume', volume, pijl(r.rmsDb, v?.rmsDb, false), `gemiddeld ${nl(r.rmsDb, 0)} dB, piek ${nl(r.piekDb, 1)} dB (boven 0 vervormt het in je muziekprogramma)`));
    } else {
      const klank = !Number.isFinite(r.thdDb) ? '—' : r.thdDb < -45 ? 'schoon' : r.thdDb < -25 ? 'warm' : 'vervormd';
      vakken.push(vak('klank', klank, pijl(r.thdDb, v?.thdDb, true), r.reden || `hoeveel het een zuivere toon kleurt: vervorming ${dbTekst(r.thdDb)}, aliasing ${dbTekst(r.aliasingDb)}`));
    }
    vakken.push(vak('naklinken', naklank, pijl(r.staart, v?.staart, true), 'hoe lang er nog geluid komt als je stopt'),
      vak('rekenwerk', zwaar, pijl(r.cpuSchatting, v?.cpuSchatting, true), `hoeveel van de computer dit vraagt: ${nl(r.cpuSchatting, 2)} % van één kern`), toetsen);
    box.append(...vakken);
    return;
  }
  if (r.instrument) {
    box.append(
      vak('cpu', nl(r.cpuSchatting, 2) + ' %', pijl(r.cpuSchatting, v?.cpuSchatting, true), 'geschat uit de gemeten blokken (één kern, WASM)'),
      vak('niveau', nl(r.rmsDb, 0) + ' dB', pijl(r.rmsDb, v?.rmsDb, false), 'effectieve waarde van wat het instrument zelf maakt (dBFS, de eerste 2 s' + (r.bespeeld ? ', bespeeld met vier noten' : '') + ')'),
      vak('piek', nl(r.piekDb, 1) + ' dB', pijl(r.piekDb, v?.piekDb, true), 'hoogste sample (dBFS); boven 0 = vervorming in de host'),
      vak('staart', staart, pijl(r.staart, v?.staart, true), 'hoe lang er na de laatste noot nog geluid komt'),
      toetsen);
    return;
  }
  box.append(
    vak('cpu', nl(r.cpuSchatting, 2) + ' %', pijl(r.cpuSchatting, v?.cpuSchatting, true), 'geschat uit de gemeten blokken (één kern, WASM)' + (r.cpuProcent !== null ? '; live meting van deze render: ' + nl(r.cpuProcent, 1) + ' %' : '')),
    vak('latency', latentie(S.ontwerp) + ' smp', null, 'wat de plugin aan de host meldt'),
    vak('staart', staart, pijl(r.staart, v?.staart, true), 'hoe lang er nog geluid komt na stilte (∞ = gaat vanzelf door)'),
    vak('aliasing', dbTekst(r.aliasingDb), pijl(r.aliasingDb, v?.aliasingDb, true), r.reden || 'energie buiten de harmonischen bij een sinus van 5 kHz, t.o.v. de grondtoon (lager = beter); gemeten met bandbeweging en modulatie stil, na ' + nl(r.inloop, 1) + ' s inlopen'),
    vak('thd 1k', dbTekst(r.thdDb), pijl(r.thdDb, v?.thdDb, true), r.reden || 'harmonische vervorming bij 1 kHz, −6 dBFS'),
    toetsen);
}
function toetsBlokjes() {
  const lijst = [
    ['keten compileert', C.fouten.length === 0],
    ['grenzen exact in float32', !C.fouten.some((f) => f.includes('float32'))],
    ['terugkoppeling heeft een vertraging', !C.fouten.some((f) => f.includes('geen vertraging'))],
    ['ingang (of een bron) en uitgang aanwezig', !C.fouten.some((f) => f.includes('geen ingang') || f.includes('geen uitgang'))],
    ['geen NaN in de meting', !!S.readout && !S.readout.fout && (!!S.readout.reden || Number.isFinite(S.readout.thdDb))],
    ...(S.readout?.instrument ? [['niet stil en niet boven 0 dBFS', S.readout.rmsDb > -60 && S.readout.piekDb <= 0]] : []),
  ];
  return lijst.map(([t, ok]) => el('i', { class: 'blokje ' + (ok ? 'ok' : 'bad'), title: t + (ok ? ': gehaald' : ': niet gehaald') }));
}

// ---------------------------------------------------------------------------------------------------- bibliotheek
// Een klein teken per familie (lijnen, geen emoji): toon, filterbocht, echo's, draaiende knop, toetsen, plus, pijl.
const TEKEN = { bron: 'M0,6 C2,0 4,0 6,6 S10,12 12,6', kleur: 'M0,3 H6 C9,3 9,10 12,10', ruimte: 'M1,11 V1 M5,11 V4 M9,11 V7',
  beweging: 'M2,10 A5,5 0 1 1 10,10 M6,6 L9,3', spelen: 'M0,1 H12 V11 H0 Z M4,1 V11 M8,1 V11', verbinden: 'M6,1 V11 M1,6 H11', vast: 'M0,6 H10 M7,3 L10,6 L7,9' };
const picto = (fam, x = 0, y = 0) => svg('path', { d: TEKEN[fam], class: 'teken', transform: `translate(${x},${y})` });
const tekenEl = (fam) => { const s = svg('svg', { viewBox: '-1 -1 14 14', class: 'tekenvak f-' + fam, 'aria-hidden': 'true' }); s.append(picto(fam)); return s; };

function tekenBib() {
  const box = $('#bib'), f = S.filter;
  box.replaceChildren(el('div', { class: 'tape' }, tech() ? 'BIBLIOTHEEK' : 'BLOKKEN'));
  const fams = FAMILIES.filter((x) => x.id !== 'vast');
  box.append(el('div', { class: 'chips' },
    el('button', { class: 'seg' + (f.familie === 'alle' || !f.familie ? ' aan' : ''), onclick: () => { f.familie = 'alle'; tekenBib(); } }, 'alles'),
    ...fams.map((x) => el('button', { class: 'seg fam' + (f.familie === x.id ? ' aan' : ''), title: x.zin, onclick: () => { f.familie = x.id; tekenBib(); } }, tekenEl(x.id), x.naam.toLowerCase())),
    el('button', { class: 'seg' + (f.familie === 'recept' ? ' aan' : ''), title: 'een paar blokken die samen één ding doen', onclick: () => { f.familie = 'recept'; tekenBib(); } }, 'combinaties')));
  if (tech()) box.append(
    el('div', { class: 'chips' }, ...[['alle', 'elk type'], ['effect', 'effect'], ['instrument', 'instrument'], ['MIDI-effect', 'MIDI'], ['analyser', 'analyser'], ['utility', 'utility'], ['modulatie', 'modulatie']].map(([w, t]) =>
      el('button', { class: 'seg' + (f.soort === w ? ' aan' : ''), onclick: () => { f.soort = w; tekenBib(); } }, t))));
  box.append(el('input', { class: 'zoek', placeholder: 'zoek: echo, filter, trommel…', value: f.zoek, oninput: (ev) => { f.zoek = ev.target.value; tekenLijst(); } }),
    S.doelKabel && knoopVan(S.doelKabel.van[0]) && knoopVan(S.doelKabel.naar[0])
      ? el('div', { class: 'doelregel' }, el('span', {}, `Kies wat er tussen ${gewoneNaam(knoopVan(S.doelKabel.van[0]).type)} en ${gewoneNaam(knoopVan(S.doelKabel.naar[0]).type)} komt.`),
        el('button', { class: 'seg klein', onclick: () => { S.doelKabel = null; smal() ? naarScherm('keten') : tekenBib(); } }, 'niet'))
      : el('p', { class: 'zacht klein' }, S.selectie?.soort === 'knoop' && knoopVan(S.selectie.id)
        ? `+ zet het achter ${gewoneNaam(knoopVan(S.selectie.id).type)}.` : '+ zet het vóór de uitgang. Kies eerst een blok om het daarachter te zetten.'),
    el('div', { id: 'bibLijst' }));
  tekenLijst();
}
function tekenLijst() {
  const f = S.filter, z = f.zoek.trim().toLowerCase(), lijst = $('#bibLijst'), fam = f.familie || 'alle';
  const pastTekst = (tekst) => !z || tekst.toLowerCase().includes(z);
  lijst.replaceChildren();
  if (fam === 'alle' || fam === 'recept') {
    const recepten = RECEPTEN.filter((r) => pastTekst(`${r.naam} ${r.kort}`) && (heeftIngang() || !r.ingang.length || fam === 'recept'));
    if (recepten.length) {
      if (fam === 'alle') lijst.append(el('div', { class: 'sub' }, 'combinaties'));
      for (const r of recepten) lijst.append(el('div', { class: 'kaart recept', title: r.kort },
        el('div', { class: 'kn' }, el('span', { class: 'kkop' }, ...[...new Set(r.knopen.map((k) => familieVan(k.type)))].map(tekenEl), r.naam),
          el('button', { class: 'plus', title: 'in de keten zetten', 'aria-label': r.naam + ' in de keten zetten', onclick: () => zetErin({ recept: r.id }) }, '+')),
        el('div', { class: 'ku' }, r.kort)));
    }
    if (fam === 'recept') return;
  }
  let vorige = null;
  for (const fx of FAMILIES) for (const [type, b] of Object.entries(BIB)) {
    if (b.vast || familieVan(type) !== fx.id) continue;
    if (fam !== 'alle' && fam !== fx.id) continue;
    if (tech() && f.soort !== 'alle' && b.soort !== f.soort) continue;
    if (!pastTekst(`${b.naam} ${gewoneNaam(type)} ${b.cat || ''} ${b.uitleg} ${ROL[type].kort}`)) continue;
    if (vorige !== fx.id && fam === 'alle') lijst.append(el('div', { class: 'sub' }, fx.naam));
    vorige = fx.id;
    const cpu = DATA.metingBlokken?.blokken?.[type]?.cpuProcent;
    lijst.append(el('div', { class: 'kaart f-rand-' + fx.id, draggable: 'true', title: tech() ? b.uitleg : ROL[type].kort + ' Sleep op een kabel om hem ertussen te zetten.',
      ondragstart: (ev) => ev.dataTransfer.setData('text/pb-blok', type) },
      el('div', { class: 'kn' }, el('span', { class: 'kkop' }, tekenEl(fx.id), tech() ? b.naam : gewoneNaam(type)),
        el('button', { class: 'plus', title: 'in de keten zetten', 'aria-label': gewoneNaam(type) + ' in de keten zetten', onclick: () => zetErin({ type }) }, '+')),
      el('div', { class: 'ku' }, tech() ? b.uitleg.split('. ')[0] + '.' : ROL[type].kort),
      tech() ? el('div', { class: 'ks' }, `${b.cat || b.groep} · ${cpu !== undefined ? nl(cpu, 2) + ' % cpu' : ''} · ${b.latency ? b.latency + ' smp' : '0 smp'}`) : null,
      tech() ? el('div', { class: 'bouw', title: 'bouwmoeite (schatting): ' + b.bouw + ' van 5' }, ...Array.from({ length: 5 }, (_, i) => el('i', { class: i < b.bouw ? 'vol' : '' }))) : null));
  }
  if (!tech()) return;
  const later = BIB_LATER.filter(([naam, , soort, cat]) => pastTekst(`${naam} ${cat}`) && (f.soort === 'alle' || soort === f.soort));
  if (later.length && fam === 'alle') {
    lijst.append(el('div', { class: 'sub' }, `nog niet gebouwd (${later.length})`));
    for (const [naam, , , cat] of later) lijst.append(el('div', { class: 'kaart later', title: 'staat in TASKS.md "Bibliotheek later"' }, el('div', { class: 'kn' }, naam), el('div', { class: 'ks' }, cat + ' · later')));
  }
}
function vrijeId(type) { let n = 1; while (knoopVan(type + n)) n++; return type + n; }
// Een blok op een vrije plek (losgelaten naast de keten): niets aangesloten.
function voegToe(type, x, y) {
  const id = vrijeId(type);
  const kn = { id, type, x: x ?? 200 + Math.random() * 400, y: y ?? 80 + Math.random() * 260, waarden: {} };
  if (type === 'vertraging') kn.vast = { max: 1200 };
  S.ontwerp.knopen.push(kn);
  if (type === 'toetsen') S.ontwerp.parameters.push(
    { naam: 'Noot', knoop: id, knop: 'toon', min: 0.25, max: 4, standaard: 1, glijMs: 5, uitleg: 'welke toets (verhouding t.o.v. de grondtoon)' },
    { naam: 'Poort', knoop: id, knop: 'poort', min: 0, max: 1, standaard: 0, glijMs: 0, uitleg: 'toets ingedrukt (1) of los (0)' });
  S.selectie = { soort: 'knoop', id };
  gewijzigd(`${BIB[type].naam} erbij`);
}
// Een blok of combinatie IN de keten: achter het gekozen blok (of vóór de uitgang), of op een kabel. Daarna wordt de
// keten netjes neergelegd en zegt een melding wat er gebeurde.
function zetErin(wat, { kabel = null } = {}) {
  kabel = kabel ?? S.doelKabel; S.doelKabel = null;
  const na = !kabel && S.selectie?.soort === 'knoop' ? S.selectie.id : null;
  const r = voegIn(S.ontwerp, wat, { na, kabel });
  if (smal() && S.scherm === 'blokken') S.scherm = 'keten';
  S.ontwerp = ordenen(r.ontwerp, blokMaat);
  S.selectie = { soort: 'knoop', id: r.nieuw.at(-1) };
  melding(r.wat);
  gewijzigd(r.wat);
}
function netjes() { S.ontwerp = ordenen(S.ontwerp, blokMaat); gewijzigd('netjes neergelegd'); }
function verwijderSelectie() {
  const s = S.selectie; if (!s) return;
  if (s.soort === 'kabel') { S.ontwerp.kabels.splice(s.index, 1); gewijzigd('kabel weg'); }
  if (s.soort === 'knoop') {
    const kn = knoopVan(s.id); if (!kn || BIB[kn.type].vast) return;
    S.ontwerp.knopen = S.ontwerp.knopen.filter((k) => k.id !== s.id);
    S.ontwerp.kabels = S.ontwerp.kabels.filter((k) => k.van[0] !== s.id && k.naar[0] !== s.id);
    S.ontwerp.parameters = S.ontwerp.parameters.filter((p) => p.knoop !== s.id);
    S.ontwerp.modulaties = (S.ontwerp.modulaties || []).filter((m) => m.bron[0] !== s.id && m.doel[0] !== s.id);
    gewijzigd(`${BIB[kn.type].naam} weg`);
  }
  S.selectie = null;
}

// ---------------------------------------------------------------------------------------------------- canvas
// maten uit rollen.js (blokMaat): dezelfde als waarmee "netjes neerleggen" rekent
const breedte = (kn) => blokMaat(kn).w;
const knoopHoogte = (kn) => blokMaat(kn).h;
const poortY = (kn, i, n) => kn.y + KOP + 14 + i * 18 + (n === 1 ? 6 : 0);
const knopX = (kn, i) => (BIB[kn.type].knoppen.length >= 2 ? breedte(kn) / 2 - 32 + (i % 2) * 64 : breedte(kn) / 2);
const isStuur = (type, poort) => poortInfo(type, poort).soort === 'sturing';
let sleep = null, laatsteVB = null;
function tekenCanvas() {
  const box = $('#canvas');
  if (!schuifVrij(box)) return;
  if (smal() && S.ketenWeergave === 'lijst') { tekenKetenLijst(box); return; }
  // het beeld past zich aan de blokken aan (zo blijven knoppen leesbaar), met ruimte voor kabels en nieuwe blokken
  // (tijdens het slepen van een blok blijft het beeld staan, anders verschuift de schaal onder de muis)
  const kn0 = S.ontwerp.knopen;
  const vb = sleep?.soort === 'knoop' && laatsteVB ? laatsteVB : [Math.min(0, ...kn0.map((k) => k.x - 30)), Math.min(0, ...kn0.map((k) => k.y - 30)),
    Math.max(640, ...kn0.map((k) => k.x + breedte(k) + 40)), Math.max(300, ...kn0.map((k) => k.y + knoopHoogte(k) + 110))];
  laatsteVB = vb;
  const [x0, y0, x1, y1] = vb;
  // nooit kleiner dan 72 %: een lange keten schuift dan opzij in plaats van onleesbaar te worden
  const s = svg('svg', { viewBox: `${x0} ${y0} ${x1 - x0} ${y1 - y0}`, class: 'keten', role: 'img', 'aria-label': 'de signaalketen', style: `min-width:${Math.round((x1 - x0) * 0.72)}px` });
  const kabelOnder = (ev) => document.elementFromPoint(ev.clientX, ev.clientY)?.closest?.('.kabelhit');
  let doelKabel = null;
  s.addEventListener('dragover', (ev) => {
    ev.preventDefault();
    const hit = kabelOnder(ev);
    if (doelKabel && doelKabel !== hit) doelKabel.previousSibling?.classList.remove('doel');
    doelKabel = hit; hit?.previousSibling?.classList.add('doel');
  });
  s.addEventListener('drop', (ev) => {
    const type = ev.dataTransfer.getData('text/pb-blok'); if (!type) return;
    ev.preventDefault();
    const hit = kabelOnder(ev), p = punt(s, ev);
    const k = hit ? S.ontwerp.kabels[+hit.dataset.kabel] : null;
    // op een kabel met geluid: ertussen (een bron wordt erbij opgeteld); anders waar je hem loslaat
    if (k && !['beweging', 'spelen'].includes(familieVan(type)) && !isStuur(knoopVan(k.van[0]).type, k.van[1])) { S.selectie = null; zetErin({ type }, { kabel: k }); }
    else voegToe(type, p.x - 40, p.y - 20);
  });
  s.addEventListener('pointermove', (ev) => sleepBeweeg(s, ev));
  s.addEventListener('pointerup', (ev) => sleepLos(s, ev));
  s.addEventListener('pointerdown', (ev) => { if (ev.target === s || ev.target.closest('.raster')) { S.selectie = null; teken(); } });
  // raster
  const raster = svg('g', { class: 'raster' });
  for (let x = Math.floor(x0 / 40) * 40; x <= x1; x += 40) raster.append(svg('line', { x1: x, y1: y0, x2: x, y2: y1 }));
  for (let y = Math.floor(y0 / 40) * 40; y <= y1; y += 40) raster.append(svg('line', { x1: x0, y1: y, x2: x1, y2: y }));
  s.append(raster);
  const { terug } = volgorde(S.ontwerp);
  // kabels: geluid in blauwgroen, sturing (noten, beweging) in oranje, een lus terug gestippeld. Elke kabel heeft een
  // brede onzichtbare rand om op te klikken en om een blok op los te laten.
  S.ontwerp.kabels.forEach((k, i) => {
    const a = knoopVan(k.van[0]), b = knoopVan(k.naar[0]); if (!a || !b) return;
    const ai = BIB[a.type].uit.indexOf(k.van[1]), bi = BIB[b.type].in.indexOf(k.naar[1]);
    const xa = a.x + breedte(a), ya = poortY(a, Math.max(0, ai), BIB[a.type].uit.length), xb = b.x, yb = poortY(b, Math.max(0, bi), BIB[b.type].in.length);
    const dx = Math.max(60, Math.abs(xb - xa) / 2);
    const isTerug = terug.includes(k), stuur = isStuur(a.type, k.van[1]);
    const d = isTerug ? `M${xa},${ya} C${xa + 80},${ya + 160} ${xb - 80},${yb + 160} ${xb},${yb}` : `M${xa},${ya} C${xa + dx},${ya} ${xb - dx},${yb} ${xb},${yb}`;
    const gekozen = S.selectie?.soort === 'kabel' && S.selectie.index === i;
    const titel = tech() ? `${k.van.join('.')} → ${k.naar.join('.')}${isTerug ? ' (terugkoppeling: leest de vorige sample)' : ''} — Delete = weg`
      : `${gewoneNaam(a.type)} → ${gewoneNaam(b.type)}${isTerug ? ' (lus terug: zo herhaalt het zich)' : stuur ? ' (stuurt)' : ''}. Klik en Delete = weg; sleep een blok hierop om het ertussen te zetten.`;
    s.append(svg('path', { d, class: 'kabel' + (isTerug ? ' terug' : '') + (stuur ? ' stuur' : '') + (gekozen ? ' gekozen' : '') }),
      svg('path', { d, class: 'kabelhit', 'data-kabel': i, onpointerdown: (ev) => { ev.stopPropagation(); S.selectie = { soort: 'kabel', index: i }; teken(); } }, svg('title', {}, titel)));
  });
  // modulaties
  for (const m of S.ontwerp.modulaties || []) {
    const a = knoopVan(m.bron[0]), b = knoopVan(m.doel[0]); if (!a || !b) continue;
    const ki = BIB[b.type].knoppen.findIndex((k) => k.id === m.doel[1]);
    const kx = b.x + knopX(b, ki), ky = b.y + KOP + 30 + Math.floor(ki / 2) * RIJ;
    s.append(svg('path', { d: `M${a.x + breedte(a)},${poortY(a, 0, 1)} C${a.x + breedte(a) + 60},${a.y} ${kx},${ky - 80} ${kx},${ky - 16}`, class: 'mod' }));
  }
  // knopen
  for (const kn of S.ontwerp.knopen) s.append(tekenKnoop(s, kn));
  if (sleep?.soort === 'kabel') s.append(svg('path', { id: 'sleepKabel', class: 'kabel sleep', d: '' }));
  box.replaceChildren(
    el('div', { class: 'kopregel' }, el('div', { class: 'tape' }, (tech() ? 'KETEN · ' : '') + S.ontwerp.naam),
      smal() ? weergaveKeuze() : null,
      el('button', { class: 'seg klein', title: 'zet de blokken op volgorde van links naar rechts, zoals het geluid loopt', onclick: netjes }, 'netjes neerleggen')),
    el('p', { class: 'samenvatting' }, beschrijf(S.ontwerp)),
    el('div', { class: 'scroller' }, s),
    el('div', { class: 'tip' }, tech()
      ? 'Sleep een blok uit de bibliotheek hierheen, of op een kabel om het ertussen te zetten. Trek een kabel van een uitgang (rechts) naar een ingang (links). Sleep de uitgang van een LFO op een knop = modulatie. Een kabel terug naar een eerder blok is een terugkoppeling (gestippeld). Delete = weg.'
      : 'Blauwgroen = geluid, oranje = sturing (noten, beweging), gestippeld = een lus terug. Trek een kabel van rechts (uit) naar links (in); waar hij past licht geel op. Sleep een oranje uitgang op een knop en die knop gaat bewegen. Klik iets en Delete = weg.'));
}
function punt(s, ev) { const p = s.createSVGPoint(); p.x = ev.clientX; p.y = ev.clientY; return p.matrixTransform(s.getScreenCTM().inverse()); }
function tekenKnoop(s, kn) {
  const b = BIB[kn.type], h = knoopHoogte(kn), W = breedte(kn), gekozen = S.selectie?.soort === 'knoop' && S.selectie.id === kn.id;
  const fam = familieVan(kn.type), trek = sleep?.soort === 'kabel' ? knoopVan(sleep.van[0]) : null;
  const g = svg('g', { class: 'knoop f-' + fam + (gekozen ? ' gekozen' : ''), transform: `translate(${kn.x},${kn.y})` });
  if (gekozen) g.append(svg('rect', { class: 'kader', x: -5, y: -5, width: W + 10, height: h + 10 }));
  const naam = tech() ? b.naam : gewoneNaam(kn.type);
  g.append(svg('rect', { class: 'kast', width: W, height: h }),
    svg('rect', { class: 'kopbalk', width: W, height: KOP, onpointerdown: (ev) => { ev.stopPropagation(); S.selectie = { soort: 'knoop', id: kn.id }; sleep = { soort: 'knoop', id: kn.id, dx: punt(s, ev).x - kn.x, dy: punt(s, ev).y - kn.y }; teken(); } },
      svg('title', {}, `${naam}: ${ROL[kn.type].kort} Sleep om te verplaatsen.`)),
    picto(fam, 6, 6),
    svg('text', { class: 'koptekst', x: 24, y: 16 }, naam.toUpperCase()));
  const label = (p, n, stuur) => (tech() ? (n > 1 ? p : '') : (n > 1 || stuur ? poortInfo(kn.type, p).naam : ''));
  b.in.forEach((p, i) => {
    const stuur = isStuur(kn.type, p), pastHier = trek && trek.id !== kn.id && past(trek.type, sleep.van[1], kn.type, p);
    g.append(
      svg('rect', { class: 'poort in' + (stuur ? ' stuur' : '') + (pastHier ? ' past' : ''), x: -6, y: poortY(kn, i, b.in.length) - kn.y - 5, width: 10, height: 10, 'data-knoop': kn.id, 'data-poort': p },
        svg('title', {}, tech() ? 'ingang ' + p : `ingang "${poortInfo(kn.type, p).naam}"${stuur ? ' (sturing: een noot, tel of beweging)' : ''}`)),
      svg('text', { class: 'poortnaam', x: 8, y: poortY(kn, i, b.in.length) - kn.y + 3 }, label(p, b.in.length, stuur)));
  });
  b.uit.forEach((p, i) => {
    const stuur = isStuur(kn.type, p);
    g.append(
      svg('rect', { class: 'poort uit' + (stuur ? ' stuur' : ''), x: W - 4, y: poortY(kn, i, b.uit.length) - kn.y - 5, width: 10, height: 10,
        onpointerdown: (ev) => { ev.stopPropagation(); sleep = { soort: 'kabel', van: [kn.id, p], x1: kn.x + breedte(kn), y1: poortY(kn, i, b.uit.length) }; tekenCanvas(); } },
        svg('title', {}, tech() ? 'uitgang ' + p + ' — sleep naar een ingang' + (b.soort === 'modulatie' ? ' of een knop' : '')
          : `uitgang "${poortInfo(kn.type, p).naam}": sleep naar een ingang${stuur ? ', of op een knop om die te laten bewegen' : ''}`)),
      svg('text', { class: 'poortnaam r', x: W - 8, y: poortY(kn, i, b.uit.length) - kn.y + 3 }, label(p, b.uit.length, stuur)));
  });
  const modTrek = trek && BIB[trek.type].soort === 'modulatie' && trek.id !== kn.id;
  b.knoppen.forEach((def, i) => { const k = tekenKnop(kn, def, knopX(kn, i), KOP + 30 + Math.floor(i / 2) * RIJ); if (modTrek) k.classList.add('past'); g.append(k); });
  return g;
}
// draaiknop: 270°, van −135° tot +135° (zoals de werkbank)
function hoekVan(v, min, max, exp) { const t = exp ? Math.log(v / min) / Math.log(max / min) : (v - min) / (max - min); return -135 + 270 * Math.min(1, Math.max(0, t)); }
function boog(r, a0, a1) {
  const p = (a) => [Math.sin(a * Math.PI / 180) * r, -Math.cos(a * Math.PI / 180) * r];
  const [x0, y0] = p(a0), [x1, y1] = p(a1);
  return `M${x0},${y0} A${r},${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1},${y1}`;
}
function tekenKnop(kn, def, cx, cy) {
  const { min, max } = knopBereik(kn, def), v = knopWaarde(kn, def), a = hoekVan(v, min, max, def.exp);
  const plugin = paramVan(kn.id, def.id) >= 0 || !!ookVan(kn.id, def.id);
  const mod = (S.ontwerp.modulaties || []).find((m) => m.doel[0] === kn.id && m.doel[1] === def.id);
  const g = svg('g', { class: 'knop' + (plugin ? ' plugin' : ''), transform: `translate(${cx},${cy})`, 'data-knoop': kn.id, 'data-knop': def.id });
  g.append(svg('circle', { r: 17, class: 'knopvlak' }), svg('path', { d: boog(15, -135, 135), class: 'boogbak' }),
    svg('path', { d: boog(15, -135, a), class: 'boog' + (mod ? ' gemod' : '') }),
    svg('line', { x1: 0, y1: 0, x2: Math.sin(a * Math.PI / 180) * 12, y2: -Math.cos(a * Math.PI / 180) * 12, class: 'naald' }),
    svg('text', { class: 'knoplabel', y: -21 }, def.naam), svg('text', { class: 'knopwaarde', y: 30 }, waardeTekst(v, def.eenheid)),
    svg('title', {}, `${def.naam}: ${waardeTekst(v, def.eenheid)}${tech() ? '' : ' — ' + (ROL[kn.type].knoppen[def.id] || '')}${plugin ? (tech() ? ' — staat in de plugin' : ' (een knop op het paneel van de plugin)') : ''}. Sleep omhoog/omlaag; shift = fijn; dubbelklik = standaard.`));
  g.addEventListener('pointerdown', (ev) => {
    ev.stopPropagation();
    S.selectie = { soort: 'knoop', id: kn.id };
    sleep = { soort: 'knop', knoop: kn.id, knop: def.id, y0: ev.clientY, v0: v };
    g.setPointerCapture?.(ev.pointerId);
  });
  g.addEventListener('dblclick', (ev) => { ev.stopPropagation(); zetKnop(kn, def, def.std, true); });
  return g;
}
function zetKnop(kn, def, v, klaar) {
  const { min, max } = knopBereik(kn, def);
  v = exactF32(Math.min(max, Math.max(min, v)));
  let pi = paramVan(kn.id, def.id);
  const o = pi < 0 ? ookVan(kn.id, def.id) : null;
  if (o) { pi = o.pi; v = exactF32(v / o.schaal); }   // de knop loopt mee: zet de parameter waar hij aan hangt
  if (pi >= 0) S.ontwerp.parameters[pi].standaard = v;
  if (!o) kn.waarden = { ...(kn.waarden || {}), [def.id]: v };
  if (pi >= 0 && !klaar) { gewijzigd(`${BIB[kn.type].naam}: ${def.naam}`, { live: { i: pi, v } }); tekenCanvasLicht(kn); }
  else gewijzigd(`${BIB[kn.type].naam}: ${def.naam} ${waardeTekst(v, def.eenheid)}`);
}
function tekenCanvasLicht() { tekenCanvas(); tekenInspector(); }
function sleepBeweeg(s, ev) {
  if (!sleep) return;
  const p = punt(s, ev);
  if (sleep.soort === 'knoop') { const kn = knoopVan(sleep.id); kn.x = Math.round((p.x - sleep.dx) / 10) * 10; kn.y = Math.round((p.y - sleep.dy) / 10) * 10; tekenCanvas(); }
  if (sleep.soort === 'kabel') { const k = $('#sleepKabel'); if (k) k.setAttribute('d', `M${sleep.x1},${sleep.y1} L${p.x},${p.y}`); }
  if (sleep.soort === 'knop') {
    const kn = knoopVan(sleep.knoop), def = knopDef(kn.type, sleep.knop), { min, max } = knopBereik(kn, def);
    const t0 = def.exp ? Math.log(sleep.v0 / min) / Math.log(max / min) : (sleep.v0 - min) / (max - min);
    const t = Math.min(1, Math.max(0, t0 + (sleep.y0 - ev.clientY) / (ev.shiftKey ? 800 : 160)));
    const v = def.exp ? min * Math.pow(max / min, t) : min + t * (max - min);
    sleep.laatste = v; zetKnop(kn, def, def.eenheid === 'ms' || def.eenheid === 'Hz' ? Math.round(v) : Math.round(v * 1000) / 1000, false);
  }
}
function sleepLos(s, ev) {
  if (!sleep) return;
  const s0 = sleep; sleep = null;
  if (s0.soort === 'kabel') {
    const doel = document.elementFromPoint(ev.clientX, ev.clientY);
    const poort = doel?.closest?.('.poort.in'), knop = doel?.closest?.('.knop');
    if (poort) {
      const naar = [poort.dataset.knoop, poort.dataset.poort];
      if (naar[0] !== s0.van[0]) {
        S.ontwerp.kabels = S.ontwerp.kabels.filter((k) => !(k.naar[0] === naar[0] && k.naar[1] === naar[1]));
        S.ontwerp.kabels.push({ van: s0.van, naar });
        gewijzigd(`kabel ${s0.van[0]} → ${naar[0]}`);
      }
    } else if (knop && BIB[knoopVan(s0.van[0]).type].soort === 'modulatie') {
      const doelKnoop = knop.dataset.knoop, doelKnop = knop.dataset.knop;
      S.ontwerp.modulaties = (S.ontwerp.modulaties || []).filter((m) => !(m.doel[0] === doelKnoop && m.doel[1] === doelKnop));
      S.ontwerp.modulaties.push({ bron: s0.van, doel: [doelKnoop, doelKnop], diepte: 0.25 });
      gewijzigd(`modulatie op ${doelKnop}`);
    } else tekenCanvas();
  } else if (s0.soort === 'knop' && s0.laatste !== undefined) {
    const kn = knoopVan(s0.knoop), def = knopDef(kn.type, s0.knop);
    gewijzigd(`${BIB[kn.type].naam}: ${def.naam} ${waardeTekst(knopWaarde(kn, def), def.eenheid)}`);
  } else teken();
}

// Een schuif voor één knop (rechts in het blokpaneel en in de lijst op de telefoon). Tijdens het slepen wordt alleen het
// getal ernaast bijgewerkt; pas bij loslaten tekent de rest opnieuw.
function schuifRij(kn, def, ...extra) {
  const { min, max } = knopBereik(kn, def), v = knopWaarde(kn, def);
  const naarT = (x) => (def.exp ? Math.log(x / min) / Math.log(max / min) : (x - min) / (max - min));
  const w = el('span', { class: 'w' }, waardeTekst(v, def.eenheid));
  const inp = el('input', { type: 'range', min: 0, max: 1000, value: Math.round(naarT(v) * 1000), 'aria-label': def.naam,
    oninput: (ev) => {
      actieveSchuif = ev.target;
      const t = ev.target.value / 1000, x = def.exp ? min * Math.pow(max / min, t) : min + t * (max - min);
      zetKnop(kn, def, def.eenheid === 'ms' || def.eenheid === 'Hz' ? Math.round(x) : Math.round(x * 1000) / 1000, false);
      w.textContent = waardeTekst(knopWaarde(kn, def), def.eenheid);
    },
    onchange: () => { actieveSchuif = null; gewijzigd(`${BIB[kn.type].naam}: ${def.naam} ${waardeTekst(knopWaarde(kn, def), def.eenheid)}`); } });
  return el('div', { class: 'knoprij' }, el('label', {}, def.naam, inp), w, ...extra);
}

// ---------------------------------------------------------------------------------------------------- de keten als lijst (telefoon)
// Van boven naar beneden zoals het geluid loopt: elk blok een kaart met zijn schuiven, en tussen twee blokken
// "+ hier": wat je dan kiest komt precies daar. Wat niet op de hoofdweg ligt (een lus terug, wat beweegt, een tweede
// bron) staat eronder, met waar het op aansluit.
function weergaveKeuze() {
  return el('div', { class: 'stand' }, ...[['lijst', 'lijst'], ['tekening', 'tekening']].map(([w, n]) =>
    el('button', { class: 'seg klein' + (S.ketenWeergave === w ? ' aan' : ''), onclick: () => { S.ketenWeergave = w; opslag.schrijf('ketenweergave', w); tekenCanvas(); } }, n)));
}
function tekenKetenLijst(box) {
  const { pad, kabels } = hoofdweg(S.ontwerp);
  const lijst = el('div', { class: 'ketenlijst' });
  const tussen = (kabel) => el('div', { class: 'tussen' },
    el('button', { class: 'seg hier', onclick: () => { S.doelKabel = kabel; S.selectie = null; naarScherm('blokken'); } },
      '+ hier iets tussen'));
  pad.forEach((id, i) => { lijst.append(blokKaart(knoopVan(id))); if (i < kabels.length) lijst.append(tussen(kabels[i])); });
  if (!pad.length) lijst.append(el('p', {}, 'Er komt nog niets bij de uitgang aan.'),
    el('button', { class: 'knopgroot', onclick: () => { S.filter.familie = heeftIngang() ? 'alle' : 'bron'; naarScherm('blokken'); } }, heeftIngang() ? '+ kies een blok' : '+ kies iets dat geluid maakt'));
  const rest = S.ontwerp.knopen.filter((k) => !pad.includes(k.id));
  if (rest.length) lijst.append(el('div', { class: 'sub' }, 'ook in dit ontwerp'), ...rest.map((k) => blokKaart(k, true)));
  box.replaceChildren(
    el('div', { class: 'kopregel' }, el('div', { class: 'tape' }, S.ontwerp.naam), weergaveKeuze()),
    el('p', { class: 'samenvatting' }, beschrijf(S.ontwerp)), lijst,
    ...(C.fouten.length ? [el('div', { class: 'fouten' }, ...C.fouten.map((f) => el('div', {}, '✕ ' + f)))] : []));
}
// waar een blok buiten de hoofdweg op aansluit, in woorden
function aansluiting(kn) {
  const o = S.ontwerp, delen = [];
  for (const k of o.kabels.filter((c) => c.van[0] === kn.id)) { const d = knoopVan(k.naar[0]); delen.push(`→ ${gewoneNaam(d.type)} (${poortInfo(d.type, k.naar[1]).naam})`); }
  for (const m of (o.modulaties || []).filter((x) => x.bron[0] === kn.id)) { const d = knoopVan(m.doel[0]); delen.push(`beweegt ${knopDef(d.type, m.doel[1])?.naam ?? m.doel[1]} van ${gewoneNaam(d.type)}`); }
  return delen.join(' · ') || 'nog nergens op aangesloten';
}
function blokKaart(kn, metAansluiting = false) {
  const b = BIB[kn.type], fam = familieVan(kn.type), gekozen = S.selectie?.soort === 'knoop' && S.selectie.id === kn.id;
  return el('div', { class: 'blokkaart f-rand-' + fam + (gekozen ? ' gekozen' : '') },
    el('button', { class: 'bkkop', onclick: () => { S.selectie = { soort: 'knoop', id: kn.id }; naarScherm('blok'); } },
      tekenEl(fam), el('b', {}, tech() ? b.naam : gewoneNaam(kn.type)), el('span', { class: 'pijltje' }, 'meer ›')),
    b.vast ? null : el('div', { class: 'ku' }, ROL[kn.type].kort),
    metAansluiting ? el('div', { class: 'ks' }, aansluiting(kn)) : null,
    ...b.knoppen.map((def) => schuifRij(kn, def)));
}

// ---------------------------------------------------------------------------------------------------- inspector
function tekenInspector() {
  const box = $('#inspector');
  if (!schuifVrij(box)) return;
  box.replaceChildren(el('div', { class: 'tape' }, 'BLOK'));
  const kn = S.selectie?.soort === 'knoop' ? knoopVan(S.selectie.id) : null;
  if (!kn) {
    box.append(el('p', { class: 'zacht' }, tech() ? 'Kies een blok in de keten om alle keuzes te zien.' : 'Klik op een blok om te zien wat het doet en wat erbij past.'),
      el('div', { class: 'sub' }, 'wat dit ontwerp doet'), el('p', {}, beschrijf(S.ontwerp)),
      el('div', { class: 'sub' }, tech() ? 'plugin-parameters' : 'knoppen op het paneel van de plugin'),
      ...S.ontwerp.parameters.map((p, i) => el('div', { class: 'regel' }, `${i + 1}. ${p.naam}`, tech() ? el('span', { class: 'zacht' }, ` ${p.knoop}.${p.knop}`) : (p.uitleg ? el('span', { class: 'zacht' }, ' — ' + p.uitleg) : null))),
      ...(C.fouten.length ? [el('div', { class: 'fouten' }, ...C.fouten.map((f) => el('div', {}, '✕ ' + f)))] : []));
    return;
  }
  const b = BIB[kn.type], rol = ROL[kn.type], fam = familie(familieVan(kn.type)), cpu = DATA.metingBlokken?.blokken?.[kn.type]?.cpuProcent;
  box.append(el('h3', { class: 'bloknaam' }, tekenEl(fam.id), tech() ? b.naam : gewoneNaam(kn.type)),
    el('div', { class: 'famlabel f-rand-' + fam.id }, fam.naam),
    el('p', {}, tech() ? b.uitleg : rol.kort));
  if (tech()) box.append(...(b.let ? [el('p', { class: 'let' }, 'Let op: ' + b.let)] : []),
    el('table', { class: 'kv' },
      el('tr', {}, el('td', {}, 'cpu'), el('td', {}, cpu !== undefined ? nl(cpu, 2) + ' % van één kern (WASM, gemeten)' : '—')),
      el('tr', {}, el('td', {}, 'latency'), el('td', {}, b.latency ? b.latency + ' sample' + (b.latency < 1 ? ' (gecompenseerd)' : '') : '0')),
      el('tr', {}, el('td', {}, 'aliasing'), el('td', {}, kn.type === 'tape' ? 'ADAA-1: −33,5 dB bij 5 kHz/12 dB (naïef −28,1)' : 'lineair: geen')),
      el('tr', {}, el('td', {}, 'bouwmoeite'), el('td', {}, b.bouw + ' van 5 (schatting)'))));
  else if (b.uitleg !== rol.kort) box.append(el('details', {}, el('summary', {}, 'meer uitleg'), el('p', { class: 'zacht' }, b.uitleg + (b.let ? ' ' + b.let : ''))));
  for (const def of b.knoppen) {
    const pi = paramVan(kn.id, def.id), v = knopWaarde(kn, def);
    const rij = schuifRij(kn, def, el('button', { class: 'seg' + (pi >= 0 ? ' aan' : ''), title: tech() ? 'in de plugin = de host kan hem automatiseren' : 'op het paneel = een knop op je plugin (in Logic ook te automatiseren); vast = alleen hier ingesteld',
        onclick: () => {
          if (pi >= 0) S.ontwerp.parameters.splice(pi, 1);
          else S.ontwerp.parameters.push({ naam: def.naam[0].toUpperCase() + def.naam.slice(1), knoop: kn.id, knop: def.id, min: def.min, max: def.max, standaard: exactF32(v), glijMs: def.glijMs, uitleg: '' });
          gewijzigd(pi >= 0 ? `${def.naam} uit de plugin` : `${def.naam} in de plugin`);
        } }, pi >= 0 ? (tech() ? 'in plugin' : 'op paneel') : 'vast'));
    if (!tech() && rol.knoppen[def.id]) rij.append(el('div', { class: 'knopuitleg' }, rol.knoppen[def.id]));
    box.append(rij);
    if (pi >= 0) {
      const p = S.ontwerp.parameters[pi];
      box.append(el('div', { class: 'bereik' }, 'naam ', el('input', { value: p.naam, onchange: (ev) => { p.naam = ev.target.value; gewijzigd('naam ' + p.naam); } }),
        ' van ', el('input', { type: 'number', value: p.min, step: 'any', onchange: (ev) => { p.min = exactF32(+ev.target.value); gewijzigd('bereik ' + p.naam); } }),
        ' tot ', el('input', { type: 'number', value: p.max, step: 'any', onchange: (ev) => { p.max = exactF32(+ev.target.value); gewijzigd('bereik ' + p.naam); } }),
        tech() ? el('select', { onchange: (ev) => { p.glijden = ev.target.value; gewijzigd('draaien ' + p.naam); } },
          el('option', { value: 'glijden', selected: p.glijden !== 'overvloeien' }, 'draaien: glijden'),
          el('option', { value: 'overvloeien', selected: p.glijden === 'overvloeien' }, 'draaien: overvloeien (plugin)')) : null));
    }
    const mod = (S.ontwerp.modulaties || []).find((m) => m.doel[0] === kn.id && m.doel[1] === def.id);
    if (!mod && !b.vast) rij.append(el('button', { class: 'seg klein beweeg', title: 'een LFO laat deze knop langzaam heen en weer gaan', onclick: () => {
      const r = voegIn(S.ontwerp, { type: 'lfo' }, { na: kn.id, knop: def.id });
      S.ontwerp = ordenen(r.ontwerp, blokMaat); melding(r.wat); gewijzigd(r.wat);
    } }, 'laat bewegen'));
    if (mod) box.append(el('div', { class: 'bereik mod' }, `beweegt door ${tech() ? mod.bron[0] : gewoneNaam(knoopVan(mod.bron[0])?.type)} · ${tech() ? 'diepte' : 'hoe ver'} `,
      el('input', { type: 'range', min: 0, max: 100, value: Math.round(mod.diepte * 100), onchange: (ev) => { mod.diepte = ev.target.value / 100; gewijzigd('modulatiediepte'); } }),
      el('button', { class: 'seg', onclick: () => { S.ontwerp.modulaties = S.ontwerp.modulaties.filter((m) => m !== mod); gewijzigd('modulatie weg'); } }, 'weg')));
  }
  for (const eig of b.vasteEig || []) {
    const huidig = kn.vast?.[eig.id] ?? eig.std;
    const zet = (w) => { kn.vast = { ...(kn.vast || {}), [eig.id]: w }; gewijzigd(`${b.naam}: ${eig.naam}${eig.keuzes ? ' ' + eig.keuzes[w] : ' ' + w}`); };
    box.append(el('div', { class: 'knoprij' }, el('label', {}, eig.naam + (eig.eenheid ? ` (${eig.eenheid})` : ''),
      eig.keuzes
        ? el('select', { onchange: (ev) => zet(+ev.target.value) }, ...eig.keuzes.map((t, j) => el('option', { value: j, selected: j === huidig }, t)))
        : el('input', { type: 'number', value: huidig, step: 1, min: 1, onchange: (ev) => zet(Math.max(1, Math.round(+ev.target.value) || eig.std)) }))));
  }
  // wat er goed bij past: één klik zet het erachter (of sluit het aan)
  if (rol.goedMet?.length) {
    box.append(el('div', { class: 'sub' }, 'past er goed bij'));
    for (const [wie, waarom] of rol.goedMet) {
      const r = wie.startsWith('recept:') ? recept(wie.slice(7)) : null, meng = wie === 'meng';
      const naam = meng ? 'Mix' : r ? r.naam : gewoneNaam(wie), fam2 = meng ? 'verbinden' : r ? familieVan(r.knopen[0].type) : familieVan(wie);
      const wat = meng ? { meng: true } : r ? { recept: r.id } : { type: wie };
      box.append(el('div', { class: 'goedmet' }, el('button', { class: 'plus', title: naam + ' erbij', 'aria-label': naam + ' erbij', onclick: () => zetErin(wat) }, '+'),
        el('span', {}, tekenEl(fam2), el('b', {}, naam), ' — ' + waarom)));
    }
  }
  if (!b.vast) box.append(el('button', { class: 'knopgroot', onclick: verwijderSelectie }, 'blok weg (Delete)'));
}

// ---------------------------------------------------------------------------------------------------- onder: tabs
// [id, eenvoudige naam, technische naam]; de taaktoets alleen in de technische weergave
const TABS = [['horen', 'Horen', 'Horen'], ['paneel', 'Paneel', 'Frontpaneel'], ['meten', 'Meten', 'Meten'], ['verken', 'Varianten', 'Verken'], ['schetsboek', 'Schetsboek', 'Schetsboek'], ['abx', 'Blind vergelijken', 'ABX'], ['taak', null, 'Taaktoets']];
function tekenOnder() {
  const box = $('#onder');
  if (!tech() && S.tab === 'taak') S.tab = 'horen';
  box.replaceChildren(el('div', { class: 'tabs' }, ...TABS.filter((t) => tech() || t[1]).map(([t, n, nt]) => el('button', { class: 'seg' + (S.tab === t ? ' aan' : ''), 'data-tab': t, onclick: () => { S.tab = t; tekenOnder(); } }, tech() ? nt : n))));
  const inhoud = el('div', { class: 'tabinhoud' });
  box.append(inhoud);
  ({ horen: tabHoren, paneel: tabPaneel, meten: tabMeten, verken: tabVerken, schetsboek: tabSchetsboek, abx: tabAbx, taak: tabTaak })[S.tab](inhoud);
}
function tabHoren(box) {
  const instrument = !heeftIngang(), toetsen = S.ontwerp.knopen.find((k) => k.type === 'toetsen');
  box.append(
    el('div', { class: 'rij' },
      el('button', { class: 'knopgroot', onclick: async () => { if (speler.speelt()) speler.stopBron(); else await speelNu(); tekenOnder(); } },
        speler.speelt() ? '■ stop' : '▶ speel'),
      ...(instrument
        ? [el('span', { class: 'zacht' }, toetsen ? 'Speel met a s d f g h j k l ; \' (of de toetsen hieronder).' : 'Dit ontwerp maakt zelf geluid: geen ingang nodig.')]
        : [el('select', { onchange: async (ev) => { S.bron = ev.target.value; if (speler.speelt()) await speler.speel(S.bron); } },
          ...[['groove', 'groove (drums en bas)'], ['stukjes', 'stukjes (korte tonen)'], ['ruis', 'ruisstoten'], ['sweep', 'sweep'], ['sinus', 'sinus 1 kHz'], ['eigen', speler.buffer ? speler.bronNaam : 'eigen audio…']].map(([w, t]) => el('option', { value: w, selected: S.bron === w }, t))),
        el('label', { class: 'bestand' }, 'eigen audio', el('input', { type: 'file', accept: 'audio/*', onchange: async (ev) => { const f = ev.target.files[0]; if (!f) return; await speler.eigenBestand(f); S.bron = 'eigen'; await speler.speel('eigen'); speler.laad(C.rij, standaardWaarden()); tekenOnder(); } })),
        el('button', { class: 'seg vasthoud', onpointerdown: (ev) => { ev.preventDefault(); speler.droog(true); }, onpointerup: () => speler.droog(false), onpointerleave: () => speler.droog(false), onpointercancel: () => speler.droog(false), oncontextmenu: (ev) => ev.preventDefault(), title: 'vasthouden = zonder plugin' }, 'droog (vasthouden)')])),
    el('div', { class: 'meterbak' }, el('div', { id: 'meter', class: 'meterbalk' })),
    ...(toetsen ? [el('div', { class: 'pad', role: 'group', 'aria-label': 'toetsen' }, ...TOETSEN.map(([code, st, label]) => el('button', { class: 'seg', 'data-code': code,
      onpointerdown: (ev) => { ev.preventDefault(); noot(code, st, true); }, onpointerup: () => noot(code, st, false), onpointerleave: () => noot(code, st, false), onpointercancel: () => noot(code, st, false), oncontextmenu: (ev) => ev.preventDefault() }, label)))] : []),
    el('div', { class: 'sub' }, 'voorbeelden — klik = laden en meteen horen'),
    el('div', { class: 'voorbeelden' }, el('span', { class: 'zacht' }, 'instrumenten'), ...INSTRUMENTEN.map(voorbeeldKnop)),
    el('div', { class: 'voorbeelden' }, el('span', { class: 'zacht' }, 'effecten'), ...EFFECTEN.map(voorbeeldKnop)),
    el('p', { class: 'zacht' }, `Motor: ${speler.modus}${speler.motorDraait ? '' : speler.ctx ? ' — draait niet' : ''}. Dezelfde WASM als de plugin; knoppen die "in plugin" staan veranderen zonder herladen, andere keuzes laden de keten opnieuw (20 ms overvloeien). Beweegt de meter maar hoor je niets? Zet het volume op en de stille modus van je telefoon uit.`));
}
const voorbeeldKnop = (a) => el('button', { class: 'seg' + (S.ontwerp.id === a.id ? ' aan' : ''), title: a.uitleg, onclick: () => kiesVoorbeeld(a) }, a.naam);
async function speelNu() {
  if (!speler.grooveData) speler.grooveData = renderKeten(E, compileer(GROOVE).rij, [new Float32Array(48000 * 8), new Float32Array(48000 * 8)]).uit;
  await speler.speel(heeftIngang() ? S.bron : 'geen');
  if (!C.fouten.length) speler.laad(C.rij, standaardWaarden());
  taakLog('afspelen');
}
async function kiesVoorbeeld(a) {
  S.ontwerp = kloon(a); S.selectie = null;
  if (overlap(S.ontwerp, blokMaat)) S.ontwerp = ordenen(S.ontwerp, blokMaat);   // de voorbeelden zijn getekend voor smallere blokken
  C = compileer(S.ontwerp);
  await speelNu();   // de klik is het gebaar dat de browser nodig heeft om geluid te mogen maken
  gewijzigd('voorbeeld ' + a.naam, { alGeladen: true });
}
// Toetsen: de middelste rij van het toetsenbord (via e.code, dus voor elke indeling), mineur-pentatonisch over twee octaven.
const TOETSEN = [['KeyA', 0, 'a'], ['KeyS', 3, 's'], ['KeyD', 5, 'd'], ['KeyF', 7, 'f'], ['KeyG', 10, 'g'], ['KeyH', 12, 'h'], ['KeyJ', 15, 'j'], ['KeyK', 17, 'k'], ['KeyL', 19, 'l'], ['Semicolon', 22, ';'], ['Quote', 24, "'"]];
const ingedrukt = [];
async function noot(code, st, aan) {
  const t = S.ontwerp.knopen.find((k) => k.type === 'toetsen'); if (!t) return;
  const pn = C.params.findIndex((p) => p.knoop === t.id && p.knop === 'toon'), pp = C.params.findIndex((p) => p.knoop === t.id && p.knop === 'poort');
  if (pn < 0 || pp < 0) { melding('Zet "toon" en "poort" van Toetsen op "in plugin" om te spelen.'); return; }
  const i = ingedrukt.findIndex((x) => x[0] === code);
  if (aan) {
    if (i >= 0) return;
    ingedrukt.push([code, st]);
    if (!speler.speelt()) { await speelNu(); tekenOnder(); }
    speler.zet(pn, 2 ** (st / 12)); speler.zet(pp, 1);
  } else {
    if (i < 0) return;
    ingedrukt.splice(i, 1);
    if (ingedrukt.length) speler.zet(pn, 2 ** (ingedrukt[ingedrukt.length - 1][1] / 12)); else speler.zet(pp, 0);   // legato: terug naar de vorige toets
  }
  for (const b of document.querySelectorAll('.pad [data-code]')) b.classList.toggle('aan', ingedrukt.some((x) => x[0] === b.dataset.code));
}
function tabPaneel(box) {
  const paneel = el('div', { class: 'frontpaneel' }, el('div', { class: 'fpnaam' }, 'PB ' + S.ontwerp.naam.toUpperCase()));
  const rij = el('div', { class: 'fprij' });
  C.params?.forEach((p, i) => {
    const kn = knoopVan(p.knoop), def = knopDef(kn.type, p.knop);
    const s = svg('svg', { viewBox: '-30 -34 60 72', class: 'fpknop' });
    s.append(tekenKnop(kn, def, 0, 0));
    rij.append(el('div', { class: 'fpvak' }, s));
    void i;
  });
  paneel.append(rij);
  box.append(paneel, el('p', { class: 'zacht' }, 'Mockup van het frontpaneel (fase 3 in PROMPTS.md). Kleuren en maat in design.md van het bouwpakket. De knoppen werken: ze zijn dezelfde als in de keten.'));
}
function tabMeten(box) {
  if (C.fouten.length) { box.append(el('p', {}, 'Eerst de fouten in de keten oplossen.')); return; }
  if (!heeftIngang()) { tabMetenInstrument(box); return; }
  const waarden = Object.fromEntries(C.params.map((p, i) => [i, p.standaard]));
  const r = respons(E, C.rij, { waarden });
  const W = 520, H = 160, fx = (f) => 40 + (Math.log(f / 20) / Math.log(21600 / 20)) * (W - 50), fy = (d) => 10 + (12 - Math.max(-60, Math.min(12, d))) / 72 * (H - 30);
  const g = svg('svg', { viewBox: `0 0 ${W} ${H}`, class: 'grafiek', role: 'img', 'aria-label': 'frequentierespons' });
  for (const f of [100, 1000, 10000]) g.append(svg('line', { x1: fx(f), y1: 10, x2: fx(f), y2: H - 20, class: 'as' }), svg('text', { x: fx(f), y: H - 6, class: 'astekst' }, f >= 1000 ? f / 1000 + 'k' : f));
  for (const d of [0, -24, -48]) g.append(svg('line', { x1: 40, y1: fy(d), x2: W - 10, y2: fy(d), class: 'as' }), svg('text', { x: 4, y: fy(d) + 4, class: 'astekst' }, d + ' dB'));
  g.append(svg('path', { class: 'reeks1', d: r.map((p, i) => (i ? 'L' : 'M') + fx(p.f).toFixed(1) + ',' + fy(p.db).toFixed(1)).join('') }));
  const k1 = 1365, h = harmonischen(midden(renderKeten(E, C.rij, [sinusBin(k1, 0.5)], { waarden }).uit[0]), k1);
  const balk = svg('svg', { viewBox: '0 0 260 160', class: 'grafiek', role: 'img', 'aria-label': 'harmonischen' });
  h.niveaus.slice(1).forEach((d, i) => { const hh = Math.max(0, (d + 120) / 120 * 120); balk.append(svg('rect', { x: 20 + i * 56, y: 140 - hh, width: 36, height: hh, class: 'balk' }), svg('text', { x: 38 + i * 56, y: 155, class: 'astekst' }, 'H' + (i + 2)), svg('text', { x: 38 + i * 56, y: 136 - hh, class: 'astekst' }, nl(d, 0))); });
  const tabel = el('table', { class: 'kv' }, ...S.ontwerp.knopen.filter((k) => !BIB[k.type].vast).map((k) => el('tr', {}, el('td', {}, BIB[k.type].naam), el('td', {}, nl(DATA.metingBlokken?.blokken?.[k.type]?.cpuProcent ?? 0, 2) + ' %'))));
  box.append(el('div', { class: 'rij' },
    el('div', {}, el('div', { class: 'sub' }, 'respons (klein signaal, impuls van −60 dBFS)'), g),
    el('div', {}, el('div', { class: 'sub' }, 'harmonischen bij 1 kHz, −6 dBFS (dB t.o.v. de grondtoon)'), balk),
    el('div', {}, el('div', { class: 'sub' }, 'cpu per blok (gemeten)'), tabel)),
    el('p', { class: 'zacht' }, 'Dezelfde meetcode als de CI-toetsen (tool/src/meten.js). Aliasing in de readout: sinus van 5 kHz, coherent op een bin; alles buiten de harmonischen telt als aliasing.'));
}
// Een instrument heeft geen ingang: geen respons of vervorming, wel wat hij zelf maakt (gemiddeld spectrum, 2 s).
function tabMetenInstrument(box) {
  const waarden = Object.fromEntries(C.params.map((p, i) => [i, p.standaard]));
  const y = renderKeten(E, C.rij, [new Float32Array(96000), new Float32Array(96000)], { waarden, gebeurtenissen: speelProef(S.ontwerp, C) }).uit[0];
  const lijst = gemiddeldSpectrum(y, 48000);
  const top = Math.max(...lijst.map((p) => p.db));
  const W = 520, H = 160, fx = (f) => 40 + (Math.log(f / 20) / Math.log(21600 / 20)) * (W - 50), fy = (d) => 10 + (Math.min(0, Math.max(-72, d - top)) / -72) * (H - 30);
  const g = svg('svg', { viewBox: `0 0 ${W} ${H}`, class: 'grafiek', role: 'img', 'aria-label': 'spectrum van het instrument' });
  for (const f of [100, 1000, 10000]) g.append(svg('line', { x1: fx(f), y1: 10, x2: fx(f), y2: H - 20, class: 'as' }), svg('text', { x: fx(f), y: H - 6, class: 'astekst' }, f >= 1000 ? f / 1000 + 'k' : f));
  for (const d of [0, -24, -48]) g.append(svg('line', { x1: 40, y1: fy(top + d), x2: W - 10, y2: fy(top + d), class: 'as' }), svg('text', { x: 4, y: fy(top + d) + 4, class: 'astekst' }, d + ' dB'));
  g.append(svg('path', { class: 'reeks1', d: lijst.map((p, i) => (i ? 'L' : 'M') + fx(p.f).toFixed(1) + ',' + fy(p.db).toFixed(1)).join('') }));
  const tabel = el('table', { class: 'kv' }, ...S.ontwerp.knopen.filter((k) => !BIB[k.type].vast).map((k) => el('tr', {}, el('td', {}, BIB[k.type].naam), el('td', {}, nl(DATA.metingBlokken?.blokken?.[k.type]?.cpuProcent ?? 0, 2) + ' %'))));
  box.append(el('div', { class: 'rij' },
    el('div', {}, el('div', { class: 'sub' }, 'spectrum van wat hij maakt (eerste 2 s, t.o.v. de piek)'), g),
    el('div', {}, el('div', { class: 'sub' }, 'cpu per blok (gemeten)'), tabel)),
    el('p', { class: 'zacht' }, 'Dit ontwerp heeft geen ingang: respons en vervorming zeggen dan niets. De readout bovenin toont niveau en piek.'));
}
function tabVerken(box) {
  box.append(el('div', { class: 'rij' },
    el('div', { class: 'sub' }, 'voorbeelden'), ...ARCHETYPEN.map(voorbeeldKnop),
    el('button', { class: 'seg', onclick: () => { S.ontwerp = { naam: 'Leeg effect', id: 'mijn_effect', uitleg: '', knopen: [{ id: 'in', type: 'in', x: 40, y: 200 }, { id: 'uit', type: 'uit', x: 960, y: 200 }], kabels: [], parameters: [], modulaties: [] }; gewijzigd('leeg effect'); } }, 'leeg effect'),
    el('button', { class: 'seg', onclick: () => { S.ontwerp = { naam: 'Leeg instrument', id: 'mijn_instrument', uitleg: '', knopen: [{ id: 'uit', type: 'uit', x: 960, y: 200 }], kabels: [], parameters: [], modulaties: [] }; gewijzigd('leeg instrument'); } }, 'leeg instrument')),
  el('div', { class: 'rij' }, el('div', { class: 'sub' }, 'varianten'),
    ...['subtiel', 'flink', 'wild'].map((w) => el('button', { class: 'seg' + (S.wildheid === w ? ' aan' : ''), onclick: () => { S.wildheid = w; tekenOnder(); } }, w)),
    el('button', { class: 'knopgroot', onclick: () => { S.zaad++; S.varianten = varianten(S.ontwerp, { zaad: S.zaad, wildheid: S.wildheid }); tekenOnder(); } }, 'maak 6 varianten')));
  const rij = el('div', { class: 'kaarten' });
  for (const v of S.varianten) {
    let r = null; try { r = meetOntwerp(v.ontwerp); } catch { /* geen meting */ }
    rij.append(el('div', { class: 'kaart' }, el('div', { class: 'kn' }, v.ontwerp.naam), el('div', { class: 'ku' }, v.wat),
      r ? el('div', { class: 'ks' }, `staart ${r.staart < 0 ? '∞' : nl(r.staart, 1) + ' s'} · thd ${dbTekst(r.thdDb)} · aliasing ${dbTekst(r.aliasingDb)}`) : null,
      el('div', { class: 'rij' }, el('button', { class: 'seg', onclick: () => { S.ontwerp = kloon(v.ontwerp); gewijzigd('variant gekozen: ' + v.wat); } }, 'neem deze'),
        el('button', { class: 'seg', onclick: () => { S.ontwerp = kloon(v.ontwerp); S.varianten = varianten(S.ontwerp, { zaad: ++S.zaad, wildheid: 'subtiel' }); gewijzigd('meer zoals: ' + v.wat); } }, 'meer zoals deze'),
        el('button', { class: 'seg', onclick: () => bewaarInSchetsboek(v.ontwerp) }, 'schetsboek'))));
  }
  box.append(rij);
}
function bewaarInSchetsboek(o) {
  let r = null;
  try { r = meetOntwerp(o); } catch { /* */ }
  S.schetsboek.push({ ontwerp: kloon(o), readout: r, wanneer: new Date().toISOString().slice(0, 16).replace('T', ' ') });
  opslag.schrijf('schetsboek', S.schetsboek);
  melding('in het schetsboek gezet');
}
function tabSchetsboek(box) {
  box.append(el('div', { class: 'rij' }, el('button', { class: 'knopgroot', onclick: () => { bewaarInSchetsboek(S.ontwerp); tekenOnder(); } }, 'bewaar dit ontwerp'),
    el('span', { class: 'zacht' }, 'Kies 2–4 om naast elkaar te leggen.')));
  const lijst = el('div', { class: 'kaarten' });
  S.schetsboek.forEach((s, i) => lijst.append(el('div', { class: 'kaart' + (S.vergelijk.includes(i) ? ' gekozen' : '') },
    el('div', { class: 'kn' }, s.ontwerp.naam), el('div', { class: 'ks' }, s.wanneer + ' · ' + s.ontwerp.knopen.length + ' blokken'),
    el('div', { class: 'rij' }, el('button', { class: 'seg', onclick: () => { S.ontwerp = kloon(s.ontwerp); gewijzigd('uit schetsboek: ' + s.ontwerp.naam); } }, 'open'),
      el('button', { class: 'seg' + (S.vergelijk.includes(i) ? ' aan' : ''), onclick: () => { S.vergelijk = S.vergelijk.includes(i) ? S.vergelijk.filter((x) => x !== i) : [...S.vergelijk, i].slice(-4); tekenOnder(); } }, 'vergelijk'),
      el('button', { class: 'seg', onclick: () => { S.schetsboek.splice(i, 1); S.vergelijk = []; opslag.schrijf('schetsboek', S.schetsboek); tekenOnder(); } }, 'weg')))));
  box.append(lijst);
  if (S.vergelijk.length >= 2) {
    const items = S.vergelijk.map((i) => S.schetsboek[i]);
    const regel = (label, f) => el('tr', {}, el('td', {}, label), ...items.map((s) => el('td', {}, f(s))));
    const pnamen = [...new Set(items.flatMap((s) => s.ontwerp.parameters.map((p) => p.naam)))];
    box.append(el('table', { class: 'vergelijk' }, el('tr', {}, el('th', {}, ''), ...items.map((s) => el('th', {}, s.ontwerp.naam))),
      regel('blokken', (s) => s.ontwerp.knopen.length), regel('staart', (s) => (s.readout ? (s.readout.staart < 0 ? '∞' : nl(s.readout.staart, 1) + ' s') : '—')),
      regel('thd 1k', (s) => (s.readout ? dbTekst(s.readout.thdDb) : '—')), regel('aliasing', (s) => (s.readout ? dbTekst(s.readout.aliasingDb) : '—')),
      regel('cpu', (s) => nl(cpuSchatting(s.ontwerp, DATA.metingBlokken), 2) + ' %'),
      ...pnamen.map((n) => regel(n, (s) => { const p = s.ontwerp.parameters.find((q) => q.naam === n); return p ? nl(p.standaard, 2) : '—'; }))));
  }
}
// ABX: A = dit ontwerp, B = een schetsboek-ontwerp of "droog" (of een eigen referentiebestand als bron). X is A of B.
function tabAbx(box) {
  const a = S.abx;
  if (!a) {
    box.append(el('p', {}, 'Blind vergelijken: je hoort A, B en X, en zegt of X gelijk is aan A of B. Na genoeg pogingen zegt de kans of je het echt hoort (p < 0,05).'),
      el('div', { class: 'rij' }, 'B = ', el('select', { id: 'abxB' }, el('option', { value: 'droog' }, 'zonder plugin (droog)'), ...S.schetsboek.map((s, i) => el('option', { value: i }, s.ontwerp.naam))),
        el('button', { class: 'knopgroot', onclick: async () => {
          const keuze = $('#abxB').value; await speler.speel(S.bron); speler.laad(C.rij, C.params.map((p) => p.standaard), 'A');
          if (keuze !== 'droog') { const c = compileer(S.schetsboek[+keuze].ontwerp); speler.laad(c.rij, c.params.map((p) => p.standaard), 'B'); }
          S.abx = { b: keuze, pogingen: [], x: Math.random() < 0.5 ? 'A' : 'B' }; tekenOnder();
        } }, 'begin')),
      el('p', { class: 'zacht' }, 'Met een referentieopname (een echte tape-echo): kies hem als eigen audio bij Horen, en B = droog. Dan vergelijk je je ontwerp met de opname zelf.'));
    return;
  }
  const hoor = (wat) => { if (wat === 'A' || (wat === 'X' && a.x === 'A')) { speler.droog(false); speler.kies('A'); } else if (a.b === 'droog') speler.droog(true); else { speler.droog(false); speler.kies('B'); } };
  const goed = a.pogingen.filter((p) => p.goed).length, n = a.pogingen.length;
  let p = 0; for (let k = goed; k <= n; k++) p += binom(n, k) * Math.pow(0.5, n);
  box.append(el('div', { class: 'rij' }, ...['A', 'B', 'X'].map((w) => el('button', { class: 'knopgroot', onclick: () => hoor(w) }, 'hoor ' + w)),
    el('span', {}, ' X is: '), ...['A', 'B'].map((w) => el('button', { class: 'seg', onclick: () => { a.pogingen.push({ gok: w, x: a.x, goed: w === a.x }); a.x = Math.random() < 0.5 ? 'A' : 'B'; bewaarAbx(); tekenOnder(); } }, w)),
    el('button', { class: 'seg', onclick: () => { S.abx = null; speler.droog(false); speler.kies('A'); tekenOnder(); } }, 'stop')),
    el('p', {}, `${goed} van ${n} goed${n ? ` · kans dat dit gokken is: p = ${nl(p, 3)}${p < 0.05 ? ' — je hoort het verschil' : ''}` : ''}`));
}
function binom(n, k) { let r = 1; for (let i = 1; i <= k; i++) r = (r * (n - k + i)) / i; return r; }
function bewaarAbx() { const log = opslag.lees('abx', []); log.push({ wanneer: new Date().toISOString(), ontwerp: S.ontwerp.naam, b: S.abx.b, pogingen: S.abx.pogingen }); opslag.schrijf('abx', log.slice(-50)); }

// Taaktoets: "maak vanaf leeg een dub-echo die zelf oscilleert" — tijd tot eerste geluid, waar vastgelopen, klaar.
const TAAK = 'Maak vanaf leeg een dub-echo die zelf oscilleert (blijft doorgaan als je stopt met spelen).';
function taakLog(soort, wat = '') {
  const t = S.taak; if (!t?.bezig) return;
  const nu = Date.now();
  if (soort === 'geluid') { if (!t.eersteGeluid && t.afgespeeld) { t.eersteGeluid = nu - t.start; t.log.push([t.eersteGeluid, 'eerste geluid']); opslag.schrijf('taak', t); } return; }
  if (soort === 'afspelen') t.afgespeeld = true;
  if (t.laatste && nu - t.laatste > 30000) t.log.push([nu - t.start, `${Math.round((nu - t.laatste) / 1000)} s niets gedaan (vastgelopen?)`]);
  t.laatste = nu; t.log.push([nu - t.start, soort + (wat ? ': ' + wat : '')]);
  opslag.schrijf('taak', t);
}
function controleerTaak() {
  const t = S.taak; if (!t?.bezig || C.fouten.length) return;
  // klaar = na een korte toon zonder invoer klinkt er na 3 s nog iets boven −40 dBFS (de lus houdt zichzelf in stand)
  const x = new Float32Array(48000 * 4); for (let i = 0; i < 4800; i++) x[i] = 0.5 * Math.sin(2 * Math.PI * 440 * i / 48000);
  const y = renderKeten(E, C.rij, [x], { waarden: Object.fromEntries(C.params.map((p, i) => [i, p.standaard])) }).uit[0];
  let e = 0; for (let i = 48000 * 3; i < y.length; i++) e += y[i] * y[i];
  const rms = 10 * Math.log10(e / 48000 + 1e-30);
  if (rms > -40) { t.bezig = false; t.klaar = Date.now() - t.start; t.log.push([t.klaar, `klaar: zelfoscillatie (${nl(rms, 0)} dBFS na 3 s)`]); opslag.schrijf('taak', t); melding('Taak gehaald in ' + Math.round(t.klaar / 1000) + ' s'); if (S.tab === 'taak') tekenOnder(); }
}
function tabTaak(box) {
  const t = S.taak;
  box.append(el('p', {}, el('b', {}, 'De taak: '), TAAK),
    el('div', { class: 'rij' },
      el('button', { class: 'knopgroot', onclick: () => { S.taak = { bezig: true, start: Date.now(), log: [[0, 'begin']], eersteGeluid: 0, klaar: 0 }; S.ontwerp = { naam: 'Taak', id: 'taak', uitleg: '', knopen: [{ id: 'in', type: 'in', x: 40, y: 200 }, { id: 'uit', type: 'uit', x: 960, y: 200 }], kabels: [], parameters: [], modulaties: [] }; opslag.schrijf('taak', S.taak); gewijzigd('taak begonnen'); } }, 'begin (canvas wordt leeg)'),
      t ? el('button', { class: 'seg', onclick: () => { const blob = new Blob([JSON.stringify(t, null, 2)], { type: 'application/json' }); download(blob, 'taaktoets.json'); } }, 'log bewaren') : null),
    t ? el('div', {}, el('p', {}, t.bezig ? 'bezig…' : (t.klaar ? `gehaald in ${Math.round(t.klaar / 1000)} s` : 'gestopt'),
      ` · eerste geluid: ${t.eersteGeluid ? Math.round(t.eersteGeluid / 1000) + ' s' : 'nog niet'}`),
    el('pre', { class: 'log' }, t.log.slice(-14).map(([ms, w]) => `${String(Math.round(ms / 1000)).padStart(4)} s  ${w}`).join('\n'))) : null,
    el('p', { class: 'zacht' }, 'De log blijft in deze browser. Geen oordeel achteraf: hij meet wat je deed.'));
}
// Bewaren: als artifact via de downloads-capability van claude.ai (een gewone downloadlink doet daar niets), als losse
// pagina met een gewone link.
async function download(blob, naam) {
  const d = globalThis.claude?.use ? await globalThis.claude.use('downloads').catch(() => null) : null;
  if (d) {
    try { await d.save({ filename: naam, data: blob }); melding('bewaard: ' + naam); }
    catch (f) { if (f?.code !== 'declined') melding('bewaren lukte niet (' + (f?.code || f) + '); de losse pagina kan het wel'); }
    return;
  }
  const a = el('a', { href: URL.createObjectURL(blob), download: naam }); document.body.append(a); a.click(); a.remove();
}

// ---------------------------------------------------------------------------------------------------- bouwen-stand
function tekenBouwen() {
  const box = $('#bouwen');
  const kosten = [
    ['DSP-bron', 'eigen C++ in dsp/, vrijstaand (geen libm)', 'gemaakt', '0', 'regels in CLAUDE.md volgen; toetsen vangen het', 'niets (Claude schrijft het)'],
    ['plugin-schil', 'CLAP-first + clap-wrapper v0.16.0 (MIT)', 'klein', '0', 'AU-parameters vóór Initialize: gepatcht', 'niets'],
    ['AU (Logic)', 'AUv2 via clap-wrapper, ad-hoc gesigneerd', 'klein', '0', 'Logic ziet hem pas na killall AudioComponentRegistrar', 'installeren (LEERPAD.md)'],
    ['VST3 (Omarchy)', 'via clap-wrapper; VST3 SDK MIT', 'klein', '0', 'Linux: -z nodelete nodig', 'naar ~/.vst3 kopiëren'],
    ['CLAP (Omarchy)', 'native', 'klein', '0', '—', 'naar ~/.clap kopiëren'],
    ['AUv3 (iPad/iPhone)', 'later: clap-wrapper kan AUv3 (Xcode)', 'middel', 'gratis 7 dagen, blijvend ±€ 99/jaar [niet bevestigd]', 'App Store-regels, sandbox', 'Xcode en een Apple-account'],
    ['CI', 'GitHub Actions, macOS arm64 + Linux', 'gemaakt', '0 (publieke repo)', 'privérepo telt macOS zwaarder [factor niet bevestigd]', 'een groene run lezen'],
    ['alternatief: Faust', 'één .dsp naar WASM en C++', 'middel', '0 (LGPL-compiler)', 'float loopt −46,8 dBFS uit elkaar bij bewegende coëfficiënten (gemeten)', 'Faust lezen (het is een taal die Clay zelf zou kunnen schrijven)'],
    ['alternatief: JUCE 9', 'AGPLv3 of commercieel', 'groot', '0 voor eigen gebruik', 'geen zelfde-DSP-in-browser', 'niets, maar de licentie bij publiceren'],
  ];
  let pakket = null;
  try { pakket = maakPakket(); } catch (f) { pakket = { fout: String(f.message || f) }; }
  box.replaceChildren(el('div', { class: 'tape' }, 'BOUWEN · ' + S.ontwerp.naam),
    el('div', { class: 'rij' }, el('div', { class: 'blok' }, el('h3', {}, 'Stack (gekozen na meting, KEUZE-STACK.md)'),
      el('p', {}, 'De DSP die je hier hoort is de DSP van de plugin: dezelfde C++ (dsp/pb_graaf.h), gecompileerd naar WASM voor de browser en native voor de plugin. In CI bit voor bit gelijk getoetst (CLAP, VST3, AU; Linux en macOS arm64).'),
      el('table', { class: 'vergelijk' }, el('tr', {}, ...['keuze', 'wat', 'moeite', 'kosten', 'risico', 'wat Clay moet doen/leren'].map((t) => el('th', {}, t))),
        ...kosten.map((r) => el('tr', {}, ...r.map((c) => el('td', {}, c)))))),
    el('div', { class: 'blok' }, el('h3', {}, 'Het bouwpakket'),
      pakket.fout ? el('p', { class: 'fouten' }, 'Kan nog niet exporteren: ' + pakket.fout) :
        el('div', {}, el('p', {}, `${Object.keys(pakket).length} bestanden: SPEC, ARCHITECTUUR, CLAUDE, TASKS, PROMPTS (fase 0–7), design, LEERPAD, de DSP, testsignalen met verwachte uitvoer (gerekend op de motor in deze browser), gereedschap voor de null-test.`),
          el('button', { class: 'knopgroot', onclick: () => { download(new Blob([zip(maakPakket(), 'bouwpakket-' + S.ontwerp.id)], { type: 'application/zip' }), `bouwpakket-${S.ontwerp.id}.zip`); } }, '⇩ exporteer bouwpakket (.zip)'),
          el('details', {}, el('summary', {}, 'SPEC.md bekijken'), el('pre', { class: 'log' }, pakket['SPEC.md'])),
          el('details', {}, el('summary', {}, 'bestanden'), el('pre', { class: 'log' }, Object.entries(pakket).map(([p, v]) => `${p}  (${(typeof v === 'string' ? v.length : v.byteLength).toLocaleString('nl')} bytes)`).join('\n')))))));
}
function maakPakket() {
  if (C.fouten.length) throw new Error(C.fouten.join('; '));
  const ontwerp = { ...kloon(S.ontwerp), id: (S.ontwerp.id || 'ontwerp').replace(/[^a-z0-9_]/gi, '_').toLowerCase() };
  const c = compileer(ontwerp);
  const testsignalen = maakTestsignalen(E, c.rij, c.params, ontwerp, { terugkoppeling: volgorde(ontwerp).terug.length > 0, render: renderKeten });
  const extra = { 'wasm/graaf.wasm': MOTOR_BYTES, 'wasm/keten.json': JSON.stringify({ rij: Array.from(c.rij), params: c.params }, null, 1) + '\n' };
  const metingen = ontwerp.id === 'dub_echo' ? DATA.metingen : {};
  return bouwpakket(ontwerp, { metingen, metingBlokken: DATA.metingBlokken, bronnen: DATA.bronnen, testsignalen, extra, uitslagen: DATA.uitslagen?.[ontwerp.id] || {} });
}
void spec;

// ---------------------------------------------------------------------------------------------------- start
bouw();
teken();
meetNu();
globalThis.PB = { S, compileer: () => C, meetNu, maakPakket, speler, gewijzigd };   // voor de toets in tools/headless
