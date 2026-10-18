Stopcriterium gehaald.

# Uitslag fase 4 — proef G2 (tak `proef/g2`)

Laatste CI-run met code (groen, alle drie de jobs): https://github.com/CKrebbers/pluginbouwer/actions/runs/39163849622 (commit `0debf98`). Deze commit verandert alleen dit verslag.

## Wat ik deed
- **`presets/` ontbrak in het pakket.** De opdracht zegt "Fabriekspresets uit `presets/`" en ARCHITECTUUR.md beschrijft
  het formaat (`presets/<sleutel>.json`: `{ naam, uitleg, waarden: { <parameternaam>: waarde } }`), maar de map was er
  niet. Ik heb er vijf gemaakt, alleen met Filter en Loslaten (Noot en Poort zijn de knoppen die de noten sturen; een
  preset laat ze op hun standaard): `basis` (900 Hz / 450 ms, de standaard), `donker` (350 / 900), `helder` (5000 / 200),
  `tokkel` (2500 / 40), `zweven` (1200 / 3000). Alle waarden zijn exact in float32.
- **In het binary gebakken** (`plugin/presets.cmake`): CMake leest de JSON (`string(JSON)`) en schrijft
  `fabriekspresets.inc` (op volgorde van sleutel). Een kapot bestand, een ontbrekend veld, een waarde die geen getal is
  of een dubbele naam faalt bij configure; elk bestand is een configure-afhankelijkheid, dus een andere waarde
  configureert opnieuw. Een onbekende parameternaam of een waarde buiten het bereik (of niet exact in float32) is een
  `static_assert` in `plugin.cpp` tegen `Ontwerp::PARAMS`, zodat de grenzen op één plek staan. Lokaal nagegaan: Filter
  9000, "Filtr", 900.1 → compileerfout; "900" als tekst, geen uitleg, dubbele naam → configure-fout. Geen JSON-lezer in
  de plugin, niets naast de plugin.
- **CLAP** (`plugin.cpp`): `preset-discovery`-factory met één provider per preset (valkuil 15); elke provider meldt in
  init één plek van soort `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN` (zonder pad, fabrieksinhoud) en geeft in
  `get_metadata` één preset: naam, sleutel als load key, het plugin-id, fabrieksvlag, maker, uitleg. `preset-load`
  (`clap.preset-load/2` en de compat-naam): de waarden van de preset, wat hij niet noemt naar de standaard, langs
  hetzelfde pad als state.load (`laadWaarden`, uit `stLoad` gehaald: actief → klaarzetten en process/flush neemt over;
  daarna rescan en request_flush), dan `loaded` aan de host. Onbekende sleutel of andere soort plek: `on_error` en
  false. Alles is constant (geen globals die init vult; valkuil 16).
- **AU**: `patches/clap-wrapper-auv2-presets.patch` (zat er al op sinds fase 0) leest dezelfde factory en maakt er
  factory presets van; ik hoefde niets aan de patch te doen. Een verse AU staat op "Untitled" (−1): dat doet `AUBase`
  zelf (AudioUnitSDK 1.1.0, `mCurrentPreset.presetNumber = -1`) en de patch verandert het pas in `NewFactoryPresetSet`.
  Ook nagegaan in de bron van pluginval v2.0.0-beta3: zijn "Plugin programs"-toets kan met −1 om (alleen een
  waarschuwing), dus geen crash in JUCE's `setCurrentProgram(-1)`.
- **Toets door het binary** (`plugin/toetsen/pb-presettoets.cpp`, 207 controles, Linux en macOS), met de verwachting uit
  `gereedschap/presets-lijst.py` (leest `presets/` los van CMake). Nodig omdat clap-validator 0.4.1 de gevonden presets
  per plek bewaart (`BTreeMap<LocationValue, …>`): alle providers melden dezelfde plek (PLUGIN, zonder pad), dus hij
  laadt er maar één. pb-presettoets: één provider per preset in de goede volgorde (de AU nummert zo), elke provider één
  plek en één preset met de juiste naam, sleutel en plugin-id; elke preset laden niet actief én actief, na eerst alle
  parameters ergens anders te zetten; `loaded` en rescan; elke parameter op de JSON-waarde of de standaard; de DSP klinkt
  bit voor bit als een verse instantie die dezelfde waarden via params.flush kreeg; onbekende sleutel, geen sleutel en
  een FILE-plek: false, `on_error`, geen `loaded`, niets veranderd. Lokaal ook met een preset zonder waarden (244
  controles groen).
- **CI**: de lijst van clap-validator-toetsen die PASSED moeten zijn (SKIPPED telt niet) kreeg `preset-discovery-crawl`,
  `preset-discovery-descriptor-consistency` en `preset-discovery-load`; pb-presettoets in beide jobs; na `auval -strict
  -v` haalt een awk de namen uit het blok "HAS FACTORY PRESETS" en moeten ze precies (en in volgorde) gelijk zijn aan
  de namen in `presets/`.

## Poorten (run 39163849622)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| clap-validator preset-toetsen (Linux) | groen | `clap-validator preset-discovery-crawl: PASSED`, `… preset-discovery-descriptor-consistency: PASSED`, `… preset-discovery-load: PASSED`; `44 tests run, 38 passed, 0 failed, 0 warnings, 6 skipped` (fase 3: 35 passed, 9 skipped), `clap-validator exit: 0` |
| auval toont de presets | groen | `HAS FACTORY PRESETS` / `ID:   0    Name: Basis` … `ID:   4    Name: Zweven`, de vergelijking met `presets/` zonder verschil, `AU VALIDATION SUCCEEDED.` |
| pb-presettoets (Linux en macOS) | groen | `pb-presettoets: 207 controles, 207 geslaagd, 0 mislukt` (beide) |
| clap-validator overige (fase 0–2) | groen | de 14 state-/param-toetsen `PASSED` (zelfde controle in CI), 0 failed |
| pluginval 10 VST3 Linux | groen | `SUCCESS` |
| pluginval 10 AU macOS (`--rtcheck enabled --repeat 3`, met GUI) | groen | `SUCCESS`, editor geopend in 3 herhalingen; de "Plugin programs"-toets meldt nu `Num programs: 5` (drie keer; de VST3 `Num programs: 0`, clap-wrapper geeft VST3 geen presetlijst) |
| pluginval 10 VST3 macOS (`pluginval-rt.sh`, met GUI) | groen | `SUCCESS` |
| null-tests (fase 1) | groen | Linux: alle rijen `−∞ (bit voor bit gelijk)`, ook automatie en noten CLAP/MIDI; VST3/AU: `max abs diff 0 <= tolerance 3.16228e-05` |
| pb-statetoets, pb-paneeltoets, pb-paneel (fase 2–3) | groen | `171 … 0 mislukt`, `79 … 0 mislukt`, `pb-paneel: 57 controles, 57 geslaagd, 0 mislukt`; artefact `macos-frontpaneel` |
| RTSan schoon en kanarie | groen | beide stappen geslaagd |

## Pushes
Twee pushes met code, beide in één keer groen:
1. `741f433` (run 39163006698): alles van fase 4. Groen bij de eerste keer, omdat ik vooraf lokaal op Linux bouwde en
   clap-validator 0.4.1 (uit de bron), pb-presettoets, pb-statetoets, pb-paneeltoets en `ci-null.sh clap` draaide.
2. `0debf98` (run 39163849622, groen): alleen CI strakker. Bij push 1 zocht CI elke naam los in de hele auval-uitvoer (`grep -F`), omdat ik het
   formaat van auval nog niet kende; nu het log het liet zien (`HAS FACTORY PRESETS`, `ID: n Name: …`) vergelijkt CI
   de lijst exact.

## Onduidelijk of fout in het pakket
- **`presets/` bestond niet**, terwijl de prompt "Fabriekspresets uit `presets/`" zegt. Ik heb de presets zelf bedacht;
  of dit de bedoelde klanken zijn kan alleen Clay zeggen (de JSON is makkelijk aan te passen; een fout is een bouwfout).
- **clap-validator 0.4.1 laadt bij PLUGIN-plekken maar één preset** (zie boven). De poort "preset-discovery-load PASSED"
  bewijst dus minder dan hij lijkt; daarom pb-presettoets. Het pakket zou dit naast valkuil 15 kunnen noemen.
- **auval-waarschuwing** `WARNING: Preset name is not retained in retrieved class data` (VERIFYING CLASS INFO, wel
  PASS). Die stond al in fase 3 (run 39162336902, zonder presets), dus niet door deze fase: auval zet een eigen
  presetnaam en clap-wrapper's `SaveState` schrijft zijn eigen `_current_program_name` in plaats van de naam van
  `PresentPreset`. Gevolg in Logic is hooguit dat een eigen preset een andere naam in de class data draagt; niet
  opgelost (buiten de opdracht), wel het melden waard.
- Een preset zet Poort op zijn standaard (0), zoals state.load: een toets die vastgehouden wordt terwijl je een preset
  kiest, klinkt dan niet meer tot de volgende aanslag. Dat volgt uit "Laden = hetzelfde pad als state.load (ontbrekende
  namen naar hun standaard)"; als dat niet de bedoeling is, moet ARCHITECTUUR.md Noot/Poort uitzonderen.
