Stopcriterium gehaald.

# UITSLAG fase 3 — frontpaneel (tak proef/f3)

Laatste CI-run: **run 68**, commit `5cc8971`, alle drie de jobs groen:
https://github.com/CKrebbers/pluginbouwer/actions/runs/39040619966
Run 67 (`a41cca3`, de code-commit, de eerste push van deze fase) was ook al helemaal groen:
https://github.com/CKrebbers/pluginbouwer/actions/runs/39040571560. Run 68 voegde alleen docs toe. Deze
UITSLAG-commit verandert ook alleen docs; CI draait er opnieuw op.

## Wat ik deed
- **Het paneel** (`plugin/bron/paneel.h/.cpp`): het paneel tekent zichzelf in een pixelbuffer, zonder
  grafische bibliotheek. Het volgt design.md: tokens paneel/inkt/tape/spoor, hoeken 0, randen 2 px, een harde
  schaduw van 3 px 3 px, geen verlopen. Het naamlabel "DUB-ECHO" staat in Silkscreen op tape. Er is één rij van 8
  draaiknoppen (270°, −135° tot +135°) in de volgorde van SPEC.md. De naam staat erboven in IBM Plex Mono Medium,
  de waarde eronder in IBM Plex Mono, met dezelfde tekst als `value_to_text` (ms/s, Hz/kHz, dB, Q, %).
  De boog is "spoor"; de laatst aangeraakte knop krijgt een boog in "tape". Slepen omhoog of omlaag: 200 punten
  is het hele bereik. Shift maakt slepen 10× fijner, dubbelklik zet de knop op zijn standaard. Tijd en de
  frequenties draaien logaritmisch. Het paneel is standaard 700 × 275 en minimaal 560 × 220 (`adjust_size`), en
  vrij schaalbaar: de indeling schaalt gelijkmatig mee. Op Retina tekent het op pixelresolutie.
  De lettertypes zitten als OFL-bytes in de plugin (`plugin/extern`, `plugin/lettertypes.cmake`). Glyphs komen
  uit stb_truetype (public domain).
- **Vensters**: `venster_mac.mm` is een NSView. De klasse krijgt bij het laden een naam die per binary uniek is,
  zodat de AU, VST3 en CLAP in één proces niet botsen. Een NSTimer van 30 Hz draait ook tijdens het slepen, en
  het bestand is gebouwd zonder ARC. `venster_x11.cpp` is een kindvenster met een eigen X-verbinding; events en
  hertekenen lopen via de host-extensies posix-fd-support en timer-support. clap-wrapper geeft de gui door aan
  de VST3 (IPlugView) en de AU (CocoaUI).
- **Via de host, nooit rechtstreeks de DSP** (`plugin.cpp`): het paneel draait alleen op de mainthread. Het
  leest de atomische spiegel. Het schrijft per parameter alleen tellers (gebaar begin/eind, nieuwe waarde) en de
  waarde, en vraagt dan `host->params->request_flush`. Daarna neemt process of flush het over: als een event op
  sample 0 in de DSP en de spiegel, en als `PARAM_GESTURE_BEGIN` / `PARAM_VALUE` / `PARAM_GESTURE_END` naar de
  host (output-events). Het paneel roept nooit process, de DSP of iets van de audiothread aan. Er zijn geen locks.
- **Eigen toets** `plugin/toets/pb-paneel.cpp` + `toets-fase3.sh`: een kleine CLAP-host die het paneel echt
  opent (NSWindow op macOS, X11 onder xvfb op Linux) en er met muis-events aan draait:
  - slepen aan Tijd;
  - Shift + slepen aan Mix;
  - een klik en een dubbelklik op Tijd;
  - de host zet Toon op 12 kHz.

  Hij controleert de volgende punten:
  - de volgorde gebaar/waarde/gebaar en de exacte waarden;
  - dat de waarde vóór de flush nog niet in de plugin staat;
  - dat een host-waarde op het paneel verschijnt en niet wordt teruggemeld;
  - dat andere knoppen onveranderd blijven;
  - de kleuren van design.md;
  - schalen naar 1120 × 440.

  Dat gebeurt twee keer: met flush op de mainthread, en met process op een eigen audiothread. Het venster wordt
  vastgelegd met `cacheDisplayInRect` (macOS) of `XGetImage` (Linux) en als PNG weggeschreven.
- **CI**: de fase-3-toets draait in de Linux- en macOS-job, plus `--audio` in de RTSan-job (pb-paneel met
  `-fsanitize=realtime`, process `[[clang::nonblocking]]`). Er zijn artefacten `macos-paneel-schermafbeelding`
  en `linux-paneel-schermafbeelding`. De bewijsstappen tonen de editor-regels van pluginval.
- Docs: ARCHITECTUUR.md (nieuw "Frontpaneel"), SPEC.md (Validators), TASKS.md (fase 3 afgevinkt).

## Poorten (bewijs uit het log van run 68)
| poort | uitslag | bewijs |
|---|---|---|
| pluginval 10 `--rtcheck enabled`, AU macOS, **zonder `--skip-gui-tests`** | groen | `pluginval AU: Strictness level: 10 SUCCESS`; `Starting tests in: pluginval / Editor...`, `Time taken to open editor (cold): 239 ms`, `Editor Automation`, `Completed tests in pluginval / Editor stress` |
| pluginval 10 `--rtcheck enabled`, VST3 macOS, zonder `--skip-gui-tests` | groen | `pluginval VST3: Strictness level: 10 SUCCESS`; `Time taken to open editor (cold): 232 ms`, `Editor stress` voltooid |
| schermafbeelding als CI-artefact | aanwezig | `Artifact macos-paneel-schermafbeelding has been successfully uploaded!` (6 PNG's: standaard, bediend, groot; met en zonder audiothread) |
| eigen paneeltoets macOS (cocoa) | groen | `paneel: geslaagd (cocoa)` en `paneel: geslaagd (cocoa, met audiothread)`; o.a. `parameter 1: gebaar-begin 1, waarden 6, gebaar-eind 1, in die volgorde`, `Tijd na 60 punten omlaag: 109.795842 ms (verwacht 109.795842)`, `Shift + slepen = fijn: Mix 0.395000000`, `dubbelklik = standaard (Tijd 375 ms)`, `vóór de flush staat Tijd nog op 375 ms: het paneel zet geen waarde zelf` |
| eigen paneeltoets Linux (x11, xvfb) | groen | `paneel: geslaagd (x11)` en `(x11, met audiothread)` |
| paneel bedienen terwijl process loopt, RTSan | groen | stap "Fase 3 met de RTSan-bouw" geslaagd, geen RTSan-melding |
| pluginval 10, VST3 Linux (nu met editor-toetsen) | groen | `pluginval VST3 Linux: Strictness level: 10 SUCCESS`; `Time taken to open editor (cold): 79 ms` |
| auval -strict | groen | `auval: AU VALIDATION SUCCEEDED.` |
| clap-validator 0.4.1 | groen | `clap-validator: 44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` |
| null-tests (CLAP blok512/wisselend/state Linux en macOS, VST3 Linux/macOS, AU macOS, waarden als automatisering) | groen | overal 4× `−∞ (bit voor bit gelijk) … ja`, `max abs diff 0`; zelfoscillatie `terts 0.000 dB, omhullende 0.000 dB` |
| fase-2-toetsen (state, tekst, automatisering) Linux, macOS en RTSan | groen | `state-toets: 8 van 8 …`, `tekst: geslaagd`, `automatisering: geslaagd` |
| RTSan-kanarie faalt | groen | `SUMMARY: RealtimeSanitizer: unsafe-library-call … in malloc` / `Kanarie gevangen door RTSan, zoals bedoeld.` |
| artefact `macos-keten_dub_echo` | aanwezig | `Artifact macos-keten_dub_echo has been successfully uploaded!` |

Ik heb geen toets of validator uitgezet. Lokaal draaiden vóór de push al: bouwen, de paneeltoets onder xvfb (met
en zonder audiothread), pluginval 10 op de VST3 (met editor-toetsen), de VST3-null-test, de CLAP-null-test, de
fase-2-toetsen en clap-validator. Alles was groen. De Cocoa-code kon ik hier niet bouwen; die werkte de eerste
keer in CI.

## Verloop
- Lokaal vond de eerste schermafbeelding een fout in mijn boogtekening: de hoekafstand werd buiten ±90° niet
  begrensd (sin wikkelt om), waardoor er een spookboog rechtsonder verscheen. Dat is gerepareerd vóór de push.
  Een pixeltoets daarop is er niet; de beelden in het artefact laten het zien.
- In CI gingen beide runs de eerste keer groen.

## Opmerkingen, onduidelijk of fout in het pakket
- **De mockup ("stand Ontwerpen, Frontpaneel" in de tool)** zit niet in dit pakket. Ik heb alleen design.md
  gevolgd. Wat daar niet staat, heb ik zelf gekozen:
  - de indeling van kop en kast;
  - de kleur van de schaduw (#0f1112), van de lege boog (#3b4045) en van het knoplijf (#2c3034);
  - de standaardgrootte 700 × 275;
  - het logaritmisch draaien van Tijd en de frequenties;
  - 200 punten voor het hele bereik.

  Het pakket legt dit niet vast; vergelijk met de mockup.
- **"Terugkoppeling" past niet in een kolom van 1/8 van 560 px** bij de gewone lettergrootte. Die naam (en
  Verzadiging) wordt daarom kleiner gezet: 7,5 px bij 560 breed, 9,4 px bij de standaardgrootte. Een kortere
  naam ("Terugk.") of een tweede regel zou in design.md of SPEC.md moeten staan.
- **clap-wrapper v0.16.0, AUv2**: `request_flush` doet niets en `gui->show` wordt niet aangeroepen. Daarom is
  het view meteen zichtbaar in `set_parent`. Draait de AU niet (transport stil, geen live-spoor in Logic), dan
  komt een knopdraai pas bij de volgende Render in de DSP en bij de host. Het paneel toont tot dan zijn eigen
  waarde. Voor Logic betekent dat: zonder audio wordt de automatisering niet meteen opgenomen. Dat moet Clay in
  fase 7 bekijken.
- **clap-wrapper AUv2 en realtime**: bij een gebaar-begin of -eind uit process doet clap-wrapper
  `_gesturedParameters.push_back` in Render (`detail/auv2/process.cpp`). Dat is een mogelijke allocatie op de
  audiothread, maar alleen als iemand aan het paneel draait terwijl er audio loopt. pluginval `--rtcheck` raakt
  dat niet (pluginval bedient geen knoppen); RTSan dekt het niet (dat draait op Linux met pb-paneel als host,
  niet met clap-wrapper). Een patch met `reserve` in clap-wrapper zou het oplossen; die heb ik niet gemaakt,
  omdat fase 3 er niet om vraagt en hij niet door een poort wordt bewezen.
- **Schermafbeelding in CI**: de macOS-runner heeft schaal 1×, dus de PNG is 700 × 275 (Retina niet getoetst). De
  afbeelding komt uit `cacheDisplayInRect` van het hostvenster. Dat is wat de view tekent, geen `screencapture`
  van het scherm (daarvoor is toestemming voor schermopname nodig).
- Geen fouten gevonden in CLAUDE.md, SPEC.md of ARCHITECTUUR.md voor deze fase; de valkuilen kwamen niet terug.
