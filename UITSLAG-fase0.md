Stopcriterium gehaald.

# UITSLAG fase 0 — skelet, CI en alle validators (tak proef/f3)

Laatste CI-run op de code: **run 42**, commit `f9b3d7b`, alle drie de jobs groen —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39037233667
(De run daarvoor, run 36 op `158dcf6`, was ook al groen: https://github.com/CKrebbers/pluginbouwer/actions/runs/39036541004.
Deze UITSLAG-commit verandert alleen docs; CI draait er opnieuw op.)

## Wat ik deed
- `plugin/CMakeLists.txt`: C++17, clap-wrapper v0.16.0 via FetchContent met
  `PATCH_COMMAND sh <pakket>/gereedschap/pas-patches-toe.sh <pakket>/patches` (beide patches, in naamvolgorde; de
  tweede configure in CI bewijst dat hij niet faalt als ze er al op zitten), `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=ON`,
  `make_clapfirst_plugins` voor CLAP, VST3 en AUv2 (maker `Vrve`, subtype `PDca`, type `aufx`), uitvoer in
  `build/plugins/PB Dub-echo.{clap,vst3,component}` en `build/pb-render` (zoals `gereedschap/ci-null.sh` verwacht).
  `dsp/` als include-pad met `-ffp-contract=off -fno-fast-math -fno-math-errno`. Valkuilen toegepast vóór de eerste
  push: 1 en 7 (patches), 2 (`-Wl,-z,nodelete` op de Linux-VST3), 3 (`enable_language(OBJC/OBJCXX)`), 4 (pb-render
  heeft al `noinline` + `[[clang::nonblocking]]`; kanarie met malloc naar een globale variabele), 5
  (`killall -9 AudioComponentRegistrar` na installeren), 6 (geen FTZ), 8 (ANSI weg vóór de grep), 9 (`pluginval validate`).
- `plugin/bron/plugin.cpp`: de CLAP-schil. Maakt het `pb::Ontwerp` in `init`, reserveert `geheugenNodig(sr)` en roept
  `activeer` aan in `activate` (mainthread), `reset` → `Ontwerp::reset`. `process` geeft de invoer door (memcpy, of
  niets bij in-place). Extensies: audio-ports (stereo in/uit), latency (uit het Ontwerp, 0), state (het
  PBST-formaat uit ARCHITECTUUR.md, nu met 0 parameters; laden slaat entries over). Geen parameters: dat is fase 1.
  `PB_KANARIE=1` (CMake-optie) zet een `malloc` in process.
- `.github/workflows/ci.yml`: jobs `linux`, `rtsan`, `macos` (macos-15, arm64), elk met een slotstap "Bewijs" die
  de kernregels uit de validatorlogs herhaalt.

## Poorten (bewijs uit het log van run 42)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1 (uit de bron via cargo) | groen | `clap-validator: 44 tests run, 20 passed, 0 failed, 1 warnings, 23 skipped` |
| pluginval v2.0.0-beta3 strictness 10, VST3 Linux (xvfb) | groen | `pluginval VST3 Linux: Strictness level: 10 SUCCESS` |
| RTSan (clang 20 van apt.llvm.org), pb-render met `-fsanitize=realtime` | groen | stap "RTSan schoon" over alle 4 testsignalen, blok 512 en wisselend 3,128,1,4096,77 met state-rondje: geen melding, exit 0 |
| RTSan-kanarie faalt zoals bedoeld | groen | `ERROR: RealtimeSanitizer: unsafe-library-call` / `Intercepted call to real-time unsafe function 'malloc' in real-time context!` met `#1 … process(…) plugin/bron/plugin.cpp:137` |
| auval -strict -v aufx PDca Vrve | groen | `auval: AU VALIDATION SUCCEEDED.` |
| pluginval strictness 10 `--rtcheck enabled`, AU macOS | groen | `pluginval AU: Strictness level: 10 SUCCESS` |
| pluginval strictness 10 `--rtcheck enabled`, VST3 macOS | groen | `pluginval VST3: Strictness level: 10 SUCCESS` (o.a. `vst3 validator … Result: 537 tests passed, 0 tests failed`) |
| artefact `macos-keten_dub_echo` | aanwezig | `Artifact macos-keten_dub_echo has been successfully uploaded!` (CLAP, VST3, component, plus een ditto-zip van de component) |

Geen toets of validator uitgezet. Dit lukte in één CI-ronde (run 36); run 42 voegde alleen de bewijsstappen toe.

## Opmerkingen, onduidelijk of fout in het pakket
- **clap-validator-waarschuwing** `process-audio-denormals`: "took 2.84x longer to process denormals" (lokaal gezien;
  in CI: 1 warning). Bij een plugin die alleen memcpy doet is dat timingruis, geen echt denormal-probleem. Volgens
  valkuil 6 heb ik er niets aan gedaan; in fase 1 regelt `pb::invoer` het. Het is een waarschuwing, geen fout.
- **Artefact en bestandsrechten**: `actions/upload-artifact` bewaart geen Unix-rechten. LEERPAD.md zegt "uitpakken en
  kopiëren"; daarom zit er naast de losse bundels ook `PB-Dub-echo-component.zip` (gemaakt met `ditto`) in het
  artefact, die de bundel intact houdt. LEERPAD zou kunnen zeggen dat Clay die zip gebruikt.
- **pluginval op macOS met `--rtcheck enabled`** was meteen groen dankzij de thread_local-patch (valkuil 7).
- **Lokaal** (in de bouwomgeving) zijn apt.llvm.org en de zip met de Actions-logs geblokkeerd (proxy 403); RTSan heb ik
  alleen in CI kunnen toetsen. Bouwen, clap-validator en pluginval (Linux, VST3) heb ik eerst lokaal gedraaid, en de
  doorgifte bit voor bit gecontroleerd met pb-render (`cmp` op de data van in.wav en uit.wav).
- Het pakket was op alle punten die fase 0 raakt duidelijk genoeg; ik vond geen fouten in CLAUDE.md, SPEC.md of
  ARCHITECTUUR.md.
