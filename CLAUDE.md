# CLAUDE.md — bouwpakket "Dub-echo" (dub_echo)

Je bouwt een audioplugin uit dit pakket. Lees eerst SPEC.md, ARCHITECTUUR.md, TASKS.md en PROMPTS.md.
Nederlands in docs, commits en comments; code-identifiers Engels. De gebruiker (Clay) programmeert geen C++ en test in
Logic Pro (MacBook Air M1). Wat hij zelf moet doen staat in LEERPAD.md.

## Wat vaststaat (niet veranderen)
- **De DSP in `dsp/` is gegeven.** Herschrijf hem niet, "verbeter" hem niet, kopieer hem niet naar een andere vorm.
  De plugin moet bit voor bit klinken als de WASM van de tool; de verwachte uitvoer in `testsignalen/` komt daaruit.
- Compileer `dsp/` met `-ffp-contract=off -fno-fast-math -fno-math-errno` (ook op macOS met Apple clang).
- Stack: CLAP-first (`clap/clap.h`), en **clap-wrapper v0.16.0** (MIT) via CMake `make_clapfirst_plugins` voor VST3 en
  AUv2 (`CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`). AU-codes: maker `Vrve`, subtype `PDca` (staan ook als
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

## Hoe je weet dat het klopt
De poorten in PROMPTS.md. Een poort is groen als CI groen is, niet als jij denkt dat het goed is. Loopt iets vast:
beschrijf wat je zag en wat je probeerde in `VERSLAG.md`; zet een toets nooit uit om groen te worden.
