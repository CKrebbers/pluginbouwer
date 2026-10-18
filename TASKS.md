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
- [x] upstream klaargezet: upstream/clap-wrapper/LEESMIJ.md (zeven punten clap-wrapper, één clap-validator) — **plakken: Clay**
- [ ] PR 1 (geluid-maken) samengevoegd met main op tak integratie/geluid-maken (toetsen 105/105, tool in Chrome klinkt);
  het artifact draait die versie — **samenvoegen in main: Clay beslist**
- [x] Referentie: echte band (Aalto-dataset, Akai GX-4000D) gemeten: ref/referentie/RAPPORT.md. Beweging van onze standaard
  2,2 × de Akai en schaalt met de delaytijd; H3 past beter op Jiles-Atherton; de band maakt H2 die wij niet maken.

## Bibliotheek later (niet bouwen vóór de dub-echo de hele keten gehaald heeft)
Oscillatoren (polyBLEP), sampler, granulair, ruis, fysische modellen (Karplus-Strong, waveguide), filters (ladder, comb,
formant), dynamiek (compressor, limiter, gate, expander), saturatie (diode, buis, transformator), delay (ping-pong,
multitap), reverb (Dattorro-plaat, FDN, veer, convolutie), modulatie (chorus, flanger, phaser, ringmod), pitch en tijd
(pitchshift, freeze), spectraal (STFT, vocoder), ruimtelijk (mid/side, breedte, panner); besturing (LFO, envelopes,
MSEG, macro's, sidechain, MIDI/MPE, tempo-sync, randomisatie); techniek (oversampling, latency melden, presets, undo);
UI (XY-vlak, meters, scopes, presetbrowser).
Uit ref/referentie/RAPPORT.md (eerst ABX): bandbeweging als snelheid ε(t) los van de delaytijd, met de Akai als ijkpunt
(0,15 % RMS, flutter 7,7 Hz) voor haperen 0,25; een kleine even term voor H2; een blok bandruis (−61 dBFS, brom + sis).
Bandhysterese (Jiles-Atherton zoals CHOWTape; zie TWEEDE-MENING.md: vervorming bij zachte niveaus en niveau-afhankelijke fase die
onze tanh mist; eerst een ABX-toets of het in de dub-echo hoorbaar is).

## Vervolg — plan van 18 okt 2026 (volgorde, wie, stopcriterium)
Stand: alles wat zonder Clay kon is gedaan (pakket v3, zes patches, referentiemeting, upstream klaargezet). Eén meting ontbreekt
waar het hele project om draait: de AU in Logic. Daarna wordt het pakket pas breder (klank, instrument).

1. **Logic (Clay, ~20 min).** Niet de kale schil uit main, maar de plugin die een verse sessie uit het pakket bouwde: die heeft
   paneel, presets, mono, bypass en een installatiepakket. Download `macos-pkg` van run 122 op proef/f3
   (https://github.com/CKrebbers/pluginbouwer/actions/runs/39048545213), installeer (Privacy en beveiliging → "Open toch"),
   haal eerst een oude kopie uit `~/Library/Audio/Plug-Ins` weg. Dan de checklist van fase 7 in PROMPTS.md. Stop: elk punt
   groen, of een lijst van wat afwijkt. Elke afwijking wordt een valkuil in het pakket en zo nodig een patch (ik).
2. **PR 1 (geluid-maken) in main (Clay beslist, ik voer uit).** Via `integratie/geluid-maken`: CI groen, het artifact draait
   het al. Zonder deze stap maakt de tool op main geen geluid en kan het pakket geen instrument exporteren. Daarna: TASKS
   herschikken (F = geluid maken, G = pakket), pakket en tool opnieuw maken met de motor van 15 blokken.
3. **Pakket v3 bewijzen (ik, verse sessies; begint zodra Clay "ga" zegt).** v3 draagt 18 valkuilen en 6 patches die nog nooit
   samen door een vers gesprek zijn gegaan. Eén keten (proef/f4), fase 0–6, pass@1. Stop: elke fase de eerste push groen;
   elke extra ronde is een gat in het pakket en wordt gedicht. Kosten: ~7 sessies in plaats van 21.
4. **De klank: eerst ABX, dan pas bouwen (ik bouw de ABX in de tool, Clay luistert).** Drie kandidaten uit
   ref/referentie/RAPPORT.md, elk als A/B in de tab ABX: (a) bandbeweging als snelheid los van de delaytijd, Akai als ijkpunt
   voor haperen 0,25; (b) Jiles-Atherton plus een kleine even term voor H2; (c) een blok bandruis. Alleen wat Clay blind
   onderscheidt gaat de DSP in. Per kandidaat die erin gaat: referentie opnieuw meten, verwachte uitvoer vernieuwen,
   null-test en harmonischen-toets groen. Niets tegelijk: één kandidaat per ronde.
5. **Instrument als plugin (ik; na stap 2).** CLAP note ports, AU-type `aumu`, MIDI naar de toetsen-knoop; de sequencer op
   het hosttempo. Dan pass@3 met een instrumentontwerp: het pakket is nu alleen op een effect bewezen. Stop: een instrument
   uit de tool speelt in Logic op een MIDI-spoor, null-test instrument tegen WASM bit voor bit.
6. **Upstream (Clay plakt, 5 min; ik volg op).** upstream/clap-wrapper/LEESMIJ.md: zeven punten clap-wrapper, één
   clap-validator. Komt er antwoord, dan pas ik de patches aan en kijk bij de volgende clap-wrapper welke kunnen vervallen.
7. **Niets weggooien.** De proeftakken (proef/e*, f*) en de onderzoekstak blijven staan: ze zijn het bewijs. Wel een index in
   README (welke tak wat bewijst), zodat niemand ze hoeft te openen om dat te weten.

Volgorde en waarom: 1 vóór alles (het bewijs waar het om gaat); 2 vóór 3 en 5 (een instrument in het pakket vraagt de motor);
3 en 4 lopen naast elkaar en vragen Clay niets dan luisteren; 5 is de eerste nieuwe bouw en komt pas na 1–3.
