Stopcriterium gehaald.

# Uitslag fase 5 — Dub-echo (tak proef/f1)

Laatste CI-run met de code (commit a665ae3): https://github.com/CKrebbers/pluginbouwer/actions/runs/39047298997
— alle vier de jobs `success` (Linux, RTSan, TSan, macOS). Na deze run kwam alleen nog deze uitslag erbij (alleen docs).

Er waren twee pushes nodig:
1. **Run 39046625176 (commit ed44eb8)**: alles groen, op één stap na: de VST3-matrix via `pluginval test`, op Linux
   en macOS. Oorzaak: het matrixpad was relatief, en pluginval lost een relatief pad op tegen de map van de config
   (`.../cfg/uit-matrix/verwacht/...: reference not found`). Lokaal had ik een absoluut pad gebruikt.
2. **Run 39047298997 (commit a665ae3)**: de scripts maken het matrixpad nu absoluut. Alles groen.

## Wat ik deed
- **Mono en stereo.** Eén ingang en één uitgang, in drie indelingen:
  - Stereo (2 → 2, de standaard);
  - Mono (1 → 1);
  - Mono naar stereo (1 → 2).

  Per formaat komt de indeling zo binnen:
  - CLAP: `audio-ports-config`, plus `-info`.
  - VST3: `configurable-audio-ports`. Daarmee zet clap-wrapper de busindeling van de VST3 door.
  - AU: clap-wrapper v0.16.0 kende alleen stereo. Daarom een vijfde patch,
    `patches/clap-wrapper-auv2-toegestane-kanalen-uit-audio-ports-config.patch`:
    - SupportedNumChannels meldt [1, 1], [1, 2] en [2, 2];
    - ValidFormat neemt die kanalen aan;
    - Initialize kiest met `select` de indeling die bij de streamformaten past, en weigert een andere.
- **Mono in-place** (de waarschuwing uit de opdracht). Bij mono naar stereo rekent het Ontwerp mono, en uitgang 1 is
  een kopie van uitgang 0.
  - Het Ontwerp leest dus nooit twee ingangen uit één buffer die ook uitgang 0 is.
  - Dat dit bit voor bit gelijk is aan de motor met dezelfde invoer op twee kanalen, toetst de matrix.
  - Deelt een host toch ingang 1 met uitgang 0, dan kopieert process ingang 1 eerst. De buffer daarvoor is vooraf
    gereserveerd, in activate.
- **Bypass zoals de host het wil.** Parameter 9 "Bypass" met `CLAP_PARAM_IS_BYPASS`. clap-wrapper maakt daar
  `kIsBypass` (VST3) en `kAudioUnitProperty_BypassEffect` (AU) van.
  - Het Ontwerp rekent tijdens de bypass door.
  - Aan- en uitzetten vloeit in 10 ms over. Vol aan geeft bit voor bit de ingang.
  - De bypass zit in de state, maar niet in een preset.
- **44,1–192 kHz, blok 1..4096.**
  - `gereedschap/maak-matrix.mjs` maakt invoer en verwachte uitvoer met de ketenmotor van de tool
    (`wasm/graaf.wasm`), op 44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz en voor elke indeling.
  - Eerst toetst het script zichzelf: op 48 en 44,1 kHz geeft het bit voor bit `testsignalen/*.verwacht.wav`.
  - `testsignalen/` en `dsp/` zijn niet veranderd.
- **CI-matrix** (er is niets uitgezet; alle poorten van eerdere fasen draaien nog):
  - CLAP via pb-render:
    - pb-render kreeg `--indeling` en `--in-place`;
    - 6 samplerates × 3 indelingen × blok 512 / wisselend 1..4096 / in-place, op Linux en macOS.
  - Bypass-toets (`ci-bypass.sh` en `vergelijk-bypass.mjs`):
    - bypass aan;
    - aan- en uitzetten midden in de render, gewoon en in-place;
    - 48 en 192 kHz, in elke indeling.
  - VST3 en AU via `pluginval test`: elke samplerate, blok 1 en 4096, en Bypass = 1.
  - AU via de nieuwe AU-host `gereedschap/pb-au-render`:
    - elke samplerate en indeling, blok 512 en wisselend;
    - de BypassEffect van de host;
    - stereo → mono moet geweigerd worden.
  - pluginval validate (strictness 10) draait met `--sample-rates 44100…192000 --block-sizes 1,64,512,4096`.
  - `ci-formaten.sh` eist:
    - "Named layouts: Mono, Stereo" voor in en uit;
    - precies 4 aangenomen busindelingen;
    - elke combinatie van samplerate en blokgrootte is gedraaid;
    - in auval [1, 1], [1, 2] en [2, 2], en geen [2, 1].
  - clap-validator: `layout-audio-ports-config` en `layout-configurable-audio-ports` moeten PASSED zijn.
  - RTSan rekent in elke indeling, in-place en met bypass-automatisering, op 44,1 en 192 kHz.
- **Docs**: ARCHITECTUUR.md heeft een nieuw stuk "Formaten (fase 5)". SPEC.md noemt parameter 9. TASKS.md en
  VERSLAG.md zijn bijgewerkt.

## Poorten (run 39047298997)
| poort | uitslag | bewijs (log / stap) |
|---|---|---|
| AU-matrix, mono / mono→stereo / stereo × 6 samplerates (pb-au-render) | groen | o.a. `- AU 192000 Hz, mono, wisselend: groen (4/4 bit voor bit)`, `- AU 44100 Hz, mono-stereo, blok512: groen (4/4 bit voor bit)`, `Null-test AU-matrix: alles groen` |
| AU-bypass van de host (BypassEffect) | groen | `- AU 96000 Hz, mono-stereo, BypassEffect: groen bypass aan (96000 Hz, 2 kanalen): 192000 samples bit voor bit de ingang` (alle 18 combinaties) |
| AU weigert een indeling die er niet is | groen | `ok 2 in, 1 uit: geweigerd (fout -10868)` (kAudioUnitErr_FormatNotSupported) |
| AU via pluginval test, elke samplerate, blok 1 en 4096, Bypass | groen | `- au-macos 192000 Hz, blok 512: groen (4/4 bit voor bit)`, `- au-macos 48000 Hz, blok 1: groen`, `- au-macos bypass 192000 Hz: groen (uitgang = ingang)`, `Null-test au-macos-matrix: alles groen` |
| VST3 via pluginval test (macOS en Linux) | groen | `- vst3-macos 176400 Hz, blok 512: groen (4/4 bit voor bit)` … `- vst3-macos bypass 48000 Hz: groen (uitgang = ingang)`, `Null-test vst3-macos-matrix: alles groen`; Linux: stap "Null-test VST3-matrix" `success` |
| CLAP-matrix (pb-render, Linux en macOS) | groen | stap "Null-test CLAP-matrix (… 6 samplerates × stereo, mono, mono naar stereo × blok 512, wisselend, in-place)" `success` op beide; lokaal 54/54 groen, alle 4/4 bit voor bit |
| Bypass CLAP (aan, naar-aan, naar-uit; 48 en 192 kHz) | groen | stap "Bypass CLAP" `success` op Linux en macOS (lokaal: `omslag op sample 96000, 481 samples overvloeiing …, daarvoor en daarna bit voor bit`) |
| auval: kanaalindelingen | groen | stap "auval -strict" `success`; de poort `ci-formaten.sh auval` eist [1, 1], [1, 2] en [2, 2] na "Reported Channel Capabilities", en geen [2, 1]; daarnaast `AU VALIDATION SUCCEEDED` |
| pluginval 10, alle samplerates × blokgroottes (VST3 Linux; AU en VST3 macOS met `--rtcheck enabled`) | groen | stappen `success`; de poort `ci-formaten.sh pluginval` eist `Named layouts: Mono, Stereo` (2×), `accepted by setBusesLayout: 4` en elke `Testing with sample rate [sr] and block size [bs]` (lokaal Linux: `Result: 537 tests passed, 0 tests failed`, `Layouts tested: 5, accepted by setBusesLayout: 4`) |
| clap-validator (ook layout-audio-ports-config, layout-configurable-audio-ports) | groen | stap "clap-validator" `success`; lokaal met 0.4.1: `44 tests run, 38 passed, 0 failed, 0 warnings, 6 skipped` |
| RTSan in elke indeling, in-place, met de bypass | groen | stap "RTSan in elke indeling, in-place, met de bypass (44,1 en 192 kHz, blok 1..4096)" `success`; de kanarie faalt nog zoals bedoeld (`Intercepted call to real-time unsafe function 'malloc'`, exitcode 43) |
| eerdere poorten (null-tests 48/44,1 kHz, automatisering, state-toets, presets CLAP en AU, paneel, TSan en kanarie) | groen | alle stappen `success` in deze run |

## Waar ik vastliep / wat onduidelijk of fout was in het pakket
- **clap-wrapper v0.16.0 en kanaalindelingen.** clap-wrapper gebruikt per formaat een andere extensie:
  - De VST3-kant gebruikt `configurable-audio-ports`, niet `audio-ports-config`.
  - De AUv2-kant kent geen van beide.

  De opdracht noemt alleen `audio-ports-config`. Met alleen die extensie blijven VST3 en AU stereo. Een valkuil 10 in
  CLAUDE.md zou dat een volgend gesprek besparen: "biedt ook configurable-audio-ports aan; voor de AU de patch".
- **De waarschuwing over mono in-place.** Die gaat uit van een ontwerp waarin beide DSP-ingangen uit één hostbuffer
  lezen. Ik heb gekozen voor mono rekenen en daarna kopiëren. Dat ontloopt het probleem, is half zo duur, en is bit
  voor bit gelijk.
  - De kopie van ingang 1 bij een gedeelde buffer zit er wel in.
  - Die regel wordt alleen niet door een toets geraakt: geen host die ik ken deelt ingang 1 met uitgang 0.
- **"Bypass zoals de host het wil"** staat niet verder uitgewerkt. Ik heb er dit van gemaakt:
  - de bypass van elk formaat (CLAP IS_BYPASS, VST3 kIsBypass, AU BypassEffect);
  - overvloeiing zonder tik;
  - het Ontwerp rekent door, zodat de staart klopt.

  SPEC.md had geen parameter-id voor de bypass. Ik heb 9 genomen, het eerste vrije id.
- **pluginval v2.** Bij `validate` moet het pluginpad vóór `--sample-rates`/`--block-sizes` staan. Die lijsten slokken
  anders het pad op ("Could not convert: --block-sizes = 1,64,512,4096,<pad>").
- **pluginval test lost relatieve paden op tegen de map van de config.** Dit kostte één CI-ronde; zie VERSLAG.md.
- **De matrix-renders zijn groot** (ongeveer 1 GB). Ze staan in `uit-matrix/` (in .gitignore). Als artefact gaan alleen
  de `uitslag.md`-tabellen mee.
