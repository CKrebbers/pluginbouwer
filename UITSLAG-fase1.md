Stopcriterium gehaald.

# Uitslag fase 1 — DSP aansluiten; null-test door het binary (proef G3)

Laatste CI-run met de code (commit `8f7fbcd`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39155555101
(alle drie de jobs groen: Linux, RTSan, macOS arm64). Deze commit voegt alleen dit verslag en het vinkje in TASKS.md
toe; CI draait er opnieuw op.

## Wat ik deed
- `plugin/src/plugin.cpp` (alleen de schil; `dsp/` en `testsignalen/` onaangeroerd):
  - **params**: vier CLAP-params, id = nummer uit SPEC.md, min/max/standaard uit `Ontwerp::PARAMS`, AUTOMATABLE, niet
    stepped. `get_value` leest een atomaire spiegel (`std::atomic<double>`, lock-vrij afgedwongen met static_assert).
    `value_to_text`/`text_to_value` volgen de tabel in SPEC.md al (komma, "k" voor kHz, procenten), zonder locale;
    fase 2 toetst ze.
  - **process**: het blok wordt bij elk event gesplitst (rekenen tot de sample van het event, dan toepassen).
    `verwerk` krijgt een stille invoer: één nulbuffer van `maxFrames`, gereserveerd in activate (ARCHITECTUUR.md
    "Instrument"); blokken langer dan die buffer gaan in stukken. Transport (als de host tempo meldt) gaat naar
    `zetTransport` (deze keten heeft geen sequencer, dus het doet niets hoorbaars).
  - **noten** precies volgens ARCHITECTUUR.md "Noten" met `pb::NootStapel` en `pb::nootVerhouding`: NOTE_ON/OFF
    (key buiten 0..127 genegeerd, geen stap 2), NOTE_CHOKE = leeg, MIDI 0x9n/0x8n (velocity 0 = uit), CC 120/123 =
    leeg; daarna stap 2 (`zetParam(0, verhouding)` + `zetParam(1, 1)` of alleen `zetParam(1, 0)`). Noten raken de
    spiegel niet; `reset` maakt de stapel leeg na `ontwerp.reset()`. Note port ongewijzigd: alleen CLAP-dialect.
  - **flush**: zelfde eventafhandeling, ook vóór activate (pb-render zet zo de parameters).
  - **state**: `"PBST"`, versie 1, aantal, per parameter (id u32, waarde f64), little-endian. Laden per id
    (onbekend overslaan, ontbrekend = standaard); andere magic, versie 0 of te korte blob = false zonder iets te
    veranderen. Actief: waarden klaarzetten + atomaire vlag, process/flush neemt ze over vóór de events; niet actief:
    direct in het Ontwerp. Daarna `host_params->rescan(VALUES)` en, als actief, `request_flush`.
  - **latency** uit het Ontwerp (0); **tail**: `staartSec()` aan het eind van elk process en in activate omgerekend
    naar samples (−1 → UINT32_MAX) en atomisch bewaard; `tail.get` leest die.
- `.github/workflows/ci.yml`: Linux: `ci-null.sh clap build` en (onder xvfb) `ci-null.sh pluginval … vst3-linux`;
  macOS: `ci-null.sh clap build`, `ci-null.sh pluginval … vst3-macos` en `… au-macos` op de geïnstalleerde bundels.
  Alles van fase 0 staat er ongewijzigd in.

## Poorten (bewijs uit het log van run 39155555101)
| poort | uitslag | bewijs |
|---|---|---|
| null-test CLAP Linux x86_64 (blok 512, wisselend, state-rondje, automatie ×2, noten CLAP/MIDI × blok 512/wisselend) | groen | stap "Null-test CLAP" geslaagd (vergelijk.mjs exit 0 per variant); lokaal dezelfde tabel: alles "−∞ (bit voor bit gelijk) … ja" |
| null-test CLAP macOS arm64 (idem) | groen | `### Null-test CLAP (state, arm64)` … 5× `−∞ (bit voor bit gelijk) … ja`; `automatie … blok 512 (arm64)` en `blok 3,128,1,4096,77` bit voor bit; `noten (CLAP), blok 512`, `noten (MIDI), blok 512`, `noten (CLAP), blok 3,128,1,4096,77`, `noten (MIDI), blok 3,128,1,4096,77`: alle `−∞ (bit voor bit gelijk)` |
| null-test VST3 Linux (pluginval test) | groen | stap "Null-test VST3 (pluginval test, xvfb)" geslaagd; lokaal: 5× bit voor bit, 5× `"outcome": "passed"` |
| null-test VST3 macOS (pluginval test) | groen | `### Null-test vst3-macos` 5× `−∞ (bit voor bit gelijk)`; `PASSED: impuls-96k/ruis-half/sinus-441/stukjes-laag/sweep-kwart`, `max abs diff 0` |
| null-test AU macOS (pluginval test) | groen | `### Null-test au-macos` 5× `−∞ (bit voor bit gelijk)`; 5× `PASSED`, `max abs diff 0` (geen "gelijk aan de standaardwaarden": valkuil 1 zit via de patch erin) |
| clap-validator 0.4.1 | groen | stap geslaagd (eist `tests run, … 0 failed` na ANSI weghalen); lokaal: `44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped` (was 21 passed in fase 0: params/state-toetsen draaien nu) |
| pluginval strictness 10 VST3 Linux | groen | stap geslaagd (exitcode 0 en `^SUCCESS`); lokaal ook `SUCCESS` |
| RTSan schoon / kanarie faalt | groen | beide stappen geslaagd; de schone stap draait nu echte DSP met noten, MIDI en 720 automatie-events |
| auval -strict -v aumu PTgg Vrve | groen | stap geslaagd (eist `AU VALIDATION SUCCEEDED`) |
| pluginval 10 `--rtcheck enabled --repeat 3` AU | groen | stap geslaagd (exitcode 0 en `^SUCCESS`) |
| pluginval 10 VST3 macOS via `pluginval-rt.sh` | groen | stap geslaagd |
| artefact `macos-keten_toetsenpad` | groen | `Artifact macos-keten_toetsenpad has been successfully uploaded!` |

## Pushes tot CI groen
Eén push met code (`8f7fbcd`), meteen groen. Vooraf lokaal (Linux x86_64): bouwen, `ci-null.sh clap` (alles bit voor
bit), `ci-null.sh pluginval` op de VST3 onder xvfb (bit voor bit), pluginval strictness 10 (SUCCESS) en
clap-validator 0.4.1 (0 failed). RTSan (clang 20) en macOS (AU, arm64) kon ik lokaal niet toetsen; die zag ik pas in CI.

## Onduidelijk of opvallend in het pakket
- Niets liep vast. Valkuil 1 hoefde ik niet op te lossen: de patch zat er sinds fase 0 al op.
- ARCHITECTUUR.md zegt niet wat een NOTE_CHOKE met een key buiten 0..127 doet (key −1 = "alle"); ik maak de stapel
  altijd leeg. Een NOTE_ON met velocity 0 telt als noot-aan (velocity speelt geen rol, regel 4).
- `staartSec()` in activate leest de smoothers vóór hun eerste sprong (die komt pas bij de eerste sample), dus de
  staart vóór het eerste process volgt nog de oude smootherstand; na één process klopt hij.
- Een lokale ASan/UBSan-bouw lukte niet bij het configureren (ik heb niet uitgezocht waarom; hij hoort niet bij de
  poorten van deze fase en staat niet in CI). SPEC.md noemt ASan/UBSan wel bij de validators; geen fase vraagt er tot
  nu toe om.
- Niets uitgezet: alle validators van fase 0 draaien ongewijzigd.
