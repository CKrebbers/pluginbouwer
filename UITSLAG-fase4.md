Stopcriterium gehaald.

# Uitslag fase 4 — Toetsenpad (proef g1)

Laatste CI-run met code (commit `fdeda43`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39161826100
(jobs linux, rtsan en macos alle drie success). De commit met dit verslag verandert alleen docs; zijn run draait dezelfde jobs.

## Wat ik deed
- **`presets/`** bestond nog niet in het pakket. Ik heb vier fabriekspresets gemaakt in de vorm van ARCHITECTUUR.md
  (`{ naam, uitleg, waarden }`): `donker.json` "Donker", `helder.json` "Helder", `kort.json` "Kort en droog" en
  `lange-staart.json` "Lange staart". Ze zetten alleen Filter en Loslaten. Noot en Poort ontbreken, dus die krijgen
  hun standaard (1 en 0). Een preset met Poort 1 zou zonder noot blijven klinken, en Noot overschrijft elke gespeelde
  noot toch.
- **`plugin/CMakeLists.txt`** leest de bestanden met `string(JSON …)` en schrijft `build/gegenereerd/presets_tabel.h`.
  De volgorde is die van de bestandsnamen; de sleutel is de bestandsnaam en dient ook als load key.
  - Deze fouten zijn een configure-fout met de reden erbij:
    - geen JSON-object;
    - `naam` of `uitleg` ontbreekt of is geen tekst;
    - een naam die al bestaat;
    - `waarden` is geen object, of een waarde is geen getal;
    - een sleutel met andere tekens dan a-z, 0-9 en `-`;
    - geen enkele preset.
  - `toetsenpad.cpp` toetst met `static_assert` tegen `PARAMS` uit `dsp/`: een onbekende parameternaam, een waarde
    buiten het bereik of een naam die twee keer voorkomt geeft een compileerfout.
  - Lokaal getoetst: `Filtr`, `Filter: 30000`, de naam "Donker" twee keer en `"300"` als tekst falen alle vier, met de
    juiste melding.
- **`plugin/src/toetsenpad.cpp`**:
  - **preset-discovery-factory**, onder `CLAP_PRESET_DISCOVERY_FACTORY_ID` en `_COMPAT`:
    - per preset een provider `nl.varve.pluginbouwer.toetsenpad.preset.<sleutel>` (valkuil 15);
    - `init` declareert één locatie van soort `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN` (location nullptr,
      fabrieksinhoud);
    - `get_metadata` geeft één preset met load key: een container van één. Daarbij komen het plugin-id
      (`clap`), de vlag fabrieksinhoud, de maker en de uitleg als beschrijving.
    - De descriptors en id's zijn constant geïnitialiseerd: geen globals die init vult (valkuil 16).
  - **preset-load** (`from_location`, ook onder `_COMPAT`): begint vanaf de standaarden, zet de waarden uit de tabel,
    en gaat dan via hetzelfde pad als `state.load`. Dat pad heb ik daarvoor uit stateLoad gehaald als `zetAlleWaarden`.
    - Actief: de waarden gaan in de spiegel met de vlag, en process of flush neemt ze over.
    - Daarna rescan, en als de plugin actief is `request_flush`; dan `loaded()` bij de host.
    - Een andere soort locatie of een onbekende load key geeft `on_error` en false, en er verandert niets.
  - De AU krijgt dezelfde lijst via de al aanwezige `patches/clap-wrapper-auv2-presets.patch`. Die begint op
    "Untitled" (−1): dat doet `AUBase` zelf, en de patch kiest geen preset bij het starten.
- **`plugin/toets/presettoets.cpp` → `pb-presettoets`** (eigen toets, door het .clap, Linux en macOS). Hij toetst:
  - de factory onder beide id's, het aantal providers en de descriptors (gelijk aan `desc`);
  - per provider de locatie en de metadata;
  - per preset, inactief en actief: `get_value` geeft de JSON-waarden (en de standaard voor wat ontbreekt), ook als
    de instantie vooraf anders stond; daarna rescan, `loaded` met de juiste key, en `request_flush` als actief;
  - dat de render bit voor bit gelijk is aan dezelfde waarden via flush;
  - dat een foute key of locatie false geeft, met on_error, zonder verandering.
  - Lokaal heb ik een fout in de plugin gezet (laden vanaf de huidige waarden in plaats van de standaarden). De toets
    ving hem, daarna heb ik de fout teruggedraaid.
- **`.github/workflows/ci.yml`**:
  - clap-validator eist nu ook `preset-discovery-crawl`, `preset-discovery-descriptor-consistency` en
    `preset-discovery-load` = PASSED (SKIPPED telt niet).
  - De auval-stap leest de namen met `gereedschap/presetnamen.sh` (node) en faalt als een naam niet in de uitvoer van
    `auval -strict -v` staat.
  - pb-presettoets draait op Linux en macOS.
  - Een laatste stap in macos zet de auval-uitslag, de presetregels en de presettoets achteraan het log (valkuil 18).
- **LEERPAD.md**: hoe Clay zelf een preset toevoegt.
- `dsp/` en `testsignalen/` zijn niet veranderd. Geen toets of validator uitgezet.

## Poorten (bewijs uit de logs van run 39161826100)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator preset-toetsen (PASSED) | groen | `clap-validator preset-discovery-crawl: PASSED`, `preset-discovery-descriptor-consistency: PASSED`, `preset-discovery-load: PASSED` |
| clap-validator 0.4.1, totaal | groen | `clap-validator: 44 tests run, 38 passed, 0 failed, 0 warnings, 6 skipped (exit 0)` (fase 3: 35 passed, 9 skipped) |
| auval toont de presets | groen | `HAS FACTORY PRESETS` / `ID: 0 Name: Donker` / `ID: 1 Name: Helder` / `ID: 2 Name: Kort en droog` / `ID: 3 Name: Lange staart`; `auval toont preset: …` voor alle vier |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.` |
| pb-presettoets Linux / macOS | groen | `pb-presettoets: 63 toetsen, 63 ok, 0 fout (4 presets)` (beide) |
| pluginval 10 AU, `--rtcheck enabled --repeat 3` | groen | `Running tests 3 times`, `pluginval AU: exit 0, laatste regel: SUCCESS`; `Num programs: 4` en `Completed tests in pluginval / Plugin programs` (pluginval wisselt nu ook van preset) |
| pluginval 10 VST3 macOS (pluginval-rt.sh) | groen | `SUCCESS`, `real-time-schendingen: 0, waarvan pluginval-host (MidiEventList::addEvent): 0` |
| pluginval 10 VST3 Linux | groen | `pluginval VST3 Linux: exit 0, laatste regel: SUCCESS` |
| state-/param-toetsen clap-validator (fase 2) | groen | de 11 toetsen `PASSED` |
| null-tests CLAP (Linux, macOS), VST3 (Linux, macOS), AU | groen | alle `### Null-test …`-tabellen `bit voor bit gelijk` |
| pb-statetoets / pb-paneeltoets / pb-scherm | groen | `212 ok, 0 fout` (Linux en macOS), `37 ok, 0 fout` (Linux en macOS), `pb-scherm: 30 toetsen, 30 ok, 0 fout` |
| RTSan / kanarie | groen | stappen "RTSan door het .clap" en "RTSan-kanarie" success |

## Pushes
Twee pushes tot CI groen was.
1. `ba92da3`: linux en rtsan waren groen, net als de drie preset-toetsen van clap-validator. Ook auval was groen en
   toonde de vier namen. In macos faalde alleen mijn nieuwe stap pb-presettoets met `dlopen: … (not a file)`: op macOS
   is een .clap een bundel, en ik opende het pad zelf in plaats van `Contents/MacOS/<naam>`. pb-statetoets deed dat
   al goed; ik had het niet overgenomen.
2. `fdeda43`: die regel toegevoegd. Alles groen.

Vooraf had ik lokaal (Linux, clang 18) gedraaid:
- pb-presettoets 63/63;
- clap-validator 0.4.1 uit de bron: 38 passed, 0 failed, de drie preset-toetsen PASSED;
- pb-statetoets 212/212 en pb-paneeltoets 37/37;
- `ci-null.sh clap`: bit voor bit;
- pluginval 10 op de VST3: SUCCESS.

De AU kon ik lokaal niet toetsen.

## Onduidelijk of fout in het pakket
- **`presets/` ontbreekt.** De prompt zegt "Fabriekspresets uit `presets/`", maar het pakket heeft die map niet. Ik
  heb de inhoud zelf bedacht. Als Clay of de Pluginbouwer echte presets wil, moet de Pluginbouwer ze meeleveren, of
  moet de prompt zeggen dat het gesprek ze maakt (en hoeveel).
- **Noot en Poort in een preset.** ARCHITECTUUR.md zegt niet wat een preset met Noot en Poort hoort te doen. Poort 1
  in een preset laat het instrument zonder toets klinken. Ik laat ze weg, zodat ze op hun standaard staan. Een zin
  "presets zetten Noot en Poort niet" zou helpen.
- **clap-validator laadt maar één preset.** `preset-discovery-load` zet de gevonden presets in een map met de
  locatie als sleutel. Alle locaties van soort PLUGIN zijn dezelfde (`Internal`), dus met één provider per preset
  laadt hij alleen de laatste. De toets is PASSED, maar bewijst niet dat elke preset laadt. Daarom laadt pb-presettoets
  ze alle vier, en pluginval wisselt op de AU van programma (`Num programs: 4`).
- **Waarschuwingen die niet falen**:
  - auval: `WARNING: Preset name is not retained in retrieved class data`. clap-wrapper v0.16.0 schrijft in
    `SaveState` `_current_program_name`, en dat werkt alleen `NewFactoryPresetSet` (uit de patch) en `RestoreState` bij.
    Een naam die de host zet via `kAudioUnitProperty_PresentPreset` komt er niet in. Dit zit in clap-wrapper, niet in
    de plugin. Ik heb het niet gerepareerd: het hoort niet bij deze opdracht en auval slaagt.
  - pluginval AU: `WARNING: Current program is -1... Is this correct?`. Dat is precies "Untitled" (−1), zoals
    ARCHITECTUUR.md vraagt.
- **Valkuil-kandidaat.** Elke eigen toets die een .clap opent, moet op macOS `Contents/MacOS/<naam>` openen. Een
  gedeelde helper in `gereedschap/` (of een zin bij de toetsen) had mijn eerste rode push voorkomen.
- Verder niets geblokkeerd.
