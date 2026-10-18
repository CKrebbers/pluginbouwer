Stopcriterium gehaald.

# Uitslag fase 4 — fabriekspresets (proef F4, tak `proef/f4`)

Laatste CI-run met de code: https://github.com/CKrebbers/pluginbouwer/actions/runs/39161865669 (commit 26f88b0,
alle drie de jobs groen). De commit met dit verslag start nog een run; die verandert geen code.

## Wat ik deed
- **Presets in het binary** (`plugin/cmake/presets.cmake`): CMake leest bij het configureren `presets/*.json`
  (`string(JSON)`, in naamvolgorde, `CONFIGURE_DEPENDS`) en schrijft `build/gegenereerd/presets_tabel.h`: per preset
  sleutel (bestandsnaam = load key), naam, uitleg en de waarden per parameternaam, plus per preset een `static_assert`
  op `presetGeldig` (`plugin/src/presets.h`, constexpr tegen `PARAMS`). Een onbekende parameternaam, een dubbele naam
  of een waarde buiten [min, max] is zo een bouwfout; JSON-fouten (geen naam, geen getal, dubbele presetnaam) stopt
  CMake al. Lokaal gecontroleerd met een tijdelijk `presets/zz-fout.json` (Tijd 99999, en een naam "Tijdd"): beide
  `static assertion failed … presets/zz-fout.json: onbekende parameternaam of waarde buiten het bereik van PARAMS`.
  Geen JSON-lezer in de plugin en niets naast de plugin. Alles is constexpr (valkuil 16).
- **CLAP preset-discovery** (`plugin/src/presets.cpp`): per preset een provider (`<clap-id>.preset.<sleutel>`) met
  één locatie van soort `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN` (location = nullptr, factory content) en daarin een
  container van precies één preset (valkuil 15), met plugin-id, vlag, maker en uitleg. `get_factory` geeft hem voor
  `CLAP_PRESET_DISCOVERY_FACTORY_ID` en de `…_COMPAT`-id.
- **preset-load** (`plugin/src/dub_echo.cpp`): `from_location` zoekt de preset bij de load key, zet ontbrekende
  namen op hun standaard en gaat hetzelfde pad als `state.load` (nieuwe functie `neemOver`: inactief direct, actief
  via de atomaire kopie en de vlag die process/flush overneemt), dan `rescan` en `host_preset_load->loaded`.
  Onbekende key of een andere locatie: false en `on_error`, niets veranderd.
- **AU factory presets**: `patches/clap-wrapper-auv2-presets.patch` stond al in het pakket en leest dezelfde
  factory. De AU begint op −1 "Untitled" (AUBase doet dat zelf; de patch zet geen preset bij het maken).
  In de tweede push heb ik de patch uitgebreid met `NewCustomPresetSet` (zie Pushes).
- **Toetsen**:
  - `plugin/toets/preset_toets.cpp` → `pb-presettoets` (Linux en macOS, door het .clap): de factory heeft één provider
    per JSON-bestand, elk met één PLUGIN-locatie en een container van één; elke preset laadt inactief en actief
    (achterstevoren en twee keer), daarna staat elke parameter (gezocht op de naam uit `params.get_info`) op de waarde
    uit de JSON of op zijn standaard; rescan en loaded geroepen; foute key/locatie geeft false en on_error.
  - `plugin/toets/au_preset_toets.cpp` → `pb-aupresettoets` (macOS, AudioToolbox op de geïnstalleerde .component):
    vers op −1; `kAudioUnitProperty_FactoryPresets` = de vijf namen in volgorde; elke preset via
    `kAudioUnitProperty_PresentPreset` vóór en na `AudioUnitInitialize`: daarna de huidige preset, en alle parameters
    (`AudioUnitGetParameter`) zoals de JSON.
- **CI**: Linux eist `preset-discovery-crawl`, `preset-discovery-descriptor-consistency` en `preset-discovery-load`
  PASSED (`gereedschap/cv-eis.sh`, SKIPPED telt niet). macOS haalt de namen uit `presets/*.json` en eist dat elke naam
  in `auval -strict -v` staat, en dat "Preset name is not retained" er niet in staat. Beide preset-toetsen en het
  bewijs staan in de stap "Bewijs".

## Poorten (uit het log van run 39161865669)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator preset-toetsen | groen | `preset-discovery-crawl: PASSED`, `preset-discovery-descriptor-consistency: PASSED`, `preset-discovery-load: PASSED` |
| auval toont de presets | groen | `ID:   0    Name: Lange dub` … `ID:   4    Name: Zelfoscillatie`; 5 × `auval toont preset: …`; `AU VALIDATION SUCCEEDED.` |
| preset-toets Linux / macOS | groen | 2 × `preset-toets: 46/46 controles ok, 0 fout` |
| AU-preset-toets macOS | groen | `au-preset-toets: 18/18 controles ok, 0 fout` |
| clap-validator 0.4.1 geheel + 14 state/param | groen | `44 tests run, 36 passed, 0 failed, 0 warnings, 8 skipped`; `geen FAILED`; 14 × `PASSED` |
| pluginval 10 AU `--rtcheck enabled --repeat 3` (met editor) | groen | `SUCCESS`; 3 × de vier editor-toetsen |
| pluginval 10 VST3 macOS / Linux | groen | `SUCCESS` / `SUCCESS` |
| gui-toets, paneel-toets, state-toets | groen | `gui-toets: 52/52`, `paneel-toets: 35/35` (Linux, macOS), `state-toets: 125/125` (Linux, macOS) |
| null-tests CLAP (512, wisselend, state, automatisering), VST3 Linux/macOS, AU | groen | alles `−∞ (bit voor bit gelijk) … ja`; `PASSED` per pluginval test |
| zelfoscillatie statistisch | groen | `terts 0.000 dB, omhullende 0.000 dB` |
| RTSan / kanarie, TSan paneel-toets | groen | job "RTSan" groen (stappen "RTSan schoon", "Kanarie MOET falen", TSan geslaagd) |

## Pushes
Twee pushes; CI was bij de **eerste** al groen (run 39160698093). Lokaal had ik vooraf de plugin op Linux gebouwd en
clap-validator 0.4.1 uit de bron gedraaid (de drie preset-toetsen PASSED), plus de preset-toets, state-toets en
CLAP-null-test. De AU-toets kon ik hier niet compileren (geen AudioToolbox); ik deed een syntaxcontrole met een
nagemaakte header.
De tweede push repareert een waarschuwing die de eerste run liet zien: auval -strict gaf `WARNING: Preset name is
not retained in retrieved class data`. auval zet een eigen presetnaam (PresentPreset met nummer −1 →
`NewCustomPresetSet`), maar de `SaveState` van clap-wrapper schrijft `_current_program_name`, en dat werkte alleen
`NewFactoryPresetSet` (uit de presetpatch) bij. In Logic zou een eigen preset dan als "Program" terugkomen. De patch
heeft nu ook `NewCustomPresetSet`; CI eist dat de waarschuwing weg is, en ze is weg.

## Wat onduidelijk of fout was in het pakket
- **clap-validator 0.4.1 laadt maar één preset** in `preset-discovery-load`: hij bewaart gevonden presets in een map
  per locatie, en met "per preset een provider met een PLUGIN-locatie" (wat valkuil 15 aanraadt) hebben ze allemaal
  dezelfde locatie, zodat alleen de laatste overblijft. PASSED bewijst dus niet dat alle presets laden; daarom
  `pb-presettoets`. Nu in ARCHITECTUUR.md "Presets".
- De presetpatch miste `NewCustomPresetSet` (hierboven); nu in de patch en in ARCHITECTUUR.md.
- pluginval meldt bij de AU `!!! WARNING: Current program is -1... Is this correct?`. Dat volgt uit de eis "laat de
  AU op Untitled (−1) beginnen" en is geen fout; nu in ARCHITECTUUR.md vermeld, zodat een volgend gesprek er niet
  over struikelt.
- De prompt noemt de drie preset-toetsen niet bij naam; ik nam `preset-discovery-crawl`,
  `preset-discovery-descriptor-consistency` en `preset-discovery-load` (de enige drie met "preset" in 0.4.1).
- VST3 krijgt van clap-wrapper geen presetlijst (zoals ARCHITECTUUR zegt); in Logic telt alleen de AU.
- auval toont ook `WARNING: AU implements MusicDeviceMIDIEvent but is of type 'aufx'`: dat komt uit clap-wrapper en
  stond er al; niet aangeraakt.
