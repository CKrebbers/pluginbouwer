Stopcriterium gehaald.

# Uitslag fase 5 — Toetsenpad (proef g1)

Laatste CI-run met code: https://github.com/CKrebbers/pluginbouwer/actions/runs/39163759591 (commit `0c51ce9`; jobs
linux, rtsan en macos alle drie success). Daarna draaide de commit met LEERPAD.md
(https://github.com/CKrebbers/pluginbouwer/actions/runs/39163820890, `18c374a`); ook die was helemaal success. De
commit met dit verslag verandert alleen docs.

## Wat ik deed
- **Mono en stereo** (valkuil 17), in `plugin/src/toetsenpad.cpp`:
  - `audio-ports-config` heeft twee configs: 0 → 2 ("Stereo", id 0) en 0 → 1 ("Mono", id 1). Geen van beide heeft
    een ingang. `select` werkt alleen als de plugin niet actief is.
  - `audio-ports-config-info` meldt de gekozen config.
  - `audio-ports` volgt de keuze: 1 of 2 kanalen, `CLAP_PORT_MONO`/`STEREO`.
  - `configurable-audio-ports` (ook onder het draft-id) is de weg van de VST3 van clap-wrapper (`setBusArrangements`).
    Hij accepteert alleen uitgang 0 met 1 of 2 kanalen, en alleen als de plugin niet actief is.
  - De AU gebruikt de al aanwezige `patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch`.
  - Mono roept `verwerk` aan met 1 kanaal. Dat is bit voor bit het linkerkanaal van stereo.
  - Feature `mono` erbij.
- **Bypass**:
  - Een eigen CLAP-parameter, id **1000** (ver buiten de nummers van SPEC.md, zodat hij nooit botst met een nieuwe
    parameter van het ontwerp).
  - Vlaggen `CLAP_PARAM_IS_BYPASS | STEPPED | AUTOMATABLE`, bereik 0..1, standaard 0, tekst "Uit"/"Aan".
  - De DSP rekent altijd door. De uitgang vloeit in round(0,01 · sr) hele samples lineair over naar de droge invoer
    (de stille buffer van het instrument): `pb::uitvoer(nat·(1−d) + droog·d)`.
  - Niet omzeild en geen overgang bezig: de uitvoer wordt niet aangeraakt, dus de null-test blijft bit voor bit.
  - Na activate of reset staat de overvloeiing meteen op het doel.
  - State bewaart de bypass als extra id. Een oudere state zonder dat id laadt als "niet omzeild". Een preset verandert
    de bypass niet.
- **`gereedschap/maak-verwacht.mjs`**: de verwachte uitvoer op elke samplerate (stereo of mono), uit `wasm/graaf.wasm`
  + `keten.json`.
  - `controle` doet eerst testsignalen/ na: de 5 gevallen, automatie en noten, alle 7 bit voor bit.
  - Noten volgen ARCHITECTUUR.md. `pb::nootVerhouding` staat er in JS in; `exp_` uit `pb_math.h` is regel voor regel
    overgenomen.
  - Voor `pluginval test` schrijft hij ook invoer-WAV's met de nieuwe samplerate in de kop (pluginval eist dat), en de
    noten als MIDI-bestand. PPQ en tempo zijn zo gekozen dat 1 tick precies 1 sample is.
- **`gereedschap/pb-render.cpp`**: `--kanalen 1|2` kiest de config vóór activate. De uitvoer-WAV heeft zoveel kanalen
  als de uitgangspoort.
- **`gereedschap/vergelijk.mjs --paar`**: vergelijkt twee bestanden en eist ook hetzelfde aantal kanalen, dezelfde
  samplerate en dezelfde lengte.
- **`gereedschap/ci-null.sh`**:
  - **`sr`**: CLAP via pb-render op 44100, 48000, 88200, 96000, 176400 en 192000 Hz, stereo én mono. Per samplerate
    en indeling: de 5 gevallen (blok 512), automatisering (wisselend), noten als CLAP en als MIDI (blok 512 en
    wisselend 3,128,1,4096,77), en noten met blok 1 (stereo) of 4096 (mono). Dat zijn 132 renders.
  - **`pluginval-sr`**: VST3 en AU via `pluginval test` op dezelfde zes samplerates. Per samplerate de 5 gevallen en de
    noten (MIDI-bestand), met blok 64, 512 of 4096 afhankelijk van de samplerate.
- **`plugin/toets/formaattoets.cpp` → `pb-formaattoets`** (147 toetsen, door het .clap), op Linux en macOS:
  - **Indelingen**: beide configs; select en configurable, ook de foute verzoeken (ingang, 3 kanalen, verkeerd
    poorttype, terwijl actief).
  - **Samplerates**: 6 samplerates × blok 1, 4096 en wisselend × stereo en mono. Blokgrootte-onafhankelijk (bit voor
    bit), mono = links (bit voor bit), en geen NaN, oneindig of subnormale float.
  - **Bypass, de render**: per samplerate en indeling bit voor bit volgens de regel. Vóór de bypass gelijk aan
    zonder; precies L samples overvloeien; exact stil; L samples terug; daarna weer bit voor bit gelijk aan zonder
    bypass (de DSP rekende door).
  - **Bypass, de rest**: bypass vóór activate is stil vanaf de eerste sample; de state houdt hem.
  - Lokaal getoetst: met een overvloeitijd van 11 ms faalden 24 van de 147 toetsen.
- **CI** (`.github/workflows/ci.yml`):
  - **pluginval validate** (VST3 Linux, AU met `--rtcheck enabled --repeat 3`, VST3 macOS via `pluginval-rt.sh`)
    draait met `--sample-rates 44100,48000,88200,96000,176400,192000 --block-sizes 512,1,64,4096`, met het pad ervoor
    (valkuil 18). `pluginval-rt.sh` geeft daarvoor extra opties door.
  - **Bus-indelingen**: `gereedschap/bus-indeling.sh` eist dat de bus-toetsen van pluginval op de uitgang "Mono,
    Stereo" zien en minstens twee indelingen accepteren.
  - **auval**: `gereedschap/au-kanalen.sh` eist dat auval `[0, 2]  [0, 1]` meldt.
  - **clap-validator** moet nu ook `layout-audio-ports-config`, `layout-configurable-audio-ports`,
    `process-varying-sample-rates`, `process-varying-block-sizes` en `process-random-block-sizes` PASSED geven.
  - **RTSan** draait ook mono, 44,1 en 192 kHz, blok 1 en 4096, en de bypass aan en uit midden in een blok.
  - **Nieuwe stappen**: `ci-null.sh sr` (Linux, macOS), `pluginval-sr` (VST3 Linux, VST3 macOS, AU) en
    pb-formaattoets (Linux, macOS).
- **Eerdere toetsen aangepast aan de bypass**, zonder iets uit te zetten:
  - pb-statetoets: de bypass doet niet mee in de gevallen van fase 2 (een omzeilde render is stil), maar staat wel
    als laatste id in de verwachte blob.
  - pb-presettoets: de bypass blijft wat hij vóór de preset was.
  - pb-paneeltoets: de bypass is geen knop op het paneel.
- LEERPAD.md: mono, stereo en bypass voor Clay. TASKS.md: fase 5 afgevinkt.
- `dsp/` en `testsignalen/` zijn niet veranderd. Geen toets of validator uitgezet.

## Poorten (bewijs uit de logs van run 39163759591)
| poort | uitslag | bewijs |
|---|---|---|
| generator doet testsignalen/ na | groen | `\| noten \| 48000 \| bit voor bit gelijk \|` en de andere 6 regels, Linux en macOS |
| null-test CLAP, 6 samplerates, stereo en mono, ook de noten (CLAP en MIDI), blok 1..4096 | groen | `null-test samplerates (44100 48000 88200 96000 176400 192000; stereo en mono): 0 render(s) boven de grens` (Linux en macOS) |
| null-test VST3, 6 samplerates, ook de noten (MIDI-bestand) | groen | `null-test vst3-linux samplerates (…): 0 render(s) boven de grens`, idem `vst3-macos` |
| null-test AU, 6 samplerates, ook de noten | groen | `null-test au samplerates (…): 0 render(s) boven de grens`; bijvoorbeeld `### Null-test au op 192000 Hz (pluginval test, blok 4096; …)` met `\| noten \| −∞ (bit voor bit gelijk) \|` |
| alle tabellen van de null-tests | groen | Linux 194 en macOS 235 regels "ja" (bit voor bit), 0 regels "nee", 0 keer BOVEN DE GRENS |
| null-tests van fase 1 (48 kHz, state, automatisering, noten) | groen | ongewijzigde stappen, alle bit voor bit |
| clap-validator 0.4.1 | groen | `clap-validator: 44 tests run, 40 passed, 0 failed, 0 warnings, 4 skipped (exit 0)` (fase 4: 38 passed); `layout-audio-ports-config: PASSED`, `layout-configurable-audio-ports: PASSED`, `process-varying-sample-rates: PASSED`, `process-varying-block-sizes: PASSED`, `process-random-block-sizes: PASSED`; de state-, param- en preset-toetsen PASSED zoals in fase 2 en 4 |
| pluginval 10 VST3 Linux, 6 samplerates × 4 blokgroottes | groen | `pluginval VST3 Linux: exit 0, laatste regel: SUCCESS`; `bus-indeling: Bus layout processing: 3 getest, 3 geaccepteerd` |
| pluginval 10 AU, `--rtcheck enabled --repeat 3`, 6 samplerates × 4 blokgroottes | groen | `Running tests 3 times`, `pluginval AU: exit 0, laatste regel: SUCCESS`; `bus-indeling: uitgang, named layouts: Mono, Stereo (3 keer)` |
| pluginval 10 VST3 macOS (pluginval-rt.sh), idem | groen | `real-time-schendingen: 0, waarvan pluginval-host (MidiEventList::addEvent): 0`; `named layouts: Mono, Stereo` |
| auval -strict, kanaalindelingen | groen | `AU VALIDATION SUCCEEDED.`; `Reported Channel Capabilities (explicit): [0, 2]  [0, 1]`; de vier presetnamen staan er nog |
| pb-formaattoets | groen | `pb-formaattoets: 147 toetsen, 147 ok, 0 fout` (Linux en macOS) |
| pb-statetoets / pb-paneeltoets / pb-presettoets / pb-scherm | groen | `213 ok, 0 fout`, `37 ok, 0 fout`, `63 ok, 0 fout (4 presets)`, `pb-scherm: 30 toetsen, 30 ok, 0 fout` |
| RTSan / kanarie | groen | `RTSan: plugin schoon (12 renders met …, mono, 44,1-192 kHz, blok 1 en 4096, bypass)`; `RTSan-kanarie gevangen zoals bedoeld (exit 43)` |

## Pushes
**Eén push** met code (`0c51ce9`) was meteen groen; de tweede push (`18c374a`) was alleen LEERPAD.md.

Vooraf heb ik lokaal (Linux, clang 18) gedraaid:
- `maak-verwacht.mjs controle`;
- `ci-null.sh sr` op alle zes samplerates: 0 renders boven de grens;
- `ci-null.sh pluginval-sr` op de VST3 met pluginval v2.0.0-beta3, op 44,1 en 192 kHz: alles bit voor bit, ook de
  noten via het MIDI-bestand;
- pluginval validate strictness 10 met alle samplerates en blokgroottes;
- clap-validator 0.4.1 uit de bron: 40 passed, 0 failed;
- alle eigen toetsen.

De AU kon ik lokaal niet toetsen. Dat de AU-null-test met MIDI-noten in één keer bit voor bit was, wist ik pas uit CI.

## Onduidelijk of fout in het pakket
- **Fout in de CI van eerdere fasen**: een bestaande regel liet een fout stil door. Een `run:`-stap zonder `shell:`
  draait onder `bash -e` zonder pipefail. Een regel als `[ "$status" -eq 0 ] && grep -q "AU VALIDATION SUCCEEDED"
  auval.log` midden in een stap (er kwamen in fase 4 nog regels achter) laat de stap dan niet falen. Een mislukte auval
  was zo als groen doorgegaan. Ik heb zulke regels `|| exit 1` gegeven. Dat is een valkuil-kandidaat voor CLAUDE.md:
  "eindig een test midden in een stap nooit op `a && b`".
- **Het id van de bypass staat nergens.** SPEC.md en ARCHITECTUUR.md noemen de bypass niet als parameter, en geven ook
  geen id of naam. Ik koos id 1000 en de naam "Bypass" met "Uit"/"Aan". De Pluginbouwer zou dat in SPEC.md moeten
  vastleggen ("nooit hergebruiken"), net als de keuze dat een preset de bypass niet verandert.
- **De fase-2-toets van het pakket raakt de bypass.** De prompt van fase 2 zegt "(a) een blob zonder de laatste
  parameter". Na fase 5 is de laatste parameter de bypass, en `anders()` in de toetsen zet hem aan (stille render). Ik
  heb pb-statetoets, pb-presettoets en pb-paneeltoets de bypass apart laten behandelen. Het pakket zou kunnen zeggen
  dat de bypass niet tot de parameters uit SPEC.md hoort.
- **Wat Logic doet met de bypass** van een AU-instrument (zijn eigen bypass of onze parameter) kon ik niet toetsen. Dat
  staat als punt in LEERPAD.md, voor fase 7.
- **Mono van de AU** is in CI getoetst via auval (de gemelde kanaalindelingen) en via pluginval
  "Bus layout processing" (process op mono). De null-test van de AU draait alleen stereo, omdat `pluginval test` geen
  kanaalindeling kan kiezen. Mono als null-test is er voor CLAP (bit voor bit tegen de WASM met 1 kanaal).
- **LEERPAD.md** heeft een kapotte kop: regel 29 is " (niet nodig om te bouwen)" zonder `##`. Die stond er al vóór
  fase 5; ik heb hem niet aangeraakt.
- **De patch voor mono/stereo** (valkuil 17) stond nu al in `patches/` en werkte zonder aanpassing. Valkuil 17 zegt nog
  "moesten hem zelf bouwen"; dat klopt voor dit pakket niet meer.
