// worklet.js — de AudioWorklet die de ketenmotor (WASM) live draait. Als tekst geëxporteerd: de tool maakt er een
// module van (addModule). Twee ketens tegelijk mogelijk ("A" en "B") voor A/B en ABX; wisselen vloeit 20 ms over.
export const WORKLET_BRON = `
class PbMotor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.e = null; this.h = { A: -1, B: -1 }; this.actief = 'A'; this.vorige = null; this.fade = 0;
    this.droog = false; this.meter = 0; this.tel = 0;
    // een fout hier (bv. de WASM die niet instantieert) zou de worklet stil laten doorlopen met alleen de droge ingang;
    // daarom altijd terugmelden, dan zegt de tool het
    this.port.onmessage = (m) => { try { this.bericht(m.data); } catch (f) { this.port.postMessage({ type: 'fout', tekst: String(f && f.message || f) }); } };
    this.port.onmessageerror = () => this.port.postMessage({ type: 'fout', tekst: 'bericht kwam niet aan (messageerror)' });
  }
  bericht(d) {
    if (d.type === 'module') {
      // de motor komt als BYTES en wordt hier gecompileerd: een WebAssembly.Module zelf komt in Chrome niet aan in een
      // AudioWorklet (messageerror, gevonden 15 okt 2026), en dan liep alles droog door. In een worklet mag
      // synchroon compileren (de grens van 4 kB geldt alleen op de hoofdthread).
      this.e = new WebAssembly.Instance(d.bytes ? new WebAssembly.Module(d.bytes) : d.module, {}).exports;
      this.port.postMessage({ type: 'motor' });
      return;
    }
    if (!this.e) return;
    const e = this.e;
    if (d.type === 'keten') {
      const slot = d.slot || 'A';
      const h = e.pg_maak(sampleRate);
      if (h < 0) { this.port.postMessage({ type: 'fout', tekst: 'geen vrije instantie' }); return; }
      new Float64Array(e.memory.buffer, e.pg_rij(), d.rij.length).set(d.rij);
      if (!e.pg_laad(h, d.rij.length)) { e.pg_weg(h); this.port.postMessage({ type: 'fout', tekst: 'keten geweigerd' }); return; }
      for (const [i, v] of (d.waarden || []).entries()) e.pg_zet(h, i, v);
      const oud = this.h[slot];
      this.h[slot] = h;
      if (slot === this.actief && oud >= 0) { this.vorige = oud; this.fade = 0; } else if (oud >= 0) e.pg_weg(oud);
      this.port.postMessage({ type: 'geladen', slot });
    } else if (d.type === 'param') { const h = this.h[d.slot || 'A']; if (h >= 0) e.pg_zet(h, d.i, d.v); }
    else if (d.type === 'kies') { if (d.slot !== this.actief && this.h[d.slot] >= 0) { this.vorige = null; this.wisselNaar = d.slot; this.fade = 0; } }
    else if (d.type === 'droog') this.droog = !!d.aan;
  }
  render(h, inp, n, kanalen) {
    const e = this.e;
    for (let c = 0; c < kanalen; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), n).set(inp[c]);
    e.pg_verwerk(h, kanalen, n);
    return Array.from({ length: kanalen }, (_, c) => new Float32Array(e.memory.buffer, e.pg_uit(h, c), n).slice());
  }
  process(inputs, outputs) {
    const inp = inputs[0], out = outputs[0];
    const n = out[0].length, kanalen = Math.min(2, out.length);
    const bron = [0, 1].slice(0, kanalen).map((c) => (inp && inp[c]) ? inp[c] : (inp && inp[0]) ? inp[0] : new Float32Array(n));
    if (!this.e || this.h[this.actief] < 0 || this.droog) {
      for (let c = 0; c < kanalen; c++) out[c].set(bron[c]);
    } else {
      let y = this.render(this.h[this.actief], bron, n, kanalen);
      const ander = this.wisselNaar ? this.h[this.wisselNaar] : this.vorige;
      if (ander !== null && ander !== undefined && ander >= 0) {
        const z = this.render(ander, bron, n, kanalen);
        const L = Math.round(0.02 * sampleRate);
        for (let i = 0; i < n; i++) {
          const g = Math.min(1, (this.fade + i) / L);
          const a = Math.sqrt(1 - g), b = Math.sqrt(g);
          for (let c = 0; c < kanalen; c++) y[c][i] = this.wisselNaar ? a * y[c][i] + b * z[c][i] : b * y[c][i] + a * z[c][i];
        }
        this.fade += n;
        if (this.fade >= L) {
          if (this.wisselNaar) { this.actief = this.wisselNaar; this.wisselNaar = null; }
          else { this.e.pg_weg(this.vorige); this.vorige = null; }
        }
      }
      for (let c = 0; c < kanalen; c++) out[c].set(y[c]);
    }
    let p = 0; for (let i = 0; i < n; i++) p = Math.max(p, Math.abs(out[0][i]));
    this.meter = Math.max(p, this.meter * 0.95);
    if (++this.tel % 8 === 0) this.port.postMessage({ type: 'meter', piek: this.meter, actief: this.actief });
    return true;
  }
}
registerProcessor('pb-motor', PbMotor);
`;
