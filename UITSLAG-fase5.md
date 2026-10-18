Stopcriterium gehaald.

# Uitslag fase 5 — proef G2 (tak `proef/g2`)

Laatste CI-run met code (groen, alle drie de jobs): https://github.com/CKrebbers/pluginbouwer/actions/runs/39166205466
(commit `8c8579f`). Deze commit verandert alleen dit verslag en TASKS.md.

## Wat ik deed
- **Stereo en mono** (`plugin/src/plugin.cpp`, valkuil 17): `audio-ports-config` met id 0 "Stereo" (0 → 2, de
  standaard) en id 1 "Mono" (0 → 1), ook `audio-ports-config-info`, en `configurable-audio-ports` (één verzoek voor
  uitgang 0 met 1 of 2 kanalen, passend of leeg poorttype; de VST3 van clap-wrapper vraagt zo in
  `setBusArrangements`). Kiezen kan alleen niet actief; `audio-ports` meldt daarna de gekozen uitgang. De AU kiest via de
  patch die al in `patches/` stond (`…volgt-presets-mono-stereo.patch`): auval meldt `[0, 2]  [0, 1]`. Ik hoefde niets
  aan clap-wrapper te veranderen. Feature `mono` erbij.
- **Bypass**: parameter id 5 "Bypass" (`CLAP_PARAM_IS_BYPASS | CLAP_PARAM_IS_STEPPED`, 0..1, standaard 0), van de schil
  en niet van het Ontwerp. De DSP rekent altijd door; na elk stuk van het blok vloeit de uitvoer per sample lineair in
  10 ms over naar de droge invoer (stilte: een instrument heeft geen ingang), en die mix gaat door `pb::uitvoer`. Na
  activate/reset springt hij naar zijn doel, zoals de smoothers. Zonder bypass blijft de uitvoer van de DSP bit voor bit
  staan. Een waarde tussen de stappen telt vanaf 0,5 als aan, zoals de AU-wrapper `kAudioUnitProperty_BypassEffect` leest
  en VST3 een stap afrondt. Tekst "Aan"/"Uit". State bewaart hem per id (oude blobs laden, Bypass op uit); een preset zet
  hem op uit, zoals elke parameter die de preset niet noemt (hetzelfde pad als state.load).
- **44,1–192 kHz en blok 1..4096**: daarvoor hoefde ik niets aan de schil te veranderen (activate reserveerde al voor
  sr en `max_frames`); de matrix toont het nu.
- **De verwachting bij andere samplerates** (`gereedschap/wasm-render.mjs`): de ketenmotor `wasm/graaf.wasm` met
  `keten.json`, met events op hun sample en de noten volgens ARCHITECTUUR.md "Noten". De noottoon komt uit
  `pb-noottabel`, een programma van een paar regels dat `pb::nootVerhouding` uit de gegeven `pb_noten.h` afdrukt; zo
  bouw ik de DSP niet in JS na. Het script doet eerst alle zeven verwachtingen in `testsignalen/` bit voor bit na (lukte
  de eerste keer) en maakt dan per samplerate stereo en mono: de vijf gevallen, automatisering, noten, bypass (aan en uit
  midden in blokken, ook korter dan het overvloeien, en een waarde 0,7) en `bypassaan` (op een blokgrens, voor de AU). Ook
  pluginval-configs met de noten als MIDI-bestand (één tik per sample: ppq = sr/100, 10 000 µs per kwart).
- **Toetsen**: `ci-null.sh verwacht | matrix | au-matrix | pluginval-matrix`. `pb-render --kanalen 1|2 --via
  config|configurable` kiest de uitgang, schrijft zoveel kanalen als `audio-ports` daarna meldt, en vult de buffers vooraf
  met een patroon. `pb-au-render` (nieuw, macOS) is een AU-host zonder host-callbacks: de stream-indeling op de
  output-scope, MIDI met offset, en bypass via `kAudioUnitProperty_BypassEffect` op een blokgrens. `vergelijk.mjs --paar`
  eist daarbij ook dezelfde samplerate, hetzelfde aantal kanalen en dezelfde lengte: een mono-render tegen een
  stereo-verwachting zakt dus. pb-statetoets en pb-presettoets geven een getrapte parameter nu hele waarden (eerst gaven
  ze Bypass 0,25 en 0,625); pb-statetoets heeft 15 tekstcontroles voor Bypass erbij.
- **CI**: `SAMPLERATES=44100,48000,88200,96000,176400,192000`. pluginval validate (VST3 Linux, VST3 en AU macOS) draait
  met `--sample-rates $SAMPLERATES --block-sizes 1,64,512,4096`. `pluginval-rt.sh` geeft extra opties door, na het pad
  (valkuil 18). `gereedschap/pluginval-matrix-bewijs.sh` eist elke combinatie in het log, "Named layouts: Mono, Stereo",
  en dat "Bus layout processing" elke indeling aannam. clap-validator moet daarnaast `layout-audio-ports-config`,
  `layout-configurable-audio-ports`, `process-varying-sample-rates`, `-varying-block-sizes` en `-random-block-sizes` als
  PASSED tonen. auval moet `[0, 2]` en `[0, 1]` melden. RTSan draait ook met bypass, mono via beide extensies, 44,1 en
  192 kHz en blok 1. Timeout van de macOS-job van 90 naar 150 min (de run had hem niet nodig).

## Poorten (run 39166205466)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| WASM doet testsignalen/ na | groen | `wasm-render na: … bit voor bit gelijk` voor alle 7 (Linux en macOS) |
| null-test CLAP-matrix (6 sr × stereo/mono × blok 1, 4096, `1,4096,7,512,333`; noten CLAP en MIDI; bypass) | groen | Linux en macOS: `matrix (CLAP (pb-render)): 432 renders, 432 bit voor bit gelijk, 0 boven de grens` |
| null-test AU-matrix (pb-au-render, 6 sr × stereo/mono × blok 1, 512, 4096; gevallen, noten, bypass) | groen | `au-matrix (AU (pb-au-render)): 252 renders, 252 bit voor bit gelijk`, bv. `bypassaan, 192000 Hz, mono, blok 1 … ja` |
| null-test VST3/AU per samplerate (pluginval test, gevallen + noten als MIDI) | groen | `pluginval-matrix vst3-linux: 36 configs, 36 met max abs diff 0`, idem `vst3-macos` en `au-macos` |
| pluginval 10 VST3 Linux (alle sr en blokken) | groen | `SUCCESS`; `pluginval: elke samplerate (…) met elke blokgrootte (1,64,512,4096)`; `Named layouts Mono, Stereo` |
| pluginval 10 VST3 macOS (`pluginval-rt.sh`, GUI) | groen | `SUCCESS`, `real-time-schendingen: 0`, elke sr/blok, Mono/Stereo |
| pluginval 10 AU macOS (`--rtcheck enabled --repeat 3`, GUI) | groen | `SUCCESS`, elke sr/blok, `Bus layout processing nam elke indeling aan (3 keer)`, `editor geopend in 3 herhalingen` |
| auval -strict | groen | `[0, 2]  [0, 1]`, `AU VALIDATION SUCCEEDED.`, presets als in fase 4 |
| clap-validator | groen | `44 tests run, 40 passed, 0 failed, 0 warnings, 4 skipped` (fase 4: 38 passed), `exit: 0`, de vijf nieuwe toetsen `PASSED` |
| null-tests fase 1 (testsignalen/) | groen | Linux: alle rijen `−∞ (bit voor bit gelijk)`; VST3/AU `max abs diff 0` |
| pb-statetoets / pb-paneeltoets / pb-presettoets / pb-paneel | groen | `204 … 0 mislukt`, `79 … 0 mislukt`, `217 … 0 mislukt`; artefact `macos-frontpaneel` |
| RTSan schoon en kanarie | groen | stap "RTSan schoon" geslaagd; `RealtimeSanitizer: unsafe-library-call … malloc … plProcess`, `Kanarie gevangen zoals bedoeld` |

## Pushes
Eén push met code (`8c8579f`), in één keer groen. Ik had vooraf lokaal op Linux gebouwd en gedraaid: clap-validator
0.4.1 (uit de bron), pluginval v2.0.0-beta3 op de VST3 (validate op 24 combinaties, en pluginval test op de
samplerates), de volledige CLAP-matrix (432 renders, alle bit voor bit) en de drie toetsen. Alleen `pb-au-render` en de
macOS-stappen kon ik niet lokaal draaien; die werkten de eerste keer.

## Onduidelijk of fout in het pakket
- **Waar komt Bypass in SPEC.md?** De parametertabel kent vier id's en zegt "nooit hergebruiken". Ik gaf Bypass id 5 (het
  eerste vrije; `static_assert` in plugin.cpp) en zette hem in SPEC.md als parameter van de schil. Ook nieuw: "Stapjes:
  geen" heeft nu een uitzondering. Als de Pluginbouwer later een vijfde parameter maakt, botst die met id 5: de tool zou
  id 5 moeten reserveren (of de bypass moet een vast hoog id krijgen).
- **"bypass zoals de host het wil"** heeft per formaat een andere vorm. CLAP: de parameter, sample-accuraat. VST3:
  `kIsBypass`. AU: `kAudioUnitProperty_BypassEffect`, dat clap-wrapper op de parameter zet, maar alleen tussen blokken.
  En bij uitzetten roept clap-wrapper eerst `Reset` aan, dus de DSP begint dan opnieuw in plaats van door te rekenen.
  Daarom toetst de AU-matrix alleen bypass aan, op een blokgrens. Dat Reset zit in clap-wrapper (v0.16.0, `SetProperty`);
  ik heb het niet gepatcht, want de opdracht vroeg het niet en een host die dat zo doet is "zoals de host het wil".
- **Valkuil 17 klopte en de patch werkte meteen**, maar het pakket zegt niet dat pluginval test geen kanaalindeling kan
  kiezen (altijd de standaard, stereo). Een mono-null-test op de AU kan dus alleen met een eigen host (`pb-au-render`), en
  op de VST3 heb ik er geen: daar is het bewijs alleen dat pluginval "Bus layout processing" Mono aanneemt en verwerkt,
  en dat de CLAP-render via configurable-audio-ports (hetzelfde pad als de VST3-wrapper) bit voor bit klopt.
- **Wat getrapt betekent is per standaard anders**: CLAP zegt dat een getrapte waarde wordt afgekapt (0,7 → 0), terwijl
  de AU-wrapper (midden) en VST3 (normalized·(stappen+1)) afronden. Ik koos afronden vanaf 0,5, zodat de drie formaten
  hetzelfde doen.
- `wasm/graaf.wasm` heeft geen bron in het pakket (`dsp/pb_graaf_wasm.cpp` ontbreekt, `bouw-wasm.sh graaf` verwijst
  ernaar). Daardoor wist ik de C-ABI (`pg_maak/pg_laad/pg_zet/pg_verwerk/…`) alleen uit `vergelijk.mjs` en de exports.
  Het werkte, maar een korte beschrijving in het pakket zou helpen.
