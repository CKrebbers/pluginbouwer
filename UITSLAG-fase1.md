# UITSLAG fase 1 — DSP aangesloten; null-test door het binary

**Stopcriterium gehaald.** De laatste CI-run met codewijzigingen (commit `6f5f842`) is helemaal groen:
https://github.com/CKrebbers/pluginbouwer/actions/runs/38793598795
(Daarna volgt nog één commit met alleen dit bestand en `TASKS.md`.)

Fase 1 haalde CI in één keer: de eerste push was meteen groen. Valkuil 1 ("gelijk aan de standaardwaarden") kwam
niet terug, want de patch uit fase 0 zit er al in.

## Null-tests (gemeten in CI)
Elke null-test kwam uit op **bit voor bit gelijk**: piek −∞ dBFS, RMS −∞ dBFS. pluginval meldt `max abs diff 0`.

| null-test | echo-standaard | echo-heet | sweep-441 | zelfoscillatie (statistisch) |
|---|---|---|---|---|
| CLAP pb-render, blok 512, Linux x86_64 | −∞ | −∞ | −∞ | −∞; terts 0,000 dB, omhullende 0,000 dB |
| CLAP pb-render, wisselend (3,128,1,4096,77), Linux | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP pb-render, state-rondje, Linux | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP pb-render, blok 512 / wisselend / state, macOS arm64 | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| VST3 pluginval test (blok 512), Linux | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| VST3 pluginval test, macOS arm64 | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| AU pluginval test, macOS arm64 | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |

De grens is ≤ −90 dBFS, en voor de statistische toets binnen 1 dB.

## Per poort
| poort | uitslag |
|---|---|
| null-test CLAP (pb-render), Linux en macOS | groen |
| null-test VST3 (pluginval test), Linux en macOS | groen |
| null-test AU (pluginval test), macOS | groen |
| clap-validator 0.4.1, Linux | groen. Lokaal met dezelfde build: `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped`. In fase 0 werden er nog 25 overgeslagen; de parameter- en state-toetsen draaien nu mee. |
| pluginval strictness 10, VST3 Linux (xvfb) | groen (`SUCCESS`) |
| pluginval 10 `--rtcheck enabled`, AU en VST3 macOS | groen (`SUCCESS`; fuzz parameters op alle 8 parameters) |
| auval -strict | groen |
| RTSan: plugin schoon (nu ook een render met parameters en state-rondje) | groen |
| RTSan-kanarie moet falen | groen: `unsafe-library-call … malloc … plProcess`, exitcode 43 |
| artefact `macos-keten_dub_echo` | aanwezig (artefact-id 12001676144) |

Extra, alleen lokaal en niet in CI: met ASan en UBSan (gcc) gaf pb-render met wisselende blokken, het state-rondje
en een onbekend parameter-id geen meldingen.

## Wat ik deed
`plugin/src/dub_echo.cpp`:
- **Parameters.**
  - Ze zijn CLAP-params: het id is het nummer uit SPEC.md, en bereik, standaard en vlaggen komen uit `Ontwerp::PARAMS`.
  - De waarden zijn gewone eenheden (ms, Hz, …). De host normaliseert ze lineair; clap-wrapper doet dat voor VST3 en AU.
  - Het Ontwerp krijgt de index in `PARAMS`, niet het id.
- **Events op hun sample.**
  - process rekent het Ontwerp tot `event.time`, zet dan de waarde en rekent verder. Zo wordt het blok bij elk event gesplitst.
  - `params.flush` zet de waarden direct. Volgens CLAP loopt flush nooit tegelijk met process.
- **Threads.**
  - De mainthread raakt het Ontwerp niet aan. `get_value` en state lezen een kopie in `std::atomic<double>`, die lock-free is.
  - Een state-load zet nieuwe waarden plus een bit in een atomisch "vuil"-masker. process, flush of activate geeft ze daarna door aan het Ontwerp.
  - Na een state-load vraagt de plugin de host om `rescan(VALUES)` en `request_flush`.
- **latency** komt uit `Ontwerp::latentie()` (0 samples).
- **tail** komt uit `Ontwerp::staartSec()`, omgerekend naar samples. Bij `-1` (lusversterking ≥ 1) is de waarde `UINT32_MAX`, oneindig.
  - De berekening gebeurt aan het eind van elk process-blok en in activate, en wordt atomisch bewaard. `staartSec` leest de smoother-toestand van de audiothread, dus de mainthread mag hem niet zelf aanroepen.
- **State** volgt ARCHITECTUUR.md: `"PBST"`, versie u32 (=1), aantal u32, dan per parameter id u32 en waarde f64, alles little-endian.
  - Laden gaat per id: een onbekend id wordt overgeslagen en een ontbrekend id krijgt zijn standaard.
  - Waarden worden binnen het bereik geklemd; een NaN of Inf wordt genegeerd.
  - Lezen en schrijven gaan in een lus, omdat een stream minder bytes kan teruggeven dan gevraagd.
- **Tekst ↔ waarde** is nu een getal plus eenheid. Fase 2 maakt dat af.

`.github/workflows/ci.yml`:
- Linux: `sh gereedschap/ci-null.sh clap build` en `xvfb-run -a sh gereedschap/ci-null.sh pluginval <vst3> <pluginval> vst3-linux`.
- macOS: `ci-null.sh clap build`, en `ci-null.sh pluginval` op de geïnstalleerde VST3 (`vst3-macos`) en AU (`au-macos`).
- RTSan: een extra schone render (zelfoscillatie, wisselende blokken, state-rondje, parameters).

`dsp/` en `testsignalen/` zijn niet veranderd.

## Onduidelijk of opvallend in het pakket
- **`tail` en `staartSec`.**
  - `staartSec()` rekent met de huidige smoother-waarden (`glij.nu`), niet met de doelen. Die toestand hoort bij de audiothread.
  - Het contract zegt niet op welke thread je `staartSec` mag aanroepen, terwijl CLAP `tail.get` op beide threads toestaat. Daarom reken ik hem in process uit.
  - Direct na activate (voor de eerste sample) geeft dat nog de staart bij de standaardwaarden. Na het eerste blok klopt hij.
  - Een `staartSec(doelen)`, of een notitie over de thread in het contract, zou dit opruimen.
- **`host_tail.changed()` roep ik niet aan**, omdat clap-wrapper v0.16.0 dat niet realtime-veilig doorgeeft. Voor AU wordt het `PropertyChanged`, voor VST3 `restartComponent(kLatencyChanged)`. Vanuit process zou pluginval `--rtcheck` dat waarschijnlijk afkeuren.
  - Gevolg: de host vraagt de staart op wanneer hij hem nodig heeft, bijvoorbeeld bij een bounce. Een wijziging van de staart wordt niet actief gemeld.
  - Dat is voor fase 7 (bounce met staart in Logic) iets om te controleren.
- **`pluginval test`** (het subcommando dat `ci-null.sh` gebruikt) staat niet in `pluginval --help` van v2.0.0-beta3. `test --help` geeft "unknown option". Het werkt wel.
- **RTSan lokaal:** de bouwomgeving heeft clang 18, en RTSan vraagt ≥ 20. De RTSan-stap heb ik dus alleen in CI gezien.
