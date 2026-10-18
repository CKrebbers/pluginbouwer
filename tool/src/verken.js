// verken.js — varianten van een ontwerp (zoals "Verken" in de Stackbouwer). Deterministisch: een zaad geeft altijd
// dezelfde varianten. Wildheid: subtiel (knoppen ±15 %), flink (±40 % en soms een blok erbij of eraf), wild (alles).
import { BIB, compileer } from './keten.js';

function toeval(zaad) {
  let s = zaad >>> 0 || 1;
  return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
}
const kloon = (o) => JSON.parse(JSON.stringify(o));
const exact = (v) => Math.fround(v) === v ? v : Math.round(v * 8) / 8;   // terug naar iets dat exact in float32 past

export function varianten(ontwerp, { zaad = 1, wildheid = 'subtiel', aantal = 6 } = {}) {
  const r = toeval(zaad), uit = [];
  const mate = { subtiel: 0.15, flink: 0.4, wild: 0.8 }[wildheid] ?? 0.15;
  for (let n = 0; n < aantal && uit.length < aantal * 3; n++) {
    const v = kloon(ontwerp), wat = [];
    v.naam = `${ontwerp.naam} · ${n + 1}`;
    for (const p of v.parameters) {
      const span = p.max - p.min;
      const nieuw = Math.min(p.max, Math.max(p.min, p.standaard + (r() * 2 - 1) * mate * span));
      const ex = exact(Math.round(nieuw * 1000) / 1000);
      if (Math.abs(ex - p.standaard) > span * 0.03) { wat.push(`${p.naam} ${fmt(p.standaard)} → ${fmt(ex)}`); p.standaard = ex; }
      const kn = v.knopen.find((k) => k.id === p.knoop); if (kn) kn.waarden = { ...(kn.waarden || {}), [p.knop]: p.standaard };
    }
    if (wildheid !== 'subtiel' && r() < 0.5) {
      // een blok erbij: bandsnelheid op een vertraging zonder beweging, of een tweede filter in de lus
      const lijn = v.knopen.find((k) => k.type === 'vertraging');
      if (lijn && !v.kabels.some((k) => k.naar[0] === lijn.id && k.naar[1] === 'beweging')) {
        v.knopen.push({ id: 'band' + n, type: 'bandsnelheid', x: lijn.x, y: lijn.y - 140, waarden: { diepte: 0.5 } });
        v.kabels.push({ van: ['band' + n, 'snelheid'], naar: [lijn.id, 'beweging'] });
        wat.push('bandsnelheid erbij');
      } else if (lijn && r() < 0.5) {
        const naLijn = v.kabels.find((k) => k.van[0] === lijn.id);
        if (naLijn) {
          const id = 'kleur' + n;
          v.knopen.push({ id, type: 'laagdoor1', x: lijn.x + 70, y: lijn.y + 110, waarden: { freq: exact(Math.round(2000 + r() * 6000)) } });
          v.kabels.push({ van: [id, 'uit'], naar: [...naLijn.naar] });
          naLijn.naar = [id, 'in'];
          wat.push('extra hoog-weg in de lus');
        }
      }
    }
    if (compileer(v).fouten.length === 0 && wat.length) uit.push({ ontwerp: v, wat: wat.join(', ') });
  }
  return uit.slice(0, aantal);
}
const fmt = (x) => (Math.abs(x) >= 100 ? Math.round(x) : Math.round(x * 100) / 100).toString().replace('.', ',');
export { BIB };
