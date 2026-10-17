Stopcriterium gehaald.

# Uitslag fase 0 — Dub-echo (tak proef/f2)

Laatste CI-run met code (commit 2e7ee72): run 37 —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39036544613 — alle drie de jobs groen, **bij de eerste push**.
(De commit met dit bestand verandert alleen docs; zijn CI-run draait dezelfde code.)

## Wat ik deed
- `plugin/CMakeLists.txt`: clap-wrapper v0.16.0 via FetchContent met
  `PATCH_COMMAND sh gereedschap/pas-patches-toe.sh patches` (beide patches, naamvolgorde),
  `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`, `make_clapfirst_plugins` (CLAP, VST3, AUV2; AU `aufx` / `PDca` / `Vrve`,
  maker "Varve"). DSP-vlaggen `-ffp-contract=off -fno-fast-math -fno-math-errno` op de impl-bibliotheek, include-pad `dsp/`.
  Valkuil 2 (`-Wl,-z,nodelete` op de Linux-VST3) en valkuil 3 (`enable_language(OBJC/OBJCXX)` op macOS) staan er vanaf het begin in.
  `pb-render` wordt in hetzelfde project gebouwd (optie `PB_RTSAN` → `-fsanitize=realtime`); optie `PB_KANARIE` bouwt
  een tweede CLAP die in process `malloc` doet en de pointer in een globale `volatile` bewaart.
- `plugin/dub_echo.cpp` + `entry.cpp`: de CLAP-schil. Descriptor uit `Ontwerp::META`, stereo audio-ports, latency
  (`Ontwerp::latentie()`), tail (0 in deze fase). `activate` reserveert `Ontwerp::geheugenNodig(sr)` en roept `activeer`
  aan; `process` geeft de invoer ongewijzigd door (DSP aansluiten = fase 1). Nog geen params/state (fase 1).
- `.github/workflows/ci.yml`: jobs `linux`, `rtsan`, `macos` (macos-15, arm64).
- Lokaal (Linux) vooraf getoetst: build, pb-render pass-through bit voor bit gelijk aan de invoer, clap-validator
  0.4.1 "44 tests run, 19 passed, 0 failed, 0 warnings, 25 skipped", pluginval strictness 10 SUCCESS.

## Poorten (run 37)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| Linux bouwen met clang | groen | stap "Bouwen (clang)" geslaagd, `PB-Dub-echo.clap` en `.vst3` in `build/plugins` |
| clap-validator 0.4.1 (uit de bron, cargo) | groen, 0 mislukt | stap slaagt alleen bij exitcode 0 én `grep "tests run, .* 0 failed"` op de uitvoer zonder ANSI (valkuil 8); lokaal: 44 run, 19 passed, 0 failed, 25 skipped (geen params/state in fase 0) |
| pluginval v2.0.0-beta3 strictness 10, VST3, Linux/xvfb | groen | "vst3 validator … Result: 537 tests passed, 0 tests failed", "vst3 validator exited with code: 0", slotregel `SUCCESS` |
| RTSan (clang 20, apt.llvm.org) schoon | groen | 4 testsignalen × blok 512 en `3,128,1,4096,77` door pb-render met `-fsanitize=realtime`: "RTSan: geen overtreding in process" |
| RTSan-kanarie faalt | groen (faalt zoals bedoeld) | "ERROR: RealtimeSanitizer: unsafe-library-call / Intercepted call to real-time unsafe function `malloc` in real-time context!" #1 `process … plugin/dub_echo.cpp:68`, #2 `doeProcess … pb-render.cpp:97`; "Kanarie gevangen zoals bedoeld (exitcode 43)" |
| macOS arm64 bouwen, ad-hoc signeren, installeren, `killall -9 AudioComponentRegistrar` | groen | stappen geslaagd; `codesign --verify` op .clap/.vst3/.component |
| `auval -strict -v aufx PDca Vrve` | groen | stap slaagt alleen bij exitcode 0 én `grep "AU VALIDATION SUCCEEDED"` |
| pluginval strictness 10 `--rtcheck enabled`, AU | groen | exitcode 0 én `grep SUCCESS` |
| pluginval strictness 10 `--rtcheck enabled`, VST3 | groen | slotregel `SUCCESS` ("Time taken to run all tests: 32 secs") |
| artefact `macos-keten_dub_echo` | groen | "Artifact macos-keten_dub_echo has been successfully uploaded! … Artifact ID is 12102144141" |

Niets uitgezet: geen `--skip-gui-tests`, geen uitgeschakelde pluginval-tests, geen `continue-on-error`.

## Waar het schuurde / wat onduidelijk was in het pakket
- Niets liep vast. De valkuilen 1 (AU-params vóór Initialize) en 7 (thread_local in clap-wrapper) zijn al in de patches
  opgelost; of 7 echt nodig is, bewijst deze run niet los (de patch zat er vanaf het begin op), wel dat `--rtcheck` op
  macOS met de patch groen is.
- `clap/clap.h` staat niet in het pakket (CLAUDE.md zegt "CLAP-first (`clap/clap.h`)"): ik gebruik de CLAP-SDK die
  clap-wrapper zelf binnenhaalt (target `clap`, CLAP 1.2.6). Dat werkt, maar het pakket zou kunnen zeggen waar hij vandaan komt.
- De prompt zegt niet waar de kanarie moet zitten (pb-render of de plugin). CLAUDE.md valkuil 4 zegt "malloc in
  process", dus ik heb een aparte kanarie-CLAP gemaakt (`-DPB_KANARIE=ON`), zodat de gewone plugin schoon blijft.
- Vanuit de bouwomgeving was apt.llvm.org niet bereikbaar (proxy 403); RTSan is dus alleen in CI getoetst, niet lokaal.
- Fase 0 heeft nog geen params- en state-extensie; clap-validator slaat daardoor 25 tests over. Die komen in fase 1/2.
