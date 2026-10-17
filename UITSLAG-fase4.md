Stopcriterium gehaald.

# UITSLAG fase 4 — fabriekspresets (tak proef/f3)

Laatste CI-run: **run 89**, commit `ac57ea9`. Alle drie de jobs zijn groen:
https://github.com/CKrebbers/pluginbouwer/actions/runs/39043191196
Run 88 (`bde16de`) was ook helemaal groen. Run 89 veranderde alleen een regel in een bewijsstap. Deze UITSLAG-commit
verandert alleen docs; CI draait er opnieuw op.

## Wat ik deed
- **Presets als bestand naast de plugin**: CMake kopieert `presets/*.json` bij elke build.
  - Op macOS gaan ze in elke bundel (CLAP, VST3, AU) onder `Contents/Resources/presets`.
  - Op Linux gaan ze in de map `PB Dub-echo-presets` naast `PB Dub-echo.clap`.

  De plugin vindt de map via het pad dat `clap_entry.init` krijgt. Dat pad wordt absoluut gemaakt met `realpath`,
  want een FILE-locatie moet volgens clap-validator een absoluut pad zijn. `presets/` zelf is niet veranderd.
- **CLAP preset-discovery** (`plugin/bron/plugin.cpp`):
  - één provider met één locatie van het soort FILE (fabrieksinhoud) en het bestandstype `json`;
  - elk bestand is één preset, zonder laadsleutel;
  - per preset meldt hij naam, plugin-id, maker en uitleg.
- **preset-load**:
  - het bestand lezen met een eigen kleine JSON-lezer, getallen zonder locale;
  - dan dezelfde weg als een state-load: een onbekende naam wordt overgeslagen, de rest krijgt zijn standaard;
  - daarna rescan (en `request_flush` als de plugin actief is), dan `loaded`, of bij een fout `on_error`.
- **AU factory presets** met een nieuwe patch, `patches/clap-wrapper-auv2-presets.patch`, want clap-wrapper v0.16.0
  kan het niet:
  - de AUv2 doorzoekt in `PostConstructor` de preset-discovery-factory van de CLAP, zoals een host;
  - `kAudioUnitProperty_FactoryPresets` geeft die lijst;
  - `PresentPreset` laadt een preset via preset-load en meldt de nieuwe waarden aan de luisteraars.

  De crawl-code heb ik lokaal los gecompileerd en tegen de Linux-CLAP gedraaid: 5 presets gevonden en geladen.
- **Eigen toets** `plugin/toets/toets-fase4.sh` (`pb-toets presets`). De verwachting komt uit `presets/*.json` (node),
  niet uit de plugin. De toets controleert:
  - de factory, en precies één bestaande locatie met precies de 5 presets, met naam en plugin-id;
  - na het laden elke waarde exact, en `standaard.json` gelijk aan de standaardwaarden;
  - bit voor bit hetzelfde geluid als dezelfde waarden via flush, ook als de preset geladen wordt terwijl de plugin
    actief is;
  - hetzelfde onder `nl_NL.UTF-8`;
  - foutgevallen: een onbekend bestand of soort locatie geeft false en `on_error`, en de waarden blijven staan.

  De toets draait in de Linux-, macOS- en RTSan-job.
- **CI**:
  - de drie preset-toetsen van clap-validator moeten PASSED zijn (SKIPPED telt niet);
  - op macOS moet elke presetnaam uit `presets/*.json` in de uitvoer van `auval -strict -v` staan;
  - de bewijsstappen tonen beide.
- Docs: ARCHITECTUUR.md (Presets), SPEC.md (Validators), TASKS.md (fase 4 afgevinkt), CLAUDE.md (valkuilen 10 en 11).

## Poorten (bewijs uit het log van run 89)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator preset-toetsen | groen | `preset-discovery-crawl: └── PASSED`, `preset-discovery-descriptor-consistency: └── PASSED`, `preset-discovery-load: └── PASSED ─ 8ms` |
| clap-validator totaal | groen | `44 tests run, 36 passed, 0 failed, 0 warnings, 8 skipped` (fase 3: 33 passed, 11 skipped) |
| auval toont de presets | groen | `HAS FACTORY PRESETS` / `ID: 0 Name: Lange dub` … `ID: 4 Name: Zelfoscillatie`; stap: `auval toont: ID: 2 Name: Standaard` enz. voor alle 5 |
| auval -strict | groen | `auval: AU VALIDATION SUCCEEDED.` |
| eigen preset-toets Linux, macOS (ook nl_NL), RTSan | groen | `presets: geslaagd (5 presets)`; o.a. `de locatie is een map die bestaat: …/PB Dub-echo.clap/Contents/Resources/presets`, `zelfoscillatie.json (nl_NL.UTF-8): preset-load geslaagd`, `geladen terwijl actief (request_flush, dan process): bit voor bit gelijk` |
| pluginval 10 `--rtcheck`, AU en VST3 macOS (met editor) | groen | `pluginval AU: Strictness level: 10 SUCCESS`, `pluginval VST3: Strictness level: 10 SUCCESS`, `Completed tests in pluginval / Editor stress` |
| pluginval 10 VST3 Linux | groen | `pluginval VST3 Linux: Strictness level: 10 SUCCESS` |
| null-tests (CLAP Linux/macOS blok 512, wisselend, state; VST3 Linux/macOS; AU; waarden als automatisering) | groen | overal `−∞ (bit voor bit gelijk) … ja`; zelfoscillatie `terts 0.000 dB, omhullende 0.000 dB` |
| fase 2 (state, tekst, automatisering) en fase 3 (paneel) | groen | `state-toets: 8 van 8 …`, `tekst: geslaagd`, `automatisering: geslaagd`, `paneel: geslaagd (cocoa)` / `(x11)` / met audiothread |
| RTSan schoon, kanarie faalt | groen | `RTSan schoon: echo-standaard` … ; `SUMMARY: RealtimeSanitizer: unsafe-library-call … in malloc` / `Kanarie gevangen door RTSan, zoals bedoeld.` |

Ik heb geen toets of validator uitgezet en clap-validator niet aangepast.

## Verloop (4 CI-ronden)
1. **Run 82**: CLAP was groen. auval toonde geen presets, en mijn nieuwe auval-poort ving dat. Oorzaak: de
   initialisatievolgorde van statics. De AU van clap-wrapper heeft een globale `Clap::Library`, en die roept
   `clap_entry.init` al aan tijdens de statische initialisatie. Dat kan vóór de constructor van mijn globale
   `std::string presetMap` in `plugin.cpp`, die het pad daarna weer leeg maakte. Bij CLAP en VST3 gebeurt dat niet.
   Ik heb er een functie-static van gemaakt.
2. **Run 85**: macOS groen, en auval toonde de 5 presets. Linux faalde op mijn eigen PASSED-controle: `awk … exit`
   sloot de pijp, `sed` kreeg een schrijffout (exit 4), en onder `pipefail` brak dat de stap af. In run 82 ging dat
   toevallig goed. awk leest nu door.
3. **Run 88 en 89**: alles groen.

## Waar ik vastliep, en wat onduidelijk of fout was in het pakket
- **clap-validator 0.4.1 loopt vast op container-presets** (nu valkuil 10 in CLAUDE.md). Mijn eerste opzet bakte de
  presets in de plugin: één locatie van het soort PLUGIN, een container met laadsleutels. Lokaal gaven
  `preset-discovery-crawl` en `-load` daarop "Timed out after 45 seconds". De oorzaak zit in clap-validator zelf, in
  `src/plugin/preset_discovery/metadata_receiver.rs`: `begin_preset` houdt de mutex `result` vast en roept dan
  `flush_preset()` aan, die dezelfde mutex nog eens pakt. Op de hoofdtak van clap-validator is dat ook nog zo. Ik heb
  de validator niet gepatcht. Ik ben overgestapt op één bestand per preset (FILE, geen laadsleutel), wat ook beter
  past bij ARCHITECTUUR.md ("als JSON naast de plugin").
- **clap-wrapper v0.16.0 kent geen presets**: geen AU factory presets en geen VST3-programma's. Daarvoor is de patch
  nodig (nu valkuil 11). Hij gebruikt `_paramsBeforeInit` uit de patch van valkuil 1, en moet dus daarna in de
  naamvolgorde komen: `auv2-params…` < `auv2-presets`. VST3-presets vraagt fase 4 niet, dus die heb ik niet gemaakt.
- **ARCHITECTUUR.md zei "een preset is een state-blob plus naam"**, maar `presets/*.json` bevat waarden per naam,
  geen blob. Ik heb de JSON gevolgd en ARCHITECTUUR.md aangepast.
- **De AU begint niet op een preset.** clap-wrapper sorteert de presets op pad, dus ID 0 is "Lange dub". Die laden
  bij het openen zou de beginstand veranderen. Daarom blijft de huidige preset van de AU "Untitled" (−1) en blijven
  de standaardwaarden staan. `standaard.json` is daaraan gelijk; de toets controleert dat.
- **auval meldt `WARNING: Preset name is not retained in retrieved class data`** (bij VERIFYING CLASS INFO, en
  toch PASS). Dat stond er in run 82 ook al, toen de AU nog geen presets had. Het komt uit clap-wrapper `SaveState`
  (vaste naam "Program"). Ik heb het niet opgelost, omdat het geen poort raakt.
- **Linux kent geen bundel**, dus de plek van de presets daar (`PB Dub-echo-presets` naast de .clap) is mijn eigen
  keuze. Fase 6 (installeren) moet die map meenemen. Op macOS zitten ze in de bundels en gaan ze vanzelf mee.
- **Voor Clay (fase 7)**: in Logic staan de presets in het presetmenu bovenaan het pluginvenster (Factory). Kies er
  een en kijk of alle knoppen meegaan.
