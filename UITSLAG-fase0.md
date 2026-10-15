# UITSLAG fase 0 — skelet, CI en alle validators

**Stopcriterium gehaald.** Laatste CI-run met code: <https://github.com/CKrebbers/pluginbouwer/actions/runs/38791994775>
(commit `263e5f3`). De commit met dit bestand verandert alleen docs; die run draait dezelfde code opnieuw.

## Per poort
| poort | uitslag | bewijs in het log |
|---|---|---|
| Linux bouwen (clang) | groen | job "Linux" |
| clap-validator 0.4.1 (uit de bron via cargo) | groen | `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` (de stap leest ook "0 failed" uit het log) |
| pluginval v2.0.0-beta3 strictness 10, VST3 Linux (xvfb) | groen | `SUCCESS`, exitcode 0 |
| RTSan (clang 20, pb-render `-fsanitize=realtime`) schoon | groen | 4 testsignalen × (blok 512, wisselende blokken, state-rondje) |
| RTSan-kanarie faalt zoals bedoeld | groen | `RealtimeSanitizer: unsafe-library-call … malloc … in pProcess`, exitcode 43 |
| macOS arm64 bouwen + ad-hoc signeren + installeren | groen | `codesign --verify --deep --strict` op .clap/.vst3/.component |
| auval -strict -v aufx PDca Vrve | groen | `AU VALIDATION SUCCEEDED` (de stap grept erop) |
| pluginval 10 `--rtcheck enabled`, AU | groen | exitcode 0 |
| pluginval 10 `--rtcheck enabled`, VST3 macOS | groen | exitcode 0 |
| artefact `macos-keten_dub_echo` | groen | .component, .vst3, .clap |

Niets is uitgezet of overgeslagen (geen `--skip-gui-tests`, geen `--disabled-tests`, geen `-x` bij clap-validator).

## Wat ik deed
- `plugin/CMakeLists.txt`: clap-wrapper v0.16.0 via FetchContent met `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON` en
  `make_clapfirst_plugins` (CLAP, VST3, AUV2; Vrve/PDca/aufx). Patches via `PATCH_COMMAND` met een klein script
  (`plugin/cmake/pas-patch-toe.cmake`) dat overslaat als de patch er al op zit (anders faalt `git apply` bij een
  tweede configure). Valkuil 2 (`-z nodelete` op de Linux-VST3) en 3 (`enable_language(OBJC/OBJCXX)`) vooraf
  ingebouwd. pb-render zit in dezelfde build (`build/pb-render`, plugins in `build/plugins/`, zoals ci-null.sh verwacht).
- `plugin/src/dub_echo.cpp`: de CLAP-schil. Ik heb process meteen op het Ontwerp aangesloten (de prompt stond stil of
  doorgeven toe, maar aansluiten was even klein): 8 params (id/bereik/standaard uit `PARAMS`), events op hun sample,
  state `PBST` zoals ARCHITECTUUR.md, latency en tail. Lokaal geeft `sh gereedschap/ci-null.sh clap build` al
  **bit voor bit** gelijk voor alle gevallen (blok 512, wisselend, state-rondje). De null-test zit nog niet in CI:
  dat is fase 1.
- RTSan-kanarie: CMake-optie `DUB_ECHO_KANARIE` (malloc in process, pointer naar een globale `volatile`).
- `.github/workflows/ci.yml`: de drie jobs uit de prompt.

## Waar het vastliep (en wat ik deed)
1. **macOS, pluginval `--rtcheck` op de AU**: `intercepted call to real-time unsafe function calloc`, stack:
   `_tlv_get_addr` ← `Clap::Plugin::AlwaysAudioThread` ← `WrapAsAUV2::Render`. Oorzaak in clap-wrapper v0.16.0
   (`clap_proxy.cpp`): de tellers `g_main_thread_override`/`g_audio_thread_override` zijn `thread_local`, en dyld
   maakt de opslag daarvan lui aan met calloc bij de eerste toegang in een thread, dus op de audiothread van de
   host. Hetzelfde pad zit in `WrapAsVST3::process`. Oplossing: tweede patch
   `patches/clap-wrapper-threadcheck-zonder-tls.patch` — op macOS/Linux staan die tellers in pthread-keys (vaste
   TSD-slots, geen allocatie; de keys worden bij het laden gemaakt), Windows houdt `thread_local`. Daarna groen.
   **Dit staat niet in de valkuilen van CLAUDE.md**; voorstel: als valkuil 7 toevoegen.

## Wat in het pakket onduidelijk of fout was
- CLAUDE.md noemt één patch; voor `--rtcheck` op macOS is een tweede nodig (hierboven).
- pluginval v2.0.0-beta3 meldt `--validate is deprecated`; CI gebruikt `pluginval validate …`. `ci-null.sh` gebruikt
  al `pluginval test`, dat klopt.
- SPEC.md verwijst bij de toleranties naar "PROMPT.md"; bedoeld is waarschijnlijk PROMPTS.md.
- In de bouwomgeving was apt.llvm.org niet bereikbaar, dus RTSan kon ik alleen in CI draaien (daar werkt het:
  de kanarie wordt gevangen in `pProcess`, dus de `noinline` + `[[clang::nonblocking]]` in pb-render werkt).
- De prompt zegt "process mag stil zijn"; met een stille plugin zeggen pluginval en clap-validator weinig over de
  DSP-aansluiting. Ik zou fase 0 en de aansluiting samen laten gaan (de null-test blijft fase 1).
