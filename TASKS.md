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
- [ ] SPEC.md: doelgeluid (RE-201-gegevens, gemarkeerd wat niet bevestigd is), parameters, budgetten, toleranties
- [x] DSP-blokken: fractionele delay (Lagrange-3), tape-saturatie met ADAA, TPT-SVF in de lus, wow/flutter, mix
- [x] Python float64-referentie (andere code), 16× oversampling voor de niet-lineaire delen
- [~] metingen: ESS, aliasing, stabiliteit, staart, null, statistische zelfoscillatietoets gedaan; **IMD nog niet**
- [ ] checkpoint C

## D. De ontwerper
- [ ] BIB met de blokken van de dub-echo (+ de rest als lijst hieronder), getallen uit de meetharnas
- [ ] keten tekenen (slepen, kabels, feedback), draaiknoppen, modulatie slepen, pijltjes bij de laatste wijziging
- [ ] horen (AudioWorklet + dezelfde WASM), eigen audio of testsignalen, mockup frontpaneel
- [ ] meten live (respons, harmonischen, aliasing, latency, CPU)
- [ ] verken: varianten, archetypes (dub-echo, veerreverb, sidechain-pomp), schetsboek
- [ ] bouwen-stand: stack, formaten, moeite, kosten, risico's, leerpad
- [ ] ABX tegen een referentie; taaktoets-log
- [ ] artifact + losse pagina — checkpoint D

## E. Bouwpakket en prompts
- [ ] export: SPEC, ARCHITECTUUR, design, CLAUDE, TASKS, PROMPTS, testsignalen, dsp/, CI-sjabloon
- [ ] 3 verse sessies bouwen fase 0 en 1 met alleen het pakket; pass@3 in PROMPTS.md
- [ ] de AU van zo'n run haalt auval en pluginval 10 in CI; null-test binnen tolerantie
- [ ] Clay laadt hem in Logic (checklist fase 7) — **wacht op Clay**

## Bibliotheek later (niet bouwen vóór de dub-echo de hele keten gehaald heeft)
Oscillatoren (polyBLEP), sampler, granulair, ruis, fysische modellen (Karplus-Strong, waveguide), filters (ladder, comb,
formant), dynamiek (compressor, limiter, gate, expander), saturatie (diode, buis, transformator), delay (ping-pong,
multitap), reverb (Dattorro-plaat, FDN, veer, convolutie), modulatie (chorus, flanger, phaser, ringmod), pitch en tijd
(pitchshift, freeze), spectraal (STFT, vocoder), ruimtelijk (mid/side, breedte, panner); besturing (LFO, envelopes,
MSEG, macro's, sidechain, MIDI/MPE, tempo-sync, randomisatie); techniek (oversampling, latency melden, presets, undo);
UI (XY-vlak, meters, scopes, presetbrowser).
