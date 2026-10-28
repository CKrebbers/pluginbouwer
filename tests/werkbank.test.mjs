// werkbank.test.mjs — samen bouwen met Claude (tool/src/werkbank.js): wat er anders is tussen twee versies in gewone
// woorden, een afdruk die alleen verandert als het geluid verandert, en het nalopen van voorstellen uit de database.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DUB_ECHO, TOETSENPAD } from '../tool/src/archetypen.js';
import { voegIn } from '../tool/src/rollen.js';
import { klankAfdruk, verschil, werkbankVorm, nietMeekijken, bijGeschiedenis, normaliseerVoorstel, openVoorstellen, bijVersies, getal, GESCHIEDENIS, VERSIES } from '../tool/src/werkbank.js';

const kopie = (o) => JSON.parse(JSON.stringify(o));

test('afdruk: verschuiven en hernoemen klinkt hetzelfde, een knop draaien niet', () => {
  const a = kopie(DUB_ECHO), b = kopie(DUB_ECHO);
  b.knopen[2].x += 300; b.naam = 'Anders';
  assert.equal(klankAfdruk(a), klankAfdruk(b));
  b.parameters[0].standaard = 500;
  assert.notEqual(klankAfdruk(a), klankAfdruk(b));
  assert.match(klankAfdruk(a), /^[0-9a-f]{8}$/);
});

test('verschil: hetzelfde ontwerp = niets; verschoven = niets', () => {
  const b = kopie(DUB_ECHO); b.knopen[1].y -= 80;
  assert.deepEqual(verschil(DUB_ECHO, kopie(DUB_ECHO)), []);
  assert.deepEqual(verschil(DUB_ECHO, b), []);
});

test('verschil: een blok erbij, een knop gedraaid, een paneelknop, een kabel, een modulatie', () => {
  const b = voegIn(DUB_ECHO, { type: 'galm' }, { na: 'mix' }).ontwerp;
  b.parameters.find((p) => p.naam === 'Feedback').standaard = 0.875;
  const z = verschil(DUB_ECHO, b);
  assert.ok(z.includes('Reverb erbij'), z.join(' | '));
  assert.ok(z.some((x) => /^Delay: .*Feedback|0,875/.test(x)), z.join(' | '));
  assert.ok(z.some((x) => /^kabel .*→ Reverb erbij$/.test(x)), z.join(' | '));
  assert.ok(z.some((x) => /^kabel Mix → Output weg$/.test(x)), z.join(' | '));
  const c = voegIn(DUB_ECHO, { type: 'lfo' }, { na: 'toon' }).ontwerp;
  assert.ok(verschil(DUB_ECHO, c).some((x) => /^LFO beweegt frequency van Filter$/.test(x)), verschil(DUB_ECHO, c).join(' | '));
  assert.ok(verschil(c, DUB_ECHO).some((x) => /niet meer$/.test(x)));
  assert.ok(verschil(b, DUB_ECHO).includes('Reverb eruit'));
});

test('verschil: een vaste keuze en een knop zonder paneelknop', () => {
  const a = kopie(TOETSENPAD), b = kopie(TOETSENPAD);
  const osc = b.knopen.find((k) => k.type === 'osc');
  if (osc) {
    osc.vast = { ...(osc.vast || {}), golf: 3 };
    assert.ok(verschil(a, b).some((x) => /waveform .* → triangle$/.test(x)), verschil(a, b).join(' | '));
  }
  const d = kopie(DUB_ECHO); d.knopen.find((k) => k.id === 'tape').waarden.asymmetrie = 0.5;
  assert.ok(verschil(DUB_ECHO, d).some((x) => /^Tape: asymmetry 0,0996 → 0,5$/.test(x)), verschil(DUB_ECHO, d).join(' | '));
});

test('getallen zoals in de tool: komma, niet te veel cijfers', () => {
  assert.equal(getal(0.85), '0,85'); assert.equal(getal(375), '375'); assert.equal(getal(12.34), '12,3'); assert.equal(getal(-3), '−3');
});

test('wat de tool doorgeeft: ontwerp, afdruk, laatste wijzigingen; uit = niets van het ontwerp', () => {
  let g = [];
  for (let i = 0; i < GESCHIEDENIS + 5; i++) g = bijGeschiedenis(g, 'stap ' + i, 't' + i);
  assert.equal(g.length, GESCHIEDENIS); assert.equal(g.at(-1).wat, 'stap ' + (GESCHIEDENIS + 4));
  assert.equal(bijGeschiedenis(bijGeschiedenis([], 'Time'), 'Time').length, 1, 'dezelfde knop achter elkaar = één regel');
  assert.equal(bijGeschiedenis([], '').length, 0);
  const w = werkbankVorm(kopie(DUB_ECHO), { wat: 'Reverb erbij', geschiedenis: g });
  assert.equal(w.meekijken, true); assert.equal(w.afdruk, klankAfdruk(DUB_ECHO)); assert.equal(w.naam, 'Dub echo');
  assert.deepEqual(Object.keys(nietMeekijken()).sort(), ['meekijken', 'wanneer']);
});

test('voorstellen uit de database: nagelopen, open op volgorde, status alleen bekende woorden', () => {
  const goed = { titel: 'Meer ruimte', waarom: 'De echo mag in een galm.', ontwerp: voegIn(DUB_ECHO, { type: 'galm' }).ontwerp, basis: klankAfdruk(DUB_ECHO), wanneer: '2026-10-28T13:00:00Z' };
  const v = normaliseerVoorstel(goed, 'v-1');
  assert.equal(v.status, 'open'); assert.equal(v.basis, klankAfdruk(DUB_ECHO));
  assert.equal(normaliseerVoorstel({ ...goed, status: 'gehackt', basis: '<script>' }, 'v-1').status, 'open');
  assert.equal(normaliseerVoorstel({ ...goed, basis: '<script>' }, 'v-1').basis, '');
  assert.equal(normaliseerVoorstel({ ...goed, ontwerp: { knopen: [{ id: 'x', type: 'onbekend' }] } }, 'v-1'), null);
  assert.equal(normaliseerVoorstel(goed, 'a/b'), null);
  const lijst = [{ ...v, id: 'v-2', wanneer: '2026-10-28T14' }, { ...v, id: 'v-3', status: 'afgewezen' }, v];
  assert.deepEqual(openVoorstellen(lijst).map((x) => x.id), ['v-1', 'v-2']);
});

test('versies: de oude komt erbij (een kopie), hooguit ' + VERSIES, () => {
  const o = kopie(DUB_ECHO);
  let l = bijVersies([], o, 'vóór voorstel');
  o.naam = 'veranderd';
  assert.equal(l[0].ontwerp.naam, 'Dub echo', 'een kopie, geen verwijzing');
  for (let i = 0; i < VERSIES + 3; i++) l = bijVersies(l, o, 'x');
  assert.equal(l.length, VERSIES);
});
