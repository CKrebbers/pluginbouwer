Stopcriterium gehaald.

# Uitslag fase 2 — parameters, automatisering, state (proef F4, tak `proef/f4`)

Laatste CI-run met de code: https://github.com/CKrebbers/pluginbouwer/actions/runs/39156467867 (commit 31100ba,
alle drie de jobs groen). De commit met dit verslag start nog een run; die verandert geen code.

## Wat ik deed
- De schil zelf hoefde niet te veranderen: tekst ↔ waarde (`plugin/src/tekst.cpp`, volgens de SPEC-tabel, zonder
  printf/strtod), `params.flush` (neemt een klaarstaande state over en past de events toe, buiten process) en de
  state ("PBST", per id, rescan + request_flush, false bij foute magic/versie 0/te kort) stonden er sinds fase 0 al.
  `plugin/src/dub_echo.cpp` is in deze fase niet aangeraakt.
- `plugin/toets/state_toets.cpp` → `build/pb-statetoets <plugin.clap>`: een kleine CLAP-host die het gebouwde .clap
  laadt (dlopen; op macOS de geïnstalleerde bundel) en 125 controles doet, met een host die rescan en
  request_flush telt en een stream die in stukjes van 7 bytes leest:
  - (a) blob met de hand, versie 1, `aantal` = N − 1 (Ruis ontbreekt) na eerst alles op een andere waarde te zetten:
    laadt, de andere 8 krijgen de blobwaarden, Ruis gaat naar zijn standaard, één rescan.
  - (b) versie 2 met twee onbekende id's (4242 midden in, 0xFFFFFFF0 aan het eind, `aantal` = N + 2): laadt, alle
    bekende waarden kloppen.
  - (c) magic `PBSX` en `TSBP`, versie 0, 1 byte te kort, laatste parameter weg maar `aantal` = N, afgekapt in de kop,
    leeg: elk false, geen enkele get_value veranderd, geen rescan of flush-verzoek, en state.save daarna byte voor
    byte gelijk aan ervoor.
  - save → load → save byte voor byte gelijk; alles (a)–(c) zowel inactief als actief (actief met een process-blok
    erna, zodat de audiothread de klaarstaande waarden overneemt).
  - params.flush buiten process: events (één buiten het bereik → geklemd) zijn meteen te lezen, en na een blok nog.
  - Tekst: voor alle 9 parameters geeft `value_to_text(standaard)` precies de tabeltekst ("375 ms", "50 %",
    "3,00 kHz", "100 Hz", "Q 0,71", "6,0 dB", "25 %", "38 %", "0 %"); `text_to_value` daarvan geeft het getal uit de
    tekst, binnen een halve resolutie van de standaard, en dat geeft weer dezelfde tekst. Plus 24 invoerregels uit
    SPEC (komma/punt, met/zonder eenheid, "3k", procenten, klemmen, onleesbaar = false) en 18 009 waarden over alle
    bereiken heen en terug naar dezelfde tekst.
  - Tegenproef (lokaal, niet gecommit): met een schil die versie 0 accepteert en een ontbrekend id zijn oude waarde
    laat houden gaf de toets 18 FOUT en exitcode ≠ 0. De toets vangt dus wat hij moet vangen.
- `gereedschap/cv-eis.sh`: eist dat genoemde clap-validator-toetsen PASSED zijn (SKIPPED telt niet). In CI voor de 5
  state-toetsen en 9 param-toetsen (o.a. state-reproducibility-basic/-binary/-buffered, param-fuzz-basic/-bounds/
  -sample-accurate/-modulation, param-conversions).
- `.github/workflows/ci.yml`: de stappen hierboven op Linux (state-toets en cv-eis) en macOS (state-toets op het
  geïnstalleerde .clap), en hun uitslag in de stap "Bewijs" achteraan (valkuil 18).

## Poorten (uit het log van run 39156467867)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1 | groen | `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped`; `geen FAILED` |
| clap-validator state- en param-toetsen | groen | 14 × `PASSED`, o.a. `state-reproducibility-basic: PASSED`, `param-fuzz-basic: PASSED`, `param-fuzz-sample-accurate: PASSED` |
| state-toets Linux x86_64 | groen | `state-toets: 125/125 controles ok, 0 fout` |
| state-toets macOS arm64 | groen | `state-toets: 125/125 controles ok, 0 fout` |
| pluginval 10 VST3 Linux (xvfb) | groen | `SUCCESS` |
| pluginval 10 AU `--rtcheck enabled --repeat 3` | groen | `SUCCESS` |
| pluginval 10 VST3 macOS `--rtcheck enabled` | groen | `SUCCESS` |
| auval -strict | groen | `VERIFYING PROPERTY: Tail Time` … `AU VALIDATION SUCCEEDED.` |
| null-test CLAP Linux en macOS (blok 512, wisselend, state-rondje) | groen | alles `−∞ (bit voor bit gelijk) … ja` |
| null-test automatisering (720 events, blok 512 en wisselend), Linux en macOS | groen | `automatie (events midden in blokken) \| −∞ (bit voor bit gelijk)` |
| null-test VST3 Linux/macOS en AU macOS (pluginval test) | groen | 4 × bit voor bit, 3 × `PASSED` elk |
| zelfoscillatie statistisch | groen | `terts 0.000 dB, omhullende 0.000 dB` |
| RTSan | groen | `RTSan: schoon` |
| RTSan-kanarie | faalt zoals bedoeld | `ERROR: RealtimeSanitizer: unsafe-library-call … malloc` |

## Pushes
Eén push tot CI groen. Vooraf lokaal (Linux, clang 18): de state-toets 125/125 (plus de tegenproef), en clap-validator
0.4.1 uit de bron met `cv-eis.sh` (14 × PASSED, 0 failed). macOS kon ik lokaal niet toetsen; de toets ging daar in één
keer goed (de bundel-afhandeling van pb-render nagedaan).

## Wat onduidelijk of fout was in het pakket
- "`text_to_value` daarvan terug geeft de waarde" kan niet letterlijk voor Resonantie en Mix: de standaarden 0.70703125
  en 0.375 worden "Q 0,71" en "38 %", en die tekst geeft 0,71 en 0,38 terug. Ik toets: het getal uit de tekst, binnen
  een halve resolutie van de standaard, en weer dezelfde tekst. De prompt zou kunnen zeggen welke van de twee bedoeld is.
- Net als in fase 1: de schil van fase 0 had al bijna alles van deze fase (tekst, flush, state); fase 2 was hier
  vooral toetsen. De prompt leest alsof het nog gebouwd moet worden.
- "Automatisering in een DAW verandert niets aan de null-test" is niet als losse toets uitgewerkt; ik reken het gedekt
  door de automatie-null-test van fase 1 (720 events, ook midden in blokken, bit voor bit) die nog groen is.
- clap-validator 0.4.1 heeft geen toets die "param-fuzz" of "state-reproducibility" precies zo heet; het zijn families
  (`param-fuzz-*`, `state-reproducibility-*`). De uitvoer heeft per toets `┌─ naam` … `└── PASSED`; daar grept
  `cv-eis.sh` op. Misschien handig als gegeven gereedschap, zoals fase 4 het ook nodig heeft (PASSED, niet SKIPPED).
