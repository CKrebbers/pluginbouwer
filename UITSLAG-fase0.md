Stopcriterium gehaald.

# Uitslag fase 0 — skelet, CI en alle validators (proef F4, tak `proef/f4`)

Laatste CI-run met de code: https://github.com/CKrebbers/pluginbouwer/actions/runs/39154101610 (commit 9ba4237,
alle drie de jobs groen). De commit met dit verslag start nog een run; die verandert geen code.

## Wat ik deed
- `plugin/CMakeLists.txt`: clap-wrapper v0.16.0 via FetchContent, `PATCH_COMMAND sh gereedschap/pas-patches-toe.sh
  patches` (alle zes de patches in naamvolgorde; een tweede configure slaat ze over), `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`
  (de CLAP-headers komen zo mee, doel `clap`), `make_clapfirst_plugins` voor CLAP, VST3 en AUv2 (Vrve / PDca / aufx),
  uitvoer in `build/plugins/`, `enable_language(OBJC/OBJCXX)` op macOS (valkuil 3), `-Wl,-z,nodelete` op de Linux-VST3
  (valkuil 2), `-ffp-contract=off -fno-fast-math -fno-math-errno` op alles wat `dsp/` insluit. Ook `pb-render`
  (gegeven, `gereedschap/pb-render.cpp`) in `build/`, zoals `ci-null.sh` hem verwacht; `-DPB_RTSAN=ON` bouwt hem met
  `-fsanitize=realtime`, `-DPB_KANARIE=ON` zet een malloc in process (pointer naar een globale `volatile`).
- `plugin/src/dub_echo.cpp`: de CLAP-schil rond het Ontwerp (descriptor uit `META`/de #defines, 9 parameters uit
  `PARAMS` in hun eigen eenheden, state "PBST" v1 per ARCHITECTUUR.md met rescan en request_flush, audio-ports stereo,
  latency, tail). process splitst het blok bij elk event en geeft de invoer door (langs `pb::invoer`/`pb::uitvoer`);
  de parameters gaan al naar het Ontwerp (`zetParam`), maar `verwerk()` en `staartSec()` sluit fase 1 aan (tail meldt
  nu 0). Draden volgens valkuil 11 (atomaire kopie, vlag voor state.load terwijl actief).
- `plugin/src/tekst.cpp`: tekst ↔ waarde volgens de SPEC-tabel, zonder printf/strtod (locale); lokaal getoetst: de
  standaarden geven precies de tekst uit de tabel en elke tekst gaat heen en terug naar dezelfde tekst.
- `.github/workflows/ci.yml`: drie jobs, elk met een stap "Bewijs" aan het eind (valkuil 18).

## Poorten (uit het log van run 39154101610)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1 (uit de bron via cargo), Linux | groen | `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` (ANSI weggehaald, valkuil 8; ook exitcode 0) |
| pluginval v2.0.0-beta3 strictness 10, VST3 Linux, xvfb | groen | `SUCCESS` |
| RTSan (clang 20.1.8 via apt.llvm.org), pb-render op alle testsignalen (blok 512 en 3,128,1,4096,77) en automatie (720 events, state-rondje) | groen | `RTSan: schoon` |
| RTSan-kanarie | faalt zoals bedoeld | `==ERROR: RealtimeSanitizer: unsafe-library-call … malloc … #1 plProcess … #2 doeProcess`, exitcode 43 |
| auval -strict -v aufx PDca Vrve (macOS 15 arm64, ad-hoc gesigneerd, geïnstalleerd, `killall -9 AudioComponentRegistrar`) | groen | `AU VALIDATION SUCCEEDED.` (Tail Time wordt geverifieerd: de tailtime-patch werkt) |
| pluginval 10 `--rtcheck enabled --repeat 3`, AU | groen | `SUCCESS` |
| pluginval 10 `--rtcheck enabled`, VST3 macOS | groen | `SUCCESS` |
| artefact `macos-keten_dub_echo` | geüpload | .clap, .vst3, .component elk als `ditto`-zip (vaste naam, geen matrixnaam) |

## Pushes
Eén push tot CI groen. Vóór de push heb ik op Linux lokaal gebouwd en de plugin door pb-render (bit voor bit gelijk aan
de invoer, met state-rondje en wisselende blokken), clap-validator 0.4.1 en pluginval 10 (xvfb) gehaald; de twee
lokale bouwfouten (`request_flush` hoort bij `clap_host_params`, niet bij `clap_host`; de statische impl moet
`POSITION_INDEPENDENT_CODE` hebben omdat hij in drie gedeelde modules gaat) kwamen dus nooit in CI. macOS kon ik
lokaal niet toetsen; dat ging in één keer goed dankzij de patches en valkuilen in het pakket.

## Wat onduidelijk of fout was in het pakket
- Niets dat me blokkeerde. Kleine punten:
  - `CMAKE_POSITION_INDEPENDENT_CODE` staat nergens als valkuil: clap-wrapper zet het alleen in zijn eigen scope, en
    zonder faalt het linken van de .clap op Linux (`relocation R_X86_64_PC32 … recompile with -fPIC`).
  - auval geeft twee WARNINGs die niet van de plugin komen: "Preset name is not retained in retrieved class data" en
    "AU implements MusicDeviceMIDIEvent but is of type 'aufx'" (clap-wrapper). De validatie slaagt wel.
  - `gereedschap/pluginval-rt.sh` (rtcheck relaxed met een filter) wordt in de prompt niet genoemd; ik gebruikte
    `--rtcheck enabled` zoals de prompt vraagt, en dat slaagde voor AU en VST3 (een effect krijgt geen MIDI).
  - Een AU met `--repeat 3` duurt ruim 2 minuten; prima, maar het is de langste stap.
