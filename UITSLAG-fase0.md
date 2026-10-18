Stopcriterium gehaald.

# Uitslag fase 0 — skelet, CI en alle validators (proef G3)

Laatste CI-run met de code (commit `4e6e85b`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39154010166
(alle drie de jobs groen). Deze commit voegt alleen dit verslag en het vinkje in TASKS.md toe; CI draait er opnieuw op.

## Wat ik deed
- `plugin/src/plugin.cpp`: CLAP-schil. Descriptor uit `Ontwerp::META`, features `instrument`, `synthesizer`, `stereo`.
  Extensies: `audio-ports` (0 ingangen, 1 stereo-uitgang "Uit"), `note-ports` (1 ingang, alleen `CLAP_NOTE_DIALECT_CLAP`,
  valkuil 19), `latency` (uit het Ontwerp, 0). Het Ontwerp wordt in `init` gemaakt, krijgt in `activate` zijn geheugen
  (`geheugenNodig`/`activeer`), en `process` schrijft stilte (constant_mask gezet). Geen params/state: dat is fase 1–2.
  De entry-functies zitten in de impl-bibliotheek; `plugin/src/entry.cpp` exporteert `clap_entry` (geen globale
  std::string, valkuil 16).
- `plugin/CMakeLists.txt`: clap-wrapper v0.16.0 via FetchContent met
  `PATCH_COMMAND sh gereedschap/pas-patches-toe.sh patches` (alle zes, naamvolgorde; een tweede configure slaagt lokaal),
  `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON` (CLAP-headers via het doel `clap`), `make_clapfirst_plugins` met CLAP, VST3 en
  AUV2, AU `Varve`/`Vrve`/`PTgg`/`aumu`. DSP-vlaggen `-ffp-contract=off -fno-fast-math -fno-math-errno`.
  `enable_language(OBJC/OBJCXX)` op macOS (valkuil 3), `-Wl,-z,nodelete` op de Linux-VST3 (valkuil 2).
  Uitvoer in `build/plugins/`, `build/pb-render` (zoals `ci-null.sh` verwacht). Optie `PB_RTSAN` bouwt pb-render met
  `-fsanitize=realtime`; optie `PB_KANARIE` bouwt `build/kanarie/kanarie.clap`: dezelfde plugin met een malloc in
  process, pointer naar een globale `volatile` (valkuil 4).
- `.github/workflows/ci.yml`: jobs `linux`, `rtsan`, `macos` (macos-15, arm64).

## Poorten (bewijs uit het log van run 39154010166)
| poort | uitslag | bewijs |
|---|---|---|
| Linux bouwen (clang) | groen | job "Linux", stap Bouwen |
| clap-validator 0.4.1 (uit de bron via cargo) | groen | `Installed package clap-validator v0.4.1 (…tag=0.4.1#152b9823)`; `44 tests run, 21 passed, 0 failed, 0 warnings, 23 skipped`; exitcode 0 (ANSI eerst weggehaald, valkuil 8) |
| pluginval v2.0.0-beta3 strictness 10, VST3 Linux (xvfb) | groen | `Strictness level: 10` … `SUCCESS`, `pluginval exitcode: 0` |
| RTSan schoon | groen | pb-render (clang 20, `-fsanitize=realtime`) door het .clap: sinus/ruis/impuls, blok 512 en 3,128,1,4096,77, noten als CLAP en als MIDI, automatie (720 events): `RTSan: geen real-time-schending in process` |
| RTSan-kanarie faalt | groen (faalt zoals bedoeld) | `==ERROR: RealtimeSanitizer: unsafe-library-call` / ``Intercepted call to real-time unsafe function `malloc` in real-time context!`` / `#1 … (anonymous namespace)::process(…) plugin.cpp` / `kanarie exitcode: 43` |
| macOS arm64 bouwen, ad-hoc signeren, installeren | groen | `Image: macos-15-arm64`; `codesign --verify --strict` per bundel; `killall -9 AudioComponentRegistrar` |
| auval -strict -v aumu PTgg Vrve | groen | `VALIDATING AUDIO UNIT: 'aumu' - 'PTgg' - 'Vrve'`, `Input Scope … Default Bus Count:0`, `[0, 2]`, `Test MIDI PASS`, `AU VALIDATION SUCCEEDED.`, exitcode 0 |
| pluginval strictness 10 `--rtcheck enabled --repeat 3`, AU | groen | `Strictness level: 10`, `Running tests 3 times` … `SUCCESS`, `pluginval AU exitcode: 0` |
| pluginval strictness 10, VST3 macOS via `gereedschap/pluginval-rt.sh` (valkuil 22) | groen | `SUCCESS`, `real-time-schendingen: 0, waarvan pluginval-host (MidiEventList::addEvent): 0` |
| artefact `macos-keten_toetsenpad` | groen | `Artifact macos-keten_toetsenpad has been successfully uploaded!` (drie met `ditto -c -k --keepParent` gezipte bundels, zie LEERPAD.md) |

## Pushes tot CI groen
Eén push met code (`4e6e85b`), meteen groen. Vooraf lokaal (Linux) getoetst: bouwen, tweede configure, pluginval
strictness 10 op de VST3 onder xvfb (SUCCESS), clap-validator 0.4.1 (0 failed), pb-render door de .clap en de kanarie.
RTSan en macOS kon ik lokaal niet toetsen (apt.llvm.org niet bereikbaar uit de sandbox, geen Mac); die zag ik pas in CI.

## Onduidelijk of opvallend in het pakket
- De prompt zegt "ad-hoc signeren"; ik signeer de drie bundels met `codesign --force --sign - --timestamp=none`.
- auval meldt in deze fase geen "Tail Time"-regel bij de aanbevolen eigenschappen; de plugin heeft nog geen
  `tail`-extensie (valkuil 10 en de tailtime-patch horen bij fase 1, als `tail` uit het Ontwerp komt).
- `pb_noten.h` noemt `plugin/src/plugin.cpp` als de schil; ik heb die naam aangehouden.
- pluginval's Linux-binary heeft libasound/fontconfig/freetype nodig; die staan in de apt-stap.
- Niets uitgezet: geen `--skip-gui-tests`, geen uitgeschakelde toetsen, `--rtcheck enabled` op de AU.
