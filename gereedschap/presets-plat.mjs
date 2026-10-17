// presets-plat.mjs <map> — de fabriekspresets uit <map>/*.json plat, één regel per preset, in de volgorde die de
// plugin moet aanbieden ("standaard" eerst, de rest op bestandsnaam):
//   sleutel<TAB>naam<TAB>uitleg<TAB>Param=waarde<TAB>…
// Voor plugin/toetsen/preset-toets en au-preset-toets: zo toetsen die tegen de JSON zelf, niet tegen de tabel die
// CMake ervan maakte.
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

const map = process.argv[2];
if (!map) { console.error("gebruik: node presets-plat.mjs <map>"); process.exit(2); }
const namen = readdirSync(map).filter(f => f.endsWith(".json")).sort();
const volgorde = namen.includes("standaard.json") ? ["standaard.json", ...namen.filter(f => f !== "standaard.json")] : namen;
for (const f of volgorde) {
  const p = JSON.parse(readFileSync(join(map, f), "utf8"));
  const velden = [f.slice(0, -5), p.naam, p.uitleg ?? ""];
  for (const [k, v] of Object.entries(p.waarden)) velden.push(`${k}=${v}`);
  if (velden.some(v => /[\t\n]/.test(v))) { console.error(`${f}: tab of nieuwe regel in een tekst`); process.exit(1); }
  console.log(velden.join("\t"));
}
