// rollen.test.mjs — de tool in gewone woorden (tool/src/rollen.js): elk blok heeft een familie, een zin en uitleg per
// knop; invoegen zet een blok of combinatie op de goede plek zonder de keten te breken; netjes neerleggen laat geen
// blokken overlappen; de beschrijving volgt de weg van het geluid.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BIB, compileer } from '../tool/src/keten.js';
import { ARCHETYPEN, INSTRUMENTEN, EFFECTEN, DUB_ECHO } from '../tool/src/archetypen.js';
import { ROL, FAMILIES, RECEPTEN, voegIn, ordenen, overlap, blokMaat, beschrijf, poortInfo, past, gewoneNaam } from '../tool/src/rollen.js';

const ALLE = [...new Map([...ARCHETYPEN, ...INSTRUMENTEN, ...EFFECTEN].map((o) => [o.id, o])).values()];
const LEEG_E = { naam: 'Leeg effect', id: 'e', knopen: [{ id: 'in', type: 'in', x: 40, y: 200 }, { id: 'uit', type: 'uit', x: 960, y: 200 }], kabels: [], parameters: [], modulaties: [] };
const LEEG_I = { naam: 'Leeg instrument', id: 'i', knopen: [{ id: 'uit', type: 'uit', x: 960, y: 200 }], kabels: [], parameters: [], modulaties: [] };
const WAT = [...Object.keys(BIB).filter((t) => !BIB[t].vast).map((type) => ({ type })), ...RECEPTEN.map((r) => ({ recept: r.id }))];
// vaktaal die een producer niet nodig heeft (Engelse termen als feedback, VCA of cutoff mogen: die staan ook in een DAW)
const JARGON = /\b(ADAA|TPT|RBJ|FDN|polyBLEP|Lagrange|tanh|Karplus|Zavalishin|dBFS|sample)\b/i;

test('elk blok heeft een familie, een zin zonder vaktaal, en uitleg bij elke knop', () => {
  const families = new Set(FAMILIES.map((f) => f.id));
  for (const [type, b] of Object.entries(BIB)) {
    const r = ROL[type];
    assert.ok(r, `${type} staat niet in ROL`);
    assert.ok(families.has(r.familie), `${type}: onbekende familie ${r.familie}`);
    assert.ok(r.kort && r.kort.length < 110, `${type}: zin ontbreekt of is te lang`);
    assert.ok(!JARGON.test(r.kort), `${type}: vaktaal in "${r.kort}"`);
    for (const k of b.knoppen) assert.ok(r.knoppen[k.id] && !JARGON.test(r.knoppen[k.id]), `${type}: geen gewone uitleg bij knop ${k.id}`);
    for (const p of Object.keys(r.poorten || {})) assert.ok(b.in.includes(p) || b.uit.includes(p), `${type}: poort ${p} bestaat niet`);
    for (const [ander] of r.goedMet || []) assert.ok(ander === 'meng' || (ander.startsWith('recept:') ? RECEPTEN.some((x) => 'recept:' + x.id === ander) : !!BIB[ander]), `${type}: goedMet ${ander} bestaat niet`);
    assert.ok(!/[()]/.test(gewoneNaam(type)), `${type}: gewone naam "${gewoneNaam(type)}" heeft nog vaktaal tussen haakjes`);
  }
});

test('sturing past op sturing, geluid op geluid, "keer" op allebei', () => {
  assert.equal(poortInfo('sequencer', 'poort').soort, 'sturing');
  assert.equal(poortInfo('osc', 'uit').soort, 'geluid');
  assert.ok(past('sequencer', 'poort', 'trommel', 'poort'));
  assert.ok(!past('osc', 'uit', 'trommel', 'poort'));
  assert.ok(past('omhulling', 'omhulling', 'vermenigvuldig', 'b') && past('osc', 'uit', 'vermenigvuldig', 'b'));
});

test('invoegen: elk blok en elke combinatie, in elk voorbeeld en in leeg, breekt niets', () => {
  for (const basis of [LEEG_E, LEEG_I, ...ALLE]) {
    const voor = compileer(basis).fouten.length, snap = JSON.stringify(basis);
    for (const wat of WAT) for (const na of [null, ...basis.knopen.map((k) => k.id)]) {
      const r = voegIn(basis, wat, { na });
      const c = compileer(r.ontwerp);
      assert.ok(c.fouten.length <= voor, `${basis.naam} + ${JSON.stringify(wat)} na ${na}: ${c.fouten.join('; ')}`);
      assert.ok(r.wat && r.nieuw.length, `${basis.naam} + ${JSON.stringify(wat)}: geen uitleg`);
      assert.equal(JSON.stringify(basis), snap, 'het origineel blijft onaangeroerd');
      for (const k of r.ontwerp.kabels) assert.ok(r.ontwerp.knopen.some((n) => n.id === k.van[0]) && r.ontwerp.knopen.some((n) => n.id === k.naar[0]), 'kabel naar een blok dat niet bestaat');
      const naar = r.ontwerp.kabels.map((k) => k.naar.join('.'));
      assert.equal(new Set(naar).size, naar.length, `${basis.naam} + ${JSON.stringify(wat)}: twee kabels op één ingang`);
    }
  }
});

test('invoegen in een leeg effect: het geluid loopt van ingang via het nieuwe blok naar de uitgang', () => {
  for (const wat of WAT.filter((w) => w.type ? ROL[w.type].familie !== 'bron' && !['beweging', 'spelen'].includes(ROL[w.type].familie) : RECEPTEN.find((r) => r.id === w.recept).ingang.length)) {
    const o = voegIn(LEEG_E, wat).ontwerp;
    assert.ok(beschrijf(o).startsWith('Weg van het geluid: Input → '), `${JSON.stringify(wat)}: ${beschrijf(o)}`);
    assert.ok(!o.kabels.some((k) => k.van[0] === 'in' && k.naar[0] === 'uit'), 'de oude kabel ingang → uitgang is weg');
  }
});

test('invoegen na een blok: het komt direct achter dat blok', () => {
  const r = voegIn(DUB_ECHO, { type: 'galm' }, { na: 'mix' });
  const galm = r.nieuw[0];
  assert.ok(r.ontwerp.kabels.some((k) => k.van[0] === 'mix' && k.naar[0] === galm));
  assert.ok(r.ontwerp.kabels.some((k) => k.van[0] === galm && k.naar[0] === 'uit'));
  assert.match(r.wat, /tussen Mix en Output/);
});

test('een bron bij een bestaand geluid wordt opgeteld, en hoort de noten als er al iemand speelt', () => {
  const basis = INSTRUMENTEN.find((o) => o.id === 'acidlijn');
  const r = voegIn(basis, { type: 'snaar' });
  assert.match(r.wat, /opgeteld/);
  const snaar = r.nieuw[0];
  assert.ok(r.ontwerp.kabels.some((k) => k.naar[0] === snaar && k.naar[1] === 'poort'), 'de snaar krijgt de slag van de sequencer');
});

test('een LFO op een gekozen blok beweegt zijn frequentie', () => {
  const r = voegIn(DUB_ECHO, { type: 'lfo' }, { na: 'toon' });
  assert.ok(r.ontwerp.modulaties.some((m) => m.doel[0] === 'toon' && m.doel[1] === 'freq'));
  assert.match(r.wat, /beweegt nu frequency van Filter/);
});

test('"Synth voice" speelt meteen: keyboard met Note en Gate, één toetsenbord per ontwerp', () => {
  const r = voegIn(LEEG_I, { recept: 'noot' });
  assert.equal(compileer(r.ontwerp).fouten.length, 0);
  assert.deepEqual(r.ontwerp.parameters.map((p) => p.naam), ['Note', 'Gate']);
  const twee = voegIn(r.ontwerp, { recept: 'noot' }).ontwerp;
  assert.equal(twee.knopen.filter((k) => k.type === 'toetsen').length, 1, 'het toetsenbord wordt gedeeld');
});

test('netjes neerleggen: geen enkel voorbeeld overlapt daarna, en invoegen + neerleggen ook niet', () => {
  for (const o of ALLE) {
    assert.equal(overlap(ordenen(o, blokMaat), blokMaat), null, o.naam);
    for (const wat of WAT) assert.equal(overlap(ordenen(voegIn(o, wat).ontwerp, blokMaat), blokMaat), null, `${o.naam} + ${JSON.stringify(wat)}`);
  }
  const d = ordenen(DUB_ECHO, blokMaat), x = (id) => d.knopen.find((k) => k.id === id).x;
  assert.ok(x('in') < x('tape') && x('tape') < x('lijn') && x('lijn') < x('toon') && x('toon') < x('uit'), 'van links naar rechts zoals het geluid loopt');
  assert.ok(d.knopen.find((k) => k.id === 'band').y < d.knopen.find((k) => k.id === 'lijn').y, 'de bandsnelheid staat boven de vertraging');
  assert.deepEqual(compileer(d).rij, compileer(DUB_ECHO).rij, 'neerleggen verandert niets aan het geluid');
});

test('de beschrijving volgt de weg van het geluid en noemt de lus', () => {
  const t = beschrijf(DUB_ECHO);
  assert.match(t, /^Weg van het geluid: Input → Tape → Delay → Low cut → Filter → Output\./);
  assert.match(t, /gaat terug/);
  assert.match(beschrijf(EFFECTEN.find((o) => o.id === 'sidechain') ?? ALLE.find((o) => /sidechain/i.test(o.naam))), /Compressor luistert naar Drum/);
});

test('mengen: een Mix om een galm, met het origineel op "origineel"', () => {
  const basis = voegIn(LEEG_E, { type: 'galm' }).ontwerp, galm = basis.knopen.find((k) => k.type === 'galm').id;
  const r = voegIn(basis, { meng: true }, { na: galm });
  const mix = r.nieuw[0];
  assert.ok(r.ontwerp.kabels.some((k) => k.van[0] === 'in' && k.naar.join() === `${mix},droog`));
  assert.ok(r.ontwerp.kabels.some((k) => k.van[0] === galm && k.naar.join() === `${mix},nat`));
  assert.ok(r.ontwerp.kabels.some((k) => k.van[0] === mix && k.naar[0] === 'uit'));
  assert.equal(compileer(r.ontwerp).fouten.length, 0);
  assert.match(beschrijf(r.ontwerp), /Input → Reverb → Output/);
});

test('loslaten op een kabel: het blok komt precies op die kabel', () => {
  const kabel = DUB_ECHO.kabels.find((k) => k.van[0] === 'tape' && k.naar[0] === 'lijn');
  const r = voegIn(DUB_ECHO, { type: 'vouwer' }, { kabel });
  assert.match(r.wat, /tussen Tape en Delay/);
  assert.equal(compileer(r.ontwerp).fouten.length, 0);
});

test('elke suggestie "past er goed bij" sluit zich aan op dat blok', () => {
  for (const [type, rol] of Object.entries(ROL)) for (const [wie] of rol.goedMet || []) {
    const leeg = ['bron', 'beweging', 'spelen'].includes(rol.familie) ? LEEG_I : LEEG_E;
    const basis = voegIn(leeg, { type }).ontwerp;
    const na = basis.knopen.find((k) => k.type === type).id;
    const wat = wie === 'meng' ? { meng: true } : wie.startsWith('recept:') ? { recept: wie.slice(7) } : { type: wie };
    const r = voegIn(basis, wat, { na });
    assert.ok(compileer(r.ontwerp).fouten.length <= compileer(basis).fouten.length, `${type} + ${wie}: ${compileer(r.ontwerp).fouten}`);
    assert.ok(!/nog aan|sleep zijn uitgang|sluit "/.test(r.wat), `${type} + ${wie}: "${r.wat}" sluit zichzelf niet aan`);
  }
});
