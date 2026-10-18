Stopcriterium gehaald.

# Uitslag fase 0 — Toetsenpad (proef g1)

Laatste CI-run met code (commit `cbf83b1`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39154088156
(jobs linux, rtsan en macos alle drie success). De commit met dit verslag verandert alleen docs; zijn run draait dezelfde jobs.

## Wat ik deed
- `plugin/CMakeLists.txt`: clap-wrapper v0.16.0 via FetchContent (`GIT_TAG v0.16.0`), `PATCH_COMMAND sh
  gereedschap/pas-patches-toe.sh patches` (alle zes patches, in naamvolgorde), `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`
  (de CLAP-headers komen mee, doel `clap`). De plugin is een statische bibliotheek `toetsenpad-impl`;
  `make_clapfirst_plugins` maakt CLAP, VST3 en AUv2 (`AUV2_INSTRUMENT_TYPE aumu`, maker `Vrve`, subtype `PTgg`) in
  `build/plugins/`. `dsp/` op het include-pad, gecompileerd met `-ffp-contract=off -fno-fast-math -fno-math-errno`.
  Valkuil 3 (`enable_language(OBJC/OBJCXX)` op macOS) en valkuil 2 (`-Wl,-z,nodelete` op de Linux-VST3) staan erin.
  Ook `pb-render` (uit `gereedschap/`) wordt meegebouwd in `build/`, zoals `ci-null.sh` verwacht; optie `PB_RTSAN`
  zet `-fsanitize=realtime` op pb-render, optie `PB_KANARIE` bouwt `build/kanarie/kanarie.clap`.
- `plugin/src/toetsenpad.cpp`: CLAP-instrument, features instrument/synthesizer/stereo; `audio-ports` 0 ingangen en 1
  stereo-uitgang; `note-ports` één ingang, alleen `CLAP_NOTE_DIALECT_CLAP` (valkuil 19); `latency` uit het Ontwerp.
  Het Ontwerp wordt per instantie gemaakt en krijgt in activate zijn geheugen (`geheugenNodig`/`activeer`); process
  schrijft stilte (de DSP aansluiten is fase 1). Geen globals die init vult (valkuil 16).
  Met `PB_KANARIE` doet process een `malloc` naar een globale `volatile` pointer (valkuil 4).
- `.github/workflows/ci.yml` met drie jobs; de bewijsstappen staan achteraan (valkuil 18).

## Poorten (bewijs uit de logs van run 39154088156)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1 (uit de bron via cargo), Linux | groen | `clap-validator: 44 tests run, 21 passed, 0 failed, 0 warnings, 23 skipped (exit 0)` |
| pluginval v2.0.0-beta3 strictness 10, VST3 Linux onder xvfb | groen | `SUCCESS` / `pluginval VST3 Linux: exit 0, laatste regel: SUCCESS` |
| RTSan (clang 20 via apt.llvm.org), plugin | groen | 6 renders door pb-render (`-fsanitize=realtime`): noten als CLAP en MIDI en 720 automatie-events, blok 512 en 3,128,1,4096,77: `RTSan: plugin schoon` |
| RTSan-kanarie moet falen | groen (faalt zoals bedoeld) | `==4649==ERROR: RealtimeSanitizer: unsafe-library-call` … `malloc in real-time context` ← `pluginProcess` ← `doeProcess`; `RTSan-kanarie gevangen zoals bedoeld (exit 43)` |
| auval -strict -v aumu PTgg Vrve (macOS arm64) | groen | `aumu PTgg Vrve  -  Varve: PB Toetsenpad` in `auval -a`; `AU VALIDATION SUCCEEDED.` |
| pluginval strictness 10, `--rtcheck enabled`, `--repeat 3`, AU | groen | `Running tests 3 times` (87 testblokken), `SUCCESS`, `pluginval AU: exit 0` |
| pluginval strictness 10 VST3 macOS via `gereedschap/pluginval-rt.sh` | groen | `SUCCESS`, `real-time-schendingen: 0, waarvan pluginval-host (MidiEventList::addEvent): 0` |
| artefact `macos-keten_toetsenpad` | aanwezig | `Artifact macos-keten_toetsenpad has been successfully uploaded!` (de drie bundels, elk met `ditto -c -k --keepParent` gezipt, zie LEERPAD.md) |

## Pushes
Twee pushes tot CI groen was.
1. `5b46f78`: linux en macos meteen groen; rtsan rood door mijn eigen fout: ik had in de RTSan-stap ook een render met
   `--state-rondje` gezet, en pb-render stopt dan met `geen state-extensie` (de state komt pas in fase 1). De zes
   andere RTSan-renders waren al schoon en de kanarie was nog niet aan de beurt.
2. `cbf83b1`: die render eruit; alle drie de jobs groen.

Lokaal (Linux, clang 18) had ik vóór de eerste push al gebouwd en clap-validator 0.4.1 en pluginval 10 op de VST3
gedraaid (beide groen). RTSan kon ik lokaal niet proberen: apt.llvm.org was vanuit deze omgeving niet bereikbaar.

## Onduidelijk of fout in het pakket
- Niets geblokkeerd. Geen toets of validator uitgezet.
- De opdracht noemt het state-rondje niet, maar `ci-null.sh clap` (fase 1) doet het wel: wie in fase 0 al pb-render
  breder inzet, loopt erop vast zolang er geen state-extensie is. Een zin in PROMPTS.md fase 0 ("pb-render zonder
  `--state-rondje`; state komt in fase 1") zou dat voorkomen.
- pluginval `--repeat 3` drukt maar één `SUCCESS` af aan het eind; het bewijs dat er drie rondes waren is de regel
  `Running tests 3 times`, niet het aantal keer SUCCESS.
- De patches gingen schoon op v0.16.0; de AU-patches (presets, mono/stereo) hebben in fase 0 niets te doen, maar
  auval en pluginval waren er zonder klagen mee tevreden met een plugin zonder presets en zonder audio-ports-config.
