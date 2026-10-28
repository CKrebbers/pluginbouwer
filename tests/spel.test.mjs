// spel.test.mjs — het spel (tool/src/spel.js): elke proefopstelling klinkt en het blok maakt hoorbaar verschil; de
// knop van Draai na doet iets; elke opdracht faalt op het begin en slaagt met een oplossing die je in de tool met
// een paar tikken bouwt (voegIn); de rondes zijn eerlijk (het goede antwoord zit erbij, geen dubbele keuzes).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BIB, compileer } from '../tool/src/keten.js';
import { voegIn, ROL, gewoneNaam } from '../tool/src/rollen.js';
import { zinZonderNaam, PROEF, HOORBAAR, proefOntwerp, LEVELS, OPDRACHTEN, maakRondes, draaiScore, knopWaarde, knopStand, nieuweStand, antwoord, levelKlaar, open, gekendAantal, GEKEND, RONDES_PER_LEVEL } from '../tool/src/spel.js';
import { laadMotor, render, groove, stilte } from './lib/keten.mjs';

const E = laadMotor(), SR = 48000;
const heeftIngang = (o) => o.knopen.some((k) => k.type === 'in');
function hoor(o, waarden = {}) {
  const c = compileer(o);
  assert.equal(c.fouten.length, 0, o.naam + ': ' + c.fouten.join('; '));
  const w = Object.fromEntries(c.params.map((p, i) => [i, p.standaard])); Object.assign(w, waarden);
  return render(c.rij, heeftIngang(o) ? groove(SR, 3) : stilte(SR, 3), { sr: SR, waarden: w })[0];
}
const rmsDb = (y, van = SR) => { let s = 0; for (let i = van; i < y.length; i++) s += y[i] * y[i]; return 10 * Math.log10(s / (y.length - van) + 1e-30); };
const verschilDb = (a, b, van = SR) => { let s = 0, t = 0; for (let i = van; i < a.length; i++) { s += (a[i] - b[i]) ** 2; t += b[i] ** 2; } return 10 * Math.log10(s / (t + 1e-30) + 1e-30); };

test('elke proefopstelling klinkt, en het blok erin maakt een hoorbaar verschil', () => {
  const droog = groove(SR, 3)[0];
  for (const type of HOORBAAR) {
    const y = hoor(proefOntwerp(type));
    assert.ok(rmsDb(y) > -45, `${type}: te zacht (${rmsDb(y).toFixed(1)} dB)`);
    if (heeftIngang(proefOntwerp(type))) assert.ok(verschilDb(y, droog) > -20, `${type}: klinkt bijna als het origineel (${verschilDb(y, droog).toFixed(1)} dB)`);
  }
});

test('twee proefopstellingen klinken nooit hetzelfde', () => {
  const geluid = Object.fromEntries(HOORBAAR.map((t) => [t, hoor(proefOntwerp(t))]));
  for (const a of HOORBAAR) for (const b of HOORBAAR) if (a < b) assert.ok(verschilDb(geluid[a], geluid[b]) > -12, `${a} en ${b} lijken te veel op elkaar`);
});

test('Draai na: de knop doet over zijn bereik hoorbaar iets, en de score klopt', () => {
  for (const type of HOORBAAR.filter((t) => PROEF[t].knop)) {
    const knop = PROEF[type].knop, o = proefOntwerp(type, { knop });
    assert.equal(o.parameters.length, 1);
    const a = hoor(o, { 0: knopWaarde(type, knop, 0.2) }), b = hoor(o, { 0: knopWaarde(type, knop, 0.8) });
    assert.ok(verschilDb(a, b) > -15, `${type}.${knop}: 0,2 en 0,8 klinken bijna hetzelfde (${verschilDb(a, b).toFixed(1)} dB)`);
    assert.ok(Math.abs(knopStand(type, knop, knopWaarde(type, knop, 0.37)) - 0.37) < 1e-9);
    const doel = knopWaarde(type, knop, 0.5);
    assert.equal(draaiScore(type, knop, doel, doel).sterren, 3);
    assert.equal(draaiScore(type, knop, knopWaarde(type, knop, 0.9), doel).sterren, 0);
  }
});

// Per opdracht een oplossing zoals je hem in de tool maakt: een paar keer + en hooguit één knop.
const knop = (o, type, id, v) => { o.knopen.find((k) => k.type === type).waarden[id] = v; return o; };
const OPLOSSING = {
  geluid: (o) => voegIn(o, { type: 'osc' }).ontwerp,
  donker: (o) => knop(voegIn(o, { type: 'laagdoor1' }).ontwerp, 'laagdoor1', 'freq', 800),
  echo: (o) => voegIn(o, { recept: 'echo' }).ontwerp,
  wiebel: (o) => { const r = voegIn(o, { type: 'svf' }); return voegIn(r.ontwerp, { type: 'lfo' }, { na: r.nieuw[0] }).ontwerp; },
  noot: (o) => voegIn(o, { recept: 'noot' }).ontwerp,
  twee: (o) => voegIn(voegIn(o, { type: 'osc' }).ontwerp, { type: 'ruis' }).ontwerp,
  dub: (o) => {
    const r = voegIn(o, { recept: 'echo' }), som = r.ontwerp.knopen.find((k) => k.type === 'som').id;
    const t = voegIn(r.ontwerp, { type: 'tape' }, { na: som }).ontwerp;
    return knop(t, 'versterker', 'hoeveel', 1.2);
  },
};
test('elke opdracht: het begin is niet gehaald, de oplossing wel; een halve oplossing niet', () => {
  for (const [id, op] of Object.entries(OPDRACHTEN)) {
    assert.equal(op.controleer(E, op.begin()).gehaald, false, id + ': het begin telt al als gehaald');
    const r = op.controleer(E, OPLOSSING[id](op.begin()));
    assert.equal(r.gehaald, true, `${id}: de oplossing haalt het niet (${r.waarom})`);
    assert.ok(op.doel && op.hint && r.waarom);
  }
  // halve oplossingen die er bijna op lijken
  assert.equal(OPDRACHTEN.donker.controleer(E, voegIn(OPDRACHTEN.donker.begin(), { type: 'laagdoor1' }).ontwerp).gehaald, false, 'hoog weg op 4 kHz is nog niet donker genoeg');
  assert.equal(OPDRACHTEN.echo.controleer(E, voegIn(OPDRACHTEN.echo.begin(), { type: 'vertraging' }).ontwerp).gehaald, false, 'een vertraging zonder origineel');
  assert.equal(OPDRACHTEN.noot.controleer(E, voegIn(voegIn(OPDRACHTEN.noot.begin(), { type: 'osc' }).ontwerp, { type: 'toetsen' }).ontwerp).gehaald, false, 'toetsen zonder omhulling klinken door');
  assert.equal(OPDRACHTEN.dub.controleer(E, voegIn(OPDRACHTEN.dub.begin(), { recept: 'echo' }).ontwerp).gehaald, false, 'een gewone echo sterft uit');
});

test('rondes: acht per level, eerlijk, en hetzelfde zaad = dezelfde rondes', () => {
  const alle = Object.keys(BIB).filter((t) => !BIB[t].vast);
  assert.deepEqual([...new Set(LEVELS.slice(0, -1).flatMap((l) => l.blokken))].sort(), alle.sort(), 'elk blok zit in een level');
  for (const l of LEVELS) for (const zaad of [1, 2, 3, 99]) {
    const rondes = maakRondes(l.id, zaad);
    assert.equal(rondes.length, RONDES_PER_LEVEL);
    assert.deepEqual(rondes, maakRondes(l.id, zaad));
    for (const r of rondes) {
      assert.ok(l.blokken.includes(r.blok), `${l.id}: ${r.blok} hoort niet in dit level`);
      if (r.keuzes) { assert.ok(r.keuzes.includes(r.blok)); assert.equal(new Set(r.keuzes).size, r.keuzes.length); assert.ok(r.keuzes.length >= 3); }
      if (r.soort === 'hoor' || r.soort === 'draai') assert.ok(HOORBAAR.includes(r.blok));
      if (r.soort === 'draai') assert.ok(draaiScore(r.blok, r.knop, r.begin, r.doel).sterren < 3, 'Draai na begint niet al goed');
    }
  }
});

test('voortgang: gekend na drie keer goed, sterren, levels gaan open', () => {
  let s = nieuweStand();
  assert.ok(open(s, 'bron') && !open(s, 'kleur'));
  for (let i = 0; i < GEKEND; i++) s = antwoord(s, 'osc', true);
  s = antwoord(s, 'ruis', false);
  assert.equal(gekendAantal(s), 1);
  assert.equal(s.reeks, 0); assert.equal(s.besteReeks, GEKEND);
  const k = levelKlaar(s, 'bron', 6);
  assert.equal(k.sterren, 2);
  assert.ok(open(k.stand, 'kleur'));
  assert.equal(levelKlaar(k.stand, 'bron', 2).stand.levels.bron.sterren, 2, 'een slechtere poging neemt geen sterren af');
});

test('Wie ben ik?: de zin verraadt de naam van het blok niet', () => {
  for (const t of Object.keys(BIB).filter((x) => !BIB[x].vast)) {
    const z = zinZonderNaam(t, ROL[t].kort).toLowerCase();
    for (const w of gewoneNaam(t).toLowerCase().split(/\s+/).filter((w) => w.length >= 4)) assert.ok(!z.includes(w.slice(0, -1)), `${t}: "${z}" noemt nog "${w}"`);
  }
  assert.equal(zinZonderNaam('trommel', ROL.trommel.kort), 'Eén ___: kick, tom, snare of hihat.');
});
