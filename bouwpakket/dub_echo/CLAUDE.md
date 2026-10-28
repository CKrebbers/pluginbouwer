# CLAUDE.md — bouwpakket "Dub echo" (dub_echo)

Je bouwt een audioplugin uit dit pakket. Lees eerst SPEC.md, ARCHITECTUUR.md, TASKS.md en PROMPTS.md.
Nederlands in docs, commits en comments; code-identifiers Engels. De gebruiker (Clay) programmeert geen C++ en test in
Logic Pro (MacBook Air M1). Wat hij zelf moet doen staat in LEERPAD.md.

## Wat vaststaat (niet veranderen)
- **De DSP in `dsp/` is gegeven.** Herschrijf hem niet, "verbeter" hem niet, kopieer hem niet naar een andere vorm.
  De plugin moet bit voor bit klinken als de WASM van de tool; de verwachte uitvoer in `testsignalen/` komt daaruit.
- Compileer `dsp/` met `-ffp-contract=off -fno-fast-math -fno-math-errno` (ook op macOS met Apple clang).
- Stack: CLAP-first (`clap/clap.h`), en **clap-wrapper v0.16.0** (MIT) via CMake `make_clapfirst_plugins` voor VST3 en
  AUv2 (`CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`). AU-codes: maker `Vrve`, subtype `PDca`, type `aufx` (staan ook als
  #define in `dsp/ontwerpen/keten_dub_echo.h`).
- Geen betaalde diensten of licenties. Ad-hoc signeren (`codesign --sign -`) is genoeg voor Clay's eigen Mac.

## Bekende valkuilen (al eens misgegaan; zo los je ze op)
1. **AU-parameters vóór Initialize bereiken de CLAP niet** (clap-wrapper v0.16.0, `wrapasauv2.cpp` SetParameter).
   pluginval zet de state vóór prepareToPlay; de AU-null-test faalt dan met "gelijk aan de standaardwaarden".
   Oplossing: `patches/clap-wrapper-auv2-params-voor-init.patch` via `PATCH_COMMAND` in FetchContent.
2. **Linux-VST3 crasht na "SUCCESS" in pluginval**: statics in de VST3 SDK draaien bij exit in uitgeladen geheugen.
   Oplossing: `target_link_options(<vst3-target> PRIVATE -Wl,-z,nodelete)` op Linux.
3. **macOS: CMAKE_OBJCXX_COMPILE_OBJECT ontbreekt** → `enable_language(OBJC)` en `enable_language(OBJCXX)` in jouw project.
4. **RTSan vangt niets**: markeer de functie die process aanroept `[[clang::nonblocking]]` (achter de parameterlijst) én
   `noinline`, en maak een kanarie (malloc in process, pointer naar een globale variabele) die MOET falen.
5. **auval vindt de AU niet**: kopieer naar `~/Library/Audio/Plug-Ins/Components`, dan `killall -9 AudioComponentRegistrar`.
6. **Denormale invoer**: de DSP regelt het zelf; zet geen eigen FTZ-truc ervoor.
7. **pluginval `--rtcheck` op macOS vangt calloc in clap-wrapper** (`_tlv_get_addr` ← `AlwaysAudioThread` ←
   `WrapAsAUV2::Render`): twee `thread_local`-tellers die dyld per thread lui aanmaakt. Oplossing:
   `patches/clap-wrapper-geen-thread-local-in-process.patch`. Alle patches gaan erop met
   `PATCH_COMMAND sh <pakket>/gereedschap/pas-patches-toe.sh <pakket>/patches` (in naamvolgorde, overgeslagen als hij er
   al op zit, anders faalt een tweede configure; een `;` direct in PATCH_COMMAND splitst CMake in losse argumenten).
8. **clap-validator kleurt zijn uitvoer altijd** (ook met NO_COLOR): haal de ANSI-codes weg vóór je op "0 failed" grept,
   of vertrouw op de exitcode.
9. **pluginval v2**: `pluginval validate …` (de vlag `--validate` is verouderd); de null-test gebruikt `pluginval test`.
10. **De AU meldt geen TailTime** (auval: "Tail Time: Recommended Property is not supported"): clap-wrapper v0.16.0 geeft
   `SupportsTail()` vast false. Logic hoort de staart dan nooit (bounce met staart). Oplossing:
   `patches/clap-wrapper-auv2-tailtime.patch`.
11. **Draden**: zolang de plugin actief is, raakt alleen de audiothread het Ontwerp aan. `params.get_value` en
   `state.save` lezen een atomaire kopie; `state.load` zet waarden klaar die process of flush overneemt;
   `tail.get` leest een staart die process uitrekende (`staartSec()` leest de smoothers).
12. **CLAP-headers** komen van clap-wrapper (`CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON` haalt CLAP 1.2.x op; doel `clap`);
   het pakket heeft ze niet zelf.
13. **pluginval AU "Parameter thread safety" crasht af en toe in process** (`VALIDATION CRASHED`, process ←
   `ProcessAdapter::process` ← `WrapAsAUV2::Render`): clap-wrapper v0.16.0 zet in `addParameterEvent` events in
   dezelfde vectors die process op de audiothread sorteert en wist, zonder lock, vanaf de thread van SetParameter.
   Oplossing: `patches/clap-wrapper-auv2-setparameter-draadveilig.patch` (gevonden door een vers gesprek, proef F1).
   Draai pluginval op de AU met `--repeat 3`: de race sloeg één op de drie runs toe.
14. **"Knoppen via de host" bij CLAP**: een CLAP-host stuurt een uitvoerevent van de plugin niet terug. Het paneel zet
   een verandering als BEGIN/WAARDE/EIND in een lock-vrije rij en vraagt `request_flush`; process of `params.flush`
   pusht ze als `CLAP_EVENT_PARAM_GESTURE_BEGIN`/`PARAM_VALUE`/`GESTURE_END` naar de host én past de waarde zelf
   toe, zoals een event van de host. Het paneel raakt het Ontwerp nooit. Onder AUv2 doet `request_flush` niets
   (clap-wrapper v0.16.0): de host hoort de verandering pas bij de volgende Render.
15. **clap-validator 0.4.1 loopt vast op een preset-container met meer dan één preset** (`preset-discovery-crawl` en
   `-load`: "Timed out after 45 seconds"): `begin_preset` houdt de mutex `result` vast en `flush_preset` pakt hem
   opnieuw. Gevonden door twee verse gesprekken los van elkaar (F2, F3). Zolang dat niet gerepareerd is: per preset een
   provider met een container van één, of één bestand per preset (FILE-locatie, geen load key).
16. **Statische initialisatie in de AU**: de AU van clap-wrapper heeft een globale `Clap::Library` die
   `clap_entry.init` al tijdens de statische initialisatie aanroept, mogelijk vóór jouw eigen globals. Een globale
   `std::string` die init vult wordt daarna weer leeg gemaakt: gebruik een functie-static.

17. **Kanaalindelingen kiest elk formaat anders**: CLAP via `audio-ports-config`, de VST3 van clap-wrapper via
   `configurable-audio-ports` (`setBusArrangements`), de AUv2 van clap-wrapper v0.16.0 alleen de vaste poort (zonder
   patch biedt Logic de AU alleen stereo aan). Implementeer beide extensies; `patches/clap-wrapper-auv2-volgt-presets-
   mono-stereo.patch` maakt de indelingen van audio-ports-config de AU-kanaalindelingen. Alle drie de verse gesprekken
   van fase 5 (F1, F2, F3) moesten hem zelf bouwen.
18. **pluginval v2 op de opdrachtregel**: bij `validate` staat het pluginpad vóór `--sample-rates`/`--block-sizes` (die
   lijsten slokken anders het pad op); `pluginval test` lost een relatief pad op tegen de map van zijn config: geef
   absolute paden. De GitHub-tools geven van een job alleen de laatste 5000 logregels: zet bewijsstappen achteraan.
19. **Linken faalt op Linux zonder PIC**: `relocation R_X86_64_PC32 … recompile with -fPIC` bij het .clap. De impl is een
   statische bibliotheek die in drie gedeelde modules gaat, en clap-wrapper zet `CMAKE_POSITION_INDEPENDENT_CODE` alleen
   in zijn eigen scope: `set(CMAKE_POSITION_INDEPENDENT_CODE ON)` in jouw project (twee verse gesprekken, 18 okt).
20. **Het bewijs staat in de samenvatting, niet in de staart van het log**: `ci-null.sh` schrijft zijn tabellen naar
   `GITHUB_STEP_SUMMARY` en naar stderr, en de GitHub-tools geven alleen de staart van een job. Laat elke bewijsstap
   (auval, pluginval, clap-validator, null-test) aan het eind van de job nog één samenvattende regel schrijven.
   `ci-null.sh pluginval` zet `$PV` zonder aanhalingstekens neer: `"xvfb-run -a …/pluginval"` werkt als pluginval-commando.
21. **"Oneindig" bestaat niet in een AU**: CLAP meldt een oneindige staart als `INT32_MAX` of meer, en de AU van
   clap-wrapper deelt dat door de samplerate (op 48 kHz ±12 uur). Dat is goed genoeg; verwacht in Logic geen "oneindig".
22. **Een toets midden in een CI-stap faalt niet op `a && b`**: GitHub draait `run:` met `bash -e`, en een mislukte `a`
   in een `&&`-lijst laat de stap doorgaan. Een mislukte auval ging zo als groen door (proef G1). Zet elke controle
   als eigen regel of schrijf `… || exit 1`.
23. **Tekst ↔ waarde zonder locale**: `snprintf` en `strtod` volgen `LC_NUMERIC` van het proces, en dat zet de host.
   Komma vervangen door een punt en dan `strtod` breekt in een host met een komma-locale. Maak en lees de getallen zelf
   (gehele delen en decimalen), los van de locale (proef G2; clap-validator en pluginval vangen het niet).
## Hoe je weet dat het klopt
De poorten in PROMPTS.md. Een poort is groen als CI groen is, niet als jij denkt dat het goed is. Loopt iets vast:
beschrijf wat je zag en wat je probeerde in `VERSLAG.md`; zet een toets nooit uit om groen te worden.
