Stopcriterium gehaald.

# Uitslag fase 5 — Dub-echo (tak proef/f2)

Laatste CI-run met code (commit 5c413d3): run 104 —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39046065519 — alle drie de jobs groen.
De code zelf was al groen bij de **eerste** push (commit f2287d5, run 101:
https://github.com/CKrebbers/pluginbouwer/actions/runs/39044903825). Commit 5c413d3 zette alleen twee macOS-stappen
achteraan, zodat hun uitvoer in de laatste 5000 logregels valt die de GitHub-tool teruggeeft (de pluginval-stappen
zijn erg lang), en vinkte TASKS.md af. De commit met dit bestand verandert alleen docs.

## Wat ik deed
- **Mono en stereo.** Drie poortindelingen (`INDELINGEN` in `plugin/dub_echo.cpp`; id = index, nooit hernummeren):
  - 0 Stereo (2 → 2, de standaard);
  - 1 Mono (1 → 1);
  - 2 Mono naar stereo (1 → 2).

  Er zijn drie extensies voor:
  - `clap.audio-ports-config` en `-info`, zoals de prompt vraagt;
  - `clap.configurable-audio-ports`: dat gebruikt clap-wrapper voor de VST3 (`setBusArrangements`).

  Kiezen kan alleen als de plugin niet actief is. `audio-ports` volgt de keuze; `in_place_pair` alleen bij evenveel
  kanalen.
- **process** neemt het aantal kanalen uit de buffers van de host. De DSP rekent met zoveel kanalen als de uitgang
  heeft (1 of 2). Bij mono naar stereo krijgen beide DSP-ingangen het ene invoerkanaal.
- **De valkuil uit de prompt.** De DSP leest de invoer nu **altijd** uit een vooraf gereserveerde kopie. Een
  hostbuffer die invoer én uitgang 0 is, kan zo niet halverwege overschreven worden.
  - Tegenproef (lokaal): met een plugin die direct uit de hostbuffer leest, wijkt mono → stereo in-place
    −13,6 dBFS af van de WASM.
  - Met de kopie is hij bit voor bit gelijk.
- **Bypass zoals de host het wil.** Parameter 9 "Bypass" met `CLAP_PARAM_IS_BYPASS`, getrapt en automatiseerbaar.
  clap-wrapper maakt er de VST3-bypass en `kAudioUnitProperty_BypassEffect` van.
  - De DSP rekent door.
  - De uitgang schuift in 10 ms lineair naar de droge invoer en terug.
  - In rust is de uitvoer bit voor bit de DSP of bit voor bit de invoer.
  - Vóór de eerste sample na activate/reset springt hij, net als de glijders van de DSP. Zo is het gelijk of de
    host hem vóór activate zet of in het eerste blok (JUCE doet dat laatste).
  - In volle bypass is de staart 0.
  - Hij staat in de state. Een state zonder id 9 (fase 4) zet hem uit; een preset laat hem staan.
- **AU** (patch 4, `patches/clap-wrapper-stereo-en-mono-als-au-kanaalindelingen.patch`): clap-wrapper v0.16.0 kende
  voor AUv2 alleen een vaste stereo-indeling. De patch doet het volgende:
  - de indelingen uit `audio-ports-config` worden `SupportedNumChannels` (1-1, 1-2, 2-2);
  - `ValidFormat` neemt die kanaalaantallen aan;
  - `Initialize` kiest vóór `activateCLAP` de indeling die bij de stream-formats past (anders
    `kAudioUnitErr_FormatNotSupported`).

  De patch is generiek. De naam valt achteraan in naamvolgorde.
- **44,1–192 kHz en blok 1..4096.** `wasm/graaf.wasm` geeft bij 48 en 44,1 kHz bit voor bit alle vier
  `testsignalen/*.verwacht.wav` (lokaal gecontroleerd, ook zelfoscillatie). Hij is dus de referentie bij elke
  samplerate en indeling:
  - `gereedschap/wasm-render.mjs` maakt de referenties;
  - `gereedschap/null-matrix.mjs` rendert en vergelijkt, met dezelfde grens en statistiek als `vergelijk.mjs`.
- **Nieuwe gereedschappen en toetsen:**
  - `pb-render --kanalen stereo|mono|mono-stereo --in-place`;
  - `plugin/toetsen/formaat-toets.cpp`: CLAP, Linux, macOS en RTSan;
  - `plugin/toetsen/au-render.cpp`: AudioToolbox op de geïnstalleerde AU, met mono, mono→stereo, stereo en
    BypassEffect.
- **CI uitgebreid** met de null-matrix:
  - CLAP: 6 samplerates × 3 indelingen × blok 512, wisselend in-place, en blok 1 en 4096; 192 renders, Linux en
    macOS;
  - VST3 en AU via `pluginval test`: 6 samplerates, blok 1..4096, en Bypass aan; 44 renders per formaat;
  - AU via au-render: 108 renders.

  Daarnaast:
  - een stap die eist dat zeven clap-validator-toetsen draaien en slagen (layout-*, process-varying-*,
    process-random-block-sizes, process-audio-basic-*);
  - RTSan in elke indeling, bij 44,1 en 192 kHz, plus de formaat-toets onder RTSan;
  - de auval-kanaalregels in het log.
- SPEC.md ("Formaten", parameter 9, toleranties, validators), ARCHITECTUUR.md ("Formaten", state) en TASKS.md
  bijgewerkt. `dsp/` en `testsignalen/` niet aangeraakt.

## Poorten (run 104; waar het log van run 104 afgekapt is: run 101, dezelfde code, alle stappen groen)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| null-matrix CLAP (Linux, macOS) | groen | "192 van 192 binnen de grens" (Linux en "(darwin arm64)"); elke rij "−∞ \| ja", bv. "44100 Hz, mono-stereo, wisselend, in-place \| echo-heet \| −∞ \| ja" |
| null-matrix VST3 (Linux, macOS; pluginval test) | groen | "44 van 44 binnen de grens"; o.a. "192000 Hz, blok 1 \| zelfoscillatie \| −∞ \| ja", "48000 Hz, blok 512, Bypass aan (= de invoer) \| echo-heet \| −∞ \| ja" |
| null-matrix AU via pluginval (macOS) | groen | "### Null-matrix pluginval au-macos (darwin arm64)" … "44 van 44 binnen de grens" |
| null-matrix AU via AudioToolbox (macOS) | groen | "### Null-matrix au build/au-render (darwin arm64)"; "192000 Hz, mono-stereo, wisselend \| sweep-441 \| −∞ \| ja"; "48000 Hz, mono, BypassEffect (= de invoer) \| echo-standaard \| −∞ \| ja"; "108 van 108 binnen de grens" |
| auval: kanaalindelingen | groen | "Input/Output Channel Handling:" met X onder 1-1, 1-2 en 2-2; "AU VALIDATION SUCCEEDED." |
| clap-validator fase 5 | groen | "layout-audio-ports-config success", "layout-configurable-audio-ports success", "process-varying-sample-rates success", "process-varying-block-sizes success", "alle 7 geslaagd" |
| clap-validator (alles) | groen | "44 tests run, 38 passed, 0 failed, 0 warnings, 6 skipped" (fase 4: 36 passed, 8 skipped; de twee layout-toetsen draaien nu); fase-2-subset "alle 9 geslaagd"; presets "alle 3 geslaagd"; fuzz "OK: fuzzing finished successfully" |
| formaat-toets (Linux, macOS, RTSan) | groen | in CI: stap groen, `GESLAAGD`. De regels zelf heb ik uit de lokale run geciteerd (de toets is deterministisch): o.a. "mono naar stereo, in-place, event in process: vóór sample 30011 de DSP, na 482 samples overgang de invoer, na het uitzetten weer de DSP; alles bit voor bit (0/0/0 verschillend)"; "terwijl de plugin actief is: kiezen geweigerd" |
| pluginval 10 VST3 Linux | groen | "Result: 537 tests passed, 0 tests failed", `SUCCESS` |
| pluginval 10 `--rtcheck` AU en VST3 macOS | groen | VST3 "Result: 537 tests passed, 0 tests failed", `SUCCESS`; AU `SUCCESS`. "Bus layout processing": "Trying layout: in [Mono] out [Mono]", "in [Mono] out [Stereo]", "in [Stereo] out [Mono] / setBusesLayout() rejected the layout", "Layouts tested: 5, accepted by setBusesLayout: 4"; "Fuzz testing parameter: 8 - Bypass" |
| null-tests fase 1 | groen, bit voor bit | CLAP (blok 512, wisselend, state), VST3 en AU: "−∞ (bit voor bit gelijk)"; zelfoscillatie "terts 0.000 dB, omhullende 0.000 dB" |
| eigen toetsen fase 2–4 | groen | elke aanroep `GESLAAGD` (tekst, automatisering, state zonder N voor N = 1..8, ui-toets, gui-toets, preset-toets, au-preset-toets) |
| RTSan schoon + kanarie faalt | groen | "RTSan: geen overtreding in process in elke indeling, samplerate, en met bypass" (plus die van fase 1–4); kanarie: "Intercepted call to real-time unsafe function `malloc` in real-time context!", "Kanarie gevangen zoals bedoeld (exitcode 43)" |
| artefacten | groen | "Artifact macos-keten_dub_echo has been successfully uploaded!" |

## Waar het schuurde / wat onduidelijk of fout was
- **clap-wrapper v0.16.0 kent geen mono voor AUv2.** `ValidFormat` nam alleen het kanaalaantal van de vaste poort
  aan. Zonder patch 4 had Logic de AU alleen als stereo kunnen invoegen.
  - De VST3-kant had het al (via `configurable-audio-ports`, niet via `audio-ports-config`).
  - De plugin heeft daarom beide extensies.
  - Kandidaat om upstream aan te bieden.
- **Eén eigen toets aangepast, niet uitgezet.** `ui-toets` eiste "8 knoppen == aantal host-parameters". De host heeft
  nu 9 parameters (Bypass als laatste). De eis is nu: 8 knoppen, en de host heeft daarna alleen Bypass (id 9, met
  `IS_BYPASS`).
- **De fuzzer van clap-validator ving een subnormale uitvoersample in bypass** (droge invoer ~1e-39). Opgelost door
  `poetsUitvoer` ná de bypass-overgang te doen.
  - Gevolg: in volle bypass is de uitvoer bit voor bit de invoer, behalve subnormale invoersamples, die 0 worden.
  - Dat staat zo in SPEC.md.
- **Keuzes die de prompt openliet:**
  - Bypass laat de DSP doorrekenen; hij gaat niet "koud" opnieuw van start. Een AU-host roept bij het uitzetten
    zelf `Reset` aan.
  - De overgang is 10 ms.
  - Een preset verandert de bypass niet.
  - Mono → stereo is als derde indeling toegevoegd, omdat Logic die standaard aanbiedt voor effecten op een
    monospoor.
- **Niet bit voor bit getoetst: de VST3 in mono.**
  - `pluginval test` rendert alleen in de standaardindeling (stereo).
  - pluginval validate laat de VST3 wel door Mono/Mono en Mono/Stereo gaan ("Bus layout processing") en slaagt.
  - De CLAP- en AU-matrix tonen dat dezelfde plugincode in mono bit voor bit klopt.
  - Een eigen VST3-host zou het gat dichten.
- **"Breid de CI-matrix uit"** heb ik opgevat als een matrix van renders binnen de bestaande jobs (een lus over
  samplerates, indelingen en blokgroottes), niet als een GitHub `strategy.matrix`. Dat scheelt zes keer bouwen; het
  log heeft per variant een regel.
- **De GitHub-tool geeft van een job alleen de laatste 5000 logregels**, en het logarchief gaf 403. Daarom kwamen de
  AU-matrix en de auval-kanaalregels in run 101 niet in beeld (de stap was wel groen, en de matrix eindigt met
  exit 1 bij één afwijking). Ze staan nu achteraan in de job.
- In Logic (fase 7) is dit de plek om na te kijken:
  - de plugin op een monospoor invoegen als Mono en als Mono→Stereo;
  - de bypassknop van Logic. Logic kan ook zelf bypassen zonder de AU aan te roepen; dan is dit pad niet in gebruik.
