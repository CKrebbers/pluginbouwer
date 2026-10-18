Stopcriterium gehaald.

# Uitslag fase 1 — DSP aansluiten; null-test door het binary (proef F4, tak `proef/f4`)

Laatste CI-run met de code: https://github.com/CKrebbers/pluginbouwer/actions/runs/39155195193 (commit 4fab201,
alle drie de jobs groen). De commit met dit verslag start nog een run; die verandert geen code.

## Wat ik deed
- `plugin/src/dub_echo.cpp`:
  - process splitst het blok bij elk event (dat zat er sinds fase 0 al) en laat nu het Ontwerp elk stuk rekenen
    (`verwerk`). De DSP heeft twee kanalen; een ontbrekend ingangskanaal leest het laatste (of stilte), extra
    uitgangskanalen krijgen een kopie. De DSP schrijft per sample eerst uitgang 0: dient één hostbuffer als beide
    DSP-ingangen en is hij ook uitgang 0 (mono in-place), dan wordt de invoer eerst gekopieerd naar een buffer uit
    activate. Een stuk is nooit groter dan `maxFrames` van activate (de kopiebuffer).
  - tail: `staartSec()` aan het eind van elk process en in activate (audiothread; ARCHITECTUUR "staartSec"), als
    samples in een atomaire variabele; −1 (oneindig) en alles ≥ INT32_MAX wordt INT32_MAX (CLAP: oneindig; de VST3
    van clap-wrapper maakt er `kInfiniteTail` van).
  - latency uit `Ontwerp::latentie()`, de parameters (id = SPEC-nummer, bereik/standaard uit `PARAMS`, in hun eigen
    eenheid) en de state ("PBST" v1, per id, rescan + request_flush) stonden er sinds fase 0 al zoals gevraagd.
  - activate neemt een klaarstaande state over vóór de parameters naar het Ontwerp gaan.
- `.github/workflows/ci.yml`: `sh gereedschap/ci-null.sh clap build` op Linux en macOS;
  `sh gereedschap/ci-null.sh pluginval <plugin> <pluginval> <label>` voor de VST3 op Linux (onder xvfb) en macOS en
  voor de AU op macOS (de geïnstalleerde bundels, absolute paden; valkuil 18). De uitslagtabellen staan in de stap
  "Bewijs" aan het eind van elke job. pluginval ophalen is op Linux een eigen stap geworden.
- `dsp/` en `testsignalen/` niet aangeraakt.

## Poorten (uit het log van run 39155195193)
| poort | uitslag | bewijs |
|---|---|---|
| null-test CLAP Linux x86_64: blok 512, wisselend (3,128,1,4096,77), state-rondje | groen | alle 4 gevallen × 3 varianten `−∞ (bit voor bit gelijk) … ja` |
| null-test CLAP Linux automatisering (720 events), blok 512 en wisselend | groen | `automatie (events midden in blokken) \| −∞ (bit voor bit gelijk)` |
| null-test CLAP macOS arm64 (zelfde varianten + automatisering) | groen | idem, alles `ja` |
| null-test VST3 Linux (pluginval test, blok 512) | groen | 4 × bit voor bit; `PASSED: echo-heet / echo-standaard / sweep-441` |
| null-test VST3 macOS (pluginval test) | groen | 4 × bit voor bit; 3 × `PASSED` |
| null-test AU macOS (pluginval test) | groen | 4 × bit voor bit; 3 × `PASSED` (geen "gelijk aan de standaardwaarden": de patch van valkuil 1 werkt) |
| statistische toets zelfoscillatie (alle formaten) | groen | `statistisch: terts 0.000 dB, omhullende 0.000 dB` |
| clap-validator 0.4.1 | groen | `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` |
| pluginval 10 VST3 Linux (xvfb) | groen | `SUCCESS` |
| RTSan (clang 20), pb-render op alle signalen + automatie, nu mét de echte DSP | groen | `RTSan: schoon` |
| RTSan-kanarie | faalt zoals bedoeld | `ERROR: RealtimeSanitizer: unsafe-library-call … malloc` |
| auval -strict | groen | `VERIFYING PROPERTY: Tail Time` … `AU VALIDATION SUCCEEDED.` |
| pluginval 10 AU `--rtcheck enabled --repeat 3` | groen | `SUCCESS` |
| pluginval 10 VST3 macOS `--rtcheck enabled` | groen | `SUCCESS` |

## Pushes
Eén push tot CI groen. Vóór de push heb ik lokaal (Linux, clang 18) gebouwd en gedraaid: `ci-null.sh clap` (14/14
renders bit voor bit), `ci-null.sh pluginval` op de VST3 onder xvfb (bit voor bit, 3 × PASSED), pluginval 10 validate
(SUCCESS) en clap-validator 0.4.1 uit de bron (0 failed). macOS en RTSan (geen clang 20 lokaal) kon ik niet lokaal
toetsen; die gingen in één keer goed.

## Wat onduidelijk of fout was in het pakket
- Niets dat me blokkeerde. Kleine punten:
  - De fase-0-schil had parameters, state, latency en het splitsen bij events al af; fase 1 was hier alleen `verwerk`,
    de staart en de CI-stappen. De prompt van fase 1 leest alsof dat allemaal nog moet.
  - Welke waarde "oneindig" bij tail moet zijn staat in ARCHITECTUUR ("INT32_MAX of meer"). De AU van clap-wrapper
    deelt die gewoon door de samplerate (bij 48 kHz ±12 uur); dat is geen fout, maar Logic krijgt dan een zeer lange
    staart in plaats van "oneindig" (AU kent geen oneindig).
  - `ci-null.sh` schrijft zijn tabellen naar stderr en naar de job-samenvatting; voor het bewijs in het log (valkuil
    18) moet je de uitvoer zelf naar een bestand tee-en en die achteraan grep-en.
