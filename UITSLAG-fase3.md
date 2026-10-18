Stopcriterium gehaald.

# Uitslag fase 3 — proef G2 (tak `proef/g2`)

Laatste CI-run met code (groen, alle drie de jobs): https://github.com/CKrebbers/pluginbouwer/actions/runs/39161233033
(commit `7243ecf`). Schermafbeeldingen: artefact `macos-frontpaneel` van die run (`frontpaneel.png` op de beginmaat
640 × 250, `frontpaneel-560x220.png` op de maat van de mockup, `frontpaneel-bediend.png` na bedienen). Deze commit
verandert alleen dit verslag en het vinkje in TASKS.md.

## Wat ik deed
- **Model zonder platform** (`plugin/src/paneel.{h,cpp}`): indeling in de coördinaten van `frontpaneel.svg` (560 × 220,
  y omlaag), gelijk geschaald naar de venstermaat en in het midden (640 × 250 → schaal 250/220). Tekent naar een lijst
  opdrachten (vlak, rand, open lijn, cirkel, tekst): paneel met rand 2 px en harde schaduw 3/3, tape met de naam in
  Silkscreen, per knop label (hooguit 11 tekens, volle naam in de tooltip), groef −135°..+135°, spoor tot de stand,
  knop r 16, wijzer, waardetekst via `value_to_text` van de plugin. Muis: slepen 200 punten omhoog = het hele bereik,
  lineair in de CLAP-waarde; Shift = 10× fijner (verandert Shift halverwege, dan gaat het verder vanaf de vorige plek,
  zonder sprong); dubbelklik = standaard; de laatst aangeraakte knop is de gekozen knop (tape-boog).
- **Knoppen via de host** (`plugin.cpp`, valkuil 14): het paneel leest alleen de atomaire spiegel. Een wijziging gaat als
  BEGIN/WAARDE/EIND in een lock-vrije SPSC-rij (schrijver: de mainthread; lezer: process of params.flush, die nooit
  tegelijk draaien; wat niet past wacht op de mainthread in volgorde), de plugin vraagt `request_flush`. process/flush
  geven ze als `CLAP_EVENT_PARAM_GESTURE_BEGIN`/`PARAM_VALUE`/`GESTURE_END` op sample 0 aan de host en passen de
  waarde toe met dezelfde weg als een host-event. Paneel dicht midden in een gebaar → GESTURE_END. Het paneel raakt het
  Ontwerp en de audiothread nooit; process alloceert nog steeds niet (RTSan groen).
- **Cocoa** (`plugin/src/paneel_mac.mm`, CLAP gui-extensie, alleen `cocoa` ingebed; clap-wrapper geeft het door aan
  VST3 en AUv2): een NSView die de opdrachten met CoreGraphics/CoreText tekent in sRGB. De NSView-klasse wordt met de
  objc-runtime gemaakt met het adres van de code in de naam, zodat CLAP, VST3 en AU in één host elk hun eigen klasse
  hebben. IBM Plex Mono en Silkscreen (SIL OFL 1.1, met licentie in `plugin/lettertypen/`) zitten als bytes in het binary
  (`lettertypen/insluiten.cmake`) en worden alleen voor het paneel geladen. Resize: minimaal 560 × 220
  (`adjust_size`), vrij schaalbaar. Linux en Windows krijgen (nog) geen gui-extensie.
- **Toetsen**:
  - `pb-paneeltoets` (Linux en macOS, 79 controles): het model met de **echte** rij van een plugin uit de statische
    impl. Leest `frontpaneel.svg` en vergelijkt per knop label, waardetekst, het eind van de boog en de wijzer (op 0,006
    px), kleuren; daarna slepen/Shift/dubbelklik/klemmen/venster dicht tijdens een gebaar, met de verwachte
    BEGIN/VALUE/END uit `params.flush` (niet actief) en uit `process` (actief), `get_value` pas na flush/process, een
    render die bit voor bit gelijk is aan die na een host-event, 5000 waarden zonder flush (niets kwijt, in volgorde),
    en een host-verandering die bij de volgende tik op het paneel staat. Op Linux schrijft hij de tekening als SVG
    (artefact `linux-paneel-svg`).
  - `pb-paneel` (macOS, 57 controles), door het **gesigneerde, geïnstalleerde .clap**: een CLAP-host met een NSWindow;
    toetst de gui-extensie, zet het paneel in het venster, tekent het in een sRGB-bitmap (de PNG's), leest pixels terug
    (tape, paneel, schaduw, rand, groef, spoor, knoprand, tekst in label en waarde, labelbreedte), bedient het met
    gesimuleerde NSEvents en controleert de events en waarden, en maakt/sluit het vijf keer.
  - CI: pluginval op macOS draait **zonder `--skip-gui-tests`** (zoals al sinds fase 0), en eist nu dat de
    editortoetsen een editor van de plugin openden (grep op "Testing opening Editor with released processing", alleen
    gelogd als de plugin een editor heeft): VST3 één keer, AU in alle drie de herhalingen.

## Poorten (run 39161233033)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| pluginval 10 AU macOS, met GUI-toetsen (`--rtcheck enabled --repeat 3`) | groen | `SUCCESS`, `pluginval AU: editor geopend in 3 herhalingen` |
| pluginval 10 VST3 macOS, met GUI-toetsen (`pluginval-rt.sh`) | groen | `SUCCESS`, `pluginval VST3: editortoetsen draaiden met de editor van de plugin` |
| schermafbeelding als artefact | groen | `Artifact macos-frontpaneel has been successfully uploaded!` (3 PNG's) |
| paneel door het binary (pb-paneel) | groen | `pb-paneel: 57 controles, 57 geslaagd, 0 mislukt`; o.a. `paneel: #232629 bij (400, 100)`, `Filter 50 en 100 px omhoog: B3 V3=2875 V3=4850 E3`, `actief: process geeft B2 V2=0.2 E2`, `paneel dicht tijdens een gebaar: B1 E1`, `naamlabel 139 px breed: de naam in Silkscreen 14 px + 12 (139.75 …)` |
| paneelmodel (pb-paneeltoets) Linux en macOS | groen | `pb-paneeltoets: 79 controles, 79 geslaagd, 0 mislukt` (beide) |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.` |
| clap-validator 0.4.1 (Linux) | groen | `44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped`, `clap-validator exit: 0`, de 14 state-/param-toetsen `PASSED` |
| pluginval 10 VST3 Linux | groen | `SUCCESS` (Linux heeft geen paneel) |
| state- en teksttoets (fase 2) | groen | `pb-statetoets: 171 controles, 171 geslaagd, 0 mislukt` (Linux en macOS) |
| null-tests (fase 1) | groen | Linux: alle rijen `−∞ (bit voor bit gelijk)`, ook automatie en noten CLAP/MIDI; macOS AU/VST3: `max abs diff 0 <= tolerance 3.16228e-05` |
| RTSan schoon en kanarie | groen | beide stappen geslaagd |

## Pushes
Drie (plus een tekstcorrectie in dit verslag): de code (`f65e904`), één fix (`7243ecf`) en dit verslag.
- Run 1 (`f65e904`): alles groen behalve `pb-paneel` (49/56): pluginval met GUI-toetsen op AU en VST3 en alle
  bediening via de host waren meteen goed, maar (a) donkere kleuren kwamen te licht terug (`#232629` als `#2f3336`,
  groef `#3a3e42` als `#4a4f54`): de bitmap van `bitmapImageRepForCachingDisplayInRect` staat in de kleurruimte van het
  scherm, niet sRGB. Opgelost in de toets: tekenen in een eigen sRGB-bitmap en ruwe pixels lezen (nu exact). (b) Het
  naamlabel was 139 px, de toets eiste 122–136 rond de 129 van de mockup. Silkscreen 14 px is breder dan de mockup
  aanneemt (zie hieronder); de toets meet de naam nu zelf uit het lettertype in de repo (label 139,75 px) met Menlo
  (121,6) als tegenproef, zodat hij nog steeds bewijst dat het ingebedde lettertype geladen is.
- Run 2 (`7243ecf`): alles groen.
Lokaal (Linux x86_64) vóór de eerste push: bouwen, `pb-paneeltoets` (79/79), `pb-statetoets`, `ci-null.sh clap`,
clap-validator 0.4.1 en pluginval 10 op de VST3. De Objective-C++ (`paneel_mac.mm`, `pb-paneel.mm`) kon ik hier niet
compileren; die bouwde in CI meteen.

## Wat onduidelijk of fout was in het pakket
- **De mockup klopt niet met zijn eigen lettertype**: het naamlabel is 129 px breed, maar "PB TOETSENPAD" in Silkscreen
  14 px is 127,75 px, dus met de 6 px marge links en rechts 139,75. Ik maakte het label zo breed als de tekst + 12
  (anders loopt de naam over de rand van de tape); de rest van de mockup (knoppen, bogen, wijzers, teksten) is exact
  gevolgd en getoetst.
- **design.md vs. mockup**: de mockup is 560 × 220 "op ware grootte", de beginmaat is 640 × 250. Ik nam de mockup als
  ontwerpmaat en schaal gelijk (in het midden); de vier knoppen staan daardoor links, zoals in de mockup.
- "200 px omhoog" — ik las punten van het venster (niet geschaalde ontwerp-px), dus het voelt op elke grootte gelijk.
- Een NSView-klasse met een vaste naam in een statische bibliotheek komt in alle drie de binaries; een host die de AU
  en de VST3 laadt krijgt dan dubbele klassen. Het pakket zegt daar niets over; het hoort misschien bij de valkuilen.
- `bitmapImageRepForCachingDisplayInRect` geeft pixels in de kleurruimte van het scherm: wie kleuren in een
  schermafbeelding toetst, moet zelf een sRGB-bitmap maken (zie Pushes). Ook een kandidaat-valkuil.
- Onder AUv2 doet `request_flush` niets (valkuil 14): als de host niet rendert komt een paneelwijziging pas bij de
  volgende Render aan. Het paneel toont wat het stuurde ±0,5 s en daarna weer wat de host heeft, zodat het nooit een
  waarde laat zien die niet geldt. Niet getoetst in een echte AU-host met muis; alleen door pluginval (openen/sluiten,
  ook tijdens process) en via de CLAP in `pb-paneel`.
- De GitHub-proxy van deze sessie laat het downloaden van artefacten niet toe; ik heb de PNG's zelf niet gezien, alleen
  de pixelcontroles en de SVG-tekening van het model (lokaal met Chromium naast de mockup gelegd: gelijk).
- Niets uitgezet of overgeslagen.
