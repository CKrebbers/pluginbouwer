// bouwpakket.js — wat de tool exporteert: alles wat een vers Claude Code-gesprek nodig heeft om de plugin te bouwen.
// Puur: (ontwerp, metingen, bronbestanden, testsignalen) → { pad: inhoud }. Gebruikt door tools/bouwpakket.mjs en de
// exportknop van de tool. De teksten zijn het product: een vers gesprek krijgt ALLEEN dit pakket.
import { compileer, BIB, latentie, volgorde } from './keten.js';
import { ketenHeader, auSubtype } from './codegen.js';

const PROMPT_VERSIE = 'v3 (17 okt 2026)';

const fmt = (x, d = 2) => (x === undefined || x === null || Number.isNaN(x) ? '—' : (Math.abs(x) === Infinity ? (x < 0 ? '−∞' : '∞') : Number(x).toFixed(d)).replace('-', '−').replace('.', ','));

// Tekst op het paneel en in de host: dezelfde regels als de tool (app.js waardeTekst), plus % voor eenheidloos.
const nlG = (v, d) => v.toFixed(d).replace('.', ',');
export function waardeTekst(v, eenheid) {
  if (eenheid === 'Hz') return v >= 1000 ? nlG(v / 1000, v >= 10000 ? 1 : 2) + ' kHz' : Math.round(v) + ' Hz';
  if (eenheid === 'ms') return v >= 100 ? Math.round(v) + ' ms' : nlG(v, 1) + ' ms';
  if (eenheid === 'dB') return nlG(v, 1) + ' dB';
  if (eenheid === 'Q') return 'Q ' + nlG(v, 2);
  return Math.round(v * 100) + ' %';
}
const TEKST_REGEL = {
  Hz: 'onder 1000: hele Hz ("300 Hz"); vanaf 1000 kHz met 2 decimalen, vanaf 10 000 met 1 ("3,00 kHz", "12,0 kHz")',
  ms: 'onder 100: 1 decimaal ("20,0 ms"); vanaf 100 hele ms ("375 ms")',
  dB: '1 decimaal ("6,0 dB")',
  Q: '"Q " en 2 decimalen ("Q 0,71")',
  '': 'eenheidloos: × 100, hele procenten ("38 %"); boven 1 gaat het door ("125 %")',
};
// Label op het paneel: hooguit 11 tekens (een knopkolom is 76 px breed, Plex Mono 11 px ≈ 6,6 px per teken)
export const kortLabel = (p) => p.kort ?? (p.naam.length <= 11 ? p.naam : p.naam.slice(0, 10) + '.');

function ketenTekst(ontwerp, c) {
  const naam = (id) => { const k = ontwerp.knopen.find((x) => x.id === id); return `${id} (${BIB[k.type].naam})`; };
  const { terug } = volgorde(ontwerp);
  const regels = c.orde.map((id, i) => {
    const kn = ontwerp.knopen.find((k) => k.id === id);
    const ins = BIB[kn.type].in.map((poort) => {
      const kab = ontwerp.kabels.find((k) => k.naar[0] === id && k.naar[1] === poort);
      if (!kab) return null;
      const t = terug.includes(kab) ? ' (vorige sample: terugkoppeling)' : '';
      return `${poort} ← ${kab.van[0]}.${kab.van[1]}${t}`;
    }).filter(Boolean);
    return `${i + 1}. ${naam(id)}${ins.length ? ': ' + ins.join(', ') : ''}`;
  });
  return regels.join('\n');
}

export function cpuSchatting(ontwerp, metingBlokken) {
  let som = metingBlokken?.motorProcent ?? 0;
  for (const k of ontwerp.knopen) som += metingBlokken?.blokken?.[k.type]?.cpuProcent ?? 0;
  return som;
}

export function spec(ontwerp, { metingen = {}, metingBlokken = null } = {}) {
  const c = compileer(ontwerp);
  const cpu = cpuSchatting(ontwerp, metingBlokken);
  const terugkoppeling = volgorde(ontwerp).terug.length > 0;
  const m = metingen;
  const p = c.params.map((q, i) => `| ${i + 1} | ${q.naam} | ${q.min} – ${q.max} | ${q.eenheid || '—'} | ${q.standaard} | ${q.glijMs} ms | ${q.glijden === 'overvloeien' ? 'overvloeien' : 'glijden'} | ${q.uitleg || ''} |`);
  return `# SPEC — ${ontwerp.naam}

Gegenereerd door de Pluginbouwer uit het ontwerp \`${ontwerp.id}\`. De DSP staat
vast in \`dsp/\` (één bron voor browser en plugin); deze SPEC zegt wat de plugin eromheen moet doen en waaraan hij
getoetst wordt.

## Doel
${ontwerp.uitleg}

## Doelgeluid
${ontwerp.doelgeluid || `**Nog niet vastgelegd.** Er is (nog) geen referentieopname van een echte tape-echo. Tot die er is, toetst
de plugin alleen tegen zijn eigen ontwerp (null-test) en tegen de theorie; "klinkt als een tape-echo" is niet bewezen.
Wat Clay kan leveren: een opname van een tape-echo (droog en nat, zelfde fragment), met de instellingen erbij.

Over het voorbeeld (Roland RE-201): één opneemkop, drie weergavekoppen, de echotijd via de bandsnelheid
(Repeat Rate), intensity = terugkoppeling, bass/treble op de echo. **Niet bevestigd**: tijden in ms, bandsnelheid,
kopafstanden, wow/flutter-getallen. Gezocht (15 okt 2026): websearch op service notes en specificaties; het
service-handboek staat op archive.org ("Roland_RE-101_RE-201_Service_Manual") en ManualsLib (999287), maar die waren
vanuit de bouwomgeving niet te openen. De bereiken hieronder zijn ontwerpkeuzes, geen RE-201-waarden.`}

## Keten (uitvoervolgorde per sample)
${ketenTekst(ontwerp, c)}

Terugkoppeling: ${terugkoppeling ? 'ja — een kabel naar een eerdere knoop leest de uitgang van de vorige sample (z⁻¹).' : 'nee.'}

## Parameters (plugin-id = het nummer; nooit hergebruiken of hernummeren)
| id | naam | bereik | eenheid | standaard | glijtijd | bij draaien | wat het doet |
|---|---|---|---|---|---|---|---|
${p.join('\n')}

- CLAP-parameters zijn waarden in hun eigen eenheid (min..max hierboven), niet 0..1; clap-wrapper normaliseert lineair
  voor VST3 en AU. Grenzen en standaarden zijn exact in float32, omdat een AU float doorgeeft: zo krijgt elk formaat
  bit voor bit dezelfde waarde.
- "glijden": de waarde schuift per sample naar het doel (een tape-echo laat dan de toon glijden; zo bedoeld).

## Tekst ↔ waarde (paneel, host, clap \`value_to_text\`/\`text_to_value\`)
| id | naam | label op het paneel | standaard als tekst | regel |
|---|---|---|---|---|
${c.params.map((q, i) => `| ${i + 1} | ${q.naam} | ${kortLabel(ontwerp.parameters[i])} | ${waardeTekst(q.standaard, q.eenheid || '')} | ${TEKST_REGEL[q.eenheid || ''] ?? TEKST_REGEL['']} |`).join('\n')}

- Decimaalteken een komma (Nederlands), spatie voor de eenheid. \`text_to_value\` neemt komma én punt, met of zonder
  eenheid ("375", "375 ms", "0,375 s" hoeft niet), "k" voor kHz, en voor eenheidloos procenten ("38", "38 %"). Buiten
  het bereik = klemmen, onleesbaar = false.
- Stapjes: ${c.params.some((q) => q.stappen) ? 'zie de kolom bereik.' : 'geen; elke parameter is continu (geen CLAP_PARAM_IS_STEPPED).'}

## Budgetten
| wat | budget | geschat/gemeten |
|---|---|---|
| CPU per instantie | ≤ 5 % van één kern | ${fmt(cpu)} % (WASM-ketenmotor in Node, ${metingBlokken?.machine ?? 'onbekend'}; native is meestal sneller) |
| latency | 0 samples | ${latentie(ontwerp)} samples (fracties worden in de vertraging gecompenseerd) |
| staart | gemeld aan de host | ${terugkoppeling ? 'oneindig bij een lusversterking ≥ 1, anders tot −90 dB (formule in pb_graaf.h staartSec)' : '50 ms'} |
| geheugen | vooraf, nooit in process | vertragingslijnen op de hoogste samplerate van activate |

## Toleranties (per meting, met reden)
| meting | grens | reden | gemeten |
|---|---|---|---|
| null-test browser (WASM) vs gebouwd binary, per testsignaal | piek ≤ −90 dBFS | de opdracht van de Pluginbouwer; in de praktijk 0 (bit voor bit) door de regels van CLAUDE.md | ${m.null ?? 'bit voor bit (CLAP, VST3, AU; Linux en macOS)'} |
| zelfoscillatie (chaotisch) | terts-spectrum en omhullende binnen 1 dB | 1 ULP groeit in een lus; statistisch wat je hoort | ${m.statistisch ?? '0,000 dB'} |
| lineair deel tegen de formules | ≤ 0,01 dB, ≤ 0,05° | kubische term van tanh op −60 dBFS, float32-uitvoer | ${m.lineair ? fmt(m.lineair.maxDb, 4) + ' dB, ' + fmt(m.lineair.maxGraad, 3) + '°' : '—'} |
| tegen een onafhankelijke float64-referentie (Python) | ≤ −120 dBFS | libm tegen pb_math, float32-uitvoer | ${m['referentie-gewoon'] ? fmt(m['referentie-gewoon'].piekDb, 1) + ' / ' + fmt(m['referentie-heet']?.piekDb, 1) + ' dBFS' : '—'} |
| harmonischen (ESS) tegen ideale saturatie op 16× | H3 en H5 binnen 1 dB | de opdracht van de Pluginbouwer | ${m.harmonischen ? 'H3 ' + fmt(m.harmonischen.wasm.H3 - m.harmonischen.ideaal.H3) + ' dB, H5 ' + fmt(m.harmonischen.wasm.H5 - m.harmonischen.ideaal.H5) + ' dB' : '—'} |
| intermodulatie (SMPTE 60 Hz + 7 kHz) | binnen 1 dB van ideaal | de opdracht van de Pluginbouwer | ${m.imd ? fmt(m.imd.verschilDb) + ' dB' : '—'} |
| aliasing, keten zoals je hem hoort (toon 3 kHz, 12 dB) | ≤ −60 dB onder de grondtoon | ruim onder de ruisvloer van een tape-echo **[RE-201-waarde niet bevestigd]** | ${m.aliasingKeten ? fmt(m.aliasingKeten.wasm, 1) + ' dB' : '—'} |
| stabiliteit (324 combinaties) | nooit NaN/Inf, piek ≤ 8 | saturatie houdt de lus onder 1/g | ${m.stabiliteit ? 'piek ' + fmt(m.stabiliteit.hoogstePiek) : '—'} |
| staart | onder −90 dBFS binnen de gemelde tijd | host mag na de staart stoppen met process | ${m.staart ? m.staart.map((s) => `fb ${s.terugkoppeling}: ${fmt(s.gemetenS)} s ≤ ${fmt(s.gemeldS)} s`).join('; ') : '—'} |

Ter informatie (geen eis): aliasing van de saturatie los bij 5 kHz/12 dB: naïef ${m.aliasingBlok ? fmt(m.aliasingBlok.naief, 1) : '—'} dB, ADAA
${m.aliasingBlok ? fmt(m.aliasingBlok.adaa, 1) : '—'} dB; met de toon helemaal open ${m.aliasingKeten?.toonOpen5kHz ? fmt(m.aliasingKeten.toonOpen5kHz.wasm, 1) : '—'} dB. Daar is 2× oversampling de
volgende stap als het hoorbaar is.

## Validators (CI, het log is het bewijs)
pluginval strictness 10 (AU en VST3; met \`--rtcheck enabled\` op macOS), auval -strict, clap-validator, RTSan met een
kanarie die moet falen, ASan/UBSan, null-test door het gebouwde binary (pluginval test en pb-render).
`;
}

export function architectuur(ontwerp) {
  const c = compileer(ontwerp);
  return `# ARCHITECTUUR — ${ontwerp.naam}

## Lagen
\`\`\`
dsp/                 DE DSP (gegeven, niet herschrijven): pb_*.h + de keten als constante (ontwerpen/keten_${ontwerp.id}.h)
plugin/              de CLAP-schil (fase 0–2), clap-wrapper maakt VST3 en AUv2
gereedschap/         pb-render (CLAP-host), vergelijk.mjs (null-test + statistisch), ci-null.sh
testsignalen/        invoer + verwachte uitvoer (uit de WASM van de tool) + pluginval-configs
\`\`\`

## De keten
${c.orde.length} knopen, uitgevoerd in een vaste volgorde per sample (SPEC.md "Keten"). De ketenmotor
(\`dsp/pb_graaf.h\`) leest de beschrijving \`PB_KETEN\`; \`dsp/graaf_ontwerp.h\` zet die achter het Ontwerp-contract:
\`geheugenNodig\`, \`activeer\`, \`reset\`, \`zetParam\`, \`leesParam\`, \`verwerk\`, \`latentie\`, \`staartSec\`, \`PARAMS\`, \`META\`.
- \`zetParam(i, v)\` en \`leesParam(i)\` nemen de INDEX in \`PARAMS\` (0..n−1), niet het parameter-id; de schil zoekt de
  index bij het id.
- \`staartSec()\` leest de toestand van de smoothers en hoort dus bij de audiothread: reken hem aan het eind van process
  (en in activate) uit en bewaar hem atomisch; \`tail.get\` leest die waarde.

## Threads
- **Audiothread**: alleen \`process\`. Events (parameterwaarden) worden op hun sample toegepast: het blok wordt bij elk
  event gesplitst; de DSP rekent per sample, dus de uitvoer is gelijk aan die zonder splitsen.
- **Mainthread**: \`init\`, \`activate\` (hier wordt geheugen gereserveerd), state, parameters lezen en tekst.
- Geen locks, geen allocatie, geen syscalls in \`process\` (RTSan bewaakt het in CI).
- Parameteroverdracht naar het Ontwerp: alleen via CLAP-events (\`process\` en \`params.flush\`). Wat de mainthread
  moet lezen (\`params.get_value\`, \`state.save\`) komt uit een atomaire kopie per parameter die de audiothread
  bijwerkt; \`state.load\` terwijl de plugin actief is zet de waarden in die kopie plus een atomaire vlag, en
  \`process\`/\`flush\` neemt ze over vóór de events van dat blok. Verder geen gedeelde variabelen.
- \`staartSec()\` geeft −1 voor oneindig (lusversterking ≥ 1): meld dan \`INT32_MAX\` of meer (CLAP: oneindig).
- De null-test \`ci-null.sh clap\` heeft een geval \`testsignalen/automatie/\`: 720 parameter-events, ook midden in een
  blok en twee op dezelfde sample. Bit voor bit gelijk = het splitsen bij events klopt.

## State
Binair, klein en vooruit-compatibel: \`"PBST"\`, versie (u32), aantal (u32), dan per parameter (id u32, waarde f64).
Laden gaat per id: een onbekend id (van een nieuwere versie) wordt overgeslagen, een ontbrekend id houdt zijn
standaard. Parameter-id's veranderen nooit. Versie is nu 1. Laden leest de versie: andere magic, versie 0 of een
blob korter dan zijn \`aantal\` belooft = false (niets veranderd); een hogere versie laadt gewoon per id (een nieuwere
plugin mag er velden bij hebben, nooit een id een andere betekenis geven). Na een geslaagde load: \`host_params->rescan(CLAP_PARAM_RESCAN_VALUES)\`
(clap-validator eist het in state-reproducibility), en als de plugin actief is \`request_flush\`.

## Presets (fase 4)
Een fabriekspreset is \`presets/<sleutel>.json\`: \`{ naam, uitleg, waarden: { <parameternaam>: waarde } }\` (geen
state-blob). Bak ze bij het bouwen in het binary (CMake maakt er een tabel van; een onbekende naam of een waarde buiten
het bereik is een bouwfout), dan hoeft er niets naast de plugin te staan en geen JSON-lezer in de plugin.
- CLAP: \`preset-discovery\` met locaties van soort \`CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN\` en \`preset-load\`. Laden =
  hetzelfde pad als state.load (ontbrekende namen naar hun standaard), daarna rescan en \`loaded\`.
- Per preset een eigen provider met een container van één (zie valkuil 15: clap-validator loopt vast op een container
  met meer presets).
- AU: \`patches/clap-wrapper-auv2-presets.patch\` leest dezelfde factory en geeft ze als factory presets
  (\`GetPresets\`, \`NewFactoryPresetSet\`). VST3 krijgt van clap-wrapper geen presetlijst.
- Een verse plugin staat op de standaarden; laat de AU daarom op "Untitled" (−1) beginnen, niet op preset 0.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: \`pb_math.h\` (alleen + − × ÷ sqrt floor). Vlaggen \`-ffp-contract=off\`, geen \`-ffast-math\`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (\`pb::invoer\`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
`;
}

export function claudeMd(ontwerp) {
  return `# CLAUDE.md — bouwpakket "${ontwerp.naam}" (${ontwerp.id})

Je bouwt een audioplugin uit dit pakket. Lees eerst SPEC.md, ARCHITECTUUR.md, TASKS.md en PROMPTS.md.
Nederlands in docs, commits en comments; code-identifiers Engels. De gebruiker (Clay) programmeert geen C++ en test in
Logic Pro (MacBook Air M1). Wat hij zelf moet doen staat in LEERPAD.md.

## Wat vaststaat (niet veranderen)
- **De DSP in \`dsp/\` is gegeven.** Herschrijf hem niet, "verbeter" hem niet, kopieer hem niet naar een andere vorm.
  De plugin moet bit voor bit klinken als de WASM van de tool; de verwachte uitvoer in \`testsignalen/\` komt daaruit.
- Compileer \`dsp/\` met \`-ffp-contract=off -fno-fast-math -fno-math-errno\` (ook op macOS met Apple clang).
- Stack: CLAP-first (\`clap/clap.h\`), en **clap-wrapper v0.16.0** (MIT) via CMake \`make_clapfirst_plugins\` voor VST3 en
  AUv2 (\`CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON\`). AU-codes: maker \`Vrve\`, subtype \`${auSubtype(ontwerp.id)}\` (staan ook als
  #define in \`dsp/ontwerpen/keten_${ontwerp.id}.h\`).
- Geen betaalde diensten of licenties. Ad-hoc signeren (\`codesign --sign -\`) is genoeg voor Clay's eigen Mac.

## Bekende valkuilen (al eens misgegaan; zo los je ze op)
1. **AU-parameters vóór Initialize bereiken de CLAP niet** (clap-wrapper v0.16.0, \`wrapasauv2.cpp\` SetParameter).
   pluginval zet de state vóór prepareToPlay; de AU-null-test faalt dan met "gelijk aan de standaardwaarden".
   Oplossing: \`patches/clap-wrapper-auv2-params-voor-init.patch\` via \`PATCH_COMMAND\` in FetchContent.
2. **Linux-VST3 crasht na "SUCCESS" in pluginval**: statics in de VST3 SDK draaien bij exit in uitgeladen geheugen.
   Oplossing: \`target_link_options(<vst3-target> PRIVATE -Wl,-z,nodelete)\` op Linux.
3. **macOS: CMAKE_OBJCXX_COMPILE_OBJECT ontbreekt** → \`enable_language(OBJC)\` en \`enable_language(OBJCXX)\` in jouw project.
4. **RTSan vangt niets**: markeer de functie die process aanroept \`[[clang::nonblocking]]\` (achter de parameterlijst) én
   \`noinline\`, en maak een kanarie (malloc in process, pointer naar een globale variabele) die MOET falen.
5. **auval vindt de AU niet**: kopieer naar \`~/Library/Audio/Plug-Ins/Components\`, dan \`killall -9 AudioComponentRegistrar\`.
6. **Denormale invoer**: de DSP regelt het zelf; zet geen eigen FTZ-truc ervoor.
7. **pluginval \`--rtcheck\` op macOS vangt calloc in clap-wrapper** (\`_tlv_get_addr\` ← \`AlwaysAudioThread\` ←
   \`WrapAsAUV2::Render\`): twee \`thread_local\`-tellers die dyld per thread lui aanmaakt. Oplossing:
   \`patches/clap-wrapper-geen-thread-local-in-process.patch\`. Alle patches gaan erop met
   \`PATCH_COMMAND sh <pakket>/gereedschap/pas-patches-toe.sh <pakket>/patches\` (in naamvolgorde, overgeslagen als hij er
   al op zit, anders faalt een tweede configure; een \`;\` direct in PATCH_COMMAND splitst CMake in losse argumenten).
8. **clap-validator kleurt zijn uitvoer altijd** (ook met NO_COLOR): haal de ANSI-codes weg vóór je op "0 failed" grept,
   of vertrouw op de exitcode.
9. **pluginval v2**: \`pluginval validate …\` (de vlag \`--validate\` is verouderd); de null-test gebruikt \`pluginval test\`.
10. **De AU meldt geen TailTime** (auval: "Tail Time: Recommended Property is not supported"): clap-wrapper v0.16.0 geeft
   \`SupportsTail()\` vast false. Logic hoort de staart dan nooit (bounce met staart). Oplossing:
   \`patches/clap-wrapper-auv2-tailtime.patch\`.
11. **Draden**: zolang de plugin actief is, raakt alleen de audiothread het Ontwerp aan. \`params.get_value\` en
   \`state.save\` lezen een atomaire kopie; \`state.load\` zet waarden klaar die process of flush overneemt;
   \`tail.get\` leest een staart die process uitrekende (\`staartSec()\` leest de smoothers).
12. **CLAP-headers** komen van clap-wrapper (\`CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON\` haalt CLAP 1.2.x op; doel \`clap\`);
   het pakket heeft ze niet zelf.
13. **pluginval AU "Parameter thread safety" crasht af en toe in process** (\`VALIDATION CRASHED\`, process ←
   \`ProcessAdapter::process\` ← \`WrapAsAUV2::Render\`): clap-wrapper v0.16.0 zet in \`addParameterEvent\` events in
   dezelfde vectors die process op de audiothread sorteert en wist, zonder lock, vanaf de thread van SetParameter.
   Oplossing: \`patches/clap-wrapper-auv2-setparameter-draadveilig.patch\` (gevonden door een vers gesprek, proef F1).
   Draai pluginval op de AU met \`--repeat 3\`: de race sloeg één op de drie runs toe.
14. **"Knoppen via de host" bij CLAP**: een CLAP-host stuurt een uitvoerevent van de plugin niet terug. Het paneel zet
   een verandering als BEGIN/WAARDE/EIND in een lock-vrije rij en vraagt \`request_flush\`; process of \`params.flush\`
   pusht ze als \`CLAP_EVENT_PARAM_GESTURE_BEGIN\`/\`PARAM_VALUE\`/\`GESTURE_END\` naar de host én past de waarde zelf
   toe, zoals een event van de host. Het paneel raakt het Ontwerp nooit. Onder AUv2 doet \`request_flush\` niets
   (clap-wrapper v0.16.0): de host hoort de verandering pas bij de volgende Render.
15. **clap-validator 0.4.1 loopt vast op een preset-container met meer dan één preset** (\`preset-discovery-crawl\` en
   \`-load\`: "Timed out after 45 seconds"): \`begin_preset\` houdt de mutex \`result\` vast en \`flush_preset\` pakt hem
   opnieuw. Gevonden door twee verse gesprekken los van elkaar (F2, F3). Zolang dat niet gerepareerd is: per preset een
   provider met een container van één, of één bestand per preset (FILE-locatie, geen load key).
17. **Kanaalindelingen kiest elk formaat anders**: CLAP via \`audio-ports-config\`, de VST3 van clap-wrapper via
   \`configurable-audio-ports\` (\`setBusArrangements\`), de AUv2 van clap-wrapper v0.16.0 alleen de vaste poort (zonder
   patch biedt Logic de AU alleen stereo aan). Implementeer beide extensies; \`patches/clap-wrapper-auv2-volgt-presets-
   mono-stereo.patch\` maakt de indelingen van audio-ports-config de AU-kanaalindelingen. Alle drie de verse gesprekken
   van fase 5 (F1, F2, F3) moesten hem zelf bouwen.
18. **pluginval v2 op de opdrachtregel**: bij \`validate\` staat het pluginpad vóór \`--sample-rates\`/\`--block-sizes\` (die
   lijsten slokken anders het pad op); \`pluginval test\` lost een relatief pad op tegen de map van zijn config: geef
   absolute paden. De GitHub-tools geven van een job alleen de laatste 5000 logregels: zet bewijsstappen achteraan.
16. **Statische initialisatie in de AU**: de AU van clap-wrapper heeft een globale \`Clap::Library\` die
   \`clap_entry.init\` al tijdens de statische initialisatie aanroept, mogelijk vóór jouw eigen globals. Een globale
   \`std::string\` die init vult wordt daarna weer leeg gemaakt: gebruik een functie-static.

## Hoe je weet dat het klopt
De poorten in PROMPTS.md. Een poort is groen als CI groen is, niet als jij denkt dat het goed is. Loopt iets vast:
beschrijf wat je zag en wat je probeerde in \`VERSLAG.md\`; zet een toets nooit uit om groen te worden.
`;
}

export function tasksMd(ontwerp) {
  return `# TASKS — ${ontwerp.naam}

- [ ] Fase 0 — skelet, CI en alle validators (PROMPTS.md §0)
- [ ] Fase 1 — DSP aansluiten; null-test door het binary groen (§1)
- [ ] Fase 2 — parameters, automatisering, state (§2)
- [ ] Fase 3 — UI: het frontpaneel uit design.md (§3)
- [ ] Fase 4 — presets (§4)
- [ ] Fase 5 — formaten AU/VST3/CLAP compleet, mono en stereo (§5)
- [ ] Fase 6 — ad-hoc signeren en installeren (§6)
- [ ] Fase 7 — test in Logic (checklist voor Clay, §7)
`;
}

export function designMd(ontwerp) {
  const c = compileer(ontwerp);
  return `# design.md — frontpaneel van ${ontwerp.naam}

Zelfde taal als de Pluginbouwer: hoeken 0, rand 2 px, harde schaduw (3 px 3 px 0), geen gradiënten, geen
glassmorphism, geen Inter. Lettertype IBM Plex Mono (cijfers tabular), kopjes Silkscreen.

| token | waarde | gebruik |
|---|---|---|
| paneel | #232629 | achtergrond |
| inkt | #e7e5dd | tekst, knopranden |
| tape | #f2c200 | naamlabel, de boog van de gekozen knop |
| spoor | #40cbda | boog van de knoppen |
| groef | #3a3e42 | de boog onder het spoor (het hele bereik) |
| schaduw | #0d0e0f | harde schaduw, 3 px rechts en onder |

Indeling: één rij van ${c.params.length} draaiknoppen (270°, −135° tot +135°), label erboven, waarde in gewone eenheden
eronder (tekst zoals SPEC.md "Tekst ↔ waarde"), in de volgorde van SPEC.md. Dubbelklik = standaard. Slepen: 200 px
omhoog = het hele bereik, lineair in de CLAP-waarde (zoals de host normaliseert); Shift + slepen = 10× fijner. "De
gekozen knop" = de laatst aangeraakte; zijn boog is tape. Beginmaat 640 × 250.
Kolom per knop 76 px. Labels (hooguit 11 tekens, de volle naam in de tooltip en bij de host): ${ontwerp.parameters.map((p) => kortLabel(p)).join(' · ')}.
Minimaal 560 × 220 px, schaalbaar. De mockup is \`frontpaneel.svg\` in dit pakket (op ware grootte, alle knoppen op
hun standaard); dezelfde staat in de tool (stand Ontwerpen, "Frontpaneel").
`;
}

// De mockup als SVG: één rij knoppen op hun standaard, maten en kleuren uit design.md.
export function frontpaneelSvg(ontwerp) {
  const c = compileer(ontwerp);
  const n = c.params.length, kol = 76, mx = 24, B = Math.max(560, mx * 2 + n * kol), H = 220;
  const esc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;');
  const pt = (r, a) => [r * Math.sin(a), -r * Math.cos(a)].map((x) => x.toFixed(2));
  const boog = (r, a0, a1) => { const [x0, y0] = pt(r, a0), [x1, y1] = pt(r, a1); return `M${x0} ${y0} A${r} ${r} 0 ${a1 - a0 > Math.PI ? 1 : 0} 1 ${x1} ${y1}`; };
  const knoppen = c.params.map((q, i) => {
    const t = (q.standaard - q.min) / (q.max - q.min), a0 = -0.75 * Math.PI, a = a0 + t * 1.5 * Math.PI;
    const [kx, ky] = pt(14, a), x = mx + kol * i + kol / 2;
    return `  <g transform="translate(${x} 130)"><title>${esc(q.naam)}</title>
    <text y="-34" class="l">${esc(kortLabel(ontwerp.parameters[i]))}</text>
    <path d="${boog(22, a0, 0.75 * Math.PI)}" class="spoor0"/><path d="${boog(22, a0, a)}" class="spoor"/>
    <circle r="16" class="knop"/><line x1="0" y1="0" x2="${kx}" y2="${ky}" class="wijzer"/>
    <text y="44" class="w">${esc(waardeTekst(q.standaard, q.eenheid || ''))}</text></g>`;
  });
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${B}" height="${H}" viewBox="0 0 ${B} ${H}">
  <style>text{font-family:'IBM Plex Mono',monospace;font-size:11px;fill:#e7e5dd;text-anchor:middle;font-variant-numeric:tabular-nums}
  .n{font-family:Silkscreen,monospace;font-size:14px;fill:#232629;text-anchor:start}.w{fill:#e7e5dd}
  .knop{fill:#232629;stroke:#e7e5dd;stroke-width:2}.wijzer{stroke:#e7e5dd;stroke-width:2}
  .spoor0{fill:none;stroke:#3a3e42;stroke-width:3}.spoor{fill:none;stroke:#40cbda;stroke-width:3}</style>
  <rect x="3" y="3" width="${B - 6}" height="${H - 6}" fill="#0d0e0f"/>
  <rect x="0" y="0" width="${B - 6}" height="${H - 6}" fill="#232629" stroke="#e7e5dd" stroke-width="2"/>
  <rect x="${mx}" y="18" width="${12 + 9 * (3 + ontwerp.naam.length)}" height="24" fill="#f2c200"/>
  <text x="${mx + 6}" y="35" class="n">PB ${esc(ontwerp.naam.toUpperCase())}</text>
${knoppen.join('\n')}
</svg>
`;
}

export function leerpad(ontwerp) {
  return `# LEERPAD — wat Clay zelf doet

## Installeren (gratis, geen Apple Developer-account)
1. Download in GitHub bij de laatste groene CI-run het artefact \`macos-keten_${ontwerp.id}\` (een zip). Een artefact van
   GitHub bewaart geen Unix-rechten of symlinks: laat CI daarom de bundels zelf nog eens zippen (\`ditto -c -k
   --keepParent\` of \`zip -y\`) en pak díe zip uit.
2. Uitpakken. macOS zet een quarantainevlag op downloads: \`xattr -dr com.apple.quarantine "PB ${ontwerp.naam}.component"\`
   (en hetzelfde voor .vst3 en .clap).
3. Kopieer \`PB ${ontwerp.naam}.component\` naar \`~/Library/Audio/Plug-Ins/Components/\`.
4. Terminal: \`killall -9 AudioComponentRegistrar\` (anders ziet Logic hem niet meteen).
5. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB ${ontwerp.naam}. Status moet "geslaagd" zijn.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (\`codesign --sign -\`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren | Apple Developer Program **[bedrag niet bevestigd, ±€ 99/jaar]** | niet nodig voor eigen gebruik | distributie |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Wat je kunt leren (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.
`;
}

export function promptsMd(ontwerp, { uitslagen = {} } = {}) {
  const id = ontwerp.id, au = auSubtype(id);
  const u = (f) => uitslagen[f] ? uitslagen[f] : 'nog niet getoetst';
  return `# PROMPTS — ${ontwerp.naam} (${PROMPT_VERSIE})

Per fase één prompt. Geef een vers Claude Code-gesprek dit pakket (alle bestanden) en de prompt van de fase.
Een fase is klaar als het stopcriterium waar is en de poorten in CI groen zijn. Uitslagen per fase: hoeveel van de
losse verse gesprekken (pass@3) het haalden, met de oorzaak als het misging.

---
## Fase 0 — skelet, CI en alle validators
**Prompt**
> Lees CLAUDE.md, SPEC.md, ARCHITECTUUR.md. Bouw het skelet van de plugin "${ontwerp.naam}": een CLAP-plugin in
> \`plugin/\` (C++17) met CMake, en clap-wrapper v0.16.0 via FetchContent (met de patches uit \`patches/\`, in naamvolgorde) voor VST3 en AUv2
> (\`make_clapfirst_plugins\`; de CLAP-headers komen mee met clap-wrapper, haal ze niet apart op; AU maker \`Vrve\`, subtype \`${au}\`, type \`aufx\`). De plugin gebruikt het Ontwerp uit
> \`dsp/ontwerpen/keten_${id}.h\` (include-pad \`dsp/\`); in deze fase geeft process de invoer door (de DSP aansluiten is fase 1, zodat de null-test het bewijst).
> Schrijf \`.github/workflows/ci.yml\` met: Linux (bouwen met clang, clap-validator 0.4.1 uit de bron via cargo,
> pluginval v2.0.0-beta3 strictness 10 op de VST3 onder xvfb), RTSan (clang 20 via apt.llvm.org, \`gereedschap/pb-render\`
> gebouwd met \`-fsanitize=realtime\`, met een kanarie die moet falen), en macOS arm64 (bouwen, ad-hoc signeren,
> installeren in ~/Library/Audio/Plug-Ins, \`killall -9 AudioComponentRegistrar\`, \`auval -strict -v aufx ${au} Vrve\`,
> pluginval strictness 10 met \`--rtcheck enabled\` op AU (met \`--repeat 3\`) en VST3, de plugins als artefact
> \`macos-keten_${id}\`; een artefactnaam mag geen \`:\` \`/\` \`*\` \`?\` \`"\` \`<\` \`>\` \`|\` bevatten, zet de matrixnaam dus niet ongefilterd in de naam). Push en maak CI groen. Zet niets uit om groen te worden; een valkuil uit CLAUDE.md eerst.

**Stopcriterium**: CI groen op de laatste commit: clap-validator 0 mislukt, pluginval 10 geslaagd (VST3 Linux, AU en
VST3 macOS), auval "AU VALIDATION SUCCEEDED", RTSan-bouw draait schoon en de kanarie faalt zoals bedoeld.
**Uitslag**: ${u('0')}

---
## Fase 1 — DSP aansluiten; null-test door het binary
**Prompt**
> Sluit het Ontwerp uit \`dsp/\` aan in process: parameters als CLAP-params (id = nummer in SPEC.md, bereik en standaard
> uit \`PARAMS\`), events op hun sample (blok splitsen bij elk event), \`latency\` en \`tail\` uit het Ontwerp, state zoals
> ARCHITECTUUR.md. Voeg aan CI de null-test toe: \`sh gereedschap/ci-null.sh clap <build>\` (pb-render, blok 512,
> wisselende blokken, state-rondje) en \`sh gereedschap/ci-null.sh pluginval <plugin> <pluginval> <label>\` voor de VST3
> (Linux en macOS) en de AU (macOS). Verander \`dsp/\` en \`testsignalen/\` niet. Als een render "gelijk aan de
> standaardwaarden" zegt: lees valkuil 1.

**Stopcriterium**: alle null-tests in CI ≤ −90 dBFS (verwacht: bit voor bit), de statistische toets (zelfoscillatie)
binnen 1 dB, en de validators van fase 0 nog groen.
**Uitslag**: ${u('1')}

---
## Fase 2 — parameters, automatisering, state
**Prompt**
> Maak de parameters af: tekst ↔ waarde volgens de tabel "Tekst ↔ waarde" in SPEC.md, \`params.flush\` buiten
> process. Toets met clap-validator (state-reproducibility, param-fuzz) en een eigen toets (in CI, door het binary):
> (a) een met de hand gemaakte state-blob zonder de laatste parameter (\`aantal\` = N − 1, zoals een oudere build hem
> schreef) laadt en die parameter houdt zijn standaard; (b) een blob met een extra onbekend id en versie 2 laadt;
> (c) andere magic, versie 0 of een afgekapte blob geeft false en verandert niets. Er hoeft geen tweede build te zijn.
> Daarnaast: \`value_to_text\` van de standaard geeft precies de tekst uit de tabel, en \`text_to_value\` daarvan terug
> geeft de waarde. Automatisering in een DAW verandert niets aan de null-test.
**Stopcriterium**: clap-validator en pluginval 10 groen, state-toets groen.
**Uitslag**: ${u('2')}

---
## Fase 3 — UI
**Prompt**
> Bouw het frontpaneel uit design.md en \`frontpaneel.svg\` (de mockup; CLAP gui-extensie, clap-wrapper geeft het door aan VST3/AU). Knoppen sturen
> parameters via de host, nooit rechtstreeks de DSP. Geen audiothread-toegang vanuit de UI.
**Stopcriterium**: pluginval 10 zonder \`--skip-gui-tests\` groen op macOS; een schermafbeelding in CI als artefact.
**Uitslag**: ${u('3')}

---
## Fase 4 — presets
**Prompt**
> Fabriekspresets uit \`presets/\` (JSON met parameterwaarden per naam) als CLAP preset-discovery én als AU factory presets
> (ARCHITECTUUR.md "Presets", valkuilen 15 en 16). Eis in CI dat de drie preset-toetsen van clap-validator PASSED zijn
> (SKIPPED telt niet) en dat elke presetnaam in de uitvoer van \`auval -strict -v\` staat.
**Stopcriterium**: clap-validator preset-tests groen (PASSED); auval toont de presets.
**Uitslag**: ${u('4')}

---
## Fase 5 — formaten compleet
**Prompt**
> Mono én stereo (audio-ports-config), bypass zoals de host het wil, 44,1–192 kHz, blokgrootte 1..4096. Breid de
> CI-matrix uit (samplerates in de null-test). Let op bij mono in-place: de DSP schrijft per sample eerst kanaal 0;
> lees je beide DSP-ingangen uit één hostbuffer die ook uitgang 0 is, kopieer de invoer dan eerst. Drie indelingen:
> stereo (2 → 2), mono (1 → 1) en mono → stereo (1 → 2, Logic biedt die aan op een monospoor). Lees valkuil 17 (elk
> formaat kiest zijn kanalen anders). De verwachte uitvoer bij andere samplerates maak je met \`wasm/graaf.wasm\` (die
> doet \`testsignalen/\` eerst bit voor bit na). Bypass: \`CLAP_PARAM_IS_BYPASS\`, de DSP rekent door, 10 ms overvloeien,
> en ook de droge invoer door \`pb::uitvoer\` (geen subnormale float naar de host; de fuzzer van clap-validator vangt het).
**Stopcriterium**: CI groen met de uitgebreide matrix.
**Uitslag**: ${u('5')}

---
## Fase 6 — signeren en installeren
**Prompt**
> Maak een installatiepakket (\`.pkg\` via pkgbuild, ad-hoc gesigneerd) dat de AU, VST3 en CLAP op de juiste plek zet.
> Notarisatie alleen als optie in de docs, met de kosten.
**Stopcriterium**: het pakket installeert in CI op een schone runner; auval slaagt daarna.
**Uitslag**: ${u('6')}

---
## Fase 7 — test in Logic (checklist voor Clay)
- [ ] Plug-in Manager: PB ${ontwerp.naam} "geslaagd" (na LEERPAD.md "Installeren").
- [ ] Op een audiospoor laden; geluid gaat erdoor; knoppen doen wat SPEC.md zegt.
- [ ] Een knop automatiseren (Tijd van langzaam naar snel): de toon glijdt, geen tikken.
- [ ] Terugkoppeling boven 1: hij gaat vanzelf door en blijft begrensd.
- [ ] Bouncen met de staart: Bestand → Bounce, met "Staart van audio opnemen"/"Include Audio Tail" aan; de echo
      loopt door na het einde van de regio. (De plugin meldt een andere staart niet actief met \`tail.changed\`; de
      host vraagt hem op bij de bounce. Stopt de echo te vroeg, dan is dit de plek.)
- [ ] Project bewaren, Logic sluiten, opnieuw openen: alle knoppen staan waar ze stonden.
`;
}

// Alle bestanden van het pakket. bronnen = { 'dsp/pb_math.h': tekst, … } (de tool bakt ze in, Node leest ze);
// testsignalen = { 'testsignalen/x.wav': Uint8Array, … }; extra = { pad: inhoud } (patch, gereedschap).
// presets/<slug>.json: { naam, uitleg, waarden: { <parameternaam>: waarde } } (fase 4 van PROMPTS.md)
const slug = (t) => t.toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
export function presetBestanden(ontwerp) {
  const uit = {};
  for (const p of ontwerp.presets || []) uit[`presets/${slug(p.naam)}.json`] = JSON.stringify(p, null, 2) + '\n';
  return uit;
}
export function bouwpakket(ontwerp, { metingen = {}, metingBlokken = null, bronnen = {}, testsignalen = {}, extra = {}, uitslagen = {} } = {}) {
  const c = compileer(ontwerp);
  if (c.fouten.length) throw new Error('het ontwerp heeft fouten: ' + c.fouten.join('; '));
  const pak = {
    'SPEC.md': spec(ontwerp, { metingen, metingBlokken }),
    'ARCHITECTUUR.md': architectuur(ontwerp),
    'CLAUDE.md': claudeMd(ontwerp),
    'TASKS.md': tasksMd(ontwerp),
    'PROMPTS.md': promptsMd(ontwerp, { uitslagen }),
    'design.md': designMd(ontwerp),
    'frontpaneel.svg': frontpaneelSvg(ontwerp),
    'LEERPAD.md': leerpad(ontwerp),
    'ontwerp.json': JSON.stringify(ontwerp, null, 2) + '\n',
    'metingen.json': JSON.stringify({ metingen, metingBlokken }, null, 2) + '\n',
    ...presetBestanden(ontwerp),
    [`dsp/ontwerpen/keten_${ontwerp.id}.h`]: ketenHeader(ontwerp),
  };
  for (const [pad, inhoud] of Object.entries(bronnen)) pak[pad] = inhoud;
  for (const [pad, inhoud] of Object.entries(testsignalen)) pak[pad] = inhoud;
  for (const [pad, inhoud] of Object.entries(extra)) pak[pad] = inhoud;
  return pak;
}

// De bronbestanden die in elk pakket gaan (paden in deze repo → paden in het pakket)
export const PAKKET_BRONNEN = {
  'dsp/pb_math.h': 'dsp/pb_math.h', 'dsp/pb_core.h': 'dsp/pb_core.h', 'dsp/pb_biquad.h': 'dsp/pb_biquad.h',
  'dsp/pb_delay.h': 'dsp/pb_delay.h', 'dsp/pb_svf.h': 'dsp/pb_svf.h', 'dsp/pb_tape.h': 'dsp/pb_tape.h',
  'dsp/pb_graaf.h': 'dsp/pb_graaf.h', 'dsp/graaf_ontwerp.h': 'dsp/graaf_ontwerp.h',
  'dsp/pb_wasm.cpp': 'dsp/pb_wasm.cpp', 'dsp/bouw-wasm.sh': 'dsp/bouw-wasm.sh',
  'host/pb-render.cpp': 'gereedschap/pb-render.cpp',
  'tools/pakket/ci-null.sh': 'gereedschap/ci-null.sh',
  'tools/pakket/vergelijk.mjs': 'gereedschap/vergelijk.mjs',
  'plugin/patches/clap-wrapper-auv2-params-voor-init.patch': 'patches/clap-wrapper-auv2-params-voor-init.patch',
  'plugin/patches/clap-wrapper-geen-thread-local-in-process.patch': 'patches/clap-wrapper-geen-thread-local-in-process.patch',
  'plugin/patches/clap-wrapper-auv2-tailtime.patch': 'patches/clap-wrapper-auv2-tailtime.patch',
  'plugin/patches/clap-wrapper-auv2-presets.patch': 'patches/clap-wrapper-auv2-presets.patch',
  'plugin/patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch': 'patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch',
  'plugin/patches/clap-wrapper-auv2-setparameter-draadveilig.patch': 'patches/clap-wrapper-auv2-setparameter-draadveilig.patch',
  'plugin/cmake/pas-patches-toe.sh': 'gereedschap/pas-patches-toe.sh',
};
