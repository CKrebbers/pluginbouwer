Stopcriterium gehaald.

# Uitslag fase 3 — Dub-echo (tak proef/f1)

Laatste CI-run (commit eaae5bd): https://github.com/CKrebbers/pluginbouwer/actions/runs/39042183390
— alle vier de jobs `success` (Linux, RTSan, TSan, macOS). Dezelfde commit draaide nog drie keer extra via
workflow_dispatch, en die runs waren ook groen: 39042413480, 39042415439, 39042417058.

Er waren drie ronden nodig:
1. **Run 39041481733 (commit 24b4619)**, de eerste push met het paneel: groen.
2. **Run 39041820535 (commit 5223bf1)**: rood op macOS. pluginval AU `Parameter thread safety` gaf
   `*** FAILED: VALIDATION CRASHED`, met de crash in `process` (`process + 344` ← `Clap::AUv2::ProcessAdapter::process`
   ← `WrapAsAUV2::Render`).
   - De oorzaak zit in clap-wrapper v0.16.0, niet in de plugin. `ProcessAdapter::addParameterEvent` doet een
     `emplace_back` op `std::vector _events`/`_eventindices`, zonder lock, op de thread die `SetParameter` aanroept.
   - Tegelijk sorteert `process()` op de audiothread diezelfde vectors, leest ze en wist ze. pluginval zet in die toets
     parameters vanaf andere threads terwijl Render loopt.
   - De crash kwam af en toe: twee eerdere runs met dezelfde plugincode slaagden.
   - Oplossing: een derde patch, `patches/clap-wrapper-auv2-setparameter-draadveilig.patch`, in de stijl van de andere
     twee (met herkomst in de kop). SetParameter zet het event onder de bestaande `SpinLock` van clap-wrapper in een
     eigen lijst. `process()` neemt die lijst aan zijn begin over, kort onder dezelfde lock.
   - De lock wordt nooit vastgehouden terwijl de plugin rekent. Er gaat geen toets uit.
3. **Run 39042183390 (commit eaae5bd)**: groen, en daarna de drie extra runs ook.

Daarnaast een kleine fix in commit 5223bf1. In het log van de TSan-job (gebouwd met gcc) stonden verkeerde getallen
achter "ok"-regels, zoals "gui.get_size 0 × 0". De toets zelf klopte wel. gcc maakte de tekst alleen eerder dan de
aanroep, omdat C++ de volgorde van functie-argumenten niet vastlegt. Nu doet pb-paneel eerst de aanroep en maakt het
daarna pas de tekst.

Schermafbeeldingen staan als artefact bij de run:
- `schermafbeelding-macos`: Cocoa, via de CLAP-gui in een NSWindow;
- `schermafbeelding-linux`: X11 onder xvfb.

In elk artefact zitten deze PNG's: `paneel-standaard`, `paneel-tijd-gesleept`, `paneel-host-zet-toon`, `paneel-groot`
(900 × 330) en `paneel-spelend`.

## Wat ik deed
- **Het frontpaneel uit design.md** (`plugin/src/paneel.*`):
  - Eén rij van 8 draaiknoppen, in de volgorde van SPEC.md. Ze draaien 270°, van −135° tot +135°.
  - Boven elke knop staat de naam (Silkscreen), eronder de waarde in gewone eenheden (IBM Plex Mono). Die waarde is
    dezelfde tekst als de host toont (`param_tekst.h`).
  - Bovenaan staat het naamlabel op tape, en de knoppen staan in een raam.
  - Stijl volgens design.md: hoeken 0, rand 2, harde schaduw 3 3 0, geen verlopen. Kleuren: paneel, inkt, tape, en
    spoor voor de bogen. De gekozen knop krijgt een boog in tape.
  - Bediening: slepen omhoog geeft meer (200 punten = het hele bereik), Shift + slepen is 10× fijner, dubbelklik zet de
    standaard.
  - Maat: minimaal 560 × 220 en schaalbaar. Alles schaalt mee en blijft gecentreerd.
- **Tekenen**: een eigen kleine software-tekenaar (`tekenaar.*`) met stb_truetype (public domain).
  - De lettertypen (SIL OFL 1.1, `plugin/fonts/` met de licenties) bakt CMake als bytes in de plugin. Er is dus geen
    bundel of systeemlettertype nodig.
  - Daardoor tekent het paneel op elk platform pixel voor pixel hetzelfde: macOS en Linux telden exact dezelfde aantallen
    pixels per kleur.
- **Venster** (`venster.h`):
  - macOS (`paneel_mac.mm`): een NSView met een klassenaam die per geladen binary uniek is, zodat AU, VST3 en CLAP in één
    proces niet botsen. Een NSTimer ververst de waarden; de AUv2-host van clap-wrapper biedt geen timer-support.
  - Linux (`paneel_x11.cpp`): een X11-kindvenster. De host bewaakt de X-verbinding via posix-fd-support en tikt de timer
    via timer-support.
  - clap-wrapper geeft het paneel door aan VST3 (IPlugView) en AUv2 (kAudioUnitProperty_CocoaUI).
- **Knoppen sturen via de host, nooit rechtstreeks naar de DSP:**
  - Het paneel leest alleen de spiegel die `params.get` ook geeft.
  - Een verandering gaat als BEGIN / WAARDE / EIND naar een lock-vrije ring zonder allocatie (`ui_wachtrij.h`). Daarna
    vraagt het paneel `host_params->request_flush`.
  - process (op de audiothread) of `params.flush` haalt de berichten op en pusht ze als
    `PARAM_GESTURE_BEGIN` / `PARAM_VALUE` / `PARAM_GESTURE_END` naar de host. Daarna past het de waarde toe zoals een
    event van de host.
  - Een gebaar sluit altijd: een BEGIN of WAARDE laat plek over voor het EIND erna.
- **Toetsen**:
  - `gereedschap/pb-paneel` is een CLAP-host die het paneel opent zoals een DAW, het met nagemaakte muis-events
    bedient en PNG's maakt. Het draait in CI op Linux/X11, macOS/Cocoa, onder RTSan en onder TSan.
  - TSan kreeg een kanarie: met `PB_UI_KANARIE` zet het paneel de DSP zelf, en dan MOET de toets falen.
  - pluginval moet in het log laten zien dat de editor echt openging (`Time taken to open editor`).
  - Niets is uitgezet: pluginval draait zonder `--skip-gui-tests`, zoals het al deed.
- `dsp/` en `testsignalen/` zijn niet veranderd. ARCHITECTUUR.md heeft een stuk "Frontpaneel (fase 3)" en TASKS.md is
  bijgewerkt.

## Poorten (run 39042183390)
| poort | uitslag | bewijs |
|---|---|---|
| **pluginval strictness 10 zonder `--skip-gui-tests`, macOS AU** (`--rtcheck enabled`) | groen | `Time taken to open editor (cold): 255 ms` / `(warm): 215 ms`, `Starting tests in: pluginval / Parameter thread safety...` → `Completed tests in pluginval / Parameter thread safety` … `SUCCESS` (ook in de 3 extra runs) |
| **pluginval strictness 10 zonder `--skip-gui-tests`, macOS VST3** (`--rtcheck enabled`) | groen | `Result: 537 tests passed, 0 tests failed`, `Time taken to open editor (cold): 253 ms` … `SUCCESS` |
| **schermafbeelding als artefact** | groen | `Artifact schermafbeelding-macos has been successfully uploaded! Final size is 152919 bytes. Artifact ID is 12104461382`; daarnaast `schermafbeelding-linux` |
| paneeltoets macOS (Cocoa) | groen | `ok venster-API cocoa, ingebed`, `ok achtergrond is paneel #232629`, `ok tape #f2c200 (naamlabel): 2654 px`, `ok spoor #40cbda (bogen): 981 px`, `ok het paneel vraagt de host om flush`, `ok vóór de flush is params.get nog onveranderd (het paneel zet de DSP niet zelf)`, `ok slepen Tijd 50 punten omhoog: 7 events: begin, 5 waarde(n), eind; id 1`, `ok Shift is 10× fijner: 259.6 tegen 25.96 (verhouding 10)`, `ok dubbelklik zet de standaard 375`, `ok Mix stopt op het maximum 1`, `ok het paneel tekent de waarde van de host opnieuw (timer)`, `ok gui.get_size → 900 × 330`, `ok spelend: slepen Toon 40 punten omlaag: 7 events … id 3`, `ok Toon 12000 → 9940.8 via process`, `ok 5× open en dicht terwijl hij speelt` → `pb-paneel: geslaagd` |
| paneeltoets Linux (X11, xvfb) | groen | dezelfde regels → `pb-paneel: geslaagd` |
| pluginval strictness 10, VST3 Linux (met editor, scherm 24 bit) | groen | `Time taken to open editor` … `Result: 537 tests passed, 0 tests failed`, `SUCCESS`. Lokaal hetzelfde: `(cold): 27 ms` |
| TSan: paneel (mainthread) en process (audiothread) tegelijk | groen | `pb-paneel: geslaagd`, `TSan: schoon` |
| TSan-kanarie (het paneel zet de DSP zelf) | faalt zoals bedoeld | `WARNING: ThreadSanitizer: data race` … `#3 paneelStuur … dub_echo.cpp:378` tegen `#0 pb::Glijder::stap()` … `#4 process` → `Kanarie gevangen door TSan (exitcode 66), zoals bedoeld` |
| RTSan met het paneel (gebaren door een real-time process) | groen | `pb-paneel: geslaagd`, `RTSan (paneel): schoon` |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.` |
| clap-validator 0.4.1 | groen | de stap slaagt; die eist de 11 toetsen van fase 2 als PASSED. Lokaal met dezelfde code: `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` |
| null-tests (CLAP, VST3, AU; Linux en macOS), automatisering, state-toets N = 1..8 | groen | de stappen slagen. Bijvoorbeeld `PASSED: echo-standaard … max abs diff 0`, en de state-toets met `ok oud → nieuw: 7 waarden overgenomen …` |
| RTSan (pb-render, state-toets) en de RTSan-kanarie | groen / faalt zoals bedoeld | de stappen van fase 0–2 slagen ongewijzigd |

## Waar ik op stuitte / wat in het pakket onduidelijk was
- **Een race in clap-wrapper v0.16.0 (AUv2 SetParameter tegen Render)**: zie boven. Het pakket kende hem nog niet.
  - Hij kwam pas aan het licht in fase 3, waarschijnlijk alleen door een andere timing. Hij bestond al eerder.
  - Voorstel voor het pakket: neem `clap-wrapper-auv2-setparameter-draadveilig.patch` op als valkuil 10 in CLAUDE.md.
  - Overweeg ook pluginval AU met `--repeat` in CI. Een race die één op de drie runs toeslaat, glipt anders makkelijk
    door.
- **"Via de host" bij CLAP**: een CLAP-host stuurt een uitvoerevent van de plugin niet terug. De plugin moet de waarde
  daarom zelf toepassen.
  - Ik las de eis zo: het paneel raakt de DSP nooit. Elke verandering gaat als CLAP-event langs de host (gebaar en
    waarde), en wordt toegepast op de plek die de DSP bezit (process of flush), net als een host-event.
  - TSan en de kanarie bewijzen dat de UI de DSP niet aanraakt terwijl process loopt.
  - Een zin in PROMPTS.md of ARCHITECTUUR.md hierover zou helpen.
- **AUv2 en `request_flush`**: clap-wrapper v0.16.0 doet niets met `request_flush` in AUv2 (`param_request_flush() {}`).
  - Roept een AU-host geen Render aan, bijvoorbeeld een stilstaand spoor dat niet geselecteerd is, dan wacht een
    knopverandering in de wachtrij tot de volgende Render.
  - Het paneel toont de nieuwe waarde intussen wel. De host weet het pas na de volgende Render, dus ook voor automatisering
    en voor het bewaren van het project.
  - Dit is niet getoetst in Logic; het hoort bij fase 7 ("knoppen doen wat SPEC.md zegt").
- **design.md laat open**:
  - de kleur van de harde schaduw: ik nam bijna zwart, #0d0e0f;
  - de groef onder de boog: #3a3e42;
  - wat "de gekozen knop" is: ik nam de laatst aangeklikte knop;
  - de begin- en minimale maat: ik nam 640 × 250 als begin; het minimum 560 × 220 komt uit design.md;
  - hoe slepen werkt: lineair, gelijk aan hoe clap-wrapper normaliseert. Voor Toon (300–12000 Hz) voelt dat grof aan
    het lage eind. Een logaritmische knop zou kunnen, maar dan wijkt de knop af van wat de host als 0..1 toont.
- **Silkscreen heeft alleen hoofdletters** en is breed: "TERUGKOPPELING" past bij 560 punten maar net in zijn kolom,
  bij 8 punten hoog.
- **De mockup "Frontpaneel" in de tool** kon ik niet zien, want hij zit niet in deze checkout. Ik bouwde naar de tekst
  van design.md.
- **Linux-pluginval**: xvfb-run gebruikt hier al standaard 24 bit. Ik zette `-screen 0 1280x1024x24` er toch expliciet
  bij, want op een 8-bit scherm blijft het X11-paneel effen (het werkt dan wel, je ziet het alleen niet).
- **Lokaal niet gedraaid**: macOS en RTSan. Die zijn alleen in CI getoetst, en daar groen. TSan draaide lokaal met gcc,
  omdat clang hier geen TSan-runtime heeft. In CI draait het ook met gcc.
