# TASKS — volgorde (niet omdraaien)

Status: [ ] open · [~] bezig · [x] gehaald (met bewijs: CI-run of toets)

## 0. Basis
- [x] PROMPT.md, CLAUDE.md, design.md, TASKS.md, BRONNEN.md — checkpoint 0

## A. Pijplijn (één biquad de hele weg)
- [x] `dsp/pb_math.h`: sin/cos/tan/exp/log/tanh zonder libm, getoetst tegen Math.* (tests/math.test.mjs)
- [x] `dsp/pb_biquad.h`: RBJ-lowpass, double, per sample, smoothing per sample
- [x] WASM met clang wasm32 zonder libc; Node-toets tegen de RBJ-formule (≤ 0,1 dB, ≤ 1° tot 0,9·Nyquist)
- [x] blokgrootte-onafhankelijk (1/7/128/512/4096 bit voor bit gelijk)
- [x] CLAP-plugin + clap-wrapper → .clap, .vst3, .component (CMake), met patch voor AU-parameters vóór Initialize
- [x] `host/pb-render`: CLAP-host die een WAV door het .clap stuurt
- [x] CI Linux (ook RTSan met kanarie en ASan/UBSan): bouwen, clap-validator, pluginval 10 op VST3, RTSan, null-test CLAP en VST3 vs WASM
- [x] CI macOS arm64: ad-hoc signeren, auval -strict, pluginval 10 + rtcheck (AU, VST3), null-test AU/VST3/CLAP: bit voor bit
- [x] checkpoint A (CI run 4, 15 okt 2026: alle jobs groen)

## B. Proefbouw van de stack
- [x] dezelfde biquad + fractionele delay in Faust (C++ en WASM uit één .dsp)
- [x] null-test Faust-WASM vs Faust-native, en vs onze eigen stack
- [x] licenties en formaten per framework uit de LICENSE-bestanden (BRONNEN.md)
- [x] KEUZE-STACK.md met metingen; besluit in CLAUDE.md — checkpoint B

## C. Doelgeluid en de dub-echo
- [x] SPEC.md (gegenereerd: bouwpakket/dub_echo/SPEC.md): doelgeluid **nog niet vastgelegd** (geen referentieopname; RE-201-getallen niet bevestigd), parameters, budgetten, toleranties
- [x] DSP-blokken: fractionele delay (Lagrange-3), tape-saturatie met ADAA, TPT-SVF in de lus, wow/flutter, mix
- [x] Python float64-referentie (andere code), 16× oversampling voor de niet-lineaire delen
- [x] metingen: ESS, IMD, aliasing, stabiliteit, staart, null, statistische zelfoscillatietoets (tests/dubecho.test.mjs)
- [x] checkpoint C
- [x] ketenmotor (dsp/pb_graaf.h): de dub-echo als ketendata klinkt bit voor bit als de handgeschreven (tests/graaf.test.mjs)

## D. De ontwerper
- [x] BIB met de blokken van de dub-echo (+ de rest als lijst hieronder), getallen uit de meetharnas
- [x] keten tekenen (slepen, kabels, feedback), draaiknoppen, modulatie slepen, pijltjes bij de laatste wijziging
- [x] horen (AudioWorklet + dezelfde WASM), eigen audio of testsignalen, mockup frontpaneel
- [x] meten live (respons, harmonischen, aliasing, latency, CPU)
- [x] verken: varianten, archetypes (dub-echo, veerreverb, sidechain-pomp), schetsboek
- [x] bouwen-stand: stack, formaten, moeite, kosten, risico's, leerpad
- [x] ABX tegen een referentie; taaktoets-log
  - De tool staat in tool/index.html (één bestand, `node tool/bouw.mjs`); toets in Chrome: `node tools/headless/tool.mjs` (ook in CI). Readout meet met bandbeweging en modulatie stil en na het inlopen van de echo; zelfoscillatie geeft "n.v.t." met de reden.
- [x] artifact + losse pagina — checkpoint D (artifact: tool/artifact.html, losse pagina: tool/index.html)

## E. Bouwpakket en prompts
- [x] export: SPEC, ARCHITECTUUR, design, CLAUDE, TASKS, PROMPTS, testsignalen, dsp/, CI-sjabloon
- [x] 3 verse sessies bouwen fase 0 en 1 met alleen het pakket; pass@3 in PROMPTS.md — fase 0 **3/3**, fase 1 **3/3**
  (tak proef/e1–e3, UITSLAG-fase0/1.md per tak; uitslagen in tools/pakket/uitslagen.json). Pakket v2 (tweede patch, SPEC/
  ARCHITECTUUR-verduidelijkingen) is gemaakt NA de proef en nog niet door verse sessies getoetst.
- [x] de AU van zo'n run haalt auval en pluginval 10 in CI; null-test binnen tolerantie (alle drie: bit voor bit)
- [ ] Clay laadt hem in Logic (checklist fase 7) — **wacht op Clay**

## F. Pakket v2 door alle fasen (verse sessies, tak proef/f1–f3)
- [x] fase 0–6 elk **3/3** (17 okt; per fase het aantal rondes en wat gevonden is in tools/pakket/uitslagen.json en PROMPTS.md)
- [x] vondsten terug in main: subnormale uitvoer (pb::uitvoer), vierde clap-wrapper-patch (AU SetParameter tegen Render,
  pluginval AU met --repeat 3), vijfde en zesde patch (AU-fabriekspresets, AU-kanaalindelingen), bypass zonder subnormalen,
  pakket v3 (tekst ↔ waarde, stateversie, frontpaneel.svg, presets in het binary, fase-6-prompt, valkuilen 13–18)
- [x] upstream klaargezet: upstream/clap-wrapper/LEESMIJ.md (zes punten clap-wrapper, één clap-validator) — **plakken: Clay**
- [ ] PR 1 (geluid-maken) samengevoegd met main op tak integratie/geluid-maken (toetsen 105/105, tool in Chrome klinkt);
  het artifact draait die versie — **samenvoegen in main: Clay beslist**
- [ ] Referentie: echte band (Aalto-dataset, Akai) tegen onze tanh en Jiles-Atherton (.github/workflows/referentie.yml)

## Bibliotheek later (niet bouwen vóór de dub-echo de hele keten gehaald heeft)
Oscillatoren (polyBLEP), sampler, granulair, ruis, fysische modellen (Karplus-Strong, waveguide), filters (ladder, comb,
formant), dynamiek (compressor, limiter, gate, expander), saturatie (diode, buis, transformator), delay (ping-pong,
multitap), reverb (Dattorro-plaat, FDN, veer, convolutie), modulatie (chorus, flanger, phaser, ringmod), pitch en tijd
(pitchshift, freeze), spectraal (STFT, vocoder), ruimtelijk (mid/side, breedte, panner); besturing (LFO, envelopes,
MSEG, macro's, sidechain, MIDI/MPE, tempo-sync, randomisatie); techniek (oversampling, latency melden, presets, undo);
UI (XY-vlak, meters, scopes, presetbrowser).
Bandhysterese (Jiles-Atherton zoals CHOWTape; zie TWEEDE-MENING.md: vervorming bij zachte niveaus en niveau-afhankelijke fase die
onze tanh mist; eerst een ABX-toets of het in de dub-echo hoorbaar is).
