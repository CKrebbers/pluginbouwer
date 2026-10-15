# BRONNEN — primair materiaal, met versie en datum van lezen

Regel: een getal of claim in deze repo verwijst hierheen. Wat niet bevestigd is, staat als **[niet bevestigd]** met
waar gezocht is. Gelezen = de broncode of het LICENSE-bestand zelf (git clone), niet een samenvatting.

## Formaten en SDK's (gelezen 15 okt 2026)
| Bron | URL | Versie / commit | Licentie (uit het bestand) | Gebruikt voor |
|---|---|---|---|---|
| CLAP | https://github.com/free-audio/clap | main a47f6ba (28 jul 2026) | MIT | plugin-API |
| clap-wrapper | https://github.com/free-audio/clap-wrapper | main 1cca996 (8 aug 2026) | MIT | CLAP → VST3, AUv2 (ook AUv3, WCLAP) via `make_clapfirst_plugins` |
| VST3 SDK | https://github.com/steinbergmedia/vst3sdk | 3cdf9ca (11 aug 2026); clap-wrapper haalt `v3.8.0_build_66` | **MIT** (LICENSE.txt, © 2026 Steinberg) | VST3 |
| AudioUnitSDK | https://github.com/apple/AudioUnitSDK | bd98b31 (5 dec 2025); clap-wrapper haalt `AudioUnitSDK-1.1.0` | Apache 2.0 | AUv2 |
| pluginval | https://github.com/Tracktion/pluginval | main 286684f (15 okt 2026), VERSION 2.0.0; laatste tag `v2.0.0-beta3` | GPLv3 (alleen als gereedschap gebruikt, niet gelinkt) | validatie, `pluginval test` = render door het binary |
| clap-validator | https://github.com/free-audio/clap-validator | main b2f1d9b (24 jul 2026) | MIT | CLAP-validatie |

### Wat pluginval 2.0 doet (uit `source/tests/*.cpp` en `CHANGELIST.md`)
- Strictness > 5: auval draait mee als `auval -strict -stress 20 -v …` (Tests.cpp r. 889); 1000 i.p.v. 100 blokken met
  willekeurige parameters (r. 614); subnormals zijn fouten (r. 495).
- Strictness ≥ 8: "Discrete parameter state restoration" en exacte binaire state-vergelijking.
- De Steinberg VST3-validator zit erin en draait vanaf strictness 5 voor VST3.
- `pluginval test <config.json>` rendert met een vaste state en invoer en vergelijkt met een referentie-WAV
  (`comparison.sample` = absolute tolerantie per sample). Dit gebruiken we als native kant van de null-test.
- `--rtcheck enabled` (alleen macOS) controleert real-time veiligheid in process.
- Geen CLAP: CLAP valideren we met clap-validator.
- Release-binaries: `https://github.com/Tracktion/pluginval/releases/download/<tag>/pluginval_<macOS|Linux|Windows>.zip`
  (uit `.github/actions/common/common.sh`).

## Frameworks (LICENSE-bestand op de standaardtak, gelezen 15 okt 2026)
| Framework | Commit / laatste tag | Licentie | AU? | Zelfde DSP in de browser? |
|---|---|---|---|---|
| JUCE | 454fe89 (12 okt 2026) / 9.0.4 | AGPLv3 of commercieel (JUCE 9 EULA) | ja | niet officieel **[niet bevestigd]**: geen WASM-doel in de LICENSE/README gezien |
| iPlug2 | d54f690 (19 aug 2026) / v1.0.0-beta | zlib-achtig (vrij) | ja | ja, web-doel (WAM) volgens README **[niet zelf gebouwd]** |
| Faust | 1a52f77 (12 okt 2026) / v2-5-10a; COPYING noemt compiler 2.90.8 | compiler **LGPL 2.1** | via architecturen | ja: C++ en WASM uit één .dsp (getoetst in B) |
| Cmajor | b112e0c (15 okt 2026) / 1.0.3209 | GPLv3 of commercieel; gegenereerde C++ is van jou (LICENSE.md) | via JUCE/CLAP-export | ja, WASM/web-export **[niet zelf gebouwd]** |
| nih-plug (Rust) | de42101 (3 sep 2026) | ISC; VST3-bindings GPLv3 (README) | **nee** (VST3 en CLAP) | niet officieel |
| DPF | 4238e1c (23 okt 2025) | ISC; per formaat in LICENSING.md (AU: ISC) | ja | **[niet bevestigd]** |

## DSP
| Bron | Waar | Status |
|---|---|---|
| R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients" | W3C-kopie: https://www.w3.org/TR/audio-eq-cookbook/ | formules gebruikt in `dsp/pb_biquad.h` en, apart uitgeschreven, in `ref/` |
| V. Zavalishin, "The Art of VA Filter Design" rev. 2.1.2 | Native Instruments (gratis PDF) | TPT-SVF (hoofdstuk 4) **[PDF niet opgehaald in deze sessie; formules uit eigen kennis, getoetst tegen de analytische overdracht]** |
| T. Laakso, V. Välimäki, M. Karjalainen, U. Laine, "Splitting the Unit Delay", IEEE SP Magazine 1996 | — | Lagrange-interpolatie; fout getoetst tegen de ideale fractionele delay |
| J. Dattorro, "Effect Design" deel 1 (1997) en 2 | JAES | modulatie van delaylijnen **[niet opgehaald]** |
| J. Parker, V. Zavalishin, E. Le Bivic, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution", DAFx 2016 | — | ADAA eerste orde voor tanh |
| A. Farina, "Simultaneous measurement of impulse response and distortion with a swept-sine technique", AES 2000 | — | ESS-meting in `tests/` en `ref/` |
| J. Chowdhury, "Real-time physical modelling for analog tape machines", DAFx 2019 + CHOWTapeModel | https://github.com/jatinchowdhury18/AnalogTapeModel 604372e / v2.11.4, **GPLv3** | tweede mening voor hysterese (niet overnemen) |

### Blokken van 15 okt 2026 (`dsp/pb_bronnen.h`)
Uit eigen kennis opgeschreven, **niet opgehaald in deze sessie**. Elk blok is getoetst tegen de theorie die de bron
geeft, niet tegen de tekst van de bron (tests/bronnen.test.mjs).
| Bron | Gebruikt voor | Toets |
|---|---|---|
| V. Välimäki, A. Huovilainen, "Antialiasing Oscillators in Subtractive Synthesis", IEEE SP Magazine 24(2), 2007 | polyBLEP in de oscillator (zaag, blok) | boventonen = Fourier-reeks ±0,3 dB tot H7; ≥ 10 dB minder aliasing dan naïef op 5 kHz |
| K. Karplus, A. Strong, "Digital Synthesis of Plucked-String and Drum Timbres", CMJ 7(2), 1983 | snaar | — |
| D. Jaffe, J. O. Smith, "Extensions of the Karplus-Strong Plucked-String Algorithm", CMJ 7(2), 1983 | stemming van de snaar (lusvertraging incl. het middelende filter, fractioneel) | toonhoogte ±0,5 % op 110–880 Hz |
| J.-M. Jot, A. Chaigne, "Digital Delay Networks for Designing Artificial Reverberators", AES 90th Convention, 1991 | galm: 8 lijnen, Hadamard-terugkoppeling, g = 10^(−3L/(T60·sr)) | nagalmtijd (Schroeder-integraal) ±15 % van de knop |
| M. R. Schroeder, "New Method of Measuring Reverberation Time", JASA 37, 1965 | de meting van de nagalmtijd in de toets | — |
| Feed-forward compressor met harde knie (leerboekvorm) | compressor | statische kromme ±0,5 dB |

## Doelgeluid
Zie SPEC.md van de dub-echo. Elk getal over de RE-201 staat daar met bron of als **[niet bevestigd]**.
