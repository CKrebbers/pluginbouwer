Stopcriterium gehaald.

# Uitslag fase 5 — formaten compleet (proef G3)

Laatste CI-run met de code (commit `f4bfe74`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39164830837
(alle drie de jobs groen: Linux, RTSan, macOS arm64). Deze commit voegt alleen dit verslag en het vinkje in TASKS.md
toe; CI draait er opnieuw op.

## Wat ik deed
- **`plugin/src/plugin.cpp`** (alleen de schil; `dsp/` en `testsignalen/` onaangeroerd):
  - **Kanalen (valkuil 17).** `audio-ports-config` met twee indelingen: id 0 = 0 → 2 ("Stereo"), id 1 = 0 → 1
    ("Mono"), ook `audio-ports-config-info`. `select` werkt alleen als de plugin niet actief is. `audio-ports` meldt
    daarna het gekozen aantal kanalen (CLAP_PORT_STEREO of CLAP_PORT_MONO). Voor de VST3 van clap-wrapper
    (`setBusArrangements`) is er `configurable-audio-ports`. Die neemt alleen uitgang 0 met 1 of 2 kanalen aan, en
    nooit een ingang. De AU gebruikt de bestaande `patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch`; die
    deed het zonder wijziging. Mono = `verwerk` met één kanaal: wat de DSP in mono doet (en wat de WASM ook zo
    narekent). Feature `mono` is erbij gekomen.
  - **Bypass.** Een eigen parameter van de schil, **id 5** "Bypass" (`CLAP_PARAM_IS_BYPASS | STEPPED | AUTOMATABLE`,
    0..1, standaard 0, tekst "Uit"/"Aan"). Id 1..4 zijn van de DSP. Een `static_assert` bewaakt dat 5 niet botst. Zo
    werkt hij:
    - De DSP rekent altijd door.
    - Per sample schuift een teller (0..L, L = round(0,010 × sr)) één stap naar het doel. De uitvoer wordt dan
      `pb::uitvoer(nat × (1 − g) + droog × g)` met g = teller/L. Droog is de invoer van het Ontwerp; bij dit
      instrument is die stil, dus volledig gebypast = precies 0. Zo komt er nooit een subnormale float naar de host.
    - Zonder bypass (teller 0, doel uit) raakt de schil de uitvoer niet aan. De null-tests blijven daardoor bit voor
      bit.
    - Bij activate en reset springt hij meteen naar het doel, zonder overvloei.
  - **Bypass in de state.** De bypass staat in de state (aantal = 5). Een blob van een oudere build (4 parameters)
    zet hem op zijn standaard. Een fabriekspreset laden zet hem ook op uit, net als elke naam die de preset niet
    noemt (ARCHITECTUUR.md "Presets").
  - **Samplerates en blokgroottes**: activate reserveerde al per samplerate en hakte lange blokken in stukken. Hier
    hoefde niets aan te veranderen; de nieuwe toetsen bewijzen het.
- **`gereedschap/maak-sr.mjs`** (nieuw) maakt de verwachte uitvoer met `wasm/graaf.wasm` + `keten.json`:
  - Eerst doet hij `testsignalen/` na: de 5 gevallen, de automatisering (720 events) en de noten. Dat gaat met een
    letterlijke JS-kopie van `pb::exp_`/`nootVerhouding` en `NootStapel`. Is één ervan niet bit voor bit gelijk, dan
    stopt hij (exit 1).
  - Daarna maakt hij per samplerate (44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz), in stereo en in mono:
    - de noten (die van `testsignalen/noten`, afgerond op 10 ms, zodat ze op elke samplerate op een hele sample
      vallen, ook in pluginval na `lround(sec × sr)`);
    - vaste knoppen met een open poort (het geval `stukjes-laag`).
  - Ook schrijft hij `noten.mid` voor pluginval.
- **Toetsen**:
  - **`pb-render --kanalen 1|2`** kiest de indeling via `audio-ports-config` en eist dat `audio-ports` hem meldt.
  - **`gereedschap/pb-aurender.cpp`** (nieuw, macOS) is een eigen AU-host zonder host-callbacks:
    - hij eist dat SupportedNumChannels 0→1 en 0→2 meldt;
    - hij zet het uitgangsformaat op 1 of 2 kanalen en de samplerate, en eist dat Initialize dat aanneemt;
    - hij speelt de noten als MIDI met hun offset in het blok, of rendert met vaste knoppen of met bypass.
  - **`vergelijk.mjs --paar`**: één render tegen één verwachting. Kanalen, lengte en samplerate moeten gelijk zijn,
    en de verwachting moet hoorbaar zijn.
  - **`vergelijk.mjs --bypass`**:
    - vóór "aan" is de render bit voor bit gelijk aan dezelfde render zonder bypass;
    - daarna vloeit hij lineair over in L samples, nooit luider en nooit subnormaal;
    - dan precies 0 tot "uit";
    - na "uit" vloeit hij in L samples terug, en daarna is hij weer bit voor bit gelijk.
  - **`ci-null.sh sr`** (CLAP): per samplerate × stereo/mono:
    - noten als CLAP-noten, blok 512;
    - noten als MIDI, blok 3,128,1,4096,77;
    - noten als CLAP-noten, blok 4096;
    - vaste knoppen, blok 1;
    - bypass aan na 1 s en uit na 2,5 s, midden in een wisselend blok.

    Samen 60 renders.
  - **`ci-null.sh pluginval-sr`** (VST3 en AU): `pluginval test` met `input.midi` en `render_duration` op elke
    samplerate, met per samplerate een andere blokgrootte (512, 1, 4096, 333, 64, 4096). Dat gebeurt voor de noten en
    voor de knoppen (`state.parameters`). Elke render gaat eerst door `--paar` en daarna door pluginval's eigen
    vergelijking met de WASM-verwachting.
  - **`ci-null.sh au-sr`** (macOS): pb-aurender in stereo en mono op elke samplerate, met noten, knoppen en bypass.
  - **`pb-statetoets`** kent de bypass nu:
    - (a)–(c) houden de bypass uit, zodat er iets te horen is;
    - (a) laat zoals in fase 2 de laatste DSP-parameter weg;
    - nieuw (d): een blob zonder bypass (oudere build) zet hem op standaard. Een blob met bypass aan geeft
      get_value 1, en het geluid is stil: meteen bij laden vóór activate, of na 480 samples overvloei bij laden
      terwijl actief. Een state-rondje houdt hem;
    - tekst "Uit"/"Aan", ook "on"/"off" en 0/1.
- **CI** (`.github/workflows/ci.yml`):
  - pluginval validate draait op Linux-VST3, AU (`--rtcheck enabled --repeat 3`) en macOS-VST3 (`pluginval-rt.sh`,
    dat nu extra opties doorgeeft) met `--sample-rates 44100,48000,88200,96000,176400,192000 --block-sizes
    1,64,512,1000,4096`. De stap eist dat elke samplerate met blok 1 getoetst is, en dat de uitgang "Named layouts:
    Mono, Stereo" heeft.
  - clap-validator: `layout-audio-ports-config` en `layout-configurable-audio-ports` moeten PASSED zijn.
  - RTSan rendert ook mono op 192 kHz met blok 1, stereo met blok 4096 en 44,1 kHz met wisselende blokken, telkens
    met bypass-events.
  - Alles van fase 0–4 is ongewijzigd. Eén reparatie: achter `[ "$status" = 0 ] && grep -q "^SUCCESS"` staat nu
    `|| exit 1`, omdat er regels na komen (zoals al in fase 4 bij auval).

## Poorten (bewijs uit het log van run 39164830837)
| poort | uitslag | bewijs |
|---|---|---|
| verwachte uitvoer uit de WASM, die testsignalen/ eerst nadoet | groen | stap "Verwachte uitvoer bij andere samplerates…" geslaagd op Linux en macOS. Hij eindigt met exit 1 als één zelftoets niet bit voor bit is; lokaal: 7× `zelftoets … bit voor bit gelijk` |
| null-test CLAP samplerates × stereo/mono × blok 1..4096, noten CLAP en MIDI | groen | stap geslaagd (Linux x86_64 en macOS arm64); elke regel `\| noten (CLAP), 44100 Hz, mono, blok 512 \| −∞ (bit voor bit gelijk) \| … \| ja \|`; lokaal 48/48 `ja` |
| bypass CLAP | groen | stap geslaagd; 12× `\| bypass, … \| … samples precies 0, … overvloei-samples lineair \| ja \|` |
| null-test VST3 samplerates (pluginval test + MIDI) | groen | Linux en macOS geslaagd; per samplerate `PASSED: noten-<sr>` / `PASSED: knoppen-<sr>`, `max abs diff 0` |
| null-test AU samplerates (pluginval test + MIDI) | groen | o.a. `\| noten, 48000 Hz, blok 1 (au-macos) \| −∞ (bit voor bit gelijk) …\| ja \|`, `PASSED: noten-192000`, `max abs diff 0 <= tolerance 3.16228e-05` |
| AU stereo en mono (pb-aurender, valkuil 17) | groen | `AU SupportedNumChannels: 0→2 0→1`; `AU geïnitialiseerd: 1 kanalen, 192000 Hz`; 24× `\| AU noten (MIDI)…/AU vaste knoppen… \| −∞ (bit voor bit gelijk) \| … \| ja \|` |
| bypass AU | groen | 12× bv. `\| AU bypass, 192000 Hz, mono \| aan op 192000, uit op 480000, overvloei 1920 samples \| 286081 samples precies 0, 3839 overvloei-samples lineair \| ja \|` |
| pluginval 10 VST3 Linux, alle samplerates en blokgroottes, Mono en Stereo | groen | stap geslaagd; de stap eist `SUCCESS`, `Named layouts: Mono, Stereo` en `Testing with sample rate [<sr>] and block size [1]` voor alle 6 (lokaal: `Layouts tested: 3, accepted by setBusesLayout: 3`, 120 combinaties) |
| pluginval 10 AU `--rtcheck enabled --repeat 3`, alle samplerates/blokken | groen | stap geslaagd (1 min 56 s) met dezelfde eisen |
| pluginval 10 VST3 macOS (pluginval-rt.sh), alle samplerates/blokken | groen | stap geslaagd met dezelfde eisen; het script faalt op elke niet-toegestane real-time-schending |
| clap-validator | groen | stap geslaagd; lokaal `44 tests run, 40 passed, 0 failed, 0 warnings, 4 skipped` (was 38 passed / 6 skipped); `layout-audio-ports-config: PASSED`, `layout-configurable-audio-ports: PASSED`; alle eerdere eisen nog PASSED |
| RTSan schoon (nu ook mono, 192 kHz, blok 1, bypass) / kanarie faalt | groen | beide stappen geslaagd |
| auval -strict, pb-aupresets, paneel, pb-statetoets (fase 0–4) | groen | stappen geslaagd; pb-statetoets lokaal `291 controles, 0 fout` (was 262) |
| null-tests fase 1 (CLAP, VST3, AU) | groen | stappen ongewijzigd geslaagd |
| artefact `macos-keten_toetsenpad` | groen | `Artifact macos-keten_toetsenpad has been successfully uploaded!` |

## Pushes tot CI groen: 1
`f4bfe74` was meteen groen. Lokaal (Linux) had ik vooraf alles gedraaid wat hier kan:
- bouwen;
- maak-sr (zelftoets bit voor bit), `ci-null.sh sr` (60/60) en de oude `ci-null.sh clap`/`pluginval`;
- `ci-null.sh pluginval-sr` op de VST3 (12/12 bit voor bit);
- pluginval 10 met alle samplerates en blokgroottes (SUCCESS, 15 s);
- clap-validator 0.4.1 (3× 0 failed);
- pb-statetoets.

pb-aurender kon ik hier niet bouwen. De syntax heb ik gecontroleerd tegen een kleine stub van de AudioToolbox-API.

## Onduidelijk of fout in het pakket
- **Het bypass-id staat nergens.** SPEC.md noemt alleen id 1..4 en zegt "nooit hergebruiken". Ik koos 5 voor de
  bypass van de schil. Komt er ooit een vijfde DSP-parameter, dan moet die een ander nummer krijgen; de
  `static_assert` vangt een botsing. SPEC.md (tabel "Tekst ↔ waarde") noemt de bypass niet. Ik heb SPEC.md niet
  veranderd; de tekst "Uit"/"Aan" staat in de code en in pb-statetoets.
- **Bypass en presets.** ARCHITECTUUR.md zegt dat ontbrekende namen naar hun standaard gaan. Dus zet een
  fabriekspreset de bypass ook uit. In een DAW kan dat verrassen: wie een gebypast instrument een preset geeft, hoort
  het weer. Het kan ook andersom (de bypass laten staan), maar dan klopt de regel van ARCHITECTUUR.md niet meer en
  faalde pb-statetoets. Een keuze voor de Pluginbouwer.
- **Mono = kanaal 0 van de DSP**, geen (L+R)/2. De opdracht zegt niet wat mono is. De WASM met één kanaal rekent het
  zo, dus de null-test is daarop gebouwd. Bij deze keten verschilt links van rechts alleen door de chorus.
- **`pluginval test` kent geen kanaalindeling** (altijd de standaard, stereo). Mono door de AU toetst daarom
  pb-aurender, en door de VST3 alleen pluginval validate ("Bus layout processing": mono geaccepteerd en verwerkt,
  zonder null-test). Een null-test van de VST3 in mono ontbreekt dus. Ook valkuil 17 zegt het niet.
- **Valkuil 17 is achterhaald voor dit pakket.** Hij zegt dat alle drie de verse gesprekken de AU-patch zelf
  moesten bouwen, maar `patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch` zat er al in en deed het meteen
  (`AU SupportedNumChannels: 0→2 0→1`).
- **`pluginval test` met MIDI** werkt: `input.midi` + `render_duration` (een instrument krijgt geen invoer-WAV, en een
  invoer-WAV moet dezelfde samplerate hebben als `sample_rate`). Noten in hele milliseconden die een veelvoud van
  10 ms zijn, vallen op alle zes de samplerates exact op een sample. Dat staat niet in het pakket; het is handig voor
  een volgende versie van `testsignalen/`.
- **clap-validator `process-audio-denormals` blijft SKIPPED** (de plugin heeft geen audio-ingang). Subnormale uitvoer
  tijdens het overvloeien toetst `vergelijk.mjs --bypass` daarom zelf.
- **Kleinigheid in pb-render**: `tee /dev/stderr` in ci-null.sh kapt een bestand af als stderr naar een bestand gaat.
  Lokaal ziet het log er dan leeg uit; in CI (een pipe) niet. Niet veranderd.
- Niets uitgezet: alle validators, null-tests, het paneel en de presets van fase 0–4 draaien ongewijzigd, en
  pluginval draait op meer samplerates en blokgroottes dan eerst.
