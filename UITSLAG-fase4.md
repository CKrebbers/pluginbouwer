Stopcriterium gehaald.

# Uitslag fase 4 — fabriekspresets (proef G3)

Laatste CI-run met de code (commit `4b1a0cb`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39162493000
(alle drie de jobs groen: Linux, RTSan, macOS arm64). Deze commit voegt alleen dit verslag en het vinkje in TASKS.md
toe; CI draait er opnieuw op.

## Wat ik deed
- **`presets/` bestond niet.** PROMPTS.md zegt "Fabriekspresets uit `presets/`", maar de map zat niet in het pakket.
  Ik heb er vier gemaakt, in de vorm uit ARCHITECTUUR.md (`{ naam, uitleg, waarden }`): `helder` (Filter 5000 Hz,
  Loslaten 300 ms), `pluk` (1800 Hz, 40 ms), `wolk` (1200 Hz, 3500 ms) en `zacht` (450 Hz, 1200 ms). Noot en Poort
  staan er niet in, want die zet elke noot. Ze krijgen dus hun standaard ("ontbrekende namen naar hun standaard").
- **`plugin/presets.cmake`** leest bij het configureren `presets/*.json` (in naamvolgorde) met `string(JSON)` en
  schrijft `build/presets/presets_tabel.h` (macro `PB_PRESETS`). Op deze fouten stopt de configure met een melding:
  geen object, een veld dat ontbreekt of te veel is, een waarde die geen getal is, een naam die twee keer voorkomt,
  een dubbele parameter of een ongeldige bestandsnaam (die wordt de load key).
- **`plugin/src/plugin.cpp`**:
  - Bouwfouten via `static_assert` tegen `PARAMS` uit `dsp/` (één bron), voor een onbekende parameternaam, een waarde
    buiten het bereik of een waarde die niet exact in float32 past. Getoetst: alle drie breken de bouw af, met hun
    eigen melding.
  - preset-discovery-factory (ook onder het `…-draft-2`-id). Elke preset heeft een eigen provider met één locatie
    (soort `PLUGIN`, zonder pad, fabriekspresets) en een container van één preset (valkuil 15), met naam, load key,
    plugin-id, maker en uitleg.
  - preset-load (`from_location`). Een preset laden loopt via hetzelfde pad als `state.load` (`zetWaarden`, uit
    `stLoad` gehaald): spiegel, Ontwerp of klaar plus vlag, rescan en, als de plugin actief is, `request_flush`. Daarna
    `loaded`; een onbekende load key geeft `on_error` en false.
  - Alles is constant geïnitialiseerd (constexpr-tabellen, `std::array` van descriptors). Er is dus geen global die
    init vult (valkuil 16).
- **AU**: de bestaande `patches/clap-wrapper-auv2-presets.patch` deed het meteen. De AU begint op −1 "Untitled".
  auval waarschuwde wel: "WARNING: Preset name is not retained in retrieved class data". Een eigen preset van de host
  (`PresentPreset` met nummer < 0) kwam niet in ClassInfo, omdat SaveState van clap-wrapper `_current_program_name`
  schrijft. Daarvoor is er nu een nieuwe patch, **`patches/clap-wrapper-auv2-eigen-presetnaam-in-classinfo.patch`**.
  Die overschrijft `NewCustomPresetSet` en hangt niet af van de andere patches: ik heb getoetst dat hij zowel op de
  kale als op de gepatchte bron past, en dat een tweede configure hem overslaat.
- **Toetsen** (nieuw, door de gebouwde binaries):
  - `gereedschap/presets-verwacht.sh` maakt met jq een tabel uit de JSON. Die staat los van presets.cmake, zodat de
    toets niet dezelfde lezer gebruikt als de bouw.
  - `pb-statetoets --presets` (CLAP, Linux en macOS) toetst:
    - discovery: het aantal providers, unieke id's, één locatie PLUGIN, een container van één, naam en load key per
      JSON-bestand, en geen presets voor een bestandslocatie;
    - laden van elke preset, niet actief en actief: waarden = JSON (de rest op standaard), rescan, `loaded`, het geluid
      bit voor bit gelijk aan dezelfde waarden via `params.flush`, en een state-rondje;
    - een onbekende key geeft false, `on_error` en verandert niets.
  - `pb-aupresets` (nieuw, macOS, door de geïnstalleerde AU) toetst:
    - de lijst `FactoryPresets` (namen en nummers zoals in de JSON);
    - een verse instantie op −1 "Untitled" en op de standaarden;
    - elke preset vóór en na Initialize: de waarden, PresentPreset, en dat de waarden na Render blijven staan;
    - ClassInfo met de presetnaam, die in een nieuwe instantie laadt;
    - een eigen preset die in ClassInfo komt, en een onbekend nummer dat geweigerd wordt.
- **CI** (`.github/workflows/ci.yml`):
  - De drie preset-toetsen van clap-validator staan in de lijst die PASSED moet zijn (SKIPPED telt niet).
  - De auval-stap eist "HAS FACTORY PRESETS" en elke presetnaam uit `presets/*.json` als regel `ID: n  Name: <naam>`.
    Hij faalt op "Preset name is not retained".
  - De stap draait nu ook pb-aupresets, en pb-statetoets krijgt `--presets`.
  - Alles van fase 0–3 is ongewijzigd. Eén reparatie: de controle `[ … ] && grep -q "AU VALIDATION SUCCEEDED"` stond
    vroeger als laatste regel van de stap. Nu er regels na komen, staat er `|| exit 1` achter, anders vangt `bash -e`
    hem niet.

## Poorten (bewijs uit het log van run 39162493000)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator preset-toetsen PASSED | groen | `clap-validator preset-discovery-crawl: PASSED`, `… preset-discovery-descriptor-consistency: PASSED`, `… preset-discovery-load: PASSED`; `44 tests run, 38 passed, 0 failed, 0 warnings, 6 skipped` (was 35 passed, 9 skipped) |
| auval toont de presets | groen | `HAS FACTORY PRESETS`, `ID: 0 Name: Helder`, `ID: 1 Name: Pluk`, `ID: 2 Name: Wolk`, `ID: 3 Name: Zacht`; 4× `auval toont preset "…" (presets/….json)`; `AU VALIDATION SUCCEEDED.`, `auval exitcode: 0`; geen "Preset name is not retained" meer |
| presets door de AU (pb-aupresets) | groen | `40 controles, 0 fout`, o.a. `een verse instantie staat op preset -1 "Untitled"`, `preset 0 "Helder" [voor Initialize]: waarden 1=1 2=0 3=5000 4=300 = de JSON`, `na twee keer Render nog steeds …`, `een eigen preset (-1, "Eigen klank") … in ClassInfo ("Eigen klank")` |
| presets door het .clap (pb-statetoets) | groen | `262 controles, 0 fout` (Linux en macOS; was 143 zonder presets) |
| clap-validator param/state PASSED (fase 2) | groen | 12× `…: PASSED`, `clap-validator exitcode: 0` |
| pluginval 10 AU `--rtcheck enabled --repeat 3`, met GUI | groen | `pluginval AU exitcode: 0`, `SUCCESS`, `pluginval AU: editor geopend (6×)` |
| pluginval 10 VST3 macOS (pluginval-rt.sh) | groen | `SUCCESS`, `real-time-schendingen: 0` |
| pluginval 10 VST3 Linux | groen | `pluginval exitcode: 0` |
| paneel (fase 3) | groen | `225 controles, 0 fout`; artefact `macos-paneel-schermafbeelding` (7 bestanden) geüpload |
| null-test CLAP/VST3/AU | groen | stappen geslaagd; `max abs diff 0 <= tolerance 3.16228e-05` (5× Linux, 10× macOS) |
| RTSan schoon / kanarie faalt | groen | `RTSan: geen real-time-schending in process`; `kanarie gevangen zoals bedoeld` (malloc in `process`, exit 43) |
| artefact `macos-keten_toetsenpad` | groen | `Artifact macos-keten_toetsenpad has been successfully uploaded!` |

## Pushes tot CI groen: 1 (plus 1 voor een waarschuwing)
1. `770f19b`: meteen alles groen (run 39161596808). Lokaal (Linux) had ik vooraf de plugin, clap-validator 0.4.1 uit
   de bron (de drie preset-toetsen PASSED), pb-statetoets en de null-test gedraaid. pb-aupresets kon ik hier niet
   bouwen; de syntax heb ik gecontroleerd tegen een kleine stub van de AudioToolbox-API.
2. `4b1a0cb`: in het log van run 1 stond een auval-waarschuwing ("Preset name is not retained in retrieved class
   data"). Ik heb die gerepareerd (de patch hierboven) en CI faalt er nu op. Ook toetst CI de presetnamen nu in de
   lijst van auval zelf, niet ergens in het log.

## Onduidelijk of fout in het pakket
- **`presets/` ontbreekt.** De opdracht en ARCHITECTUUR.md gaan uit van een map met presets, maar die zat niet in
  het pakket. Ik heb zelf namen en waarden gekozen; Clay of de Pluginbouwer kan ze vervangen. Een nieuwe JSON in
  `presets/` is genoeg: CMake neemt hem op (`CONFIGURE_DEPENDS`) en CI toetst hem.
- **clap-validator `preset-discovery-load` laadt maar één preset.** Hij verzamelt de presets in een map per locatie,
  en de locatie van soort PLUGIN is bij elke provider dezelfde (`<plugin>`). Door de omweg van valkuil 15 overschrijft
  elke provider de vorige. PASSED betekent dus "de laatste preset laadt". Daarom laadt pb-statetoets ze allemaal.
- **De auval-uitvoer**: auval toont de presets als `ID:   n    Name: …` onder `HAS FACTORY PRESETS`. Die indeling
  stond niet in het pakket; nu staat hij in de CI.
- **Nieuwe valkuil (voor CLAUDE.md):** auval "Preset name is not retained in retrieved class data". Opgelost met
  `patches/clap-wrapper-auv2-eigen-presetnaam-in-classinfo.patch`. Ik heb CLAUDE.md zelf niet veranderd.
- **AU: parameters vlak voor een preset.** Zet een host na Initialize een parameter en kiest hij meteen daarna een
  preset, zonder Render ertussen, dan wint de parameter bij de volgende Render. clap-wrapper zet een SetParameter
  namelijk als event in een rij, en process neemt de preset (zoals een state-load) vóór de events van het blok over.
  In een DAW die speelt komt dat niet voor; pb-aupresets rendert daarom tussendoor, zoals een host. Ik heb het zo
  gelaten. Het geldt ook voor RestoreState en is ontwerp van ARCHITECTUUR.md ("vóór de events van dat blok").
- VST3 krijgt geen presetlijst (zoals ARCHITECTUUR.md zegt; clap-wrapper v0.16.0 kent preset-discovery alleen in de
  AU-patch).
- Niets uitgezet: alle validators, null-tests en het paneel van fase 0–3 draaien ongewijzigd.
