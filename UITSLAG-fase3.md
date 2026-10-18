Stopcriterium gehaald.

# Uitslag fase 3 — UI, het frontpaneel (proef G3)

Laatste CI-run met de code (commit `abc7296`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39159860211
(alle drie de jobs groen: Linux, RTSan, macOS arm64). Deze commit voegt alleen dit verslag en het vinkje in TASKS.md
toe; CI draait er opnieuw op.

## Wat ik deed
- `plugin/src/paneel.h` (zonder platform): een lock-vrije rij met één schrijver en één lezer (BEGIN/WAARDE/EIND), de
  maten uit design.md en frontpaneel.svg, en de "brug": alles wat het paneel van de plugin mag gebruiken (de atomaire
  spiegel, `value_to_text`, de rij, `request_flush`).
- `plugin/src/paneel_mac.mm`: het paneel als NSView (CoreGraphics en CoreText, ARC). Op 560 × 220 valt elke lijn op
  de plek uit de mockup: de doos met een harde schaduw van 3 px, de rand van 2 px, het tape-label "PB TOETSENPAD"
  in Silkscreen, en vier knoppen (Noot, Poort, Filter, Loslaten) in kolommen van 76 px. Bij elke knop staat het
  label erboven, de groef met daarop het spoor (270°, van −135° tot +135°), de knop met zijn wijzer, en eronder de
  waarde in de tekst van SPEC.md (de eigen `value_to_text` van de plugin). Een groter venster schaalt het geheel
  gelijkmatig; de beginmaat is 640 × 250, het minimum 560 × 220. Muis: 200 px omhoog = het hele bereik, lineair in de
  CLAP-waarde; Shift = 10× fijner; dubbelklik = de standaard; de laatst aangeraakte knop krijgt een tape-boog. Een
  tooltip geeft de volle naam. De Objective-C-klasse wordt tijdens het draaien gemaakt, met een naam die per binary
  uniek is: zo botsen de CLAP, VST3 en AU niet in één host.
- Lettertypen: `plugin/lettertypen.cmake` haalt IBM Plex Mono en Silkscreen (SIL OFL 1.1) bij het configureren op
  uit google/fonts. Het controleert of het TrueType is, logt grootte en sha256, en bakt ze als byte-array in het
  binary. Zo hoeft er niets naast de plugin te staan.
- `plugin/src/plugin.cpp`: de gui-extensie, alleen op macOS (api cocoa, niet zwevend). Zo stuurt een knop een
  parameter (valkuil 14):
  - Het paneel zet BEGIN/WAARDE/EIND in de rij en vraagt `request_flush`.
  - `process` (en `params.flush` als de plugin niet actief is) pusht ze op sample 0, vóór de events van de host.
    Ze gaan als `CLAP_EVENT_PARAM_GESTURE_BEGIN`/`PARAM_VALUE`/`GESTURE_END` naar de host, en de waarde gaat door
    dezelfde `pasToe` als een event van de host.
  - Het paneel raakt het Ontwerp nooit en leest alleen de atomaire spiegel. Tot process de waarde heeft overgenomen,
    toont de knop zijn eigen waarde. Dat volgt het paneel met een volgnummer in de rij, zodat de knop niet terugspringt.
  - Wat niet in de rij past, wacht in een buffer op de mainthread; er gaat niets verloren. Bij `gui.destroy` tijdens
    het slepen gaat er nog een EIND naar de host.
  - Op Linux biedt de plugin geen gui aan (geen X11-paneel): het stopcriterium vraagt macOS, en Clay gebruikt Logic.
- `gereedschap/pb-paneel.mm` (nieuw, alleen macOS): een toets die het gebouwde .clap en de geïnstalleerde AU laadt.
  Hij maakt de schermafbeeldingen en toetst 225 controles:
  - gui-extensie: maat, minimum, schalen, hide/show, destroy, en twee keer openen;
  - beeld: kleuren en plekken uit de mockup op 640 × 250, 560 × 220 en 1120 × 440;
  - slepen, Shift en dubbelklik met nagebootste NSEvents. get_value verandert pas na process, de host krijgt
    BEGIN/VALUE/END en de waarden kloppen (2875 Hz; 489,9 ms; 900 Hz);
  - als de plugin niet actief is, gaat het via `params.flush`;
  - het geluid na een knopdraai via het paneel is bit voor bit gelijk aan dezelfde waarde als host-event;
  - een host-event laat de knop meebewegen zonder dat er events teruggaan;
  - de AU: dezelfde view via `kAudioUnitProperty_CocoaUI`. Na slepen en één `AudioUnitRender` is de AU-parameter
    2875, en een AUEventListener hoort het begin en het eind van het gebaar.
- `.github/workflows/ci.yml` (macOS):
  - Een nieuwe stap "Paneel" draait pb-paneel; de PNG's gaan als artefact `macos-paneel-schermafbeelding`, ook als
    de stap faalt.
  - pluginval AU en VST3 draaiden al zonder `--skip-gui-tests`. Nu eisen ze ook dat de editor echt opende: de regel
    "Testing opening Editor with released processing" moet in het log staan. Zonder editor slaat pluginval die
    toetsen namelijk stil over.
  - Verder is alles van fase 0–2 ongewijzigd.

## Poorten (bewijs uit het log van run 39159860211)
| poort | uitslag | bewijs |
|---|---|---|
| pluginval 10 `--rtcheck enabled --repeat 3` AU, met GUI | groen | `pluginval AU exitcode: 0`, `SUCCESS`, `pluginval AU: editor geopend (6×)`; 3× "Starting tests in: pluginval / Editor…", "/ Editor Automation…", "/ Editor stress…" |
| pluginval 10 VST3 macOS (pluginval-rt.sh), met GUI | groen | `SUCCESS`, `real-time-schendingen: 0`, `pluginval VST3: editor geopend (2×)` |
| schermafbeelding als artefact | groen | `Artifact macos-paneel-schermafbeelding has been successfully uploaded!`. Het artefact bevat 7 PNG's: paneel-640x250, -560x220, -1120x440, -na-slepen, -host-zet-noot, au-paneel-640x250 en au-paneel-na-slepen |
| pb-paneel (beeld, knoppen via de host, AU) | groen | `225 controles, 0 fout`, o.a. `560×220: paneel (… #232629)`, `naamlabel tape (… #f2c200)`, `Noot boog spoor (… #40cbda)`, `het paneel verandert de parameter niet zelf: get_value blijft 900 tot process`, `slepen Filter: … GESTURE_BEGIN, PARAM_VALUE…, GESTURE_END voor id 3`, `geluid: Filter via het paneel = Filter als host-event, bit voor bit (4096 samples, piek 0.392…)`, `AU: Filter na slepen + Render = 2875`, `AU: de host hoort begin (1) en eind (1) van het gebaar` |
| lettertypen in het binary | groen | `lettertype IBMPlexMono-Regular.ttf: 135580 bytes, sha256 6a3412f0…`, `lettertype Silkscreen-Regular.ttf: 32220 bytes, sha256 c8454733…` |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.`, `auval exitcode: 0` |
| clap-validator 0.4.1 (+ param/state PASSED) | groen | `44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped`, `clap-validator exitcode: 0`, 12× `…: PASSED` |
| pluginval 10 VST3 Linux | groen | `pluginval exitcode: 0` (zonder gui-extensie op Linux) |
| state-toets (Linux, macOS) | groen | `143 controles, 0 fout` (2×) |
| null-test CLAP/VST3/AU | groen | stappen geslaagd; 20× `max abs diff 0 <= tolerance 3.16228e-05` (pluginval test) |
| RTSan schoon / kanarie faalt | groen | `RTSan: geen real-time-schending in process`; `kanarie gevangen zoals bedoeld` (malloc in `process`, exit 43) |
| artefact `macos-keten_toetsenpad` | groen | `Artifact macos-keten_toetsenpad has been successfully uploaded!` |

## Pushes tot CI groen: 3
1. `b1020bb`: de macOS-bouw faalde in pb-paneel. `AUEventListenerDispose` bestaat niet; het is `AUListenerDispose`.
   Het paneel zelf compileerde meteen. Ik kon de Objective-C++ hier (Linux) niet compileren; de plugin.cpp-kant heb ik
   wel lokaal met en zonder `__APPLE__` gebouwd, en de null-test en de state-toets waren lokaal bit voor bit/0 fout.
2. `897ca6c`: de bouw slaagde en alles werkte: de events, het geluid bit voor bit, de AU-parameter en de
   AU-gebaren. Toch faalden 45 van de 225 controles, allemaal kleurcontroles van mijn toets. De cache-bitmap van het
   venster (`cacheDisplayInRect`) zit in de kleurruimte van het scherm. Elke kleur kwam er gelijkmatig lichter uit
   (#232629 → #2f3336, #3a3e42 → #4a4f54), ook na omrekenen naar sRGB.
3. `abc7296`: pb-paneel tekent nu in een bitmap die als sRGB getagd is (`displayRectIgnoringOpacity:inContext:`) en
   leest de ruwe pixels. Elke kleur is nu exact die uit design.md. Alles is groen.

## Onduidelijk of opvallend in het pakket
- **AU en de echo van gebaren in clap-wrapper v0.16.0**: `NOTIFYDIRECT` staat aan in `wrapasauv2.cpp`. Een
  PARAM_VALUE/GESTURE van de plugin gaat daardoor nog op de audiothread naar `AUEventListenerNotify` en
  `Globals()->SetParameter`, en `_gesturedParameters.push_back` alloceert daar. Dat gebeurt alleen als iemand aan een
  knop draait, dus pluginval `--rtcheck` ziet het niet (pluginval draait niet aan het paneel). Voor Logic werkt het
  (clap-wrapper zegt "works fine in all hosts"). Strikt genomen is het een allocatie op de audiothread van de wrapper,
  niet van de plugin; ik heb er geen patch voor gemaakt.
- **request_flush onder AUv2** doet niets (valkuil 14). Draait een AU-host een gestopte plugin niet (geen Render),
  dan blijft een knopdraai in de rij staan tot de volgende Render; de knop toont zolang zijn eigen waarde. In Logic
  rendert een instrumentspoor altijd, dus dat speelt daar niet.
- **De mockup heeft een klasse `.l`** (labels) zonder stijl; ik heb de standaardstijl voor `text` gebruikt (Plex Mono
  11 px, inkt, gecentreerd), zoals een browser de SVG toont.
- **Groter dan 560 × 220**: design.md zegt "schaalbaar", maar niet hoe de verhouding bij 640 × 250 hoort
  (1,143 tegen 1,136). Ik schaal gelijkmatig met de kleinste factor en vul de rest met de paneelkleur.
- **Lettertypen**: het pakket heeft ze niet. Ik haal ze bij het bouwen op van `google/fonts` (tak main, niet op een
  vaste commit). De sha256 staat in het log; is de bron weg, dan faalt de configure met een duidelijke melding. Ze
  in het pakket opnemen (met OFL.txt) zou robuuster zijn; hier kon ik ze niet downloaden (de proxy weigerde).
- **Linux heeft geen paneel**: pluginval op Linux slaat de editortoetsen dus over, zoals in fase 0–2.
- De schermafbeeldingen zelf heb ik niet met eigen ogen gezien (ik kan het artefact hier niet openen). pb-paneel
  toetst ze wel op de kleuren en plekken uit de mockup, ook de tekst: labels, waarden en het naamlabel.
- Niets uitgezet: alle validators en null-tests van fase 0–2 draaien ongewijzigd.
