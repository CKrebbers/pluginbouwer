Stopcriterium gehaald.

# Uitslag fase 3 — Toetsenpad (proef g1)

Laatste CI-run met code (commit `ac4c724`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39159392408
(jobs linux, rtsan en macos alle drie success). De commit met dit verslag verandert alleen docs; zijn run draait dezelfde jobs.

## Wat ik deed
- **Het paneel** (`plugin/src/paneel.h`, `paneel.cpp`) volgt design.md en `frontpaneel.svg`. Het heeft één rij van vier
  draaiknoppen (Noot, Poort, Filter, Loslaten) die 270° draaien, van −135° tot +135°. Elke knop heeft een groef, een
  spoorboog, een knop met een rand van 2 px en een wijzer. Het label staat erboven en de waarde eronder, als tekst van
  `value_to_text` (SPEC.md). Verder: de harde schaduw (3 px), een paneelrand van 2 px, hoeken 0 en geen gradiënten.
  De kleuren zijn de tokens uit design.md.
  - Het lettertype is IBM Plex Mono, met Silkscreen voor de naam.
  - Het paneel tekent zichzelf in software in een RGBA-buffer. Vormen krijgen anti-aliasing via afstandsfuncties.
    Tekst komt van stb_truetype (public domain, `plugin/extern/stb`). De lettertypen staan in het binary (SIL OFL 1.1,
    `plugin/extern/fonts`, met de licenties); CMake maakt er een tabel van. Zo tekent het overal hetzelfde, en is de
    tekening ook op Linux te toetsen.
  - De mockup-coördinaten schalen met `s = min(b/560, h/220)`. De beginmaat is 640 × 250, de kleinste 560 × 220.
  - Slepen: 200 pt omhoog is het hele bereik, lineair in de CLAP-waarde. Met Shift gaat het 10× fijner, en aan de
    grenzen wordt geklemd. Dubbelklikken zet de standaard terug.
  - De laatst aangeraakte knop krijgt een tapekleurige boog. De volle naam staat in de tooltip.
- **macOS** (`paneel_mac.mm`): de CLAP gui-extensie (Cocoa, ingebed, schaalbaar; `adjust_size` klemt op 560 × 220).
  - Een NSView tekent de buffer als sRGB-CGImage, op de backing scale (retina).
  - De klasse wordt tijdens het draaien gemaakt, met een naam per geladen binary. Zo botsen de AU en de VST3 niet als
    ze in één proces geladen zijn.
  - Een timer van 30 Hz tekent opnieuw als een getoonde waarde verandert, bijvoorbeeld door automatisering of state.
  - `destroy` mag twee keer worden aangeroepen. clap-wrapper's AUv2 `Cleanup` doet dat: eerst `_destroyWindow()`, dan
    nog eens `gui->destroy`.
  - clap-wrapper geeft het paneel door aan de VST3 (IPlugView) en de AUv2 (`kAudioUnitProperty_CocoaUI`). Op Linux
    is er geen gui-extensie (geen X11). pluginval opent daar dus geen editor; de tekening wordt daar via de toets getoetst.
- **Knoppen via de host** (valkuil 14, `toetsenpad.cpp`). Het paneel zet BEGIN/WAARDE/EIND in een lock-vrije
  SPSC-rij: één schrijver (de mainthread) en één lezer (process, of flush). Daarna vraagt het `request_flush`.
  - process (vóór de host-events van het blok) en `params.flush` sturen ze als `GESTURE_BEGIN`/`PARAM_VALUE`/
    `GESTURE_END` naar de host en passen de waarde zelf toe, zoals een event van de host.
  - Het paneel raakt het Ontwerp nooit aan. Het leest alleen de atomaire spiegel, en per knop een atomaire teller van
    waarden die nog onderweg zijn. Tot de waarde is aangekomen toont het paneel zijn eigen waarde; onder AUv2 doet
    `request_flush` niets.
  - Is de rij vol (een host die niet flusht en niet rendert), dan gaat een gebaar zonder BEGIN ook zonder WAARDE en
    EIND, en blijft er altijd plaats voor EIND.
- **Toetsen**:
  - `plugin/toets/paneeltoets.cpp` → `pb-paneeltoets`, Linux en macOS. Hij gaat door het .clap via een eigen
    extensie (`paneel_toets.h`, die een host niet kent). Hij toetst:
    - de kleuren op plekken uit de svg, ook op 2× en op 560 × 220;
    - slepen, Shift, klemmen en dubbelklik;
    - dat `get_value` pas na flush verandert;
    - de vorm van de gebaren (B, V…, E);
    - actief: een paneelwaarde in process klinkt bit voor bit als dezelfde waarde van de host.
  - `plugin/toets/scherm_mac.mm` → `pb-scherm`, macOS. Hij doet wat een host doet: create, get_size, set_parent op een
    NSWindow, show. Daarna maakt hij schermafbeeldingen van het echte Cocoa-paneel en stuurt echte NSEvents (hitTest,
    slepen, Shift, dubbelklik). Ook toetst hij set_size naar 800 × 320 en destroy twee keer.
- **CI**:
  - pluginval op macOS draaide al zonder `--skip-gui-tests`. Nu opent hij de editor ook echt; de AU-stap drukt de
    editor-toetsen af.
  - Twee nieuwe artefacten met de schermafbeeldingen: `macos-schermafbeelding` (8 PNG's) en `linux-schermafbeelding`
    (4 PNG's).
- `dsp/` en `testsignalen/` zijn niet veranderd. Geen toets of validator uitgezet.

## Poorten (bewijs uit de logs van run 39159392408)
| poort | uitslag | bewijs |
|---|---|---|
| pluginval 10 AU, `--rtcheck enabled --repeat 3`, zonder `--skip-gui-tests` | groen | `Running tests 3 times`, `pluginval AU: exit 0, laatste regel: SUCCESS`; 3 × `Completed tests in pluginval / Editor`, `/ Open editor whilst processing`, `/ Editor Automation`, `/ Editor stress`, `Opening editor...` |
| pluginval 10 VST3 macOS via pluginval-rt.sh, zonder `--skip-gui-tests` | groen | `Completed tests in pluginval / Editor` (en Editor Automation, Editor stress, Open editor whilst processing), `SUCCESS`, `real-time-schendingen: 0` |
| schermafbeelding in CI als artefact | groen | `Artifact macos-schermafbeelding has been successfully uploaded! … Artifact ID is 12147548935` (8 bestanden: paneel-mac.png, paneel-mac-gekozen.png, paneel-mac-800.png, paneel-mac-venster.png en de software-tekeningen); `linux-schermafbeelding` (4 bestanden) |
| pb-scherm (Cocoa-paneel via gui-extensie, NSEvents) | groen | `pb-scherm: 30 toetsen, 30 ok, 0 fout`, onder meer `flush na NSEvents: B3 V3=1295 … V3=4850 E3`, `achtergrond is paneel: #232629`, `de gekozen knop (Filter): boog is tape: #f2c200` |
| pb-paneeltoets Linux / macOS | groen | `pb-paneeltoets: 37 toetsen, 37 ok, 0 fout` (beide) |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.` |
| clap-validator 0.4.1 | groen | `44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped (exit 0)`, de 11 state-/param-toetsen PASSED |
| pluginval 10 VST3 Linux | groen | `pluginval VST3 Linux: exit 0, laatste regel: SUCCESS` |
| null-tests CLAP (512, wisselend, state, automatisering, noten CLAP/MIDI), VST3 Linux/macOS, AU | groen | alle tabellen `bit voor bit gelijk` |
| pb-statetoets Linux / macOS | groen | `pb-statetoets: 212 toetsen, 212 ok, 0 fout` |
| RTSan / kanarie | groen | `RTSan: plugin schoon (8 renders …)`, `RTSan-kanarie gevangen zoals bedoeld (exit 43)` |

## Pushes
Twee pushes tot CI groen was.
1. `9f1dacf`: alles was groen, behalve 8 kleurmetingen in pb-scherm, bijvoorbeeld `#2f3336 (verwacht #232629)`.
   - Oorzaak: de toets las de schermafbeelding uit `bitmapImageRepForCachingDisplayInRect`. Die bitmap staat in de
     kleurruimte van het scherm, en het omzetten heen en terug verschoof de kleuren. Het paneel zelf was niet fout:
     het beeld is als sRGB getagd, en dezelfde metingen in pb-paneeltoets op macOS waren exact.
   - De pluginval-editortoetsen op AU en VST3 waren in deze run al groen.
2. `ac4c724`: de toets vangt nu in een sRGB-bitmap en leest ruwe bytes. Alles groen, alle kleuren exact.

Lokaal (Linux, clang 18) heb ik vooraf gedraaid:
- pb-paneeltoets 37/37 (ik heb de PNG's zelf bekeken);
- pb-statetoets 212/212;
- `ci-null.sh clap`: bit voor bit;
- pluginval 10 op de VST3: SUCCESS;
- clap-validator: 0 failed.

De Cocoa-code kon ik hier alleen op syntax controleren (de `__APPLE__`-blokken tijdelijk aangezet).

## Onduidelijk of fout in het pakket
- **Beginmaat tegenover mockup.** design.md zegt "Beginmaat 640 × 250" en "Minimaal 560 × 220", en de mockup (op ware
  grootte) is 560 × 220. Zo is niet duidelijk wat bij 640 × 250 met de extra ruimte moet. Ik koos: de inhoud schaalt
  uniform met min(b/560, h/220), en het paneelvlak (rand en schaduw) rekt mee met het venster.
- **De tape is te smal voor Silkscreen.** In de mockup is de tape 129 breed, maar "PB TOETSENPAD" in Silkscreen 14 px
  is ± 138 breed (de mockup rekende met een vervangend lettertype). De tape groeit nu mee met de tekst, met 6 marge
  aan elke kant.
- **"200 px omhoog"** zegt niet of het gaat om punten van het venster of om mockup-eenheden. Ik koos punten, wat de
  muis echt aflegt, onafhankelijk van de schaal.
- **Lettertypen.** design.md noemt ze, maar het pakket heeft ze niet, en een plugin kan niet op systeemfonts rekenen.
  Ik heb ze van google/fonts gehaald (OFL) en ingebakken. Een zin daarover in design.md zou helpen.
- **"Knoppen sturen parameters via de host".** Onder AUv2 doet `request_flush` niets (valkuil 14). Ook zonder flush
  moet het paneel dus zijn eigen waarde tonen tot process die toepast. Anders springt een knop na het loslaten terug
  zolang de host niet rendert.
- **Stopcriterium "pluginval 10 zonder `--skip-gui-tests` groen op macOS".** De pluginval-stappen draaiden al zonder
  die vlag en bleven ook zonder editor groen. Het criterium bewijst dus pas iets als er gecontroleerd wordt dat de
  editor-toetsen echt een editor openen. Ik druk de editor-regels af en heb pb-scherm toegevoegd. Een eis in de prompt,
  bijvoorbeeld "pluginval meldt `Opening editor...`", zou dat vastleggen.
- **Valkuil-kandidaten.** clap-wrapper v0.16.0 roept `gui->destroy` twee keer aan (AUv2 `Cleanup`), en zijn AU-view
  roept `show` nooit aan. `destroy` moet dus idempotent zijn, en de view moet zichtbaar zijn na `set_parent`.
- Verder niets geblokkeerd.
