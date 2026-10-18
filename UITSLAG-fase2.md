Stopcriterium gehaald.

# Uitslag fase 2 — parameters, automatisering, state (proef G3)

Laatste CI-run met de code (commit `61667fa`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39156822955
(alle drie de jobs groen: Linux, RTSan, macOS arm64). Deze commit voegt alleen dit verslag en het vinkje in TASKS.md
toe; CI draait er opnieuw op.

## Wat ik deed
- `plugin/src/plugin.cpp` (`dsp/` en `testsignalen/` onaangeroerd; process ongewijzigd):
  - **tekst → waarde**: de cijfers worden eerst een geheel getal en dan één keer gedeeld door 10^k (fase 1 telde
    `0,1 × cijfer` op, wat afrondfouten opstapelt). Komma én punt, eenheid hoofdletterongevoelig, spaties vóór en
    achter mogen, "k"/"kHz" voor kHz, eenheidloos in procenten ("38" en "38 %"); buiten het bereik klemmen,
    onleesbaar (lege tekst, "abc", een verkeerde eenheid) = false.
  - **waarde → tekst**: volgens de tabel (zoals al in fase 1), nu met false bij NaN en nooit "-0 %".
  - **params.flush** buiten process: werkte al (zelfde eventafhandeling als process, ook vóór activate, met de
    klaargezette state eerst); de nieuwe toets zet alle waarden via flush.
- `gereedschap/pb-statetoets.cpp` (nieuw, CMake-doel `pb-statetoets`): een kleine CLAP-host die het gebouwde `.clap`
  laadt (dlopen) en toetst, elk state-geval twee keer: laden terwijl de plugin niet actief is en terwijl hij actief is.
  - (a) blob `PBST`, versie 1, aantal = N − 1 (zonder Loslaten): laadt; Loslaten = 450 (de standaard, niet de waarde
    die er vóór de load stond); de host krijgt één rescan(VALUES); state.save daarna laadt weer tot hetzelfde.
  - (b) versie 2 met onbekende id's (9999 vooraan, 77 in het midden, 0xFFFFFFFF achteraan): laadt, de bekende id's
    krijgen hun waarde.
  - (c) magic "PBSX" en "pbst", versie 0, leeg, afgekapt na 3, 11 en 12 bytes, 1 byte te kort, aantal N+1 en
    0xFFFFFFFF: false, en get_value, de bytes van state.save, rescan/request_flush en het geluid blijven gelijk.
    Als controle laadt dezelfde blob heel wel.
  - Bij elk geval ook het geluid: een noot na de load is bit voor bit gelijk aan dezelfde noot door een verse
    instantie die de verwachte waarden via `params.flush` kreeg (de state komt echt in het Ontwerp, ook via de
    atomaire vlag als hij actief is).
  - De stream levert hooguit 5 bytes per read (een plugin moet korte reads aankunnen).
  - Tekst: `value_to_text` van de standaard = "100 %", "0 %", "900 Hz", "450 ms" (de tabel) en `text_to_value`
    daarvan = precies de standaard; plus de voorbeelden uit de kolom "regel" ("38 %", "125 %", "300 Hz",
    "3,00 kHz", "12,0 kHz", "20,0 ms", "375 ms", …), 30 leesgevallen, en een rondje tekst → waarde → tekst over
    201 punten van elk bereik.
  - Mutatietoets lokaal: met "ontbrekend id houdt zijn oude waarde" in plaats van de standaard faalde de toets op 8
    controles (a); teruggezet → 0 fout.
- `.github/workflows/ci.yml`: `pb-statetoets` op Linux en macOS arm64; na clap-validator moeten de param- en
  state-toetsen bij naam PASSED zijn (SKIPPED of niet gevonden = rood): param-set-events, param-fuzz-basic,
  -bounds, -sample-accurate, -modulation, param-conversions, param-default-values, state-invalid-empty,
  state-invalid-random, state-reproducibility-basic, -binary, -buffered. Alles van fase 0 en 1 staat er ongewijzigd in.

## Poorten (bewijs uit het log van run 39156822955)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1 | groen | `44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped`, `clap-validator exitcode: 0` |
| clap-validator state-reproducibility / param-fuzz | groen | `clap-validator state-reproducibility-basic: PASSED`, `-binary: PASSED`, `-buffered: PASSED`, `param-fuzz-basic: PASSED`, `param-fuzz-bounds: PASSED`, `param-fuzz-sample-accurate: PASSED`, `param-fuzz-modulation: PASSED`, `param-conversions: PASSED` (en de overige vier) |
| state-toets + tekst (pb-statetoets) Linux x86_64 | groen | `143 controles, 0 fout` |
| state-toets + tekst (pb-statetoets) macOS arm64 | groen | `143 controles, 0 fout` |
| pluginval strictness 10 VST3 Linux | groen | `SUCCESS`, `pluginval exitcode: 0` |
| pluginval 10 `--rtcheck enabled --repeat 3` AU | groen | `SUCCESS`, `pluginval AU exitcode: 0` |
| pluginval 10 VST3 macOS via `pluginval-rt.sh` | groen | `SUCCESS` |
| auval -strict -v aumu PTgg Vrve | groen | `AU VALIDATION SUCCEEDED.`, `auval exitcode: 0` |
| null-test CLAP (Linux, macOS; ook automatie en noten) | groen | stappen geslaagd (vergelijk.mjs eist het); lokaal alles `−∞ (bit voor bit gelijk)` |
| null-test VST3/AU macOS (pluginval test) | groen | 5× + 5× `max abs diff 0 <= tolerance 3.16228e-05` |
| null-test VST3 Linux | groen | stap geslaagd |
| RTSan schoon / kanarie faalt | groen | beide stappen geslaagd |
| artefact `macos-keten_toetsenpad` | groen | `Artifact macos-keten_toetsenpad has been successfully uploaded!` |

"Automatisering in een DAW verandert niets aan de null-test": de automatie-null-test van fase 1 (720 events, ook
midden in een blok en twee op dezelfde sample) blijft bit voor bit gelijk op Linux en macOS; process is in deze fase
niet veranderd.

## Pushes tot CI groen
Eén push met code (`61667fa`), meteen groen. Vooraf lokaal (Linux x86_64): bouwen, pb-statetoets (143/0),
clap-validator 0.4.1 (0 failed; de awk-controle op PASSED per toets lokaal tegen echte uitvoer geprobeerd, ook met een
SKIPPED-toets en een onbekende naam), `ci-null.sh clap` (alles bit voor bit) en pluginval strictness 10 op de VST3
onder xvfb (SUCCESS). macOS (AU, arm64) en RTSan zag ik pas in CI.

## Onduidelijk of opvallend in het pakket
- Niets liep vast. Het meeste stond al sinds fase 1 (tekst ↔ waarde, flush, state); deze fase is vooral de toets.
- SPEC.md zegt "onder 1000: hele Hz; vanaf 1000 kHz": een waarde als 999,7 zou als "1000 Hz" afronden. Ik schakel
  bij 999,5 al naar kHz ("1,00 kHz"), net als bij ms (99,95 → "100 ms"), zodat de tekst nooit "1000 Hz" of
  "100,0 ms" wordt.
- "Noot" heeft in `PARAMS` de eenheid "×", "Poort" een lege: de tabel noemt beide "eenheidloos" (procenten); de schil
  behandelt alles wat niet "Hz" of "ms" is als procent. Een parameter met eenheid "dB" of "%" (die `pb_core.h` noemt)
  zou in een ander ontwerp dus fout gaan; voor Toetsenpad speelt het niet.
- `snprintf` volgt de locale van het proces: zet een host LC_NUMERIC op een taal met een komma, dan blijft het een
  komma (goed); een andere decimaalteken-locale zou er doorheen komen. Het lezen gebruikt geen locale.
- Niets uitgezet: alle validators van fase 0 en 1 draaien ongewijzigd.
