Stopcriterium gehaald.

# Uitslag fase 0 — Dub-echo (tak proef/f1)

Laatste CI-run (commit 854b250, eerste en enige ronde): https://github.com/CKrebbers/pluginbouwer/actions/runs/39036625634
— alle drie de jobs `success`.

## Wat ik deed
- `plugin/CMakeLists.txt`: clap-wrapper v0.16.0 via FetchContent, `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`, de patches
  via `PATCH_COMMAND sh gereedschap/pas-patches-toe.sh patches` (een tweede configure slaagt: lokaal getoetst).
  `make_clapfirst_plugins` met CLAP, VST3, AUV2; AU `aufx`/`PDca`/`Vrve` (naam "Varve : PB Dub-echo"). DSP-vlaggen
  `-ffp-contract=off -fno-fast-math -fno-math-errno` op het include-pad `dsp/`. Valkuil 2 (`-Wl,-z,nodelete` op de
  Linux-VST3) en 3 (`enable_language(OBJC/OBJCXX)`) staan erin. `pb-render` wordt in hetzelfde project gebouwd
  (`-DPB_RTSAN=ON` voegt `-fsanitize=realtime` toe); `-DPB_KANARIE=ON` maakt een malloc in process.
- `plugin/src/dub_echo.cpp`: CLAP-plugin rond `pb::Ontwerp` (descriptor uit `META`, 8 params uit `PARAMS` met id =
  SPEC-nummer, state "PBST" per id zoals ARCHITECTUUR.md, audio-ports stereo, latency uit het Ontwerp; geheugen van
  `geheugenNodig` gereserveerd in activate en `activeer` aangeroepen). process geeft de invoer door (DSP = fase 1).
- `.github/workflows/ci.yml`: jobs `linux`, `rtsan`, `macos` zoals de prompt vraagt.

## Poorten (bewijs uit het log van run 39036625634)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1 (uit de bron, cargo) | groen | stap "clap-validator" slaagt; hij grept (zonder ANSI) `tests run, … 0 failed`. Lokaal dezelfde versie: `44 tests run, 32 passed, 0 failed, 1 warnings, 11 skipped` (de waarschuwing: process-audio-denormals 2,2× trager; geen FTZ-truc gezet, valkuil 6) |
| pluginval v2.0.0-beta3 strictness 10, VST3 Linux (xvfb) | groen | `Time taken to run all tests: 12 secs` … `SUCCESS` (13:29:00) |
| RTSan (clang 20, apt.llvm.org) | groen | alle 4 testsignalen × (blok 512, wisselend, state-rondje): `RTSan: schoon` |
| kanarie | faalt zoals bedoeld | `==4825==ERROR: RealtimeSanitizer: unsafe-library-call` / ``Intercepted call to real-time unsafe function `malloc` in real-time context!`` / `#1 … (anonymous namespace)::process … dub_echo.cpp` / `#2 … doeProcess … pb-render.cpp` → `Kanarie gevangen door RTSan (exitcode 43), zoals bedoeld` |
| auval -strict -v aufx PDca Vrve (macOS 15 arm64) | groen | stap "auval -strict" slaagt; hij grept `AU VALIDATION SUCCEEDED` |
| pluginval strictness 10 `--rtcheck enabled`, AU macOS | groen | stap slaagt (exitcode 0 = SUCCESS) |
| pluginval strictness 10 `--rtcheck enabled`, VST3 macOS | groen | `Time taken to run all tests: 50 secs` … `SUCCESS` (13:30:53) |
| artefact `macos-keten_dub_echo` | er | `Artifact macos-keten_dub_echo has been successfully uploaded! … Artifact ID is 12101818978` (.clap, .vst3, .component, ad-hoc gesigneerd) |

## Waar ik op stuitte / wat in het pakket onduidelijk was
- clap-validator `state-reproducibility-*` faalde eerst lokaal (3 mislukt): na `state.load` moet de plugin
  `host_params->rescan(CLAP_PARAM_RESCAN_VALUES)` aanroepen, anders telt het als "waarden veranderd zonder rescan".
  Opgelost vóór de eerste push. Dit staat niet in CLAUDE.md/ARCHITECTUUR.md; een regel bij "State" zou helpen.
- De CLAP-headers zitten niet in het pakket ("`clap/clap.h`" in CLAUDE.md); ze komen van clap-wrapper (doel `clap`).
  Werkt, maar het pakket zegt het niet.
- apt.llvm.org was vanuit mijn bouwomgeving niet bereikbaar, dus RTSan is alleen in CI getoetst (daar groen).
- De valkuilen 1 en 7 (patches) zijn niet apart zichtbaar in fase 0; ze zitten erop (lokaal gecontroleerd in de
  bron na configure) en `--rtcheck` op macOS bleef schoon.
- Niets uitgezet; geen `--skip-gui-tests` (de plugin heeft nog geen GUI).
