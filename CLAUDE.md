# CLAUDE.md — bouwpakket "Toetsenpad" (toetsenpad)

Je bouwt een audioplugin uit dit pakket. Lees eerst SPEC.md, ARCHITECTUUR.md, TASKS.md en PROMPTS.md.
Nederlands in docs, commits en comments; code-identifiers Engels. De gebruiker (Clay) programmeert geen C++ en test in
Logic Pro (MacBook Air M1). Wat hij zelf moet doen staat in LEERPAD.md.

## Wat vaststaat (niet veranderen)
- **De DSP in `dsp/` is gegeven.** Herschrijf hem niet, "verbeter" hem niet, kopieer hem niet naar een andere vorm.
  De plugin moet bit voor bit klinken als de WASM van de tool; de verwachte uitvoer in `testsignalen/` komt daaruit.
- Compileer `dsp/` met `-ffp-contract=off -fno-fast-math -fno-math-errno` (ook op macOS met Apple clang).
- Stack: CLAP-first (`clap/clap.h`), en **clap-wrapper v0.16.0** (MIT) via CMake `make_clapfirst_plugins` voor VST3 en
  AUv2 (`CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`). AU-codes: maker `Vrve`, subtype `PTgg`, type `aumu` (staan ook als
  #define in `dsp/ontwerpen/keten_toetsenpad.h`).
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
19. **Note port alleen in het CLAP-dialect.** Met `CLAP_NOTE_DIALECT_MIDI` erbij maakt clap-wrapper v0.16.0 in de VST3
   16 × 130 "MIDI/controller"-parameters (IMidiMapping) die niet in de state staan: pluginval "Discrete parameter state
   restoration" faalt erop. De AU-wrapper geeft CC's toch als `CLAP_EVENT_MIDI` door, dus lees MIDI-bytes wel.
20. **Een instrument heeft geen audio-ingang**, ook niet in de AU: een host (of je eigen AU-toets) die een stream-formaat
   op de input-scope zet, krijgt een fout. pb-render en pluginval geven geen invoer mee als de plugin 0 ingangen meldt.
21. **AU-transport op tel 0**: clap-wrapper zet `CLAP_TRANSPORT_HAS_BEATS_TIMELINE` niet als de beat precies 0 is (`if
   (data._beat)`); het tempo komt wel door. Reken daar niet op een plek in de maat in het eerste blok.
22. **pluginval `--rtcheck enabled` op een VST3-instrument faalt in pluginval zelf**: zijn VST3-host alloceert als hij MIDI
   stuurt (`realloc` ← `juce::MidiEventList::addEvent`, geen frame van de plugin). Draai die stap met
   `sh gereedschap/pluginval-rt.sh <pluginval> <plugin.vst3>`: `--rtcheck relaxed`, en het script faalt zelf op elke
   andere schending. Zet rtcheck niet gewoon uit; de AU houdt `--rtcheck enabled`.
23. **pluginval's AU-host meldt een tempo** (JUCE zonder playhead: 120 bpm). Een sequencer die het hosttempo volgt, klinkt
   in `pluginval test` op de AU dus anders dan de WASM zonder transport: toets die gevallen in een eigen AU-host zonder
   host-callbacks. (Zonder sequencer speelt het niet.)
## Hoe je weet dat het klopt
De poorten in PROMPTS.md. Een poort is groen als CI groen is, niet als jij denkt dat het goed is. Loopt iets vast:
beschrijf wat je zag en wat je probeerde in `VERSLAG.md`; zet een toets nooit uit om groen te worden.
