Stopcriterium gehaald.

# Uitslag fase 3 — UI, het frontpaneel (proef F4, tak `proef/f4`)

Laatste CI-run met de code: https://github.com/CKrebbers/pluginbouwer/actions/runs/39159625165 (commit 0fb33cb,
alle drie de jobs groen). De commit met dit verslag start nog een run; die verandert geen code.
Schermafbeeldingen: artefact `macos-schermafbeelding` van die run (`paneel.png` = alle knoppen op hun standaard zoals
de mockup, `paneel-na-slepen.png`, `paneel-host.png`, `paneel-groot.png` op 1,5×; elk ook als `*-scherm.png` zoals
AppKit hem voor het venster tekent).

## Wat ik deed
- **Gui-extensie (CLAP, Cocoa)** in `plugin/src/paneel_mac.mm`: een NSView die `frontpaneel.svg` naschildert met
  CoreGraphics en CoreText: paneel #232629, rand 2 px inkt, harde schaduw 3 px, naamlabel op tape in Silkscreen 14,
  9 draaiknoppen (270°, groef onder het spoor, de gekozen knop in tape), labels en waarden in IBM Plex Mono 11 met de
  tekst uit `dub::waardeNaarTekst` (SPEC "Tekst ↔ waarde"). Tooltip per knop: volle naam plus de uitleg uit PARAMS.
  Beginmaat 732 × 220 (de mockup op ware grootte), schaalbaar en gelijkmatig geschaald, kleinste 560 × 220.
  De lettertypen (OFL, `plugin/lettertypen/` met licenties) gaan als byte-rij in het binary (`plugin/cmake/bytes.cmake`).
  De NSView-klasse wordt bij het laden gemaakt met een naam die per binary verschilt (geen botsing als een host CLAP en
  VST3 in één proces laadt). `destroy` mag twee keer (de AU van clap-wrapper doet dat bij Cleanup).
  Linux/Windows: geen gui-extensie (`paneel_geen.cpp`); daar toont de host zijn eigen knoppen.
- **Model** `plugin/src/paneel_model.{h,cpp}` (platformonafhankelijk): maten uit de mockup, knop ↔ hoek, slepen
  (200 px omhoog = het hele bereik, lineair in de CLAP-waarde; Shift 10× fijner), dubbelklik = standaard, gekozen knop =
  laatst aangeraakt. Een timer (30 Hz) neemt waarden van de host over, behalve van de knop die gesleept wordt of
  waarvan de host de laatste waarde nog niet ophaalde.
- **Knoppen via de host (valkuil 14)**: `plugin/src/brievenbus.h`, lock-vrij, per parameter alleen tellers (begin,
  eind, volgnummer) en de laatste waarde: loopt nooit vol, voegt samen als de host lang niet langskomt, begin/eind
  blijven in paren. Het paneel zet er gebaren in en vraagt `request_flush`; `process` (sample 0, vóór de host-events)
  en `params.flush` legen hem, pushen `CLAP_EVENT_PARAM_GESTURE_BEGIN`/`PARAM_VALUE`/`GESTURE_END` naar de host en
  passen de waarde zelf toe. Het paneel leest alleen de atomaire kopie; het Ontwerp en de audiothread raakt het nooit.
  clap-wrapper zet die uitvoerevents bij AU en VST3 om in Begin/EndParameterChangeGesture en ParameterValueChange
  (AU, via zijn idle-timer) en beginEdit/performEdit/endEdit (VST3); daar was geen patch voor nodig.
- **Toetsen**:
  - `plugin/toets/paneel_toets.cpp` → `pb-paneeltoets` (Linux en macOS, en onder ThreadSanitizer in de RTSan-job):
    wijzer- en booguiteinden op de standaarden gelijk aan `frontpaneel.svg` (grootste afwijking 0,005 px), hit-test,
    schalen, slepen/Shift/dubbelklik, "het paneel verandert de plugin niet zelf", de volgorde van de gebaren, en twee
    draden tegelijk (≈ 145 000 gebaren, elk heel, laatste waarde aangekomen).
  - `plugin/toets/gui_toets.mm` → `pb-guitoets` (macOS): opent het paneel uit het geïnstalleerde .clap in een echt
    NSWindow, maakt de schermafbeeldingen (de view tekent in een eigen sRGB-bitmap), controleert pixels tegen de kleuren
    van design.md, print een tekstkaartje van naamlabel en knop in het log, en draait met echte NSEvents aan de knoppen:
    vóór flush/process verandert de plugin niet; daarna komen precies `B1 W1=965 E1` (inactief, via flush) en
    `B2 W2=0.75` / `E2` (actief, via process) als uitvoerevents; dubbelklik geeft de standaard; Shift 100 px op Mix
    = +5 %; automatisering van de host komt op het paneel; resize, hide/show, twee keer destroy, opnieuw create.
- **CI**: stappen hierboven, het artefact `macos-schermafbeelding`, en in "Bewijs" welke editor-toetsen pluginval
  draaide. pluginval draait zonder `--skip-gui-tests` (zat er nooit in).

## Poorten (uit het log van run 39159625165)
| poort | uitslag | bewijs |
|---|---|---|
| pluginval 10 AU `--rtcheck enabled --repeat 3`, met editor | groen | `SUCCESS`; 3 × `Starting tests in: pluginval / Editor...`, `Editor Automation`, `Editor stress`, `Open editor whilst processing` |
| pluginval 10 VST3 macOS `--rtcheck enabled`, met editor | groen | `SUCCESS`; dezelfde vier editor-toetsen 1 × |
| schermafbeelding als artefact | groen | `ok schermafbeelding …/paneel.png (732 × 220 pixels, sRGB)` en drie meer; artefact `macos-schermafbeelding` |
| gui-toets macOS | groen | `gui-toets: 52/52 controles ok, 0 fout` |
| paneel-toets Linux / macOS / TSan | groen | 3 × `paneel-toets: 35/35 controles ok, 0 fout`; `TSan: schoon` |
| auval -strict | groen | `VERIFYING PROPERTY: Tail Time` … `AU VALIDATION SUCCEEDED.` |
| clap-validator 0.4.1 + 14 state/param-toetsen | groen | `geen FAILED`; 14 × `PASSED` |
| pluginval 10 VST3 Linux (xvfb) | groen | `SUCCESS` |
| state-toets Linux en macOS | groen | 2 × `state-toets: 125/125 controles ok, 0 fout` |
| null-tests CLAP (512, wisselend, state, automatisering), VST3 Linux/macOS, AU | groen | alles `−∞ (bit voor bit gelijk) … ja`, 3 × `PASSED` per pluginval test |
| zelfoscillatie statistisch | groen | `terts 0.000 dB, omhullende 0.000 dB` |
| RTSan / kanarie | groen / faalt zoals bedoeld | `RTSan: schoon`; `RealtimeSanitizer: unsafe-library-call … malloc` |

## Pushes
Vier pushes tot CI groen (Objective-C++ kon ik hier op Linux niet compileren; het paneel compileerde wél in één keer):
1. `: ` in een stapnaam brak de YAML (de run startte niet eens). Lokaal nu met `yaml.safe_load` gecontroleerd.
2. Twee exacte float-vergelijkingen in mijn paneel-toets vielen om op arm64 (clang contraheert daar `a*b+c` tot FMA);
   nu met tolerantie. Omdat dat in dezelfde stap zat, draaide de gui-toets nog niet: nu losse stappen.
3. De gui-toets las pixels via `NSBitmapImageRep colorAtX:` + `colorUsingColorSpace:sRGB`; dat gaf een gammaverschil
   (#232629 werd #2f3336). En ik had een steekproef precies op het eind van een boog gelegd (half bedekt). Nu tekent de
   view in een eigen sRGB-bitmap (bytes = de kleuren van design.md) en liggen de steekproeven midden op de boog.
4. Groen. Het tekstkaartje in het log bevestigt de boogrichting (van linksonder via links omhoog) en de wijzer.

## Wat onduidelijk of fout was in het pakket
- **design.md spreekt zichzelf tegen over de maat**: "Beginmaat 640 × 250" en "Kolom per knop 76 px" (9 × 76 = 684 >
  640), terwijl `frontpaneel.svg` "op ware grootte" 732 × 220 is. Ik nam de mockup en de kolom van 76 px als waarheid:
  beginmaat 732 × 220; kleiner of groter schaalt het paneel gelijkmatig. De prompt of design.md zou één maat moeten noemen.
- In de mockup hebben de labels `class="l"` zonder stijl (dus de gewone tekststijl); bedoeld, denk ik, maar onzeker.
- "Slepen: 200 px" zegt niet of dat schermpunten of mockup-pixels zijn bij een geschaald paneel; ik nam schermpunten.
- "Een schermafbeelding in CI": een echte `screencapture` vraagt op macOS 15 schermopname-toestemming; ik maak hem door
  de view te laten tekenen (AppKit), wat precies is wat het venster laat zien, zonder titelbalk.
- Het paneel is er alleen op macOS (Clay's platform). Op Linux zou een X11-paneel ook pluginval onder xvfb editor-toetsen
  geven; dat vroeg de prompt niet. Onder AUv2 doet `request_flush` niets (valkuil 14): als de AU niet rendert, toont
  het paneel wat je instelde en hoort de host het bij de volgende Render.
- Het artefact kon ik in deze omgeving niet downloaden (de proxy weigert de blob-opslag van GitHub); daarom het
  tekstkaartje in het log als controle die ik wél kon lezen.
