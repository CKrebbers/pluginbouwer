Stopcriterium gehaald.

# Uitslag fase 1 — Dub-echo (tak proef/f1)

Laatste CI-run (commit 1979c41, eerste en enige ronde): https://github.com/CKrebbers/pluginbouwer/actions/runs/39037617682
— alle drie de jobs `success`.

## Wat ik deed
- `plugin/src/dub_echo.cpp`:
  - process rekent `Ontwerp::verwerk`. Het blok wordt bij elk event gesplitst: rekenen tot `header.time`, dan het
    event toepassen (`zetParam(index, waarde)`; de schil zoekt de index bij het id, ARCHITECTUUR.md).
  - Parameters: id = SPEC-nummer, bereik en standaard uit `PARAMS` (stond al sinds fase 0), in eigen eenheden.
    `params.flush` zet het Ontwerp direct (CLAP: `[actief ? audiothread : mainthread]`, dus nooit tegelijk met process).
  - State zoals ARCHITECTUUR.md ("PBST", versie, aantal, per id u32 + f64; laden per id). Is de plugin niet actief,
    dan krijgt het Ontwerp de waarden meteen; is hij actief, dan zet `state.load` een atomische vlag en neemt process
    de waarden aan het begin van het volgende blok over (geen gedeelde niet-atomische variabelen tussen threads).
  - `latency` uit `Ontwerp::latentie()` (0); nieuw: de `tail`-extensie. `staartSec()` wordt aan het eind van process,
    in activate, na flush en na state-load (inactief) uitgerekend en atomisch in samples bewaard; `-1` (oneindig) →
    `INT32_MAX`. `tail.get` leest alleen die waarde.
  - Kanalen: min(in, uit, 2) naar de DSP, overige uitgangen stil. In-place stereo is veilig (elk kanaal leest zijn
    eigen ingang vóór hij schrijft); mono is fase 5.
- `.github/workflows/ci.yml`: Linux `ci-null.sh clap build` en `ci-null.sh pluginval <vst3> <pluginval> vst3-linux`
  (onder xvfb); macOS `ci-null.sh clap build`, en `ci-null.sh pluginval` op de geïnstalleerde VST3 (`vst3-macos`) en
  AU (`au-macos`). De renders gaan als artefact `null-test-linux` / `null-test-macos` mee. Node 22 via setup-node.
- `dsp/`, `testsignalen/` en `gereedschap/` niet veranderd. Geen toets uitgezet.

## Poorten (bewijs uit het log van run 39037617682)
| poort | uitslag | bewijs |
|---|---|---|
| null-test CLAP Linux x86_64 (blok 512, wisselend, state-rondje) | groen | stap slaagt (`vergelijk.mjs` geeft exit 1 boven −90 dBFS); lokaal dezelfde build: alle 4 gevallen × 3 varianten `−∞ (bit voor bit gelijk)` |
| null-test CLAP macOS arm64 | groen | `### Null-test CLAP (blok512, arm64)`, `(wisselend, arm64)`, `(state, arm64)`: elk geval `−∞ (bit voor bit gelijk) … ja` |
| null-test VST3 Linux (pluginval test) | groen | `### Null-test vst3-linux`: 4× `−∞ (bit voor bit gelijk)`; `PASSED: echo-heet/echo-standaard/sweep-441`, `max abs diff 0` |
| null-test VST3 macOS (pluginval test) | groen | `### Null-test vst3-macos`: 4× `−∞ (bit voor bit gelijk)`; 3× `PASSED … max abs diff 0` |
| null-test AU macOS (pluginval test) | groen | `### Null-test au-macos`: 4× `−∞ (bit voor bit gelijk)`; 3× `PASSED … max abs diff 0` (geen "gelijk aan de standaardwaarden": de patch van valkuil 1 werkt) |
| zelfoscillatie (statistisch, ≤ 1 dB) | groen | overal `statistisch: terts 0.000 dB, omhullende 0.000 dB` (en zelfs bit voor bit) |
| clap-validator 0.4.1 | groen | stap slaagt (grept `tests run, … 0 failed` zonder ANSI); lokaal: `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` |
| pluginval strictness 10, VST3 Linux | groen | stap slaagt (lokaal ook `SUCCESS`) |
| pluginval strictness 10 `--rtcheck`, AU en VST3 macOS | groen | beide stappen slagen; VST3: `Result: 537 tests passed, 0 tests failed` … `SUCCESS` |
| auval -strict | groen | stap slaagt (grept `AU VALIDATION SUCCEEDED`) |
| RTSan (nu mét DSP) | groen | 4 signalen × 3 varianten → `RTSan: schoon` |
| kanarie | faalt zoals bedoeld | `RealtimeSanitizer: unsafe-library-call` / `` `malloc` in real-time context`` / `#1 … process … dub_echo.cpp` → `Kanarie gevangen door RTSan (exitcode 43), zoals bedoeld` |
| artefact `macos-keten_dub_echo` | er | `Artifact macos-keten_dub_echo has been successfully uploaded! … Artifact ID is 12102941775` |

## Waar ik op stuitte / wat in het pakket onduidelijk was
- Niets vastgelopen; de eerste push was groen. Het pakket (SPEC/ARCHITECTUUR over index vs. id, eenheden,
  `staartSec` op de audiothread) was hierin duidelijk genoeg.
- Onduidelijk: hoe `state.load` het Ontwerp bereikt terwijl de plugin actief is. ARCHITECTUUR zegt "parameteroverdracht
  alleen via CLAP-events" en "geen gedeelde variabelen", maar state komt op de mainthread binnen. Ik koos een
  atomische vlag die process leest; een zin in ARCHITECTUUR.md "State" zou helpen.
- Niet apart getoetst: dat pad (state laden terwijl actief) doet geen van de null-tests; pb-render laadt vóór activate.
  clap-validator en pluginval slagen ermee.
- `ci-null.sh pluginval` legt zelfoscillatie bewust niet langs pluginvals eigen sample-vergelijking (statistisch geval),
  alleen langs `vergelijk.mjs`; hier was hij toch bit voor bit.
- De clap-validator-waarschuwing uit fase 0 (denormals 2,2× trager bij doorgeven) is weg nu de DSP ze zelf wegpoetst.
- RTSan kon ik lokaal niet draaien (clang 18, apt.llvm.org onbereikbaar); alleen in CI.
