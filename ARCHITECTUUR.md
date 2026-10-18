# ARCHITECTUUR — Toetsenpad

## Lagen
```
dsp/                 DE DSP (gegeven, niet herschrijven): pb_*.h + de keten als constante (ontwerpen/keten_toetsenpad.h)
plugin/              de CLAP-schil (fase 0–2), clap-wrapper maakt VST3 en AUv2
gereedschap/         pb-render (CLAP-host), vergelijk.mjs (null-test + statistisch), ci-null.sh
testsignalen/        invoer + verwachte uitvoer (uit de WASM van de tool) + pluginval-configs
```

## De keten
13 knopen, uitgevoerd in een vaste volgorde per sample (SPEC.md "Keten"). De ketenmotor
(`dsp/pb_graaf.h`) leest de beschrijving `PB_KETEN`; `dsp/graaf_ontwerp.h` zet die achter het Ontwerp-contract:
`geheugenNodig`, `activeer`, `reset`, `zetParam`, `leesParam`, `verwerk`, `latentie`, `staartSec`, `PARAMS`, `META`.
- `zetParam(i, v)` en `leesParam(i)` nemen de INDEX in `PARAMS` (0..n−1), niet het parameter-id; de schil zoekt de
  index bij het id.
- `staartSec()` leest de toestand van de smoothers en hoort dus bij de audiothread: reken hem aan het eind van process
  (en in activate) uit en bewaar hem atomisch; `tail.get` leest die waarde.

## Threads
- **Audiothread**: alleen `process`. Events (parameterwaarden) worden op hun sample toegepast: het blok wordt bij elk
  event gesplitst; de DSP rekent per sample, dus de uitvoer is gelijk aan die zonder splitsen.
- **Mainthread**: `init`, `activate` (hier wordt geheugen gereserveerd), state, parameters lezen en tekst.
- Geen locks, geen allocatie, geen syscalls in `process` (RTSan bewaakt het in CI).
- Parameteroverdracht naar het Ontwerp: alleen via CLAP-events (`process` en `params.flush`). Wat de mainthread
  moet lezen (`params.get_value`, `state.save`) komt uit een atomaire kopie per parameter die de audiothread
  bijwerkt; `state.load` terwijl de plugin actief is zet de waarden in die kopie plus een atomaire vlag, en
  `process`/`flush` neemt ze over vóór de events van dat blok. Verder geen gedeelde variabelen.
- `staartSec()` geeft −1 voor oneindig (lusversterking ≥ 1): meld dan `INT32_MAX` of meer (CLAP: oneindig).
- De null-test `ci-null.sh clap` heeft een geval `testsignalen/automatie/`: 720 parameter-events, ook midden in een
  blok en twee op dezelfde sample. Bit voor bit gelijk = het splitsen bij events klopt.

## State
Binair, klein en vooruit-compatibel: `"PBST"`, versie (u32), aantal (u32), dan per parameter (id u32, waarde f64).
Laden gaat per id: een onbekend id (van een nieuwere versie) wordt overgeslagen, een ontbrekend id houdt zijn
standaard. Parameter-id's veranderen nooit. Versie is nu 1. Laden leest de versie: andere magic, versie 0 of een
blob korter dan zijn `aantal` belooft = false (niets veranderd); een hogere versie laadt gewoon per id (een nieuwere
plugin mag er velden bij hebben, nooit een id een andere betekenis geven). Na een geslaagde load: `host_params->rescan(CLAP_PARAM_RESCAN_VALUES)`
(clap-validator eist het in state-reproducibility), en als de plugin actief is `request_flush`.

## Paneel (fase 3)
Het frontpaneel uit design.md en `frontpaneel.svg`, in drie lagen:
- `plugin/src/paneel.{h,cpp}`: het model, zonder platform. Indeling in de coördinaten van de mockup (560 × 220, y
  omlaag), gelijk geschaald naar de venstermaat en in het midden; tekent naar een lijst opdrachten (vlak, rand, open lijn,
  cirkel, tekst) en vertaalt de muis: slepen 200 punten omhoog = het hele bereik, lineair in de CLAP-waarde; Shift = 10×
  fijner (zonder sprong als Shift halverwege verandert); dubbelklik = standaard; de laatst aangeraakte knop is de gekozen
  knop (tape-boog). De waardetekst komt van `value_to_text` van de plugin.
- `plugin/src/paneel_mac.mm`: de NSView (CLAP gui-extensie, alleen `CLAP_WINDOW_API_COCOA`, ingebed; clap-wrapper geeft
  hem door aan VST3 en AUv2). Tekent met CoreGraphics/CoreText; IBM Plex Mono en Silkscreen (SIL OFL, `plugin/lettertypen/`)
  zitten als bytes in het binary. De klasse wordt met de objc-runtime gemaakt met het adres van de code in de naam, zodat
  CLAP, VST3 en AU in één host elk hun eigen klasse hebben. Linux en Windows hebben (nog) geen paneel.
- `plugin.cpp`: de koppeling (`koppeling.h`). Het paneel leest alleen de atomaire spiegel (`get_value`). Een wijziging gaat
  als BEGIN/WAARDE/EIND in een lock-vrije rij (één schrijver: de mainthread; één lezer: process of params.flush, die nooit
  tegelijk draaien; wat niet past wacht op de mainthread, in volgorde) en de plugin vraagt `request_flush`. process/flush
  geeft ze als `CLAP_EVENT_PARAM_GESTURE_BEGIN`/`PARAM_VALUE`/`GESTURE_END` (op sample 0) aan de host en past de waarde toe
  zoals een event van de host (valkuil 14). Het paneel raakt het Ontwerp en de audiothread nooit. Gaat het paneel dicht
  midden in een gebaar, dan volgt GESTURE_END. Zolang de host een gestuurde waarde nog niet overnam, toont het paneel die
  (hooguit ±0,5 s; daarna weer wat de host heeft).
- Toetsen: `pb-paneeltoets` (Linux en macOS; het model met de echte rij, tegen de getallen in `frontpaneel.svg`) en
  `pb-paneel` (macOS; door het gebouwde .clap in een NSWindow, gesimuleerde muis, pixels terug, schermafbeeldingen als
  artefact `macos-frontpaneel`). pluginval draait op macOS zonder `--skip-gui-tests`; CI eist dat zijn editortoetsen de
  editor echt openden.

## Presets (fase 4)
Een fabriekspreset is `presets/<sleutel>.json`: `{ naam, uitleg, waarden: { <parameternaam>: waarde } }` (geen
state-blob). Bak ze bij het bouwen in het binary (CMake maakt er een tabel van; een onbekende naam of een waarde buiten
het bereik is een bouwfout), dan hoeft er niets naast de plugin te staan en geen JSON-lezer in de plugin.
- CLAP: `preset-discovery` met locaties van soort `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN` en `preset-load`. Laden =
  hetzelfde pad als state.load (ontbrekende namen naar hun standaard), daarna rescan en `loaded`.
- Per preset een eigen provider met een container van één (zie valkuil 15: clap-validator loopt vast op een container
  met meer presets).
- AU: `patches/clap-wrapper-auv2-presets.patch` leest dezelfde factory en geeft ze als factory presets
  (`GetPresets`, `NewFactoryPresetSet`). VST3 krijgt van clap-wrapper geen presetlijst.
- Een verse plugin staat op de standaarden; laat de AU daarom op "Untitled" (−1) beginnen, niet op preset 0.
- Bouw: `plugin/presets.cmake` maakt `fabriekspresets.inc` (op volgorde van sleutel); een kapot bestand, een veld dat
  ontbreekt of een waarde die geen getal is faalt bij configure, een onbekende parameternaam of een waarde buiten het
  bereik (of niet exact in float32) faalt bij compileren (`static_assert` in `plugin.cpp` tegen `Ontwerp::PARAMS`).
- Toetsen: clap-validator (`preset-discovery-crawl`, `-descriptor-consistency`, `-load` moeten PASSED zijn). Hij bewaart
  gevonden presets per plek, en alle providers melden dezelfde plek (PLUGIN, zonder pad): hij laadt er dus maar één.
  Daarom `pb-presettoets` (door het .clap, Linux en macOS): elke provider en elke preset, laden niet actief en actief,
  ontbrekende namen naar de standaard, bit voor bit dezelfde klank als dezelfde waarden via params.flush, en een
  onbekende sleutel geeft false en on_error. `gereedschap/presets-lijst.py` zet `presets/` om naar wat de toetsen
  verwachten. Op macOS moet elke presetnaam in de uitvoer van `auval -strict -v` staan.

## Instrument
- Geen audio-ingang: `audio-ports` meldt 0 ingangen en 1 uitgang; `audio-ports-config` (fase 5) alleen stereo en mono
  uitgang. `verwerk(in, uit, k, n)` krijgt een invoer van nullen (één vaste stille buffer per kanaal, gereserveerd in
  activate): de keten leest hem niet, maar de pointers moeten geldig zijn.
- CLAP-features `CLAP_PLUGIN_FEATURE_INSTRUMENT` en `CLAP_PLUGIN_FEATURE_SYNTHESIZER`; clap-wrapper maakt er met
  `AUV2_INSTRUMENT_TYPE aumu` een AU-instrument van (CMake, `make_clapfirst_plugins`).

## Noten
Gebruik `dsp/pb_noten.h` (gegeven): `pb::NootStapel` (monofoon, legato, hooguit 16 vastgehouden toetsen) en
`pb::nootVerhouding(n)` = 2^((n − 57)/12) via `pb_math.h`. De verwachte uitvoer in `testsignalen/noten/` rekent precies
dit, dus wijk er niet van af:
1. Bij elk noot-event (op zijn sample, het blok wordt daar gesplitst zoals bij een parameter-event):
   noot-aan → `stapel.aan(key)`, noot-uit → `stapel.uit(key)`, `CLAP_EVENT_NOTE_CHOKE` → `stapel.leeg()`; een key
   buiten 0..127 negeren (geen stap 2).
2. Daarna altijd: is er een bovenste noot `n = stapel.boven()` (≥ 0), dan
   `zetParam(0, nootVerhouding(n))` en daarna `zetParam(1, 1.0)`; anders alleen `zetParam(1, 0.0)`.
   (Noot = index 0, Poort = index 1; dit zijn indexen in `PARAMS`, geen id's.)
3. MIDI-bytes (`CLAP_EVENT_MIDI`, de AU-wrapper geeft ze zo door): `0x9n` met velocity > 0 = noot-aan, `0x8n` of `0x9n`
   met velocity 0 = noot-uit, `0xBn` CC 120 of 123 = stapel leeg en dan stap 2.
4. Velocity speelt geen rol. De noot zet de parameterspiegel NIET (de host ziet de knopwaarde, niet de laatste noot); state
   bewaart dus geen noot. `reset` maakt de stapel leeg (na `ontwerp.reset()`).
5. Note port: één ingang, `supported_dialects = CLAP_NOTE_DIALECT_CLAP`, `preferred_dialect = CLAP_NOTE_DIALECT_CLAP`
   (valkuil 19). De null-test speelt de noten ook als MIDI-bytes: stap 3 moet dus ook werken.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
