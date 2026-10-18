Stopcriterium gehaald.

# Uitslag fase 0 — proef G2 (tak `proef/g2`)

Laatste CI-run (groen, alle drie de jobs): https://github.com/CKrebbers/pluginbouwer/actions/runs/39155115618
(commit `c8111e8`). De run ervoor, op de eerste commit `c7ec31f`, was ook al helemaal groen:
https://github.com/CKrebbers/pluginbouwer/actions/runs/39153986505. Deze commit (dit bestand) verandert alleen dit
verslag.

## Wat ik deed
- `plugin/CMakeLists.txt`: CLAP-first, clap-wrapper v0.16.0 via FetchContent (`GIT_TAG v0.16.0`), alle patches via
  `PATCH_COMMAND sh <pakket>/gereedschap/pas-patches-toe.sh <pakket>/patches` (naamvolgorde, een tweede configure slaat
  ze over), `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON` (de CLAP-headers komen zo mee; doel `clap`), `make_clapfirst_plugins`
  voor CLAP, VST3 en AUv2 met maker `Vrve`, subtype `PTgg`, `AUV2_INSTRUMENT_TYPE aumu`. De impl (statische
  bibliotheek) krijgt `-ffp-contract=off -fno-fast-math -fno-math-errno` en include-pad `dsp/`. Valkuil 2
  (`-Wl,-z,nodelete` op de Linux-VST3) en valkuil 3 (`enable_language(OBJC/OBJCXX)` op macOS) zitten erin.
  `pb-render` is een doel in hetzelfde project (`-DPB_RTSAN=ON` voor `-fsanitize=realtime`; `-DPB_KANARIE=ON` laat
  process `malloc` doen en de pointer in een globale variabele zetten).
- `plugin/src/plugin.cpp` + `entry.cpp`: één plugin, naam/id/maker/versie uit `Ontwerp::META`; features `instrument`,
  `synthesizer`, `stereo`; audio-ports 0 ingangen en 1 stereo-uitgang; één note port (alleen het CLAP-dialect,
  valkuil 19); latency (uit het Ontwerp) en tail (0). `activate` reserveert het geheugen van `Ontwerp::geheugenNodig` en
  activeert het Ontwerp op de mainthread; process schrijft stilte en leest nog geen events (fase 1). Geen globale
  objecten die `init` vult (valkuil 16).
- `.github/workflows/ci.yml` met drie jobs:
  - **linux** (ubuntu-24.04, clang): bouwen, rookproef met pb-render, clap-validator 0.4.1 (`cargo install --git … --tag
    0.4.1 --locked`, gecachet), pluginval v2.0.0-beta3 strictness 10 op de VST3 onder `xvfb-run` (pad vóór de opties,
    valkuil 18).
  - **rtsan**: clang 20 via apt.llvm.org (+ `libclang-rt-20-dev`), plugin + pb-render met `-fsanitize=realtime`;
    renders met blok 512 en wisselend (3,128,1,4096,77), noten als CLAP en als MIDI, de automatisering en een sinus;
    daarna een kanarie-bouw die moet falen (exitcode ≠ 0 én "RealtimeSanitizer" en "malloc" in het log).
  - **macos** (macos-15, arm64): bouwen, `codesign --force --deep --sign -` + verify, de bundels met `ditto -c -k
    --keepParent` gezipt als artefact `macos-keten_toetsenpad` (vaste naam, geen matrix), installeren in
    `~/Library/Audio/Plug-Ins/{Components,VST3,CLAP}`, `killall -9 AudioComponentRegistrar`, pluginval op de VST3 via
    `gereedschap/pluginval-rt.sh` (valkuil 22), pluginval op de AU met `--strictness-level 10 --rtcheck enabled
    --repeat 3`, en als laatste `auval -strict -v aumu PTgg Vrve`.

## Poorten (run 39155115618)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| clap-validator (Linux) | groen | `44 tests run, 21 passed, 0 failed, 0 warnings, 23 skipped`, `clap-validator exit: 0` (overgeslagen: params, state, presets, die er in fase 0 nog niet zijn) |
| pluginval 10, VST3 Linux | groen | `Strictness level: 10` … `vst3 validator exited with code: 0` … `SUCCESS` |
| pluginval 10, VST3 macOS (`--rtcheck relaxed` + script) | groen | `SUCCESS`, `real-time-schendingen: 0, waarvan pluginval-host (MidiEventList::addEvent): 0` |
| pluginval 10, AU macOS (`--rtcheck enabled --repeat 3`) | groen | `Strictness level: 10` … `SUCCESS` (de tests draaiden drie keer, o.a. "Allocations during process" 3×) |
| auval -strict | groen | `Reported Channel Capabilities (explicit): [0, 2]`, `Test MIDI PASS`, `AU VALIDATION SUCCEEDED.` |
| RTSan schoon | groen | `RTSan: geen schending in process` (16 renders) |
| RTSan-kanarie faalt | groen (faalt zoals bedoeld) | `ERROR: RealtimeSanitizer: unsafe-library-call` / `` Intercepted call to real-time unsafe function `malloc` `` / `#1 … plProcess … plugin.cpp` / `#2 … doeProcess … pb-render.cpp`, `kanarie exit: 43` |
| artefact | groen | `Artifact macos-keten_toetsenpad has been successfully uploaded!` |

## Pushes
Drie in totaal. De eerste (`c7ec31f`, het skelet en de CI) was meteen groen in alle jobs. De tweede (`c8111e8`)
veranderde alleen de naam van de RTSan-stap: die noemde een "state-rondje" dat de stap niet draait (zie hieronder) en
vinkte fase 0 af in TASKS.md; ook groen. De derde is dit verslag. Dat het in één keer groen was, komt doordat ik vóór de
eerste push lokaal (Linux) bouwde en clap-validator 0.4.1 en pluginval v2.0.0-beta3 op de VST3 draaide; macOS en clang
20 kon ik lokaal niet toetsen (apt.llvm.org was vanuit de sandbox geblokkeerd).

## Wat onduidelijk of fout was in het pakket, en waar ik bijna vastliep
- **PIC**: de eerste lokale Linux-bouw faalde bij het linken van het .clap (`relocation … in read-only section`): de
  impl is een statische bibliotheek en clap-wrapper zet `CMAKE_POSITION_INDEPENDENT_CODE` alleen in zijn eigen scope.
  Opgelost met `set(CMAKE_POSITION_INDEPENDENT_CODE ON)` in het eigen project. Dit staat niet bij de valkuilen; het
  is een kandidaat voor CLAUDE.md.
- **State-rondje in fase 0**: `pb-render --state-rondje` stopt met "geen state-extensie" als de plugin geen state heeft.
  In fase 0 heeft het skelet geen params en geen state (dat is fase 1/2), dus de RTSan-stap draait het rondje (nog) niet.
- **pluginval-rt.sh** verwacht schendingen van `juce::MidiEventList::addEvent` bij een VST3-instrument; hier waren het
  er 0 (misschien omdat het skelet nog geen parameters heeft, of omdat pluginval in deze run geen MIDI stuurde). Het
  script slaagt dan gewoon; het is geen fout, maar valkuil 22 gebeurt dus niet altijd.
- clap-validator bouwde uit de bron in ±40 s; met de cache wordt die stap daarna overgeslagen.
- Niets uitgezet: geen `--skip-gui-tests`, geen `--disabled-tests`, rtcheck op de AU `enabled`.
