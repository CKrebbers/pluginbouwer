// noten.mjs — het notengeval van de null-test (tools/null.mjs) en de toetsen (tests/instrument.test.mjs).
// De noten van het notengeval (seconden, aan/uit, noot): legato (een tweede toets terwijl de eerste vastzit), loslaten
// naar de noot eronder, buiten het bereik van de toon-knop (30 en 90: de parameter klemt), en twee keer dezelfde toets.
export const NOTEN = [[0.1, 'aan', 57], [0.6, 'uit', 57], [0.8, 'aan', 60], [1.0, 'aan', 64], [1.3, 'uit', 64], [1.6, 'uit', 60],
  [1.9, 'aan', 30], [2.3, 'uit', 30], [2.5, 'aan', 90], [2.8, 'aan', 69], [3.0, 'uit', 90], [3.4, 'uit', 69],
  [3.7, 'aan', 62], [3.71, 'aan', 62], [4.2, 'uit', 62]];
// Wat de schil (plugin/src/plugin.cpp, noot/speel met pb::NootStapel) bij elke noot doet, als parameter-events:
// bovenste noot → Noot = 2^((n − 57)/12) en Poort 1; geen noot meer → Poort 0. exp_ is de WASM-export pb_t_exp,
// dus dezelfde code als in de plugin (dsp/pb_math.h).
export function notenAlsEvents(e, noten, nootParam, poortParam) {
  const stapel = [], events = [];
  for (const [smp, wat, n] of noten) {
    const j = stapel.indexOf(n); if (j >= 0) stapel.splice(j, 1);
    if (wat === 'aan') { if (stapel.length === 16) stapel.shift(); stapel.push(n); }
    if (stapel.length) {
      const top = stapel[stapel.length - 1];
      events.push([smp, nootParam, e.pb_t_exp((top - 57) / 12 * Math.LN2)], [smp, poortParam, 1]);
    } else events.push([smp, poortParam, 0]);
  }
  return events;
}
