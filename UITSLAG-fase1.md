Stopcriterium gehaald.

# Uitslag fase 1 — Dub-echo (tak proef/f2)

Laatste CI-run met code (commit 2b9e527): run 47 —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39037560510 — alle drie de jobs groen, **bij de eerste push**.
(De commit met dit bestand verandert alleen docs; zijn CI-run draait dezelfde code.)

## Wat ik deed
- `plugin/dub_echo.cpp`:
  - **process** roept `Ontwerp::verwerk` aan. Events worden op hun sample toegepast: het blok wordt gesplitst bij de
    `time` van elk volgend event. Alleen `CLAP_EVENT_PARAM_VALUE` in de core-namespace telt; de rest wordt genegeerd.
    Deelt een invoerbuffer zijn geheugen met de uitgang van het andere kanaal, of ontbreekt hij, dan rekent hij op een
    kopie die in `activate` al gereserveerd is (in stukken van max_frames). Zo wordt er nooit in process gealloceerd.
  - **params**: 8 CLAP-params, id = nummer uit SPEC.md, min/max/standaard uit `Ontwerp::PARAMS` (in gewone eenheden),
    `AUTOMATABLE`. De schil zoekt de index bij het id (`zetParam` neemt een index). `get_value` leest per parameter
    een atomische kopie van de hostwaarde. `flush` past events direct toe. Voor tekst ↔ waarde is er alleen een
    eenvoudige versie (`%.6g eenheid` / strtod); de uitgewerkte versie hoort bij fase 2.
  - **state** volgens ARCHITECTUUR.md: `"PBST"`, versie 1, aantal, per parameter (id u32, waarde f64), little-endian.
    Laden gaat per id. Een onbekend id slaat hij over, een ontbrekend id houdt zijn standaard, en waarden worden
    geklemd. Is de plugin actief, dan neemt de audiothread de waarden over aan het begin van process (atomische vlag).
    Is hij niet actief, dan gaan ze direct naar het Ontwerp. Daarna volgt `host_params.rescan(VALUES)` als de host dat kent.
  - **latency** = `Ontwerp::latentie()` (0). **tail** = `staartSec()`, uitgerekend in activate en aan het eind van
    elke process, en atomisch bewaard. `-1` (lusversterking ≥ 1) wordt `UINT32_MAX` (oneindig). In fase 0 maakte
    `-1 × sr` hier ten onrechte 0 van; dat is nu verholpen.
- `.github/workflows/ci.yml`: deze stappen zijn erbij gekomen (er is niets uitgezet of weggehaald):
  - Linux: `sh gereedschap/ci-null.sh clap build` en `xvfb-run -a sh gereedschap/ci-null.sh pluginval <vst3> <pluginval> vst3-linux`.
  - macOS: `ci-null.sh clap build`, `ci-null.sh pluginval <geïnstalleerde vst3> "$PV" vst3-macos` en `… <geïnstalleerde component> "$PV" au-macos`.
- `dsp/` en `testsignalen/` zijn niet aangeraakt.
- Lokaal (Linux x86_64) vooraf getoetst: alle null-tests van CLAP en VST3 bit voor bit; pluginval strictness 10 `SUCCESS`;
  clap-validator 0.4.1 "44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped" (in fase 0: 19 passed, 25 skipped;
  de params- en state-tests draaien nu mee en slagen, waaronder state-reproducibility-basic/binary/buffered).

## Poorten (run 47)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| Null-test CLAP Linux x86_64, blok 512 / wisselend `3,128,1,4096,77` / state-rondje | groen, bit voor bit | "### Null-test CLAP (blok512, x86_64)", "(wisselend, x86_64)", "(state, x86_64)": alle 4 gevallen "−∞ (bit voor bit gelijk) … ja" |
| Null-test CLAP macOS arm64 (dezelfde 3 varianten) | groen, bit voor bit | "### Null-test CLAP (blok512, arm64)", "(wisselend, arm64)", "(state, arm64)": alle 4 "−∞ (bit voor bit gelijk)" |
| Null-test VST3 Linux (pluginval test, blok 512) | groen, bit voor bit | "### Null-test vst3-linux": alle 4 "−∞"; "PASSED: echo-heet / echo-standaard / sweep-441 … max abs diff 0" |
| Null-test VST3 macOS | groen, bit voor bit | "### Null-test vst3-macos": alle 4 "−∞"; 3× "PASSED … max abs diff 0" |
| Null-test AU macOS | groen, bit voor bit | "### Null-test au-macos": alle 4 "−∞"; 3× "PASSED … max abs diff 0". Er stond nergens "gelijk aan de standaardwaarden" (de patch voor valkuil 1 werkt) |
| Zelfoscillatie (statistisch, ≤ 1 dB) | groen | in elke tabel hierboven "statistisch: terts 0.000 dB, omhullende 0.000 dB" (en ook bit voor bit) |
| clap-validator 0.4.1 | groen | stap "clap-validator" geslaagd; die slaagt alleen bij exitcode 0 én een match op `tests run, .* 0 failed` in de uitvoer zonder ANSI-codes. Lokaal: 33 passed, 0 failed |
| pluginval strictness 10, VST3 Linux/xvfb | groen | slotregel `SUCCESS` ("Time taken to run all tests: 12 secs"); "Fuzz parameters" loopt nu over alle 8 parameters |
| RTSan schoon + kanarie faalt | groen | job RTSan geslaagd: stap "RTSan schoon" (4 signalen × 2 blokpatronen, nu mét de DSP) en stap "Kanarie moet falen" geslaagd |
| auval -strict | groen | stap geslaagd (die eist `AU VALIDATION SUCCEEDED` en exitcode 0) |
| pluginval 10 `--rtcheck enabled`, AU en VST3 macOS | groen | beide `SUCCESS` (VST3: "Time taken to run all tests: 46 secs") |
| artefact `macos-keten_dub_echo` | groen | "Artifact macos-keten_dub_echo has been successfully uploaded! … Artifact ID is 12103206052" |

## Waar het schuurde / wat onduidelijk was in het pakket
- Niets liep vast. In de prompt stond "zie valkuil 1", maar dat was niet nodig: de patch zat er al op sinds fase 0.
- `staartSec()` geeft `-1` voor oneindig. Dat staat in `pb_graaf.h`, maar niet in ARCHITECTUUR.md. De tail-code uit
  fase 0 ging ervan uit dat oneindig een groot getal was (`!(samples < 4e9)`), en zou dan 0 hebben gemeld. Het zou
  helpen als ARCHITECTUUR.md erbij zegt: "−1 = oneindig → `UINT32_MAX`".
- ARCHITECTUUR.md zegt "geen gedeelde variabelen tussen threads", maar `get_value` en state save moeten de waarde
  ergens vandaan halen. Ik gebruik per parameter een atomische kopie, plus een atomische vlag voor een state load
  tijdens het spelen. Het pakket zou kunnen zeggen of dat zo bedoeld is.
- In pluginval wordt "Process called with a larger than prepared block size" voor dit formaat overgeslagen ("INFO:
  Skipping test for plugin format"). De kopie-route knipt toch in stukken van max_frames.
- De RTSan-job stuurt geen events binnen process: pb-render zet de parameters via flush vóór activate. Het splitsen
  bij events wordt daardoor alleen getoetst door clap-validator (param-fuzz, sample-accurate tests) en pluginval,
  niet onder RTSan.
