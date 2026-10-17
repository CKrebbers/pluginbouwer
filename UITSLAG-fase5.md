Stopcriterium gehaald.

# UITSLAG fase 5 — formaten compleet (tak proef/f3)

Laatste CI-run: **run 102**, commit `21695f2`. Alle drie de jobs zijn groen:
https://github.com/CKrebbers/pluginbouwer/actions/runs/39045425881
Run 100 (`5102d17`, de echte wijziging) had alle poorten al groen. Alleen de twee bewijsstappen faalden daar, door
een fout in mijn eigen grep (zie "Verloop"). Deze UITSLAG-commit verandert alleen docs; CI draait er opnieuw op.

## Wat ik deed
- **Mono én stereo** (`plugin/bron/plugin.cpp`):
  - `audio-ports-config` met drie indelingen: Stereo (id 0, 2-2, standaard), Mono (id 1, 1-1) en Mono naar stereo
    (id 2, 1-2). Ook `audio-ports-config-info`.
  - `audio-ports` volgt de gekozen indeling. Kiezen kan alleen als de plugin niet actief is.
  - Daarnaast `configurable-audio-ports`. Die is nodig omdat de VST3 van clap-wrapper daarmee zijn indeling kiest
    (setBusArrangements), niet via audio-ports-config.
  - De AUv2 van clap-wrapper v0.16.0 meldt alleen de huidige poorten (dus alleen stereo). Daarom een nieuwe patch,
    `patches/clap-wrapper-auv2-stereo-en-mono.patch`. SupportedNumChannels en ValidFormat komen nu uit
    audio-ports-config, en Initialize kiest de indeling die past bij de streamformaten van de host. Bij een
    combinatie die niet bestaat geeft hij `kAudioUnitErr_FormatNotSupported`. De patch komt in de naamvolgorde na
    `…-auv2-presets.patch`.
- **process kopieert de invoer eerst** naar `droog` (2 × blokMax, gereserveerd in activate). Werkt de host met meer
  frames dan blokMax, dan rekent process in stukken. De DSP leest altijd uit die kopie. Dat lost de valkuil uit de
  opdracht op: bij mono naar stereo in-place schreef de DSP kanaal 0 voordat hij kanaal 1 uit dezelfde buffer las.
  - Mono rekent één DSP-kanaal. De kanalen zijn onafhankelijk en de bandbeweging wordt gedeeld, dus mono klinkt bit
    voor bit als het linkerkanaal van stereo.
  - Bij mono naar stereo komt dat ene kanaal op beide uitgangen.
- **Bypass zoals de host het wil**: een parameter van de schil, id 100 "Bypass" (0/1, stapjes),
  `CLAP_PARAM_IS_BYPASS`.
  - clap-wrapper koppelt hem aan VST3 `kIsBypass` en AU `kAudioUnitProperty_BypassEffect`.
  - De DSP draait door. De uitgang vloeit in 10 ms lineair over naar precies de invoer, en daarna weer terug.
  - Na een state, preset, `params.flush` of activate springt hij meteen (er loopt dan geen audio).
  - De state bewaart de bypass. Een oudere state zonder bypass laadt met bypass uit. Presets laten hem staan.
  - Staat de bypass op 0, dan raakt hij de uitvoer niet. Alle eerdere null-tests blijven daardoor bit voor bit.
- **44,1–192 kHz en blok 1..4096**: de DSP deed dat al. Nieuw zijn de toetsen ervoor.
- **De CI-matrix** (`gereedschap/ci-null.sh`):
  - CLAP via pb-render: 6 samplerates (44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz) × 3 indelingen, telkens in-place en
    met wisselende blokken. Dat zijn 18 tabellen, plus de bestaande varianten en blok 1 en blok 4096.
  - VST3 (Linux en macOS) en AU via `pluginval test`, stereo, op elke samplerate.
  - Waar de verwachting vandaan komt: `gereedschap/verwacht.mjs` rekent hem uit met dezelfde WASM van de tool
    (`wasm/graaf.wasm` + `keten.json`). Eerst moet dat script `testsignalen/*.verwacht.wav` bit voor bit nadoen;
    lukt dat niet, dan stopt het. `testsignalen/` is niet veranderd.
  - Hulpmiddelen die ik daarvoor uitbreidde: pb-render kreeg `--kanalen 2|1|1-2` en `--in-place`, en vergelijk.mjs
    kreeg `--verwacht <map>` plus een controle op het aantal kanalen.
  - Elke matrixstap controleert dat alle tabellen er echt zijn (18 voor de CLAP, 6 per pluginval-formaat).
- **Eigen toets** `plugin/toets/toets-fase5.sh` (`pb-toets formaten`). Hij draait in de Linux-, macOS- en RTSan-job.
  - De indelingen en het kiezen ervan, ook dat kiezen geweigerd wordt als de plugin actief is.
  - configurable-audio-ports: mono wordt aangenomen; 3 kanalen en een tweede poort worden geweigerd.
  - De bypass-parameter zelf.
  - Elke indeling, met eigen buffers en in-place, op 44,1 en 192 kHz, bij blok 512, 1, wisselend en 4096:
    - bit voor bit gelijk aan stereo;
    - bypass aan geeft precies de invoer;
    - bypass midden in het spel: precies het effect, overvloeier (tussen effect en invoer), precies de invoer,
      overvloeier, en daarna weer precies het effect.
  - **Gecontroleerd dat de toets de valkuil vangt**: met de oude manier van lezen (beide DSP-ingangen uit de
    hostbuffer) faalden precies de acht gevallen "mono naar stereo, in-place". Daarna heb ik de code teruggezet.
- In de RTSan-job rendert pb-render nu ook mono en mono naar stereo, in-place, op 192 en 44,1 kHz.
- Docs: SPEC.md (parameter 100, "Formaten", Validators), ARCHITECTUUR.md ("Formaten (fase 5)"), TASKS.md (fase 5
  afgevinkt), CLAUDE.md (valkuil 12).

## Poorten (bewijs uit het log van run 102)
| poort | uitslag | bewijs |
|---|---|---|
| null-test CLAP, matrix 44,1–192 kHz × stereo/mono/mono naar stereo, in-place, wisselend (Linux x86_64, macOS arm64) | groen | `fase 5, null-test CLAP Linux: 23 tabellen, 92 gevallen bit voor bit, 0 niet`; macOS: `null-clap.log: 23 tabellen, 92 gevallen bit voor bit, 0 niet`; o.a. `### Null-test CLAP (matrix 192000 Hz, kanalen 1-2, in-place, wisselend, arm64)` |
| null-test CLAP blok 1 en 4096 | groen | in dezelfde 23 tabellen, ook alle `ja` |
| null-test CLAP met de RTSan-bouw (ook de matrix) | groen | stap "Null-test CLAP met de RTSan-bouw" geslaagd; RTSan meldt niets |
| null-test VST3 Linux, 6 samplerates | groen | `fase 5, null-test VST3 Linux: 7 tabellen, 28 gevallen bit voor bit, 0 niet` |
| null-test AU macOS, 6 samplerates | groen | `fase 5, null-au.log: 7 tabellen, 28 gevallen bit voor bit, 0 niet`; pluginval zelf: `[sample] ok: max abs diff 0 <= tolerance 3.16228e-05` |
| null-test VST3 macOS, 6 samplerates | groen | `fase 5, null-vst3.log: 7 tabellen, 28 gevallen bit voor bit, 0 niet` |
| eigen toets fase 5 (Linux, macOS, RTSan) | groen | `formaten: geslaagd`, `216 controles ok`, `indeling 2: Mono naar stereo (1 in, 2 uit)` |
| auval -strict, met de kanaalindelingen | groen | stap "auval -strict" (eist `AU VALIDATION SUCCEEDED`) geslaagd; `Reported Channel Capabilities (explicit):  [2, 2]  [1, 1]  [1, 2]` |
| pluginval 10 `--rtcheck`, AU en VST3 macOS (met editor) | groen | stappen geslaagd; VST3: `[In: Mono: 1 Channels, Out: Mono: 1 Channels] [Succeeded]`, `[In: Mono: 1 Channels, Out: Stereo: 2 Channels] [Succeeded]`, `[Parameter Bypass persistence] [Succeeded]`; AU: `VERIFYING PROPERTY: Bypass Effect`, Bypass `isBoolean - 1 … all value strings - uit\|aan` |
| pluginval 10 VST3 Linux | groen | stap geslaagd, met dezelfde mono-indelingen en `Parameter Bypass persistence` |
| clap-validator | groen | stap geslaagd (`… 0 failed`). Lokaal: `44 tests run, 38 passed, 0 failed, 0 warnings, 6 skipped` (fase 4: 36 passed, 8 skipped) |
| fase 1–4 (null-tests blok 512/wisselend/state, state-toets, tekst, automatisering, paneel, presets, auval-presets, kanarie) | groen | `state-toets: 8 van 8 …`, `tekst: geslaagd`, `automatisering: geslaagd`, `paneel: geslaagd (cocoa)`, `presets: geslaagd (5 presets)`, `ID: 0 Name: Lange dub` … `ID: 4 Name: Zelfoscillatie`, "Kanarie MOET falen" geslaagd |

Ik heb geen toets of validator uitgezet en niets aan `dsp/` of `testsignalen/` veranderd.

## Verloop (2 CI-ronden)
1. **Run 100**: alle poorten groen. Wel faalden de bewijsstappen op Linux en macOS. Mijn regel `grep -c '^  ok:'`
   telde 0, omdat pb-toets `  ok   …` schrijft zonder dubbele punt. Daardoor gaf grep exit 1, en dat brak de stap
   af. Ik heb de telling verbeterd.
2. **Run 102**: alles groen.

De AU-patch kon ik op Linux niet compileren of draaien. Ik heb hem wel lokaal op een verse clap-wrapper v0.16.0 gezet,
samen met de andere patches in naamvolgorde, en een tweede keer toepassen slaat hem netjes over. Hij werkte in CI
meteen.

## Wat in het pakket onduidelijk of fout was, en wat opviel
- **De valkuil uit de prompt zat er echt.** De code van fase 1 las bij een mono-invoer met stereo-uitgang beide
  DSP-ingangen uit dezelfde hostbuffer. Er stond zelfs een comment bij dat dat veilig was ("de DSP leest per sample
  in[c] vóór hij uit[c] schrijft"). Voor kanaal 0 klopt dat, voor kanaal 1 niet. Geen toets van fase 0–4 vond het,
  want alles draaide stereo en met eigen buffers. De nieuwe toets vindt het wel (zie boven).
- **De drie formaten kiezen hun kanalen elk op een andere manier.** CLAP gebruikt audio-ports-config. De VST3 van
  clap-wrapper gebruikt `configurable-audio-ports`. De AUv2 van clap-wrapper v0.16.0 doet niets met beide en kent
  alleen de huidige poorten. Dat stond nergens in het pakket. Het staat nu als valkuil 12 in CLAUDE.md.
- **Geen verwachte uitvoer op andere samplerates.** `testsignalen/` heeft alleen 48 kHz (en één geval op 44,1 kHz).
  Voor de matrix gebruik ik de WASM die in het pakket zit. Die doet de gegeven referenties hier bit voor bit na, dus
  het blijft "binary tegen de tool". Het pakket zou die referenties ook zelf kunnen meeleveren.
- **`pluginval test` kiest zelf de kanalen** (de standaard van de plugin, stereo). Mono via VST3 en AU wordt daarom
  niet met de null-test getoetst. Het zit wel in de validate-toetsen van pluginval (VST3: `In: Mono … Out: Mono/Stereo
  [Succeeded]`; AU: de indelingen uit auval). Mono bit voor bit is bewezen met de CLAP, en VST3 en AU gaan door
  dezelfde process-code.
- **pluginval AU meldt** `Bypass: set 0.569603, restored as 0`. Dat is een melding en geen fout (de stap is
  geslaagd). De AU geeft de bypass waarschijnlijk via `kAudioUnitProperty_BypassEffect` en niet via de
  parameterstate terug. Ik heb het niet verder uitgezocht.
- **De bypass is een eigen keuze.** De DSP draait door terwijl de bypass aan staat, zodat terugschakelen geen gat
  geeft. De echo's van wat er tijdens de bypass binnenkwam klinken dan wel na. Bij de AU roept clap-wrapper `Reset`
  aan bij het uitzetten van BypassEffect; daar begint de echo dus leeg. Dat is zo bedoeld door clap-wrapper.
- **Id 100 voor de bypass.** SPEC zegt "id = het nummer; nooit hergebruiken". Ik nam 100, ver van de DSP-id's 1..8,
  zodat een nieuwe DSP-parameter nooit botst. Het staat in de parametertabel van SPEC.md.
- **Voor Clay (fase 7)**: zet de plugin in Logic op een mono-spoor. Logic hoort dan "Mono" en "Mono → Stereo" aan te bieden (auval meldt 1-1 en 1-2); in CI is dat niet in Logic zelf getoetst. Kijk
  of de echo in beide gevallen klinkt en of de bypassknop van Logic (de aan/uit-knop van de plugin) zonder tik
  schakelt.
