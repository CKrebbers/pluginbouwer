// werkbank.js — samen bouwen met Claude. Puur (geen DOM, geen opslag): wat er tussen twee versies van een ontwerp anders
// is in gewone woorden, een afdruk van wat klinkt (posities tellen niet), de vorm van wat de tool doorgeeft, en het
// nalopen van een voorstel van Claude. app.js bewaart: op claude.ai in de database van het artifact.
//   werkbank/nu:        { meekijken, ontwerp, naam, wat, wanneer, geschiedenis: [{ wat, wanneer }] }   (de tool schrijft)
//   voorstellen/<id>:   { titel, waarom, ontwerp, basis, status: 'open'|'aangenomen'|'afgewezen', wanneer, besloten }
//                       (Claude schrijft, de tool zet alleen status en besloten)
import { BIB } from './keten.js';
import { gewoneNaam } from './rollen.js';
import { normaliseerOntwerp, geldigId } from './templates.js';

export const GESCHIEDENIS = 20, VERSIES = 30;
const tekst = (s, n) => String(s ?? '').replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, n);
export const getal = (x) => {
  if (!Number.isFinite(x)) return String(x);
  const a = Math.abs(x);
  const t = a >= 1 || a === 0 ? x.toFixed(a >= 100 ? 0 : a >= 10 ? 1 : 2).replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '') : String(Number(x.toPrecision(3)));
  return t.replace('-', '−').replace('.', ',');
};
const eenheid = (e) => (e && !['×', 'Q'].includes(e) ? ' ' + e : '');

// Wat klinkt: blokken met hun knoppen en vaste keuzes, kabels, parameters en modulaties. Niet de plek op het canvas en
// niet de naam. Twee ontwerpen met dezelfde afdruk klinken hetzelfde.
export function klankAfdruk(o) {
  const knopen = [...(o.knopen || [])].map((k) => [k.id, k.type, k.waarden || {}, k.vast || {}]).sort((a, b) => (a[0] < b[0] ? -1 : 1));
  const kabels = (o.kabels || []).map((k) => k.van.join('.') + '>' + k.naar.join('.')).sort();
  const params = (o.parameters || []).map((p) => [p.knoop, p.knop, p.standaard, p.min, p.max, p.ook || null]);
  const mods = (o.modulaties || []).map((m) => m.bron.join('.') + '>' + m.doel.join('.') + '@' + m.diepte).sort();
  const s = JSON.stringify([knopen, kabels, params, mods]);
  let h = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193) >>> 0; }
  return h.toString(16).padStart(8, '0');
}

// Wat er van a naar b veranderd is, als korte zinnen met de namen zoals op het canvas.
export function verschil(a, b) {
  const uit = [], naam = (o, id) => { const t = o.knopen.find((k) => k.id === id)?.type; return t ? gewoneNaam(t) : id; };
  const ka = new Map(a.knopen.map((k) => [k.id, k])), kb = new Map(b.knopen.map((k) => [k.id, k]));
  const zelfde = (id) => ka.has(id) && kb.has(id) && ka.get(id).type === kb.get(id).type;
  for (const k of b.knopen) if (!zelfde(k.id)) uit.push(`${gewoneNaam(k.type)} erbij`);
  for (const k of a.knopen) if (!zelfde(k.id)) uit.push(`${gewoneNaam(k.type)} eruit`);
  // knoppen: via een parameter op het paneel (zijn standaard), anders de vaste stand op het blok
  const stand = (o, kn, knop) => o.parameters.find((p) => p.knoop === kn.id && p.knop === knop)?.standaard ?? kn.waarden?.[knop] ?? BIB[kn.type].knoppen.find((d) => d.id === knop)?.std;
  for (const [id, kn] of kb) {
    if (!zelfde(id)) continue;
    const oud = ka.get(id), b0 = BIB[kn.type];
    for (const d of b0.knoppen) {
      const x = stand(a, oud, d.id), y = stand(b, kn, d.id);
      if (x !== y && Number.isFinite(x) && Number.isFinite(y)) uit.push(`${naam(b, id)}: ${d.naam} ${getal(x)} → ${getal(y)}${eenheid(d.eenheid)}`);
    }
    for (const v of b0.vasteEig || []) {
      const x = oud.vast?.[v.id] ?? v.std, y = kn.vast?.[v.id] ?? v.std;
      if (x !== y) uit.push(`${naam(b, id)}: ${v.naam} ${v.keuzes?.[x] ?? getal(x)} → ${v.keuzes?.[y] ?? getal(y)}${eenheid(v.eenheid)}`);
    }
  }
  const pa = new Map(a.parameters.map((p) => [p.knoop + '.' + p.knop, p])), pb = new Map(b.parameters.map((p) => [p.knoop + '.' + p.knop, p]));
  for (const [s, p] of pb) if (!pa.has(s) && zelfde(p.knoop)) uit.push(`knop ${p.naam} op het paneel`);
  for (const [s, p] of pa) if (!pb.has(s) && zelfde(p.knoop)) uit.push(`knop ${p.naam} van het paneel af`);
  for (const [s, p] of pb) if (pa.has(s) && pa.get(s).naam !== p.naam) uit.push(`knop ${pa.get(s).naam} heet nu ${p.naam}`);
  const kabel = (o, k) => `${naam(o, k.van[0])} → ${naam(o, k.naar[0])}`;
  const ks = (o) => new Map(o.kabels.map((k) => [k.van.join('.') + '>' + k.naar.join('.'), k]));
  const kbA = ks(a), kbB = ks(b);
  const kabelZinnen = [...[...kbB].filter(([s]) => !kbA.has(s)).map(([, k]) => `kabel ${kabel(b, k)} erbij`), ...[...kbA].filter(([s]) => !kbB.has(s)).map(([, k]) => `kabel ${kabel(a, k)} weg`)];
  uit.push(...kabelZinnen.slice(0, 6));
  if (kabelZinnen.length > 6) uit.push(`… en ${kabelZinnen.length - 6} kabels meer`);
  const ms = (o) => new Map((o.modulaties || []).map((m) => [m.bron.join('.') + '>' + m.doel.join('.'), m]));
  const ma = ms(a), mb = ms(b);
  const knopNaam = (o, m) => BIB[o.knopen.find((k) => k.id === m.doel[0])?.type]?.knoppen.find((d) => d.id === m.doel[1])?.naam ?? m.doel[1];
  for (const [s, m] of mb) {
    if (!ma.has(s)) uit.push(`${naam(b, m.bron[0])} beweegt ${knopNaam(b, m)} van ${naam(b, m.doel[0])}`);
    else if (ma.get(s).diepte !== m.diepte) uit.push(`${naam(b, m.bron[0])} → ${naam(b, m.doel[0])}: diepte ${getal(ma.get(s).diepte)} → ${getal(m.diepte)}`);
  }
  for (const [s, m] of ma) if (!mb.has(s)) uit.push(`${naam(a, m.bron[0])} beweegt ${knopNaam(a, m)} van ${naam(a, m.doel[0])} niet meer`);
  if (tekst(a.naam, 60) !== tekst(b.naam, 60)) uit.push(`naam: ${tekst(a.naam, 60)} → ${tekst(b.naam, 60)}`);
  return uit;
}

// Wat de tool doorgeeft zolang Claude meekijkt (geschiedenis = de laatste wijzigingen, nieuwste achteraan).
export function werkbankVorm(ontwerp, { wat = '', geschiedenis = [], wanneer = new Date().toISOString() } = {}) {
  return { meekijken: true, ontwerp, naam: tekst(ontwerp.naam, 60), wat: tekst(wat, 120), wanneer, afdruk: klankAfdruk(ontwerp), geschiedenis: geschiedenis.slice(-GESCHIEDENIS) };
}
export const nietMeekijken = (wanneer = new Date().toISOString()) => ({ meekijken: false, wanneer });
export function bijGeschiedenis(lijst, wat, wanneer = new Date().toISOString()) {
  const w = tekst(wat, 120);
  if (!w) return lijst;
  const laatste = lijst[lijst.length - 1];
  return (laatste?.wat === w ? [...lijst.slice(0, -1), { wat: w, wanneer }] : [...lijst, { wat: w, wanneer }]).slice(-GESCHIEDENIS);
}

// Een voorstel van Claude zoals het uit de database komt: zonder bruikbaar ontwerp telt het niet.
export function normaliseerVoorstel(doc, id) {
  if (!geldigId(id) || !doc || typeof doc !== 'object') return null;
  const ontwerp = normaliseerOntwerp(doc.ontwerp);
  if (!ontwerp) return null;
  return { id, ontwerp, titel: tekst(doc.titel, 80) || 'Voorstel', waarom: tekst(doc.waarom, 600), basis: /^[0-9a-f]{8}$/.test(doc.basis) ? doc.basis : '',
    status: ['aangenomen', 'afgewezen'].includes(doc.status) ? doc.status : 'open', wanneer: tekst(doc.wanneer, 30), besloten: tekst(doc.besloten, 30) };
}
export const openVoorstellen = (lijst) => lijst.filter((v) => v.status === 'open').sort((a, b) => (a.wanneer < b.wanneer ? -1 : a.wanneer > b.wanneer ? 1 : 0));

// Versies op deze computer: vóór elke overname de oude erbij, zodat er nooit iets verloren gaat.
export const bijVersies = (lijst, ontwerp, waarom, wanneer = new Date().toISOString().slice(0, 16).replace('T', ' ')) =>
  [...lijst, { ontwerp: JSON.parse(JSON.stringify(ontwerp)), waarom: tekst(waarom, 120), wanneer }].slice(-VERSIES);
