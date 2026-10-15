# UITSLAG fase 1 — DSP aansluiten; null-test door het binary

**Stopcriterium gehaald.** Laatste CI-run met code: <https://github.com/CKrebbers/pluginbouwer/actions/runs/38793279978>
(commit `33430d2`, conclusie `success`). De commit met dit bestand verandert alleen docs; die run draait dezelfde code opnieuw.

## Wat ik deed
- **process/params/state/latency/tail**: stond al in `plugin/src/dub_echo.cpp` (fase 0 had het meteen aangesloten,
  zie UITSLAG-fase0.md). Ik heb het nagelopen tegen de prompt en niets hoeven veranderen:
  8 CLAP-params met id = nummer uit SPEC.md en min/max/standaard uit `Ontwerp::PARAMS`; events op hun sample
  (het blok wordt bij elk event gesplitst, `verwerkTot(h->time)`); `latency` = `Ontwerp::latentie()`, `tail` uit
  `staartSec()` (oneindig → `UINT32_MAX`); state `"PBST"`, versie, aantal, (id u32, waarde f64), laden per id.
- **CI** (`.github/workflows/ci.yml`), nieuwe stappen:
  - Linux: `sh gereedschap/ci-null.sh clap build` (blok 512, wisselend 3,128,1,4096,77, state-rondje) en
    `xvfb-run -a sh gereedschap/ci-null.sh pluginval "build/plugins/PB Dub-echo.vst3" … vst3-linux`.
  - macOS arm64: `ci-null.sh clap build`, en `ci-null.sh pluginval` op de geïnstalleerde AU (`au-macos`) en
    VST3 (`vst3-macos`), na auval en de pluginval-validaties.
  - `actions/setup-node@v4` (Node 22) voor `vergelijk.mjs`; de renders (`uit/`) als artefacten `null-test-linux`
    en `null-test-macos`.
- Lokaal (Linux x86_64) vooraf gedraaid: CLAP-null-test en pluginval-test op de VST3, beide bit voor bit.
- `dsp/` en `testsignalen/` niet aangeraakt.

## Null-tests (uit het CI-log)
Piek van het verschil met `testsignalen/*.verwacht.wav`; −∞ = bit voor bit gelijk.

| binary | variant | echo-standaard | echo-heet | sweep-441 | zelfoscillatie (statistisch) |
|---|---|---|---|---|---|
| CLAP Linux x86_64 | blok 512 | −∞ | −∞ | −∞ | −∞; terts 0,000 dB, omhullende 0,000 dB |
| CLAP Linux x86_64 | wisselend | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP Linux x86_64 | state-rondje | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP macOS arm64 | blok 512 | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP macOS arm64 | wisselend | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| CLAP macOS arm64 | state-rondje | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| VST3 Linux (pluginval test) | blok 512 | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| VST3 macOS (pluginval test) | blok 512 | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |
| AU macOS (pluginval test) | blok 512 | −∞ | −∞ | −∞ | −∞; 0,000 / 0,000 dB |

pluginval's eigen vergelijking (niet-statistische gevallen): `max abs diff 0 <= tolerance 3.16228e-05` overal.
Geen "gelijk aan de standaardwaarden": de patch van valkuil 1 werkt (de AU nult bit voor bit).

## Per poort
| poort | uitslag | bewijs in het log |
|---|---|---|
| null-tests ≤ −90 dBFS (CLAP, VST3, AU) | groen | alle −∞ (tabel hierboven) |
| zelfoscillatie binnen 1 dB | groen | terts 0,000 dB, omhullende 0,000 dB |
| clap-validator 0 mislukt | groen | `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` |
| pluginval 10 VST3 Linux | groen | `SUCCESS` |
| RTSan schoon | groen | `schoon:` voor alle 4 testsignalen |
| RTSan-kanarie faalt | groen | `RealtimeSanitizer: unsafe-library-call`, exitcode 43 |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.` |
| pluginval 10 `--rtcheck` AU en VST3 macOS | groen | `SUCCESS` (2×) |
| artefact `macos-keten_dub_echo` | groen | geüpload |

Niets uitgezet of overgeslagen.

## Vastgelopen / onduidelijk in het pakket
- Niet vastgelopen. De eerste push was groen.
- Fase 0 en fase 1 overlappen: de vorige sessie sloot de DSP al aan in fase 0, waardoor fase 1 hier alleen de
  CI-stappen was. De PROMPTS kunnen dat beter expliciet maken (of fase 0 "process stil" afdwingen, of de
  aansluiting naar fase 0 verplaatsen).
- `ci-null.sh` roept `$PV test` ongequote aan: een pluginval-pad met spaties zou breken (in CI geen probleem).
- De GitHub-MCP `get_job_logs` geeft bij `tail_lines` 3000 niet het hele macOS-log; met een grotere waarde wel.
- Voor fase 5 (mono): `pProcess` zet bij één invoerkanaal beide DSP-ingangen op dat kanaal. Werkt de host
  in-place (uitvoer 0 = invoer 0), dan leest kanaal 1 per sample al overschreven invoer, omdat de DSP per sample
  eerst kanaal 0 schrijft. Bij stereo (alles wat nu getoetst wordt) speelt dat niet; in fase 5 de invoer kopiëren.
