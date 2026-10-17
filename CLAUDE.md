# Pluginbouwer — projectgeheugen

Lees dit voordat je iets wijzigt. `PROMPT.md` is de opdracht, `TASKS.md` de volgorde, dit bestand de besluiten.
Bronnen met versie en datum: `BRONNEN.md`. Stackkeuze met metingen: `KEUZE-STACK.md`.

## Wat dit is
Een ontwerptool voor audioplugins, naar het model van de Stackbouwer (varve-site, `stackbouwer-bron/`): één catalogus
(BIB), de standen Ontwerpen en Bouwen, een readout bovenin, toetsen tegen een onafhankelijke referentie en een export
naar buiten. Hier is "buiten" een bouwpakket waarmee een vers Claude Code-gesprek de plugin bouwt.

Gebruiker: Clay (Varve). Nederlands in docs, commits, comments en tooltekst. Code-identifiers in het Engels.
Clay programmeert geen C++: wat hij zelf moet doen of leren staat apart (`LEERPAD.md`).

## De vijf regels (de reden dat de null-test kan slagen)
1. **Eén DSP-bron.** Alles wat klinkt staat in `dsp/` (C++17, header-only). Dezelfde bestanden worden gecompileerd naar
   WASM (browser, tool, toetsen in Node) en naar native (CLAP → VST3/AU). Geen tweede implementatie, ook niet "even in
   JS voor de UI". Een meting in de tool draait op die WASM.
2. **Deterministisch rekenen.** Geen `<cmath>`/libm: sin, cos, tan, exp, log, tanh komen uit `dsp/pb_math.h`
   (alleen + − × ÷ en sqrt, die in IEEE 754 exact afgerond zijn). Vlaggen overal: `-ffp-contract=off`, geen
   `-ffast-math`. Toeval uit een geseede xorshift op `uint32_t`. Denormals zelf wegpoetsen (`pb::flush`), want
   hosts zetten FTZ/DAZ soms wel en soms niet.
3. **Blokgrootte-onafhankelijk.** Elke sample wordt gerekend alsof het blok 1 lang is: parameter-smoothing per
   sample, events op hun sample. Daardoor is de uitvoer bij blok 1, 7, 128, 512 en 4096 bit voor bit gelijk
   (`tests/blokgrootte.test.mjs`). Dat is wat de null-test tussen een AudioWorklet (128) en een host (512) mogelijk maakt.
4. **Real-time veilig.** Geen allocatie, lock, syscall of uitzondering in `process`. Geheugen wordt gereserveerd in
   `activate` (op de hoogste samplerate die de plugin belooft). RTSan in CI (Linux, clang ≥ 20).
5. **Bewijs komt uit CI, niet uit een oordeel.** Een toets heeft een grens met een reden (in SPEC.md of hier).
   Faalt hij, dan is de code fout of de grens; nooit de toets stilletjes ruimer zetten.

## Gekozen stack (A en B; meetresultaten in KEUZE-STACK.md)
Besluit 15 okt 2026, na de proefbouw: onze stack (hieronder) boven Faust. Doorslag: Faust in float (de standaard)
liet browser en plugin −46,8 dBFS uit elkaar lopen zodra coëfficiënten per sample bewegen; onze stack is overal bit
voor bit gelijk, ook op macOS arm64 en ook bij zelfoscillatie.
- DSP: kale C++ in `dsp/`, naar WASM met **clang `--target=wasm32` zonder libc** (geen Emscripten nodig: de code is
  vrijstaand). Afwijking van de opdracht ("Emscripten") met reden: minder gereedschap, dezelfde compilerfamilie als
  native, geen libc-functies die stilletjes anders rekenen.
- Plugin: **CLAP-first** (`plugin/`), en **clap-wrapper** (MIT) maakt er VST3 en AUv2 van. VST3 SDK is sinds 3.8 MIT,
  AudioUnitSDK Apache 2.0, CLAP MIT: geen copyleft in de keten, geen kosten.
- Native renders voor de null-test gaan door het gebouwde binary: `pluginval test` (VST3, AU) en `host/pb-render`
  (een eigen kleine CLAP-host).

## Structuur
```
PROMPT.md  BRONNEN.md  KEUZE-STACK.md  LEERPAD.md  TASKS.md  design.md
dsp/            de ENE DSP-bron (header-only, vrijstaand). pb_math.h, pb_biquad.h, …, pb_wasm.cpp (C-ABI voor WASM)
plugin/         CLAP-first plugin + CMake met clap-wrapper → .clap, .vst3, .component
host/           pb-render: kleine CLAP-host die een WAV door een .clap stuurt (Linux en macOS);
                au-proef: AudioToolbox-host die de AU gebruikt zoals een DAW (macOS, CI; tools/au-proef.mjs toetst)
ref/            onafhankelijke float64-referentie in Python/NumPy (andere code dan dsp/)
tests/          Node-toetsen (node --test): formules, blokgrootte, null-test, statistisch
testsignalen/   gegenereerde WAV's + verwachte uitvoer (CSV/WAV), met het script dat ze maakt
tool/           de ontwerper: src/ → index.html (losse pagina) en artifact.html (claude.ai), via bouw.mjs; WASM ingebakken
tools/headless/ tool.mjs: de tool in Chrome (ook in CI)
bouwpakket/     wat de tool exporteert, per ontwerp
.github/workflows/ci.yml
```

## Valkuilen die al gevonden zijn (niet opnieuw instappen)
- **Geen globale constructors in `dsp/pb_wasm.cpp`.** Zonder libc zet wasm-ld `__wasm_call_ctors` vóór ELKE export:
  alle instanties werden bij elke aanroep teruggezet. Instanties via placement-new in ruwe opslag.
- **Een Ontwerp zet zijn standaardwaarden zelf** (constructor); niet erop rekenen dat de schil dat doet.
- **AU-parameters vóór Initialize** bereikten de CLAP niet (clap-wrapper v0.16.0). Gepatcht in `plugin/patches/`;
  bij een nieuwe clap-wrapper eerst kijken of de patch nog nodig is (de null-test op macOS zegt het).
- **thread_local in clap-wrapper alloceert op macOS** (dyld, calloc bij de eerste render op elke audiothread);
  pluginval `--rtcheck` vangt het. Tweede patch `plugin/patches/clap-wrapper-geen-thread-local-in-process.patch`,
  gevonden door alle drie de verse sessies van fase E. Onze CI ving het niet door `--skip-gui-tests`: dat sloeg
  "Open editor whilst processing" over (bewezen op tak onderzoek/tls-zonder-patch); de vlag is weg.
  Patches gaan erop via `plugin/cmake/pas-patches-toe.sh` (een `;` direct in PATCH_COMMAND splitst CMake).
- **De AU meldde geen TailTime** (clap-wrapper v0.16.0: `SupportsTail()` vast false; auval: "Recommended Property is
  not supported"). Derde patch `plugin/patches/clap-wrapper-auv2-tailtime.patch`; `host/au-proef` toetst het in CI.
- **AU SetParameter tegen Render** (clap-wrapper v0.16.0, `addParameterEvent` zonder lock): pluginval "Parameter thread
  safety" crashte één op de drie runs. Vierde patch `plugin/patches/clap-wrapper-auv2-setparameter-draadveilig.patch`
  (gevonden door verse sessie F1, fase 3); CI draait pluginval op de AU daarom met `--repeat 3`.
- **AU-fabriekspresets**: clap-wrapper v0.16.0 kent ze niet. Vijfde patch `plugin/patches/clap-wrapper-auv2-presets.patch`
  (uit proef F2) leest de preset-discovery-factory (locaties PLUGIN). Onze eigen plugin heeft nog geen presets; de
  patch staat erop zodat CI bewijst dat de vijf samen bouwen. clap-validator 0.4.1 loopt vast op een preset-container
  met meer dan één preset (F2 en F3 los van elkaar; patch in upstream/).
- **Draden in de schil**: actief = alleen de audiothread raakt het Ontwerp; get_value/state lezen `spiegel`,
  state.load zet `vuil`, tail.get leest `staartSmp` (plugin/src/plugin.cpp). Gevonden door de verse sessies (E1, E2).
- **Bypass**: clap-wrapper geeft AU BypassEffect alleen door aan een CLAP-parameter met `CLAP_PARAM_IS_BYPASS`;
  zonder die parameter deed bypass niets. De schil heeft er een (id 1000, 10 ms overvloeien; uit = bit voor bit het
  oude pad). AU ClassInfo kan pas na Initialize (clap-wrapper `RestoreState`), hosts doen dat meestal ook zo.
- **Linux-VST3 met `-z nodelete`**: anders crasht pluginval na "SUCCESS" (statics in de VST3 SDK, exit na dlclose).
- **RTSan-kanarie via een globale sink**: clang -O2 haalt een malloc/free-paar weg, ook met een volatile lokale pointer.
- **Denormale invoer** nooit als float omzetten om te testen (`pb::invoer` kijkt naar de bits).
- **Parameterbereiken exact in float32** (een AU geeft float door); `tests/standaard.test.mjs` bewaakt het.
- **ADAA-1 is voor kleine signalen een (1+z⁻¹)/2-filter**: −3 dB op fs/4 en een halve sample vertraging (die de
  dub-echo van de delaytijd aftrekt).
- **Python-referentie draait met `-I`** (geïsoleerd): een numpy in de user-site telt dan niet. CI installeert
  `python3-numpy` systeembreed en controleert met `python3 -I -c "import numpy"`.
- **Een sinusmeting op een keten met echo**: eerst inlopen (¾ van `pg_staart`, hooguit 8 s), anders is het inzetten
  van elke herhaling lek over het hele spectrum. Bandbeweging en modulatie stil bij aliasing/thd: hun zijbanden zijn
  geen aliasing (−16,5 dB met beweging, −122,6 dB zonder, op de dub-echo). Zelfoscillatie = geen getal, wel een reden.
- **AudioWorklet uit een blob-URL** weigert Chrome op `file://`; `audio.js` valt terug op een data-URL, daarna op
  ScriptProcessor (zelfde WASM op de hoofdthread). De tool zegt welke draait.
- **Als artifact op claude.ai** doet een downloadlink niets: bewaren gaat via de `downloads`-capability
  (`claude.use('downloads')`), de losse pagina gebruikt een gewone link. `tool/bouw.mjs` maakt beide versies.

## Conventies
| Onderwerp | Afspraak |
|---|---|
| Taal | Nederlands in docs, commits, comments, tooltekst. Identifiers Engels. |
| Toetsen | `node --test tests/` lokaal; CI draait alles plus de validators. Geen toets uitzetten om groen te worden. |
| WASM bouwen | `sh dsp/bouw-wasm.sh` (clang + wasm-ld). De .wasm staat in git zodat de tool zonder bouwstap werkt. |
| Plugin bouwen | `cmake -B build -S plugin -DCMAKE_BUILD_TYPE=Release && cmake --build build`. |
| Kosten | Niets betaald zonder Clay. Per kostenpost het gratis alternatief (zie LEERPAD.md "Kosten"). |
| Stijl | design.md. Geen gradiënten, glassmorphism, Inter, ronde kaartjes. |
| Git | Checkpoint (push) vóór elke grote stap. `git add` per pad. |
