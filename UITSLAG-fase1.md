# UITSLAG — fase 1 (DSP aansluiten; null-test door het binary)

**Stopcriterium gehaald.** Laatste CI-run met de code van deze fase (groen, commit 7911d67):
https://github.com/CKrebbers/pluginbouwer/actions/runs/38793642329
(De commit met dit verslag verandert alleen docs; zijn CI-run is dezelfde code.)

## Null-tests (gemeten in CI)
Grens: piek ≤ −90 dBFS; zelfoscillatie statistisch (terts-spectrum en omhullende binnen 1 dB).
Overal **bit voor bit gelijk** aan de WASM-uitvoer: piek −∞ dBFS, RMS −∞ dBFS.

| render | echo-standaard | echo-heet | sweep-441 | zelfoscillatie |
|---|---|---|---|---|
| CLAP pb-render, blok 512 (Linux x86_64) | −∞ dBFS | −∞ dBFS | −∞ dBFS | −∞ dBFS; terts 0,000 dB, omhullende 0,000 dB |
| CLAP pb-render, wisselend 3,128,1,4096,77 (Linux) | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP pb-render, state-rondje (Linux) | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP pb-render, blok 512 (macOS arm64) | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP pb-render, wisselend (macOS) | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP pb-render, state-rondje (macOS) | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| VST3 pluginval test (Linux, xvfb) | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| VST3 pluginval test (macOS) | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| AU pluginval test (macOS) | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |

Controle dat de toets kan falen (lokaal): een render zónder parameters geeft −16,0 / −9,6 / −11,0 dBFS en de melding
"gelijk aan de STANDAARDwaarden"; zelfoscillatie terts 49,9 dB. De toets meet dus echt.

## Per poort
| poort | uitslag | bewijs in het log |
|---|---|---|
| null-test CLAP (blok 512, wisselend, state-rondje), Linux en macOS | groen | tabellen hierboven |
| null-test VST3 Linux (pluginval test) | groen | |
| null-test VST3 en AU macOS (pluginval test) | groen | |
| clap-validator 0.4.1 | groen | `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` (fase 0: 19 geslaagd; params/state/tail doen nu mee) |
| pluginval 10, VST3 Linux | groen | `SUCCESS` |
| RTSan schoon (alle testsignalen, blok 512 en wisselend, plus params + state-rondje) | groen | job "RTSan" |
| RTSan-kanarie faalt zoals bedoeld | groen | stap "RTSan kanarie (MOET falen)" |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.` |
| pluginval 10 `--rtcheck enabled`, AU macOS | groen | `SUCCESS` |
| pluginval 10 `--rtcheck enabled`, VST3 macOS | groen | `537 tests passed, 0 tests failed`, `SUCCESS` |

Niets uitgezet of overgeslagen. `dsp/` en `testsignalen/` niet veranderd.

## Wat ik deed
- `plugin/src/dub_echo.cpp`:
  - **params**: 8 CLAP-params, id = nummer uit SPEC.md, min/max/standaard uit `Ontwerp::PARAMS`, automatiseerbaar.
    Het Ontwerp werkt met de index (0..7); de schil zoekt de index bij het id. Waarden worden geklemd (ook NaN).
    value↔text is in deze fase eenvoudig (`%.3f eenheid` / `strtod`); netjes maken is fase 2.
  - **process**: het blok wordt bij elk event gesplitst; elk event wordt op zijn eigen sample toegepast, dan rekent
    `Ontwerp::verwerk` tot het volgende event. Geen invoer: stilte, in-place in de uitgang (het Ontwerp leest elke
    sample vóór het schrijft, dus in-place is veilig).
  - **latency** uit `Ontwerp::latentie()` (0), **tail** uit `staartSec()`: negatief (lusversterking ≥ 1) wordt
    `UINT32_MAX` (oneindig), anders `ceil(sec × sr)`. Tijdens actief werk rekent de audiothread de tail na elk blok
    uit en zet hem in een atomic; de mainthread leest alleen die.
  - **state** zoals ARCHITECTUUR.md: `"PBST"`, versie 1, aantal, per parameter (id u32, waarde f64), little-endian.
    Laden per id: onbekend id overgeslagen, ontbrekend id houdt zijn standaard.
  - **threads**: de mainthread raakt het Ontwerp niet aan terwijl de plugin actief is. `get_value` en `state.save`
    lezen een `std::atomic<double>` per parameter die de audiothread bijwerkt; een geladen state gaat via een
    atomic vlag naar de audiothread (begin van het volgende blok, of `flush`). Niet actief: rechtstreeks.
- `.github/workflows/ci.yml`: `sh gereedschap/ci-null.sh clap build` op Linux en macOS; `ci-null.sh pluginval`
  voor de VST3 (Linux onder xvfb, macOS) en de AU (macOS, de geïnstalleerde .component); de renders als artefact
  (`null-linux`, `null-macos`). De RTSan-stap rendert nu ook met parameters en een state-rondje.

## Waar het vastliep, of wat in het pakket onduidelijk was
- Niets liep vast; de eerste CI-run was groen. Valkuil 1 kwam niet terug: de AU-patch uit `patches/` (fase 0) doet
  zijn werk — de AU-render is bit voor bit.
- Onduidelijk: SPEC.md zegt "de host geeft parameters lineair genormaliseerd door (0..1 → min..max)", maar CLAP-params
  zijn gewone waarden (pb-render geeft `1=315`, geen 0,25). Ik koos CLAP-params in eenheden (min..max); de wrapper
  normaliseert voor VST3, en pluginval zet de AU op 0..1 genormaliseerd. Zo kloppen alle drie.
- `Ontwerp::zetParam` neemt een index, geen id; dat staat nergens in het pakket (wel af te lezen uit `pb_wasm.cpp`).
- CLAUDE.md noemt `VERSLAG.md`, de opdracht `UITSLAG-fase1.md`; ik schreef alleen dit bestand.
- Klein: het mono-geval (1 ingangskanaal, 2 uit, in-place) zou bij in-place lezen na schrijven geven; de poorten zijn
  nu alleen stereo, dus het komt niet voor. Bij mono in fase 5 opletten.
