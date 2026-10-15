# UITSLAG fase 0 — skelet, CI en alle validators

**Stopcriterium gehaald.** Laatste CI-run met codewijzigingen (commit `25f3fc5`), alles groen:
https://github.com/CKrebbers/pluginbouwer/actions/runs/38791862652
(Deze uitslag wordt als laatste gecommit en start nog een run. Die bevat alleen dit bestand en `TASKS.md`.)

## Per poort
| poort | uitslag | bewijs in het log |
|---|---|---|
| clap-validator 0.4.1 (uit de bron, cargo), Linux | groen | `44 tests run, 19 passed, 0 failed, 0 warnings, 25 skipped` (de stap faalt bij ≥ 1 mislukte toets) |
| pluginval v2.0.0-beta3 strictness 10, VST3 Linux (xvfb) | groen | `SUCCESS` |
| RTSan (clang 20, pb-render `-fsanitize=realtime`), plugin schoon | groen | twee renders (blok 512 en wisselend 3,128,1,4096,77) zonder melding |
| RTSan-kanarie (malloc in process) moet falen | groen (faalt zoals bedoeld) | `RealtimeSanitizer: unsafe-library-call … malloc … plProcess … doeProcess`, exitcode 43 |
| auval -strict -v aufx PDca Vrve, macOS arm64 | groen | de stap controleert op `AU VALIDATION SUCCEEDED` |
| pluginval 10 `--rtcheck enabled`, AU macOS | groen | `SUCCESS` |
| pluginval 10 `--rtcheck enabled`, VST3 macOS | groen | `SUCCESS` (ook de Steinberg-validator: 537 passed, 0 failed) |
| artefact `macos-keten_dub_echo` (.clap, .vst3, .component, ad-hoc gesigneerd) | aanwezig | artefact-id 11999449240 |

## Wat ik deed
- `plugin/CMakeLists.txt` werkt zo: `cmake -S plugin -B build && cmake --build build`.
  - CLAP 1.2.6 en clap-wrapper v0.16.0 komen binnen via FetchContent, met `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`.
  - `make_clapfirst_plugins` maakt de CLAP, de VST3 en, op macOS, de AUv2. De uitvoer staat in `build/plugins/`, zoals `gereedschap/ci-null.sh` verwacht.
  - `pb-render` wordt in `build/` gebouwd. Naam, versie, clap-id en de AU-codes (`Vrve`/`PDca`, type `aufx`) leest CMake uit `dsp/ontwerpen/keten_dub_echo.h`.
  - De DSP-code wordt gecompileerd met `-ffp-contract=off -fno-fast-math -fno-math-errno`.
- `plugin/src/dub_echo.cpp` is de CLAP-schil:
  - De descriptor komt uit `pb::Ontwerp::META`. Er is één stereo in- en uitgang (`audio-ports`) en `latency` uit het Ontwerp.
  - `activate` reserveert het geheugen van het Ontwerp (`geheugenNodig`/`activeer`). `process` geeft de invoer ongewijzigd door, zoals fase 0 toestaat.
  - Met `-DPB_KANARIE=ON` doet `process` een `malloc` en zet de pointer in een globale variabele.
- Valkuilen uit CLAUDE.md:
  - 1: de patch zit via `PATCH_COMMAND` (git apply) in FetchContent.
  - 2: `-Wl,-z,nodelete` staat op de Linux-VST3.
  - 3: `enable_language(OBJC/OBJCXX)` staat aan op macOS.
  - 4: `pb-render` had al `[[clang::nonblocking]]` en `noinline`. De kanarie bewijst dat RTSan iets vangt.
  - 5: de CI installeert in `~/Library/Audio/Plug-Ins/…` en doet daarna `killall -9 AudioComponentRegistrar`.
- `.github/workflows/ci.yml` heeft drie jobs: `linux`, `rtsan` en `macos` (macos-15, arm64). Logs van pluginval en auval worden als artefact bewaard.

## Waar het vastliep en wat ik veranderde (nieuwe valkuil, niet in het pakket)
**pluginval `--rtcheck enabled` op macOS faalde in clap-wrapper zelf, niet in de plugin.** De eerste run (commit `6df4e08`):
https://github.com/CKrebbers/pluginbouwer/actions/runs/38790167711

```
Real-time violation: intercepted call to real-time unsafe function calloc in real-time context!
  wrap_calloc ← dyld::ThreadLocalVariables::instantiateVariable ← _tlv_get_addr
  ← Clap::Plugin::AlwaysAudioThread() ← WrapAsAUV2::Render
```

- **Oorzaak:** clap-wrapper v0.16.0 houdt in `src/clap_proxy.cpp` twee tellers bij als `static thread_local` (`g_main_thread_override`/`g_audio_thread_override`). Render (AU) en process (VST3) verhogen er één met `AlwaysAudioThread()`.
- **Waarom dat alloceert:** op macOS maakt dyld een `thread_local` in een dylib pas aan bij de eerste aanraking per thread, met `calloc`. In pluginval gebeurt dat in de test "Open editor whilst processing", die op een nieuwe thread rendert. In een echte host gebeurt het bij de eerste render op elke audiothread.
- **Oplossing:** een tweede patch, `patches/clap-wrapper-geen-thread-local-in-process.patch`. Die zet de tellers op POSIX in een `pthread_key` die bij het laden wordt aangemaakt; `pthread_getspecific`/`setspecific` alloceren niet. Op Windows blijft het een `thread_local`.
- **Wat ik niet deed:** ik heb geen toets uitgezet en niets aan `dsp/` veranderd.
- **Kanttekening:** dit is een wijziging aan gereedschap van derden. Hij hoort bij clap-wrapper upstream (issue/PR), en verdient een plek als valkuil 7 in CLAUDE.md van het pakket.

## Onduidelijk of opvallend in het pakket
- **clap-validator `process-audio-denormals` geeft soms een WAARSCHUWING:** "took 2.76x longer". Ik zag het lokaal één keer op drie runs, in CI niet. Het is een tijdmeting op een doorgeef-process van een paar ms. Een waarschuwing is geen mislukking, dus de poort ("0 mislukt") blijft groen. In fase 1 moet je dit wel in de gaten houden; de DSP regelt denormals zelf (valkuil 6).
- **De ontbrekende patch:** het pakket noemt maar één patch voor clap-wrapper, maar `--rtcheck enabled` op macOS haalt v0.16.0 niet zonder de tweede (zie hierboven).
- **De bouwmap:** PROMPTS.md zegt "plugin in `plugin/`", terwijl `ci-null.sh` `<build>/pb-render` en `<build>/plugins/*.clap` in één bouwmap verwacht. Ik heb daarom pb-render in hetzelfde CMake-project gezet (`plugin/CMakeLists.txt`); fase 1 kan zo `sh gereedschap/ci-null.sh clap build` draaien.
- **apt.llvm.org vanuit de bouwomgeving:** die was vanuit mijn sandbox niet bereikbaar, dus de RTSan-job heb ik alleen in CI kunnen toetsen. De Linux-toetsen (build, clap-validator, pluginval) draaiden lokaal en in CI hetzelfde.
