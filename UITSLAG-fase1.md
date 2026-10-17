Stopcriterium gehaald.

# UITSLAG fase 1 — DSP aangesloten; null-test door het binary (tak proef/f3)

Laatste CI-run op de code: **run 51**, commit `e6c915b`, alle drie de jobs groen —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39037978285
(Deze UITSLAG-commit verandert alleen docs; CI draait er opnieuw op.)

## Wat ik deed
- `plugin/bron/plugin.cpp` (alleen de schil; `dsp/` en `testsignalen/` niet aangeraakt):
  - **params**: 8 CLAP-params, id = `PARAMS[i].id` (1..8 zoals SPEC.md), min/max/standaard uit `PARAMS` in hun eigen
    eenheid, `CLAP_PARAM_IS_AUTOMATABLE`. De schil zoekt de INDEX bij het id (`zetParam`/`leesParam` nemen de index).
    Tekst ↔ waarde voorlopig eenvoudig (`%.4g eenheid` / `strtod`); fase 2 maakt dat af.
  - **process**: past alle events met `time ≤ pos` toe, rekent `Ontwerp::verwerk` tot het volgende event, enz.
    (blok gesplitst bij elk event). Alleen `CLAP_EVENT_PARAM_VALUE` uit de core-namespace telt.
  - **latency** = `Ontwerp::latentie()` (0). **tail**: `staartSec()` wordt aan het eind van elke process (en in
    activate) uitgerekend en atomisch bewaard in samples (−1 → `UINT32_MAX` = oneindig); `tail.get` leest die waarde.
  - **state** zoals ARCHITECTUUR.md: `"PBST"`, versie 1, aantal 8, dan (id u32, waarde f64). Laden per id: onbekend
    id overgeslagen, ontbrekend id houdt de standaard. Na laden: `host->params->rescan(CLAP_PARAM_RESCAN_VALUES)`.
  - **threads**: een atomische spiegel van de waarden voor `get_value` en state (mainthread). Het Ontwerp wordt
    alleen geschreven in process/flush, of op de mainthread als de plugin niet actief is. Een state-load terwijl hij
    actief is zet een vlag; process (of flush) neemt de waarden dan over, en de plugin vraagt `request_flush`.
- `.github/workflows/ci.yml`: Node 22 via `actions/setup-node`, en de null-tests:
  - Linux: `sh gereedschap/ci-null.sh clap build` en `ci-null.sh pluginval … "xvfb-run -a ./pluginval/pluginval" vst3-linux`.
  - RTSan-job: dezelfde CLAP-null-test met de RTSan-build (clang 20); RTSan blijft stil.
  - macOS: `ci-null.sh clap build`, en `ci-null.sh pluginval` op de geïnstalleerde AU (`au-macos`) en VST3 (`vst3-macos`).
  - De bewijsstappen herhalen de null-testtabellen.

## Poorten (bewijs uit het log van run 51)
| poort | uitslag | bewijs |
|---|---|---|
| null-test CLAP Linux (x86_64), blok 512 / wisselend 3,128,1,4096,77 / state-rondje | groen | alle 4 gevallen in alle 3 varianten `−∞ (bit voor bit gelijk) … ja` |
| null-test CLAP macOS (arm64), idem | groen | `### Null-test CLAP (blok512, arm64)`, `(wisselend, arm64)`, `(state, arm64)`: alle 4 `ja` |
| null-test CLAP met de RTSan-build | groen | stap geslaagd (vergelijk.mjs exit 0), geen RTSan-melding |
| null-test VST3 Linux (pluginval test) | groen | `### Null-test vst3-linux`: 4× `ja`; `[sample] ok: max abs diff 0 <= tolerance 3.16228e-05` |
| null-test VST3 macOS (pluginval test) | groen | `### Null-test vst3-macos`: 4× `ja`; `max abs diff 0` |
| null-test AU macOS (pluginval test) | groen | `### Null-test au-macos`: 4× `ja`; `max abs diff 0` |
| zelfoscillatie (statistisch, ≤ 1 dB) | groen | overal `statistisch: terts 0.000 dB, omhullende 0.000 dB` (en ook bit voor bit) |
| clap-validator 0.4.1 | groen | `clap-validator: 44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` |
| pluginval 10, VST3 Linux | groen | `pluginval VST3 Linux: Strictness level: 10 SUCCESS` |
| pluginval 10 `--rtcheck enabled`, AU macOS | groen | `pluginval AU: Strictness level: 10 SUCCESS` |
| pluginval 10 `--rtcheck enabled`, VST3 macOS | groen | `pluginval VST3: Strictness level: 10 SUCCESS` |
| auval -strict | groen | `auval: AU VALIDATION SUCCEEDED.` |
| RTSan schoon + kanarie faalt | groen | kanarie: `Intercepted call to real-time unsafe function 'malloc' in real-time context!` met `#1 … process(…) plugin/bron/plugin.cpp:278` |
| artefact `macos-keten_dub_echo` | aanwezig | `Artifact macos-keten_dub_echo has been successfully uploaded!` |

Geen toets of validator uitgezet.

## Verloop
- Run 50 (`72dd145`, eerste push): rood op clap-validator `state-reproducibility-basic/-binary/-buffered`:
  "After reloading the state, these parameter values changed without a rescan request". Lokaal ook gezien (cargo-build
  van clap-validator 0.4.1 was nog niet klaar toen ik pushte). Oplossing: na `state.load` de host
  `params->rescan(CLAP_PARAM_RESCAN_VALUES)` laten doen. Run 51 daarna in één keer groen.
- "Gelijk aan de standaardwaarden" kwam niet voor: de patch voor valkuil 1 zat er al op sinds fase 0.

## Opmerkingen, onduidelijk of fout in het pakket
- **Rescan na state-load** staat nergens in het pakket (ARCHITECTUUR.md "State" of PROMPTS fase 1), maar clap-validator
  eist het zodra er parameters zijn. Een zin in ARCHITECTUUR.md zou de eerste rode run voorkomen.
- **Sample-nauwkeurige events zijn niet door een null-test gedekt**: pb-render zet parameters alleen via `flush`
  vóór activate, en `pluginval test` via de state. Het splitsen van het blok bij events wordt alleen door clap-validator
  (`param-fuzz-*`, `transport-fuzz-sample-accurate`: geen crash, geen NaN) geraakt, niet op de bits. Een geval met
  automatisering midden in een blok in `gevallen.json`/pb-render zou dat bewijzen.
- De denormal-waarschuwing van clap-validator uit fase 0 is weg (0 warnings) nu de DSP echt rekent.
- Mono in-place (fase 5) is nog niet geregeld: de poorten zijn alleen stereo. Bij één invoerkanaal leest de schil
  kanaal 0 voor beide DSP-ingangen, wat in-place fout zou gaan; PROMPTS fase 5 waarschuwt hier al voor.
- Lokaal (bouwomgeving) heb ik vóór de push gedraaid: bouwen, CLAP-null-test (3 varianten), VST3-null-test via
  `pluginval test` onder xvfb, pluginval 10 en (na de fix) clap-validator: alles groen.
