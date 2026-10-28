// bouw.mjs — maakt tool/index.html: één bestand zonder build-afhankelijkheden, dat als artifact en als losse pagina
// werkt (ook van schijf, file://). De modules uit tool/src worden elk in een eigen functiescope gezet (geen
// naamsbotsingen), in de volgorde van hun imports. Ingebakken: de ketenmotor (dsp/wasm/motor/graaf.wasm, base64),
// de bronnen die in elk bouwpakket gaan, de metingen en de gemeten cpu per blok.
//   node tool/bouw.mjs            (vooraf: sh dsp/bouw-wasm.sh graaf; node tools/bouwpakket.mjs dub_echo)
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join, dirname, resolve, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PAKKET_BRONNEN } from './src/bouwpakket.js';

const TOOL = dirname(fileURLToPath(import.meta.url));
const ROOT = join(TOOL, '..');
const SRC = join(TOOL, 'src');
const IMPORT = /^import\s*\{([^}]*)\}\s*from\s*'(\.\/[\w-]+\.js)';?\s*$/gm;

// modules in importvolgorde (diepte eerst), vanaf app.js
const volgorde = [], gezien = new Set();
function bezoek(pad) {
  if (gezien.has(pad)) return;
  gezien.add(pad);
  for (const [, , van] of readFileSync(pad, 'utf8').matchAll(IMPORT)) bezoek(resolve(dirname(pad), van));
  volgorde.push(pad);
}
bezoek(join(SRC, 'app.js'));

const naamVan = (pad) => 'M_' + relative(SRC, pad).replace(/\.js$/, '').replace(/\W/g, '_');
const delen = [];
for (const pad of volgorde) {
  let code = readFileSync(pad, 'utf8');
  // export-namen: uit de module zelf (Node laadt hem); app.js heeft er geen en raakt het DOM aan
  const namen = pad.endsWith('app.js') ? [] : Object.keys(await import(pathToFileURL(pad).href));
  code = code.replace(IMPORT, (_, lijst, van) => {
    const bind = lijst.split(',').map((s) => s.trim()).filter(Boolean).map((s) => s.replace(/\s+as\s+/, ': ')).join(', ');
    return `const { ${bind} } = ${naamVan(resolve(dirname(pad), van))};`;
  });
  code = code.replace(/^export\s+(?=(async\s+)?(function|class|const|let|var)\b)/gm, '');
  code = code.replace(/^export\s*\{[^}]*\};?\s*$/gm, '');
  if (/^\s*(import|export)\b/m.test(code)) throw new Error(`${relative(ROOT, pad)}: een import/export die bouw.mjs niet kent`);
  delen.push(`// ---- ${relative(ROOT, pad)}\nconst ${naamVan(pad)} = (() => {\n${code}\nreturn { ${namen.join(', ')} };\n})();`);
}

// ingebakken data
const lees = (p) => readFileSync(join(ROOT, p));
const metingPad = join(ROOT, 'bouwpakket', 'dub_echo', 'metingen.json');
const data = {
  motor: lees('dsp/wasm/motor/graaf.wasm').toString('base64'),
  metingBlokken: JSON.parse(lees('tool/src/meting-blokken.json')),
  metingen: existsSync(metingPad) ? JSON.parse(readFileSync(metingPad, 'utf8')).metingen : {},
  uitslagen: JSON.parse(lees('tools/pakket/uitslagen.json')),
  bronnen: Object.fromEntries(Object.entries(PAKKET_BRONNEN).map(([van, naar]) => [naar, lees(van).toString('utf8')])),
};
const veilig = (s) => s.replace(/<\/(script|style)/gi, '<\\/$1').replace(/<!--/g, '<\\!--');

const html = readFileSync(join(TOOL, 'index.src.html'), 'utf8')
  .replace('/*PB_STIJL*/', () => readFileSync(join(TOOL, 'stijl.css'), 'utf8').replace(/<\/style/gi, '<\\/style'))
  .replace('/*PB_DATA*/', () => 'globalThis.PB_DATA = ' + veilig(JSON.stringify(data)) + ';')
  .replace('/*PB_CODE*/', () => veilig(`(() => {\n'use strict';\n${delen.join('\n\n')}\n})();`));
writeFileSync(join(TOOL, 'index.html'), html);
console.log(`tool/index.html: ${(html.length / 1024).toFixed(0)} kB, ${volgorde.length} modules`);

// de artifact-versie: zelfde inhoud zonder doctype/html/head/body (claude.ai zet het skelet er zelf omheen)
const artifact = html
  .replace(/^<!doctype html>\s*<html[^>]*>\s*<head>\s*/i, '')
  .replace('<meta charset="utf-8">\n', '')
  .replace('<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n', '')
  .replace('<title>De Pluginbouwer</title>\n', '<title>De Pluginbouwer</title>\n<meta name="claude:viewport-fit-sides" content="cover">\n')
  .replace(/<\/head>\s*<body>\s*/i, '')
  .replace(/\s*<\/body>\s*<\/html>\s*$/i, '\n');
if (!artifact.startsWith('<title>')) throw new Error('artifact.html begint niet met <title>');
writeFileSync(join(TOOL, 'artifact.html'), artifact);
