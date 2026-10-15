# UITSLAG — fase 0 (skelet, CI en alle validators)

**Stopcriterium gehaald.** Laatste CI-run (groen, commit 8790d0c):
https://github.com/CKrebbers/pluginbouwer/actions/runs/38791789775

## Per poort
| poort | uitslag | bewijs in het log |
|---|---|---|
| Linux: bouwen met clang | groen | job "Linux" |
| clap-validator 0.4.1 (uit de bron via cargo) | groen | `44 tests run, 19 passed, 0 failed` (25 overgeslagen: extensies die er in fase 0 nog niet zijn) |
| pluginval v2.0.0-beta3, strictness 10, VST3 Linux (xvfb) | groen | `SUCCESS` |
| RTSan (clang 20, pb-render met `-fsanitize=realtime`), plugin schoon | groen | alle 4 testsignalen, blok 512 en wisselend 3,128,1,4096,77 |
| RTSan-kanarie faalt zoals bedoeld | groen | `RealtimeSanitizer: unsafe-library-call … malloc … pluginProcess`, exit 43 |
| macOS arm64: bouwen, ad-hoc signeren, installeren, `killall -9 AudioComponentRegistrar` | groen | job "macOS" |
| `auval -strict -v aufx PDca Vrve` | groen | `AU VALIDATION SUCCEEDED.` |
| pluginval strictness 10 `--rtcheck enabled`, AU | groen | `SUCCESS` |
| pluginval strictness 10 `--rtcheck enabled`, VST3 | groen | `537 tests passed, 0 tests failed`, `SUCCESS` |
| artefact `macos-keten_dub_echo` (.component, .vst3, .clap) | geüpload | |

Geen toets uitgezet of overgeslagen (geen `--skip-gui-tests`, geen `--disabled-tests`, geen `-x` bij clap-validator).

## Wat ik deed
- `plugin/CMakeLists.txt`: clap-wrapper v0.16.0 via FetchContent met `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`, de patch
  uit `patches/` via `PATCH_COMMAND` (idempotent, zodat een tweede configure niet faalt), `make_clapfirst_plugins`
  (CLAP, VST3, AUV2; `Vrve`/`PDca`/`aufx`), uitvoer in `build/plugins/` zoals `gereedschap/ci-null.sh` verwacht,
  `pb-render` in `build/`. DSP-vlaggen `-ffp-contract=off -fno-fast-math -fno-math-errno` op de plugin-bibliotheek.
  Valkuil 2 (`-Wl,-z,nodelete` op de Linux-VST3) en 3 (`enable_language(OBJC/OBJCXX)`) vooraf ingebouwd.
- `plugin/src/dub_echo.cpp`: CLAP-plugin; descriptor uit `pb::Ontwerp::META`, stereo audio-ports, latency uit het
  Ontwerp, geheugen van het Ontwerp in `activate` (mainthread) gereserveerd en `activeer` aangeroepen. `process` geeft
  in deze fase door. Parameters, state, tail en de DSP in process zijn fase 1.
- Kanarie: optie `PB_KANARIE` bouwt een tweede CLAP met `malloc` in process (pointer in een globale variabele).
- `.github/workflows/ci.yml`: drie jobs (Linux, RTSan, macOS) zoals de prompt vraagt. clap-validator wordt gecachet.

## Waar het misging, en wat in het pakket ontbrak
1. **Nieuwe valkuil (niet in CLAUDE.md): pluginval `--rtcheck` op macOS vangt een malloc in clap-wrapper zelf.**
   Stacktrace: `wrap_malloc ← dyld4::RuntimeState::_instantiateTLVs ← tlv_get_addr ← Clap::Plugin::AlwaysAudioThread
   ← WrapAsAUV2::Render`. clap-wrapper v0.16.0 houdt twee `static thread_local uint32_t`-tellers bij
   (`src/clap_proxy.cpp`); in een dynamisch geladen image alloceert dyld die per thread bij de eerste aanraking, dus
   midden in de render-callback van een nieuwe audiothread (pluginval-test "Open editor whilst processing").
   Oplossing: een tweede patch, `patches/clap-wrapper-thread-override-zonder-tlv.patch`, die de tellers via pthread-TSD
   bijhoudt (sleutels bij het laden gemaakt; get/set alloceren niet). Op Windows blijft het `thread_local`.
   Voorstel voor het pakket: deze patch en deze valkuil (nr. 7) opnemen.
2. clap-validator 0.4.1 kleurt zijn uitvoer altijd (`NO_COLOR` helpt niet); een grep op `0 failed` moet de ANSI-codes
   eerst verwijderen. Mijn eerste run faalde daarop terwijl clap-validator zelf 0 mislukt meldde.
3. Kleine onduidelijkheden: CLAUDE.md noemt `VERSLAG.md`, de opdracht `UITSLAG-fase0.md`; ik schreef alleen dit
   bestand. Waar de CMake-root moet staan zegt het pakket niet; ik koos `plugin/` (`cmake -S plugin -B build`), zodat
   `ci-null.sh clap build` in fase 1 zonder aanpassing werkt. De "denormals"-toets van clap-validator gaf lokaal
   één keer een (tijds)waarschuwing, in CI niet; geen fout.
