// audio.js — horen: een bron (testsignaal of eigen bestand) door de ketenmotor in een AudioWorklet. Lukt de worklet
// niet (sommige omgevingen staan geen module-URL's toe), dan dezelfde WASM op de hoofdthread via ScriptProcessor; de
// tool zegt welke van de twee draait.
import { WORKLET_BRON } from './worklet.js';
import { stukjes, ruis, sweep, sinus } from './signalen.js';

export class Speler {
  constructor(wasmModule) {
    this.module = wasmModule; this.ctx = null; this.node = null; this.bron = null; this.modus = '—';
    this.opMeter = () => {}; this.opFout = () => {}; this.buffer = null; this.bronNaam = 'stukjes';
  }
  async start() {
    if (this.ctx) { await this.ctx.resume(); return; }
    this.ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 48000 });
    try {
      // eerst een blob-URL; van schijf (file://) of in een sandbox weigert Chrome die soms, dan een data-URL
      try { await this.ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET_BRON], { type: 'text/javascript' }))); }
      catch { await this.ctx.audioWorklet.addModule('data:text/javascript;base64,' + btoa(unescape(encodeURIComponent(WORKLET_BRON)))); }
      this.node = new AudioWorkletNode(this.ctx, 'pb-motor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
      this.node.port.postMessage({ type: 'module', module: this.module });
      this.node.port.onmessage = (m) => { if (m.data.type === 'meter') this.opMeter(m.data.piek); if (m.data.type === 'fout') this.opFout(m.data.tekst); };
      this.modus = 'AudioWorklet';
    } catch (fout) {
      this.maakTerugval();
      this.modus = 'hoofdthread (worklet niet toegestaan: ' + String(fout.message || fout).slice(0, 60) + ')';
    }
    this.node.connect(this.ctx.destination);
  }
  maakTerugval() {
    // dezelfde motor, hier op de hoofdthread; alleen voor omgevingen zonder worklet
    const e = new WebAssembly.Instance(this.module, {}).exports;
    const sp = this.ctx.createScriptProcessor(1024, 2, 2);
    let h = -1, droog = false, meter = 0;
    sp.onaudioprocess = (ev) => {
      const n = ev.outputBuffer.length, inn = [0, 1].map((c) => ev.inputBuffer.getChannelData(Math.min(c, ev.inputBuffer.numberOfChannels - 1)));
      for (let c = 0; c < 2; c++) {
        const uit = ev.outputBuffer.getChannelData(c);
        if (h < 0 || droog) uit.set(inn[c]);
      }
      if (h >= 0 && !droog) for (let p = 0; p < n; p += 128) {
        for (let c = 0; c < 2; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), 128).set(inn[c].subarray(p, p + 128));
        e.pg_verwerk(h, 2, 128);
        for (let c = 0; c < 2; c++) ev.outputBuffer.getChannelData(c).set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), 128), p);
      }
      const o = ev.outputBuffer.getChannelData(0); let pk = 0; for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(o[i]));
      meter = Math.max(pk, meter * 0.8); this.opMeter(meter);
    };
    this.node = sp;
    this.node.port = { postMessage: (d) => {
      if (d.type === 'keten') {
        const nh = e.pg_maak(this.ctx.sampleRate);
        new Float64Array(e.memory.buffer, e.pg_rij(), d.rij.length).set(d.rij);
        if (e.pg_laad(nh, d.rij.length)) { for (const [i, v] of (d.waarden || []).entries()) e.pg_zet(nh, i, v); if (h >= 0) e.pg_weg(h); h = nh; }
      } else if (d.type === 'param' && h >= 0) e.pg_zet(h, d.i, d.v);
      else if (d.type === 'droog') droog = !!d.aan;
    } };
  }
  laad(rij, waarden, slot = 'A') { this.node?.port.postMessage({ type: 'keten', rij: Float64Array.from(rij), waarden, slot }); }
  zet(i, v, slot = 'A') { this.node?.port.postMessage({ type: 'param', i, v, slot }); }
  kies(slot) { this.node?.port.postMessage({ type: 'kies', slot }); }
  droog(aan) { this.node?.port.postMessage({ type: 'droog', aan }); }

  maakBuffer(naam) {
    const sr = this.ctx.sampleRate, sec = 6;
    const maak = { stukjes: () => stukjes(sr, sec, 0.5), ruis: () => { const x = new Float32Array(sr * sec); x.set(ruis(sr, 0.15, 0.5, 3)); x.set(ruis(sr, 0.15, 0.5, 4), sr * 3); return x; },
      sweep: () => sweep(40, 16000, sr, sec, 0.4), sinus: () => sinus(1000, sr, sec, 0.3) }[naam];
    const x = maak(), b = this.ctx.createBuffer(2, x.length, sr);
    b.copyToChannel(x, 0); b.copyToChannel(x, 1);
    return b;
  }
  async eigenBestand(bestand) {
    await this.start();
    this.buffer = await this.ctx.decodeAudioData(await bestand.arrayBuffer());
    this.bronNaam = 'eigen: ' + bestand.name;
  }
  async speel(naam) {
    await this.start();
    this.stopBron();
    const buf = naam === 'eigen' && this.buffer ? this.buffer : this.maakBuffer(naam);
    this.bron = this.ctx.createBufferSource(); this.bron.buffer = buf; this.bron.loop = true;
    this.bron.connect(this.node); this.bron.start();
  }
  stopBron() { if (this.bron) { try { this.bron.stop(); } catch { /* al gestopt */ } this.bron.disconnect(); this.bron = null; } }
  speelt() { return !!this.bron; }
}
