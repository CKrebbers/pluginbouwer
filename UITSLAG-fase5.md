Stopcriterium gehaald.

# Uitslag fase 5 — formaten compleet (proef F4, tak `proef/f4`)

Laatste CI-run met de code: https://github.com/CKrebbers/pluginbouwer/actions/runs/39163754433 (commit 1060709,
alle drie de jobs groen). De commit met dit verslag start nog een run; die verandert geen code.

## Wat ik deed
- **Drie kanaalindelingen** (`plugin/src/dub_echo.cpp`): stereo 2 → 2 (config-id 0), mono 1 → 1 (1) en mono → stereo
  1 → 2 (2). Eén ingangs- en één uitgangspoort; hun kanalen volgen de gekozen indeling. Per formaat (valkuil 17):
  - CLAP: `audio-ports-config` (`select` alleen inactief) plus `audio-ports-config-info`;
  - VST3: `configurable-audio-ports` (clap-wrapper roept het vanuit `setBusArrangements` aan); de verzoeken samen met
    de huidige indeling moeten een van de drie zijn;
  - AU: `patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch` stond al in het pakket en maakt van de configs
    de AU-kanaalindelingen. auval meldt nu `Reported Channel Capabilities (explicit): [2, 2] [1, 1] [1, 2]`.
  - `in_place_pair` alleen bij evenveel kanalen in als uit. Mono → stereo in-place (ingang = uitgang 0): de invoer
    wordt eerst gekopieerd (buffer van 2 × maxFrames uit activate). Tegenproef: zonder die kopie gaf
    `echo-heet@96000@mono-stereo --in-place` een fout van −10,3 dBFS; de matrix vangt het dus.
- **Bypass**: parameter id 10 "Bypass", `CLAP_PARAM_IS_BYPASS | IS_STEPPED | IS_AUTOMATABLE`, 0/1, tekst "Uit"/"Aan".
  Geen parameter van het Ontwerp: hij staat naast de N parameters (`waarde[N]`, `kopie[N]`), gaat mee in de state
  (id 10; een oude blob zonder hem laadt met bypass uit) en een preset laat hem staan. De DSP rekent altijd door; per
  sample loopt g met 1/(0,010 · sr) naar het doel, dan nat · (1 − g) + droog · g door `pb::uitvoer`. Bij g = 0 wordt
  er niets gemengd (bit voor bit het Ontwerp). Bij activate en reset springt g naar het doel. clap-wrapper maakt er
  VST3 `kIsBypass` en AU `kAudioUnitProperty_BypassEffect` van. Tegenproef: zonder `pb::uitvoer` op het droge signaal
  kwamen 3200–6400 subnormale samples door; de toets vangt dat.
- **Samplerates en blokgroottes**: geen DSP-wijziging nodig; process knipt al op maxFrames.
- **Gereedschap**:
  - `gereedschap/matrix.mjs`: laat eerst `wasm/graaf.wasm` alle vier de testsignalen bit voor bit nadoen (anders
    stopt hij) en maakt dan de verwachte uitvoer voor 4 gevallen × 6 samplerates (44,1 / 48 / 88,2 / 96 / 176,4 /
    192 kHz) × 3 indelingen, plus een bypass-geval (echo-standaard met events: aan, uit, terug aan midden in de
    overvloei, uit). De bypass-verwachting rekent hij in JS met dezelfde formule. Daarna voert hij de renders uit en
    vergelijkt ze (pb-render, pluginval test, of een map renders).
  - `pb-render`: `--indeling stereo|mono|mono-stereo` (kiest de audio-ports-config vóór activate) en `--in-place`.
  - `pb-aurender` (`plugin/toets/au_render.cpp`, macOS): rendert de matrix door de geïnstalleerde .component zoals
    een AU-host (AudioToolbox, stroomformaten 2/2, 1/1, 1/2, parameters vóór Initialize, blokken 1/4096/77/3, bypass
    via `kAudioUnitProperty_BypassEffect`).
  - state-toets: de bypass telt mee als stepped parameter (rij "Uit" in de teksttabel, invoer "Aan"/"uit"/"1"/"0"/"on").
- **CI**: Linux: clap-validator-toetsen `layout-audio-ports-config`, `layout-configurable-audio-ports`, in-place,
  out-of-place, `process-varying-sample-rates`, `-varying-block-sizes` en `-random-block-sizes` moeten PASSED zijn;
  de CLAP-matrix; de VST3-matrix via pluginval test (stereo, per samplerate een andere blokgrootte: 4096, 512, 1, 77,
  4096, 3). RTSan: elke indeling in-place met bypass-events op 44,1 en 192 kHz, blokken 1..4096. macOS: de
  CLAP-matrix (arm64), de AU-matrix met pb-aurender, en de AU- en VST3-matrix via pluginval test.
- SPEC.md (bypass-parameter, tekst, sectie Formaten) en ARCHITECTUUR.md (sectie "Kanalen en bypass") bijgewerkt.

## Poorten (uit het log van run 39163754433)
| poort | uitslag | bewijs |
|---|---|---|
| graaf.wasm doet testsignalen/ na | groen | 4 × `| graaf.wasm | … | −∞ (bit voor bit gelijk) | … | ja |` (Linux en macOS) |
| CLAP-matrix Linux x86_64 / macOS arm64 | groen | `bit voor bit: 183 renders; niet bit voor bit of erger: (geen)`; `matrix clap: alles binnen de grens` (180 renders + 3 subnormaal-bypass) |
| AU-matrix (pb-aurender, 2→2, 1→1, 1→2, bypass) | groen | `pb-aurender: 90 renders`; `bit voor bit: 90 renders; … (geen)`; `matrix vergelijk: alles binnen de grens` |
| AU-matrix pluginval test | groen | `bit voor bit: 24 renders; … (geen)`; `matrix pluginval: alles binnen de grens` |
| VST3-matrix pluginval test macOS / Linux | groen | 2 × `bit voor bit: 24 renders; … (geen)` |
| clap-validator indelingen (fase 5) | groen | 7 × `PASSED` (layout-audio-ports-config, layout-configurable-audio-ports, …, process-random-block-sizes) |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.`; `[2, 2]  [1, 1]  [1, 2]`; channel handling X bij 1-1, 1-2, 2-2 |
| pluginval 10 AU `--rtcheck --repeat 3` (met editor) / VST3 macOS / VST3 Linux | groen | `SUCCESS` / `SUCCESS` / `SUCCESS` |
| clap-validator state/param (14) en presets (3) | groen | alle `PASSED` |
| null-tests van fase 1 (CLAP 512/wisselend/state/automatisering, VST3, AU) | groen | alles `−∞ (bit voor bit gelijk) … ja`, `PASSED` |
| state-, preset-, AU-preset-, paneel-, gui-toets | groen | `134/134`, `46/46`, `18/18`, `35/35`, `52/52` |
| RTSan, TSan, kanarie | groen | `RTSan: schoon`, `TSan: schoon`, `kanarie gevangen zoals bedoeld` |

## Pushes
Eén push met code, en CI was meteen groen. Lokaal (Linux) had ik vooraf gebouwd en gedraaid: clap-validator 0.4.1
uit de bron (`44 tests run, 38 passed, 0 failed`; voorheen 36: de twee layout-toetsen waren SKIPPED), de hele
CLAP-matrix, de VST3-matrix met pluginval v2.0.0-beta3, `pluginval validate --strictness-level 10`, en de state-,
preset- en paneel-toets. De twee tegenproeven (zonder in-place-kopie, zonder `pb::uitvoer` in bypass) heb ik lokaal
gedaan en daarna teruggedraaid. pb-aurender kon ik hier niet bouwen (geen AudioToolbox); ik deed een syntaxcontrole
met een nagemaakte header.

## Wat onduidelijk of fout was in het pakket
- `patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch` staat al in het pakket (sinds de vorige ronde); hij
  werkte zonder aanpassing.
- De prompt zegt niet welk id en welke tekst de bypass krijgt, of hij in de state hoort en wat een preset ermee
  doet. Ik koos id 10, "Uit"/"Aan", wel in de state, niet in presets; nu in SPEC.md.
- Een extra parameter breekt de state-toets van fase 2 als die niets van stepped weet (hij kiest 0,15 als "andere
  waarde" voor een parameter van 0/1, en de teksttabel telt rijen tegen het aantal parameters). Ik heb de toets
  uitgebreid, niet versoepeld.
- `pluginval test` kent geen kanaalindeling (hij neemt wat de plugin standaard geeft): mono en mono → stereo voor de
  AU toets ik daarom met pb-aurender. Voor de VST3 in mono is er geen render-toets: het bewijs is dat clap-validator
  `layout-configurable-audio-ports` PASSED (dezelfde extensie die clap-wrapper voor `setBusArrangements` gebruikt)
  plus pluginval validate. Een VST3-host die op de bits rendert (bv. met de VST3 SDK) zou dat gat dichten.
- `pluginval test` eist dat de samplerate in de WAV-kop gelijk is aan `sample_rate` in de config; de matrix schrijft
  daarom dezelfde samples met een andere kop per samplerate.
- clap-wrapper zet bij AU-bypass uit `Reset(kAudioUnitScope_Global)` in gang, maar die bereikt de CLAP niet: de echo
  loopt door, zoals de prompt wil ("de DSP rekent door").
- `gereedschap/cv-eis.sh` vindt toetsen waarvan de uitleg een eigen kader heeft niet (bv.
  `process-audio-double-in-place`, "niet gevonden"); ik eis alleen toetsen die hij wel leest.
