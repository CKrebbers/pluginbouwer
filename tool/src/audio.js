// audio.js — horen: een bron (testsignaal of eigen bestand) door de ketenmotor in een AudioWorklet. Lukt de worklet
// niet (sommige omgevingen staan geen module-URL's toe, of de motor start er niet), dan dezelfde WASM op de
// hoofdthread via ScriptProcessor; de tool zegt welke van de twee draait. "motorDraait" is pas waar als de motor zelf
// heeft gemeld dat hij geladen is: zonder motor loopt de worklet droog door (een effect klinkt als zijn ingang, een
// instrument zwijgt) — dat was tot 15 okt 2026 ongemerkt het geval.
import { WORKLET_BRON } from './worklet.js';
import { stukjes, ruis, sweep, sinus } from './signalen.js';

export class Speler {
  constructor(wasmModule, wasmBytes) {
    this.module = wasmModule; this.bytes = wasmBytes; this.ctx = null; this.node = null; this.bron = null; this.modus = '—';
    this.motorDraait = false;
    this.opMeter = () => {}; this.opFout = () => {}; this.buffer = null; this.bronNaam = 'stukjes';
    this.grooveData = null;   // [L, R] uit de motor (archetypen.js GROOVE), gezet door de tool
  }
  // één keer opstarten, ook als er twee keer snel op speel wordt gedrukt; daarna alleen hervatten
  start() {
    if (!this.gestart) this.gestart = this.opstarten();
    return this.gestart.then(() => this.ctx.resume());
  }
  async opstarten() {
    // iPhone/iPad: zonder dit zwijgt Web Audio als de stilteschakelaar aan staat (Audio Session API, Safari 17+)
    try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch { /* oudere Safari */ }
    this.ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 48000 });
    try {
      // eerst een blob-URL; van schijf (file://) of in een sandbox weigert Chrome die soms, dan een data-URL
      try { await this.ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET_BRON], { type: 'text/javascript' }))); }
      catch { await this.ctx.audioWorklet.addModule('data:text/javascript;base64,' + btoa(unescape(encodeURIComponent(WORKLET_BRON)))); }
      this.node = new AudioWorkletNode(this.ctx, 'pb-motor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
      let gemeld;
      const klaar = new Promise((r) => { gemeld = r; setTimeout(() => r('geen antwoord binnen 2 s'), 2000); });
      this.node.port.onmessage = (m) => {
        if (m.data.type === 'meter') this.opMeter(m.data.piek);
        if (m.data.type === 'motor') gemeld(true);
        if (m.data.type === 'stil') this.stilBevestigd = true;
        if (m.data.type === 'fout') { this.opFout(m.data.tekst); gemeld(m.data.tekst); }
      };
      this.node.port.postMessage({ type: 'module', bytes: this.bytes.slice().buffer });
      const r = await klaar;
      if (r !== true) throw new Error('de motor startte niet in de worklet: ' + r);
      this.modus = 'AudioWorklet'; this.motorDraait = true;
    } catch (fout) {
      try { this.node?.disconnect(); } catch { /* */ }
      this.maakTerugval();
      this.modus = 'hoofdthread (' + String(fout.message || fout).slice(0, 80) + ')';
    }
    this.node.connect(this.ctx.destination);
  }
  maakTerugval() {
    // dezelfde motor, hier op de hoofdthread; alleen voor omgevingen zonder worklet
    const e = new WebAssembly.Instance(this.module, {}).exports;
    const sp = this.ctx.createScriptProcessor(1024, 2, 2);
    let h = -1, droog = false, meter = 0, stil = false;
    sp.onaudioprocess = (ev) => {
      const n = ev.outputBuffer.length, inn = [0, 1].map((c) => ev.inputBuffer.getChannelData(Math.min(c, ev.inputBuffer.numberOfChannels - 1)));
      for (let c = 0; c < 2; c++) {
        const uit = ev.outputBuffer.getChannelData(c);
        if (stil) uit.fill(0); else if (h < 0 || droog) uit.set(inn[c]);
      }
      if (stil) { this.opMeter(0); return; }
      if (h >= 0 && !droog) for (let p = 0; p < n; p += 128) {
        for (let c = 0; c < 2; c++) new Float32Array(e.memory.buffer, e.pg_in(h, c), 128).set(inn[c].subarray(p, p + 128));
        e.pg_verwerk(h, 2, 128);
        for (let c = 0; c < 2; c++) ev.outputBuffer.getChannelData(c).set(new Float32Array(e.memory.buffer, e.pg_uit(h, c), 128), p);
      }
      const o = ev.outputBuffer.getChannelData(0); let pk = 0; for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(o[i]));
      meter = Math.max(pk, meter * 0.8); this.opMeter(meter);
    };
    this.node = sp; this.motorDraait = true;
    this.node.port = { postMessage: (d) => {
      if (d.type === 'keten') {
        const nh = e.pg_maak(this.ctx.sampleRate);
        new Float64Array(e.memory.buffer, e.pg_rij(), d.rij.length).set(d.rij);
        if (e.pg_laad(nh, d.rij.length)) { for (const [i, v] of (d.waarden || []).entries()) e.pg_zet(nh, i, v); if (h >= 0) e.pg_weg(h); h = nh; }
      } else if (d.type === 'param' && h >= 0) e.pg_zet(h, d.i, d.v);
      else if (d.type === 'droog') droog = !!d.aan;
      else if (d.type === 'stil') { stil = !!d.aan; if (stil && h >= 0) e.pg_reset(h); this.stilBevestigd = stil; }
    } };
  }
  laad(rij, waarden, slot = 'A') { this.node?.port.postMessage({ type: 'keten', rij: Float64Array.from(rij), waarden, slot }); }
  zet(i, v, slot = 'A') { this.node?.port.postMessage({ type: 'param', i, v, slot }); }
  kies(slot) { this.node?.port.postMessage({ type: 'kies', slot }); }
  droog(aan) { this.node?.port.postMessage({ type: 'droog', aan }); }

  maakBuffer(naam) {
    const sr = this.ctx.sampleRate, sec = 6;
    if (naam === 'groove' && this.grooveData) {
      const g = this.grooveData, b = this.ctx.createBuffer(2, g[0].length, sr);
      b.copyToChannel(g[0], 0); b.copyToChannel(g[1] || g[0], 1);
      return b;
    }
    const maak = { stukjes: () => stukjes(sr, sec, 0.5), groove: () => stukjes(sr, sec, 0.5), ruis: () => { const x = new Float32Array(sr * sec); x.set(ruis(sr, 0.15, 0.5, 3)); x.set(ruis(sr, 0.15, 0.5, 4), sr * 3); return x; },
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
    clearTimeout(this.slaap);
    this.stilBevestigd = false;
    this.node?.port.postMessage({ type: 'stil', aan: false });
    if (naam === 'geen') {
      // een instrument: geen invoer, alleen een stille bron die de motor laat lopen (en "speelt" waar maakt)
      this.bron = this.ctx.createConstantSource(); this.bron.offset.value = 0;
      this.bron.connect(this.node); this.bron.start();
      return;
    }
    const buf = naam === 'eigen' && this.buffer ? this.buffer : this.maakBuffer(naam);
    this.bron = this.ctx.createBufferSource(); this.bron.buffer = buf; this.bron.loop = true;
    this.bron.connect(this.node); this.bron.start();
  }
  // Stoppen = echt stil: de bron eraf, de motor fadet uit en zet zijn ketens terug (anders speelt een instrument of een
  // echo die doorgaat zonder invoer gewoon verder), en daarna slaapt de audio (spaart de batterij van een telefoon).
  stopBron() {
    if (this.bron) { try { this.bron.stop(); } catch { /* al gestopt */ } this.bron.disconnect(); this.bron = null; }
    if (!this.node) return;
    this.node.port.postMessage({ type: 'stil', aan: true });
    clearTimeout(this.slaap);
    this.slaap = setTimeout(() => { if (!this.bron && this.ctx?.state === 'running') this.ctx.suspend().catch(() => {}); }, 120);
  }
  speelt() { return !!this.bron; }
}
