// templates.js — templates: eigen startpunten, geordend in mappen. Puur (geen DOM, geen opslag): wat een template is,
// of een bewaarde rij te vertrouwen is, de volgorde, en wat er verandert bij verplaatsen. app.js bewaart: op claude.ai
// in de gedeelde database van het artifact (zodat Claude templates kan maken en lezen), anders in de browser.
// Bewaard formaat (één document per template, één per map):
//   templates/<id>: { naam, map, plek, ontwerp, notitie, door: 'jij' | 'claude', wanneer }
//   mappen/<id>:    { naam, plek }
import { BIB, compileer } from './keten.js';

export const LOS = 'los';   // waar een template zonder (bestaande) map staat
export const MAX = { naam: 60, notitie: 400, knopen: 64, kabels: 256, templates: 500, mappen: 50 };
export const EXPORT_SOORT = 'pluginbouwer-templates';

const tekst = (s, n) => String(s ?? '').replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, n);
const getal = (x, d) => (typeof x === 'number' && Number.isFinite(x) ? x : d);
const ID = /^[A-Za-z0-9_\-.~:@+]{1,80}$/;
export const geldigId = (id) => typeof id === 'string' && ID.test(id) && id !== '.' && id !== '..';
export const nieuwId = (voor, rnd = Math.random) => voor + '-' + Date.now().toString(36) + rnd().toString(36).slice(2, 8);

// Een ontwerp uit opslag is pas te gebruiken als elk blok bestaat en de compiler er niet op omvalt (een half ontwerp
// mag: dan zegt de tool wat er nog mist). Geeft een schone kopie of null.
export function normaliseerOntwerp(o) {
  if (!o || typeof o !== 'object' || !Array.isArray(o.knopen) || o.knopen.length > MAX.knopen) return null;
  for (const k of o.knopen) if (!k || typeof k.id !== 'string' || !BIB[k.type]) return null;
  for (const lijst of ['kabels', 'parameters', 'modulaties']) if (o[lijst] !== undefined && !Array.isArray(o[lijst])) return null;
  if ((o.kabels?.length ?? 0) > MAX.kabels) return null;
  let kopie;
  try { kopie = JSON.parse(JSON.stringify(o)); } catch { return null; }
  kopie.naam = tekst(kopie.naam, MAX.naam) || 'Template';
  kopie.kabels ??= []; kopie.parameters ??= []; kopie.modulaties ??= [];
  for (const k of kopie.knopen) { k.x = getal(k.x, 40); k.y = getal(k.y, 200); }
  try { compileer(kopie); } catch { return null; }
  return kopie;
}

export function normaliseerTemplate(doc, id) {
  if (!geldigId(id) || !doc || typeof doc !== 'object') return null;
  const ontwerp = normaliseerOntwerp(doc.ontwerp);
  if (!ontwerp) return null;
  return {
    id, ontwerp,
    naam: tekst(doc.naam, MAX.naam) || ontwerp.naam,
    map: geldigId(doc.map) ? doc.map : LOS,
    plek: getal(doc.plek, 0),
    notitie: tekst(doc.notitie, MAX.notitie),
    door: doc.door === 'claude' ? 'claude' : 'jij',
    wanneer: tekst(doc.wanneer, 30),
  };
}
export function normaliseerMap(doc, id) {
  if (!geldigId(id) || id === LOS || !doc || typeof doc !== 'object') return null;
  return { id, naam: tekst(doc.naam, MAX.naam) || 'Map', plek: getal(doc.plek, 0) };
}
// wat er in de opslag komt (zonder id: dat is de documentnaam)
export const bewaarVorm = (t) => ({ naam: t.naam, map: t.map, plek: t.plek, ontwerp: t.ontwerp, notitie: t.notitie, door: t.door, wanneer: t.wanneer });
export const mapVorm = (m) => ({ naam: m.naam, plek: m.plek });

const opPlek = (a, b) => a.plek - b.plek || a.naam.localeCompare(b.naam, 'nl') || (a.id < b.id ? -1 : 1);

// Mappen op hun plek, daarbinnen de templates op hun plek; templates van een map die er niet (meer) is staan onder "Los",
// en die groep staat achteraan, alleen als er iets in zit.
export function ordenen(templates, mappen) {
  const ms = [...mappen].sort(opPlek);
  const ids = new Set(ms.map((m) => m.id));
  const groepen = ms.map((m) => ({ map: m, templates: [] }));
  const los = { map: { id: LOS, naam: 'Los', plek: Infinity }, templates: [] };
  for (const t of templates) (ids.has(t.map) ? groepen.find((g) => g.map.id === t.map) : los).templates.push(t);
  for (const g of groepen) g.templates.sort(opPlek);
  los.templates.sort(opPlek);
  return los.templates.length ? [...groepen, los] : groepen;
}
const inMap = (templates, mappen, mapId) => ordenen(templates, mappen).find((g) => g.map.id === mapId)?.templates ?? [];

// een plek achteraan in een rij
export const plekAchteraan = (rij) => rij.reduce((m, x) => Math.max(m, x.plek), 0) + 1;

// Eén omhoog (−1) of omlaag (+1) in zijn rij: de rij wordt 1, 2, 3 … genummerd en alleen wat echt verschuift komt terug
// als { id, plek }. Zo werkt het ook als twee items dezelfde plek hadden (bv. tegelijk gemaakt).
export function schuif(rij, id, richting) {
  const r = [...rij].sort(opPlek);
  const i = r.findIndex((x) => x.id === id), j = i + richting;
  if (i < 0 || j < 0 || j >= r.length) return [];
  [r[i], r[j]] = [r[j], r[i]];
  return r.map((x, k) => ({ id: x.id, plek: k + 1, oud: x.plek })).filter((x) => x.plek !== x.oud).map(({ id, plek }) => ({ id, plek }));
}
export const schuifTemplate = (templates, mappen, id, richting) => {
  const t = templates.find((x) => x.id === id);
  return t ? schuif(inMap(templates, mappen, mappen.some((m) => m.id === t.map) ? t.map : LOS), id, richting) : [];
};
export const schuifMap = (mappen, id, richting) => schuif(mappen, id, richting);

// naar een andere map: achteraan in die map
export function naarMap(templates, mappen, id, mapId) {
  const doel = mappen.some((m) => m.id === mapId) ? mapId : LOS;
  return { id, map: doel, plek: plekAchteraan(inMap(templates, mappen, doel).filter((x) => x.id !== id)) };
}

export function nieuweTemplate(ontwerp, { naam, map = LOS, notitie = '', door = 'jij', templates = [], mappen = [], id = nieuwId('t'), wanneer = new Date().toISOString().slice(0, 16).replace('T', ' ') } = {}) {
  const o = normaliseerOntwerp(ontwerp);
  if (!o) throw new Error('dit ontwerp kan geen template worden');
  const doel = mappen.some((m) => m.id === map) ? map : LOS;
  return { id, ontwerp: o, naam: tekst(naam, MAX.naam) || o.naam, map: doel, plek: plekAchteraan(inMap(templates, mappen, doel)), notitie: tekst(notitie, MAX.notitie), door: door === 'claude' ? 'claude' : 'jij', wanneer };
}
export function nieuweMap(naam, mappen, id = nieuwId('m')) {
  return { id, naam: tekst(naam, MAX.naam) || 'Map', plek: plekAchteraan(mappen) };
}

// Een template openen: het ontwerp onder de naam van de template, met een id uit die naam (daar komt de plugin-id van:
// een template van de Dub echo wordt zo geen tweede plugin met de id van de Dub echo).
export const idVan = (naam) => tekst(naam, MAX.naam).toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '') || 'template';
export const ontwerpVan = (t) => ({ ...JSON.parse(JSON.stringify(t.ontwerp)), naam: t.naam, id: idVan(t.naam) });

// Zoeken op naam, notitie, map en de blokken die erin zitten (Engelse namen, zoals in de bibliotheek).
export function zoek(templates, mappen, q) {
  const w = tekst(q, 60).toLowerCase();
  if (!w) return templates;
  const mapNaam = Object.fromEntries(mappen.map((m) => [m.id, m.naam]));
  return templates.filter((t) => [t.naam, t.notitie, mapNaam[t.map] ?? '', ...t.ontwerp.knopen.map((k) => BIB[k.type].naam)].join(' ').toLowerCase().includes(w));
}

// Alles als één JSON-bestand (bewaren, of naar een andere browser), en terug: dezelfde id = vervangen.
export const exporteer = (templates, mappen) => ({ soort: EXPORT_SOORT, versie: 1, mappen: mappen.map((m) => ({ id: m.id, ...mapVorm(m) })), templates: templates.map((t) => ({ id: t.id, ...bewaarVorm(t) })) });
export function importeer(json) {
  if (!json || json.soort !== EXPORT_SOORT || !Array.isArray(json.templates)) throw new Error('geen templatebestand van de Pluginbouwer');
  const mappen = (json.mappen || []).slice(0, MAX.mappen).map((m) => normaliseerMap(m, m?.id)).filter(Boolean);
  const templates = json.templates.slice(0, MAX.templates).map((t) => normaliseerTemplate(t, t?.id)).filter(Boolean);
  return { mappen, templates, overgeslagen: json.templates.length - templates.length };
}
