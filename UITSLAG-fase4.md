Stopcriterium gehaald.

# Uitslag fase 4 — Dub-echo (tak proef/f1)

Laatste CI-run (commit 45ac154): https://github.com/CKrebbers/pluginbouwer/actions/runs/39044396098
— alle vier de jobs `success` (Linux, RTSan, TSan, macOS).

Er waren twee pushes nodig:
1. **Run 39043902293 (commit 8c04c56)**, de eerste push met de presets: in één keer groen.
   - In het log stonden nog twee waarschuwingen:
     - auval: `WARNING: Preset name is not retained in retrieved class data`;
     - pluginval AU: `!!! WARNING: Current program is -1... Is this correct?`.
2. **Run 39044396098 (commit 45ac154)**: de auval-waarschuwing is opgelost en is nu zelf een poort. Weer groen.

## Wat ik deed
- **Presets in de plugin gebakken.**
  - CMake leest bij configure `presets/*.json` (met `string(JSON)`) en schrijft `build/gemaakt/fabriekspresets.inc`.
  - `plugin/src/fabriekspresets.h` controleert bij het compileren (`static_assert`) drie dingen:
    - elke parameternaam staat in `PARAMS`;
    - elke waarde ligt binnen het bereik;
    - geen naam of sleutel komt dubbel voor.
  - De sleutel (load_key) is de bestandsnaam zonder `.json`.
  - Een nieuw JSON-bestand is na een nieuwe configure een nieuwe preset.
- **CLAP preset-discovery.**
  - `clap_entry.get_factory` geeft de factory onder `CLAP_PRESET_DISCOVERY_FACTORY_ID` én de `_COMPAT`-id.
  - Per preset is er een provider, met één locatie in de plugin (`CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN`, location
    NULL, vlag fabrieksinhoud).
  - Elke provider meldt één preset: naam, load_key, de CLAP-id van de plugin, maker en uitleg.
- **preset-load** (`from_location`) laadt een preset langs hetzelfde pad als `state.load`:
  - eerst alle parameters op hun standaard, dan de waarden uit de preset;
  - niet actief: meteen naar het Ontwerp; actief: via `stateNieuw`, en process neemt ze over;
  - daarna `params.rescan` en `host_preset_load.loaded`;
  - een onbekende sleutel of een bestandslocatie geeft false en `on_error`.
- **AU factory presets.** clap-wrapper v0.16.0 kent ze niet. Daarom een vierde patch,
  `patches/clap-wrapper-auv2-presets-uit-preset-discovery.patch`, in dezelfde stijl als de andere (herkomst in de kop):
  - Bij het maken van de instantie doorloopt de AUv2-wrapper de preset-discovery van de CLAP, alleen de locaties in
    de plugin. Daaruit maakt hij `kAudioUnitProperty_FactoryPresets`.
  - `PresentPreset` zetten laadt de preset met preset-load.
  - De naam gaat in de ClassInfo: die van een factory preset, en (sinds de tweede push) ook een eigen naam van de host
    (`NewCustomPresetSet`).
  - Na `RestoreState` geeft `PresentPreset` de bewaarde naam terug.
- **Toetsen** (er is niets uitgezet):
  - In de clap-validator-poort moeten nu ook `preset-discovery-crawl`, `-descriptor-consistency` en `-load` PASSED
    zijn, niet overgeslagen.
  - `gereedschap/pb-preset-toets` is een CLAP-host die de discovery doorloopt zoals een DAW en elke preset laadt.
    - Daarna moet de preset bit voor bit klinken als dezelfde waarden via `params.flush`, zowel vóór activate als
      terwijl de plugin actief is.
    - Hij draait op Linux, op macOS en onder RTSan.
    - Hij bijt: een mutatie waarbij het laden het Ontwerp niet bereikt, gaf lokaal `FOUT: … verschilt`.
  - `gereedschap/pb-au-presets` (macOS) is een AU-host via AudioToolbox, die met de geïnstalleerde AU werkt:
    - elke factory preset zetten, dan `PresentPreset` en de ClassInfo lezen;
    - een nummer buiten de lijst moet geweigerd worden;
    - een eigen naam moet in de ClassInfo komen, en die ClassInfo moet in een nieuwe instantie naam en waarden
      teruggeven.
  - `gereedschap/vergelijk-presets.mjs` legt de waarden uit beide toetsen naast `presets/*.json`: CLAP bit voor bit,
    AU als float32.
  - auval moet elke presetnaam tonen, en de waarschuwing `Preset name is not retained` laat de stap falen.
- Docs: ARCHITECTUUR.md heeft een uitgebreid stuk "Presets (fase 4)", TASKS.md is bijgewerkt en VERSLAG.md is nieuw.
  `dsp/` en `testsignalen/` zijn niet veranderd.

## Poorten (run 39044396098)
| poort | uitslag | bewijs (log) |
|---|---|---|
| **clap-validator preset-tests** (Linux) | groen | `preset-discovery-crawl: └── PASSED`, `preset-discovery-descriptor-consistency: └── PASSED`, `preset-discovery-load: └── PASSED`; `44 tests run, 36 passed, 0 failed, 0 warnings, 8 skipped` |
| **auval toont de presets** (macOS) | groen | `HAS FACTORY PRESETS` / `ID: 0 Name: Lange dub` … `ID: 4 Name: Zelfoscillatie`; `VERIFYING CLASS INFO` → `PASS` (geen waarschuwing meer); `AU VALIDATION SUCCEEDED.`; `auval toont preset: Lange dub` … `Zelfoscillatie` |
| AU-presets echt laadbaar (pb-au-presets) | groen | `5 factory presets`, per preset `ok '…' (nummer N) gezet; PresentPreset en ClassInfo dragen de naam`, `ok eigen naam 'Mijn klank' in de ClassInfo; in een nieuwe instantie: naam en waarden terug`, `pb-au-presets: alles groen (5 presets)`, `presets (AU): alle 5 gelijk aan presets/*.json` |
| CLAP-presets laden en klinken (pb-preset-toets, Linux en macOS) | groen | `pb-preset-toets: alles groen (5 presets)`, `presets: alle 5 gelijk aan presets/*.json` |
| RTSan met presets | groen | stap "RTSan met de fabriekspresets (preset laden terwijl actief, dan process)" `success` |
| pluginval 10, AU macOS (`--rtcheck enabled`) | groen | `Num programs: 5`, `Program 0 name: Lange dub` … ; `SUCCESS` |
| pluginval 10, VST3 macOS en Linux | groen | `Result: 537 tests passed, 0 tests failed` … `SUCCESS` (macOS); `SUCCESS` (Linux) |
| eerdere poorten (null-tests, automatisering, state-toets, paneel, TSan, RTSan-kanarie, auval) | groen | alle stappen `success` in deze run |

## Waar ik vastliep / wat onduidelijk of fout was
- **clap-validator 0.4.1 blijft hangen** op een preset-container met meer dan één preset. Mijn eerste opzet had één
  provider met alle vijf presets als container; lokaal gaven crawl en load `Timed out after 45 seconds`.
  - De oorzaak zit in de validator: een mutex die in `begin_preset` dubbel genomen wordt
    (`metadata_receiver.rs`). Upstream master heeft dezelfde code.
  - Ik heb de validator niet aangepast. Elke preset kreeg een eigen provider (een container van één); dat is voor
    een host even geldig.
  - Gevolg: `preset-discovery-load` laadt alleen de preset van de laatste provider, omdat de validator per locatie
    maar één resultaat bewaart. Dat alle vijf laden en klinken, toetst `pb-preset-toets`.
  - Alles staat in VERSLAG.md.
- **clap-wrapper v0.16.0 kent geen AU factory presets**, en heeft ook geen manier waarop een CLAP ze kan aanleveren.
  Daarom moest er een patch komen. Het pakket zegt daar niets over; een valkuil 10 in CLAUDE.md zou het een volgend
  gesprek besparen.
- **pluginval AU: `!!! WARNING: Current program is -1... Is this correct?`** Dat is een waarschuwing, geen fout.
  - pluginval leest bij het begin het huidige programma, en dat is −1 ("untitled"): de AU begint niet in een preset.
  - Dat klopt, ook al is "Standaard" gelijk aan de beginstand.
  - Na de wissels zet pluginval niets terug, omdat er niets was om naar terug te gaan.
- **VST3 krijgt geen presets.** clap-wrapper geeft ze daar niet door (`Scan Programs: This component does not export
  any programs`), en de opdracht vraagt alleen CLAP en AU.
- **Onduidelijk in het pakket:** ARCHITECTUUR.md noemde een preset "een state-blob plus naam, als JSON naast de
  plugin".
  - Ik heb de JSON gelezen als bron in het pakket en de presets in de plugin gebakken.
  - Losse bestanden naast de geïnstalleerde plugin zouden voor elk formaat en OS een eigen vindplaats en een
    JSON-lezer in C++ nodig hebben.
  - De keuze en hoe het werkt staan nu in ARCHITECTUUR.md.
