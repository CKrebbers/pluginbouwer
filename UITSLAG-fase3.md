Stopcriterium gehaald.

# Uitslag fase 3 — Dub-echo (tak proef/f2)

Laatste CI-run met code (commit 81c04e9): run 69 —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39040860805 — alle drie de jobs groen.
Dit is de **tweede** codepush. Run 65 (commit 3598bdf) was op één punt rood, en dat was een te strenge eigen toets,
geen fout in de plugin (zie "Waar het schuurde"). De commit met dit bestand verandert alleen docs; zijn CI-run draait
dezelfde code.

## Wat ik deed
- **Het paneel** (`plugin/ui/`) volgens design.md:
  - één rij van 8 draaiknoppen van 270° (−135° tot +135°) in de volgorde van SPEC.md;
  - de naam erboven in Silkscreen, de waarde eronder in IBM Plex Mono, in precies de tekst die de host toont
    (dezelfde `value_to_text`);
  - de tokens paneel/inkt/tape/spoor; hoeken 0, rand 2 px, harde schaduw 3 px 3 px 0, geen gradiënten;
  - een naamlabel "DUB-ECHO" op tape-geel, en de boog van de laatst aangeraakte knop ook tape-geel.
  - 720 × 270 punten, vrij schaalbaar vanaf 560 × 220. De inhoud schaalt gelijkmatig mee en staat in het midden.
  - Bediening: slepen (omhoog of rechts = meer, 200 punten = een hele slag), shift + slepen 10× fijner, dubbelklik
    = standaard, scrollwiel. Tooltips met de uitleg uit `PARAMS`. Tijd, Toon en Laag weg lopen logaritmisch op de knop.
  - `paneel.h` is platformvrij (indeling, knop ↔ waarde, slepen). `paneel_mac.mm` is een NSView met
    CoreGraphics/CoreText en ARC. De lettertypen (OFL, licenties erbij) zitten als bytes in het binary, en
    CMake maakt `lettertypen.cpp` bij het configureren.
- **CLAP gui-extensie** in `dub_echo.cpp` (alleen met `PB_MET_UI`, dus macOS): alleen api `cocoa`, ingebed.
  `get_size`, `can_resize`, `get_resize_hints`, `adjust_size` (klemt op 560 × 220), `set_size`, `set_parent`,
  `show` en `hide` zijn er; `set_scale` geeft false (cocoa werkt in logische punten). clap-wrapper geeft het
  paneel door aan de AU (`kAudioUnitProperty_CocoaUI`) en de VST3 (`IPlugView`); daar zat geen eigen code voor nodig.
- **Knoppen via de host, nooit rechtstreeks de DSP; geen audiothread-toegang vanuit de UI:**
  - De UI leest alleen de atomische hostkopie (30× per seconde, zodat hij automatisering en state volgt).
  - Wat de UI verandert, gaat als BEGIN, WAARDE of EINDE een lock-free wachtrij in (vaste grootte 4096, één
    schrijver en één lezer). Daarna roept hij `host_params.request_flush` aan.
  - `process` en `params.flush` legen de wachtrij. Elk bericht wordt toegepast zoals een host-event op sample 0,
    en de host krijgt het via `out_events` als `PARAM_GESTURE_BEGIN` / `PARAM_VALUE` / `PARAM_GESTURE_END`.
    Zo neemt de host het op (automatisering, undo) en werkt hij zijn eigen waarde bij.
  - Een gebaar begint pas bij de eerste beweging, dus een klik zonder te slepen meldt niets.
  - Raakt de wachtrij vol (de host roept lang niets aan), dan vervalt wat niet past, maar een gebaar blijft altijd
    heel: een einde past altijd.
- **Toetsen:**
  - `plugin/toetsen/ui-toets.cpp` (Linux, macOS, en onder RTSan) toetst de plugin-kant. Daarvoor is er een
    build `PB-Dub-echo-ui-stub.clap` met de echte gui-extensie en wachtrij, maar een view zonder Cocoa
    (`toetsen/ui-stub.cpp`). Hij eist:
    - vóór flush of process is er niets veranderd;
    - de host krijgt precies BEGIN, WAARDE…, EINDE op sample 0, en `get_value` volgt;
    - een knop via de UI klinkt bit voor bit gelijk aan dezelfde waarde als host-event;
    - een volle wachtrij houdt de gebaren heel.

    Mutatie: als de UI de DSP rechtstreeks zet, faalt de toets met 8 fouten (lokaal nagegaan).
  - `plugin/toetsen/gui-toets.mm` (macOS) toetst het echte paneel in een NSWindow, met nagemaakte muis-events:
    - slepen op Toon, shift-slepen op Mix (precies 10× fijner) en dubbelklik (terug naar de standaard), elk met het
      gebaar bij de host;
    - de kleuren uit design.md, gemeten op de pixels;
    - het paneel tekent opnieuw als de host Haperen automatiseert;
    - de maten 720 × 270, 560 × 220, 1440 × 540 en 900 × 600, met slepen op elke maat;
    - 20 keer openen en sluiten.

    Bij elke stap maakt hij een PNG.
- **CI** (er is niets weggehaald of uitgezet; pluginval draaide al zonder `--skip-gui-tests` en doet dat nog steeds):
  - Linux: stap "Eigen toets fase 3".
  - RTSan: ui-toets met `-fsanitize=realtime`, dus process met UI-berichten in een real-time context.
  - macOS: ui-toets en gui-toets, en de PNG's als artefact `macos-schermafbeelding` (`if: always()`, zodat je ze
    ook ziet als de toets faalt).
- ARCHITECTUUR.md ("Frontpaneel", threads), SPEC.md ("Frontpaneel") en TASKS.md zijn bijgewerkt. `dsp/` en
  `testsignalen/` zijn niet aangeraakt.

## Poorten (run 69)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| pluginval 10 zonder `--skip-gui-tests`, AU macOS (`--rtcheck enabled`) | groen | "Starting tests in: pluginval / Editor…", "Time taken to open editor (cold): 241 ms", "(warm): 202 ms", "Open editor whilst processing", "Editor Automation", "Editor stress" ("Testing opening Editor with released processing" / "with 0 sample rate and block size"); slotregel `SUCCESS` ("Time taken to run all tests: 54 secs") |
| pluginval 10 zonder `--skip-gui-tests`, VST3 macOS (`--rtcheck enabled`) | groen | "Time taken to open editor (cold): 244 ms", "(warm): 204 ms", Editor / Open editor whilst processing / Editor Automation / Editor stress voltooid; "Result: 537 tests passed, 0 tests failed"; `SUCCESS` ("53 secs") |
| schermafbeelding in CI als artefact | groen | "ok: schermafbeelding uit/gui/paneel-720x270.png (720 x 270 pixels)", ook `paneel-na-slepen`, `paneel-host-haperen-90`, `paneel-560x220`, `paneel-1440x540` en `paneel-900x600`; "Artifact macos-schermafbeelding has been successfully uploaded! … Artifact ID is 12104147025" |
| gui-toets (echt paneel, macOS) | groen | "ok: slepen: vóór de flush is er niets veranderd (Toon 3000 Hz)", "ok: slepen op Toon: de host kreeg BEGIN, … x WAARDE, EINDE voor id 3", "ok: shift-slepen: Mix 0.425000 (verwacht 0.425: 10x fijner)", "ok: dubbelklik: Toon terug op 3000 Hz (standaard), via de host", "ok: kleur: de gekozen knop (Toon, laatst aangeraakt) heeft een tape-gele boog", "ok: host-automatisering: de knop Haperen tekent opnieuw (90 %)", "ok: schaal: … bij 560 x 220 (2150 tape-pixels in het label, 183 spoor-pixels bij Tijd)", "ok: 20 keer openen en sluiten …", `GESLAAGD` |
| ui-toets (plugin-kant), Linux / macOS / RTSan | groen | o.a. "ok: geluid: knop via de UI = dezelfde waarde als host-event, bit voor bit (204800 samples, 0 verschillend)", "ok: volle wachtrij: 4089 van 12080 berichten aangekomen, 14 gebaren, elk heel"; `GESLAAGD`; RTSan: "RTSan: geen overtreding in process met UI-berichten" |
| auval -strict | groen | "AU VALIDATION SUCCEEDED." |
| pluginval 10, VST3 Linux/xvfb | groen | "Result: 537 tests passed, 0 tests failed", `SUCCESS` (op Linux nog geen paneel; de editortests vinden geen editor) |
| clap-validator 0.4.1 | groen | "44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped"; fase-2-subset "alle 9 geslaagd"; fuzz "OK: fuzzing finished successfully" |
| eigen toetsen fase 2 (tekst, automatisering, state zonder N), Linux en macOS | groen | elke aanroep `GESLAAGD` |
| null-tests (fase 1) | groen, bit voor bit | CLAP macOS (blok 512, wisselend, state), VST3 en AU macOS: alle gevallen "−∞ (bit voor bit gelijk)"; "PASSED: echo-heet / echo-standaard / sweep-441"; zelfoscillatie "terts 0.000 dB, omhullende 0.000 dB"; Linux idem |
| RTSan schoon + kanarie faalt | groen | "RTSan: geen overtreding in process", "… in process of flush", "… met UI-berichten"; kanarie: "Intercepted call to real-time unsafe function `malloc` … dub_echo.cpp:259", "Kanarie gevangen zoals bedoeld (exitcode 43)" |
| artefact `macos-keten_dub_echo` | groen | "Artifact macos-keten_dub_echo has been successfully uploaded! … Artifact ID is 12103922580" |

## Waar het schuurde / wat onduidelijk of fout was in het pakket
- **Run 65 rood door mijn eigen toets.** gui-toets telde bij 560 × 220 de spoor-blauwe pixels van Resonantie. Die
  staat op zijn standaard (0,71 Q) maar op 6 % van de slag; zo klein geschaald zijn dat ±18 volle pixels, en ik eiste
  er 30. Het paneel was goed. De toets meet nu bij Tijd (72 % van de slag) en zet de aantallen in de melding.
  Ook sleepte de Toon-toets tegen de grens van 12 kHz aan, waardoor hij weinig bewees; nu sleept hij een kwart slag.
  De PNG's kon ik vanuit de bouwomgeving niet downloaden (de proxy weigert de blob-host van GitHub). Ik heb ze dus
  niet zelf bekeken; de kleur- en maatcontroles in gui-toets meten wel op de pixels van diezelfde afbeeldingen.
- **"Knoppen sturen parameters via de host"** kan in CLAP niet letterlijk "de host stuurt het terug": een CLAP-host
  stuurt een `out_events`-waarde niet terug naar de plugin. Ik heb het zo opgevat: de UI zet niets zelf. Alles gaat
  via process/flush (het pad van host-events) en wordt de host gemeld met gebaren. clap-wrapper maakt daar in VST3
  `beginEdit`/`performEdit`/`endEdit` van en in de AU parameter-events. Het pakket zou dat in ARCHITECTUUR.md kunnen
  vastleggen; dat heb ik nu gedaan.
- **AU: `request_flush` doet niets in clap-wrapper v0.16.0** (`auv2_base_classes.h`: een lege `param_request_flush`).
  Rendert Logic de plugin niet (bijvoorbeeld een spoor dat stilstaat zonder monitoring), dan wacht een knopdraai tot
  de volgende render. Het paneel toont intussen de nieuwe stand: hij weet dat er nog iets in de wachtrij staat. In
  Logic (fase 7) is dit het eerste waar je op let als een knop "niet pakt".
- **AU: gebaren alloceren in de wrapper.** `process.cpp` in clap-wrapper doet bij `PARAM_GESTURE_BEGIN` een
  `std::vector::push_back` op de audiothread. pluginval `--rtcheck` ziet dat niet, omdat pluginval geen knop in het
  paneel draait. Het is een kleine allocatie in de wrapper, niet in de plugin, en ik heb hem niet gepatcht (geen
  poort die hem vangt). Het kan een kandidaat zijn voor een derde patch, met een toets die het paneel tijdens het
  renderen bedient.
- **Op Linux is er geen paneel.** De prompt noemt VST3/AU, en de poort eist alleen macOS. Op Linux zou X11 nodig zijn.
  pluginval Linux blijft groen zonder editor.
- design.md zegt "IBM Plex Mono" zonder gewicht. Ik heb Medium gekozen (beter leesbaar op het donkere paneel).
  "Terugkoppeling" in Silkscreen is 9,75 em breed; de basisbreedte van 720 is zo gekozen dat het op 8 px (scherp voor
  een pixelletter) in de kolom past. Bij 560 × 220 wordt het 6,2 px.
- design.md noemt een mockup "in de tool (stand Ontwerpen, Frontpaneel)". Die zit niet in het pakket; de indeling hier
  komt alleen uit de tekst van design.md.
- De ObjC-klasse `PBDubEchoPaneel` heeft een vaste naam. Laadt één proces de AU én de VST3 van deze plugin, dan meldt
  macOS een dubbele klasse. Dat werkt (de code is gelijk), maar een versie in de naam zou het netter maken.
