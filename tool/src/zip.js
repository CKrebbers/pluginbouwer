// zip.js — een ZIP zonder compressie (store), voor de export van het bouwpakket in de browser. Eigen code, geen
// afhankelijkheden. Bestanden: { pad: string | Uint8Array }.
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(b) { let c = 0xffffffff; for (let i = 0; i < b.length; i++) c = CRC[(c ^ b[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }

export function zip(bestanden, map = '') {
  const enc = new TextEncoder(), delen = [], centraal = [];
  let offset = 0;
  for (const [pad0, inhoud] of Object.entries(bestanden)) {
    const pad = enc.encode((map ? map + '/' : '') + pad0);
    const data = typeof inhoud === 'string' ? enc.encode(inhoud) : inhoud;
    const crc = crc32(data);
    const kop = new DataView(new ArrayBuffer(30));
    kop.setUint32(0, 0x04034b50, true); kop.setUint16(4, 20, true); kop.setUint16(6, 0x0800, true); kop.setUint16(8, 0, true);
    kop.setUint16(10, 0, true); kop.setUint16(12, 0x21, true); kop.setUint32(14, crc, true);
    kop.setUint32(18, data.length, true); kop.setUint32(22, data.length, true); kop.setUint16(26, pad.length, true); kop.setUint16(28, 0, true);
    delen.push(new Uint8Array(kop.buffer), pad, data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true);
    c.setUint16(10, 0, true); c.setUint16(12, 0, true); c.setUint16(14, 0x21, true); c.setUint32(16, crc, true);
    c.setUint32(20, data.length, true); c.setUint32(24, data.length, true); c.setUint16(28, pad.length, true);
    c.setUint32(38, pad0.endsWith('.sh') ? (0o100755 << 16) >>> 0 : (0o100644 << 16) >>> 0, true); c.setUint32(42, offset, true);
    centraal.push(new Uint8Array(c.buffer), pad);
    offset += 30 + pad.length + data.length;
  }
  const grootte = centraal.reduce((s, d) => s + d.length, 0);
  const eind = new DataView(new ArrayBuffer(22));
  eind.setUint32(0, 0x06054b50, true); eind.setUint16(8, centraal.length / 2, true); eind.setUint16(10, centraal.length / 2, true);
  eind.setUint32(12, grootte, true); eind.setUint32(16, offset, true);
  const alles = [...delen, ...centraal, new Uint8Array(eind.buffer)];
  const uit = new Uint8Array(alles.reduce((s, d) => s + d.length, 0));
  let p = 0; for (const d of alles) { uit.set(d, p); p += d.length; }
  return uit;
}
