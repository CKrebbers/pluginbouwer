// templates.test.mjs — templates en mappen (tool/src/templates.js): wat er uit opslag komt wordt nagelopen (onbekende
// blokken, rare velden), de volgorde klopt, verschuiven en verplaatsen geven alleen wat echt verandert, en exporteren
// en importeren is een rondje zonder verlies.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { compileer } from '../tool/src/keten.js';
import { DUB_ECHO, TOETSENPAD, ARCHETYPEN } from '../tool/src/archetypen.js';
import { LOS, normaliseerTemplate, normaliseerMap, normaliseerOntwerp, bewaarVorm, ordenen, schuif, schuifTemplate, schuifMap, naarMap, nieuweTemplate, nieuweMap, ontwerpVan, zoek, exporteer, importeer, geldigId } from '../tool/src/templates.js';

const kopie = (o) => JSON.parse(JSON.stringify(o));
const opId = (w) => [...w].sort((a, b) => (a.id < b.id ? -1 : 1));

test('elk voorbeeld kan een template worden en klinkt daarna hetzelfde', () => {
  for (const o of ARCHETYPEN) {
    const t = nieuweTemplate(o, { id: 't-1' });
    const terug = normaliseerTemplate(kopie(bewaarVorm(t)), t.id);
    assert.ok(terug, o.naam);
    assert.deepEqual(compileer(ontwerpVan(terug)).rij, compileer(o).rij, `${o.naam}: de keten veranderde`);
    assert.equal(ontwerpVan(terug).naam, o.naam);
    assert.match(ontwerpVan(terug).id, /^[a-z0-9_]+$/);
  }
});

test('wat uit opslag komt wordt nagelopen: onbekende blokken, kapotte velden, rare id\'s', () => {
  const goed = bewaarVorm(nieuweTemplate(DUB_ECHO, { id: 't-a' }));
  assert.equal(normaliseerTemplate({ ...goed, ontwerp: { ...goed.ontwerp, knopen: [...goed.ontwerp.knopen, { id: 'x', type: 'bestaatniet' }] } }, 't-a'), null);
  assert.equal(normaliseerTemplate({ ...goed, ontwerp: null }, 't-a'), null);
  assert.equal(normaliseerTemplate({ ...goed, ontwerp: { ...goed.ontwerp, kabels: 'nee' } }, 't-a'), null);
  assert.equal(normaliseerTemplate(goed, '../weg'), null);
  assert.equal(normaliseerTemplate(goed, ''), null);
  assert.ok(!geldigId('a/b') && geldigId('t-abc') && !geldigId('..'));
  const raar = normaliseerTemplate({ ...goed, naam: 'x'.repeat(500) + '\u0007', plek: 'eerste', map: 42, door: 'iemand', notitie: 7 }, 't-a');
  assert.equal(raar.naam.length, 60);
  assert.equal(raar.plek, 0); assert.equal(raar.map, LOS); assert.equal(raar.door, 'jij'); assert.equal(raar.notitie, '7');
  assert.equal(normaliseerTemplate({ ...goed, door: 'claude' }, 't-a').door, 'claude');
  assert.equal(normaliseerMap({ naam: 'Dub' }, LOS), null, 'een map mag niet "los" heten');
  assert.ok(normaliseerOntwerp({ naam: 'half', knopen: [{ id: 'in', type: 'in' }, { id: 'uit', type: 'uit' }] }), 'een ontwerp zonder kabels mag (de tool zegt wat er mist)');
});

test('ordenen: mappen op hun plek, templates op hun plek, Los achteraan en alleen als er iets in zit', () => {
  const mappen = [{ id: 'm-b', naam: 'Synth', plek: 2 }, { id: 'm-a', naam: 'Dub', plek: 1 }, { id: 'm-c', naam: 'Leeg', plek: 3 }];
  const t = (id, map, plek) => ({ ...nieuweTemplate(DUB_ECHO, { id }), map, plek });
  const lijst = [t('t-1', 'm-b', 2), t('t-2', 'm-b', 1), t('t-3', 'm-a', 1), t('t-4', 'm-weg', 1)];
  const g = ordenen(lijst, mappen);
  assert.deepEqual(g.map((x) => x.map.id), ['m-a', 'm-b', 'm-c', LOS]);
  assert.deepEqual(g[1].templates.map((x) => x.id), ['t-2', 't-1']);
  assert.deepEqual(g[3].templates.map((x) => x.id), ['t-4'], 'een template van een weggehaalde map staat onder Los');
  assert.deepEqual(ordenen(lijst.slice(0, 3), mappen).map((x) => x.map.id), ['m-a', 'm-b', 'm-c']);
});

test('schuiven: alleen wat echt verschuift, en ook met gelijke plekken', () => {
  const r = [{ id: 'a', naam: 'A', plek: 1 }, { id: 'b', naam: 'B', plek: 2 }, { id: 'c', naam: 'C', plek: 3 }];
  assert.deepEqual(opId(schuif(r, 'c', -1)), [{ id: 'b', plek: 3 }, { id: 'c', plek: 2 }]);
  assert.deepEqual(schuif(r, 'a', -1), [], 'de eerste kan niet hoger');
  assert.deepEqual(schuif(r, 'c', 1), []);
  const gelijk = [{ id: 'a', naam: 'A', plek: 5 }, { id: 'b', naam: 'B', plek: 5 }];
  const w = schuif(gelijk, 'b', -1);
  const na = gelijk.map((x) => ({ ...x, ...w.find((y) => y.id === x.id) })).sort((x, y) => x.plek - y.plek);
  assert.deepEqual(na.map((x) => x.id), ['b', 'a']);
  const mappen = [{ id: 'm-1', naam: 'X', plek: 1 }, { id: 'm-2', naam: 'Y', plek: 2 }];
  assert.deepEqual(opId(schuifMap(mappen, 'm-2', -1)), [{ id: 'm-1', plek: 2 }, { id: 'm-2', plek: 1 }]);
  // een template schuift alleen binnen zijn eigen map
  const t = [{ ...nieuweTemplate(DUB_ECHO, { id: 't-1' }), map: 'm-1', plek: 1 }, { ...nieuweTemplate(DUB_ECHO, { id: 't-2' }), map: 'm-2', plek: 1 }];
  assert.deepEqual(schuifTemplate(t, mappen, 't-2', -1), []);
});

test('naar een andere map: achteraan; een nieuwe template en map komen ook achteraan', () => {
  const mappen = [{ id: 'm-1', naam: 'Dub', plek: 1 }];
  const t1 = nieuweTemplate(DUB_ECHO, { id: 't-1', map: 'm-1', mappen });
  const t2 = nieuweTemplate(TOETSENPAD, { id: 't-2', map: 'm-1', mappen, templates: [t1] });
  assert.equal(t1.plek, 1); assert.equal(t2.plek, 2); assert.equal(t2.map, 'm-1');
  assert.equal(nieuweTemplate(DUB_ECHO, { map: 'm-bestaatniet', mappen }).map, LOS);
  assert.deepEqual(naarMap([t1, t2], mappen, 't-1', LOS), { id: 't-1', map: LOS, plek: 1 });
  const t3 = { ...nieuweTemplate(DUB_ECHO, { id: 't-3' }), map: LOS, plek: 1 };
  assert.deepEqual(naarMap([t1, t2, t3], mappen, 't-3', 'm-1'), { id: 't-3', map: 'm-1', plek: 3 });
  assert.equal(nieuweMap('Synth', mappen).plek, 2);
  assert.equal(nieuweMap('   ', []).naam, 'Map');
  assert.throws(() => nieuweTemplate({ knopen: [{ id: 'x', type: 'bestaatniet' }] }), /geen template/);
});

test('een geopende template krijgt een eigen id uit zijn naam (de plugin-id)', () => {
  const t = nieuweTemplate(DUB_ECHO, { id: 't-1', naam: 'Dub échô + Reverb!' });
  assert.equal(ontwerpVan(t).id, 'dub_echo_reverb');
  assert.equal(ontwerpVan({ ...t, naam: '***' }).id, 'template');
});

test('zoeken op naam, notitie, map en blok', () => {
  const mappen = [{ id: 'm-1', naam: 'Dub', plek: 1 }];
  const a = { ...nieuweTemplate(DUB_ECHO, { id: 't-a', naam: 'Echo A', notitie: 'voor de skank' }), map: 'm-1' };
  const b = nieuweTemplate(TOETSENPAD, { id: 't-b', naam: 'Pad B' });
  assert.deepEqual(zoek([a, b], mappen, 'skank').map((t) => t.id), ['t-a']);
  assert.deepEqual(zoek([a, b], mappen, 'dub').map((t) => t.id), ['t-a']);
  assert.deepEqual(zoek([a, b], mappen, 'keyboard').map((t) => t.id), ['t-b']);
  assert.equal(zoek([a, b], mappen, '').length, 2);
});

test('exporteren en importeren: een rondje zonder verlies, kapotte templates overgeslagen', () => {
  const mappen = [{ id: 'm-1', naam: 'Dub', plek: 1 }];
  const lijst = [nieuweTemplate(DUB_ECHO, { id: 't-1', map: 'm-1', mappen, door: 'claude', notitie: 'n' }), nieuweTemplate(TOETSENPAD, { id: 't-2' })];
  const json = JSON.parse(JSON.stringify(exporteer(lijst, mappen)));
  const r = importeer(json);
  assert.deepEqual(r.mappen, mappen);
  assert.deepEqual(r.templates, lijst);
  json.templates.push({ id: 't-3', ontwerp: { knopen: [{ id: 'q', type: 'onbekend' }] } });
  assert.equal(importeer(json).overgeslagen, 1);
  assert.throws(() => importeer({ templates: [] }), /geen templatebestand/);
});
