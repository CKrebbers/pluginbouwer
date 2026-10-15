# KEUZE-STACK — fase B: welke stack, op grond van metingen

Besluit (15 okt 2026): **kale C++ in `dsp/` (vrijstaand, eigen wiskunde) → WASM met clang, en CLAP-first met
clap-wrapper voor VST3 en AU.** Faust blijft bruikbaar als *bron* voor losse blokken (gegenereerde C++ in onze schil,
zie onder), maar niet als de stack die browser en plugin gelijk houdt.

## Wat er gemeten is
Dezelfde keten (RBJ-laagdoor → Lagrange-3 delay → droog/nat) in twee stacks, plus een scherpere proef: een filter
waarvan de afsnijfrequentie elke sample beweegt (dan rekent de stack elke sample sin/cos/pow). Alles in CI op Linux x64
én macOS arm64 (`stackproef`- en `macos`-jobs, run 4 van 15 okt 2026 en later); lokaal op Linux nagemeten.

| Stack | Keten, vaste coëfficiënten: browser vs gebouwd binary | Gemoduleerde coëfficiënten | Hoe het bewijs liep |
|---|---|---|---|
| **Onze (C++ + clap-wrapper)** | bit voor bit gelijk: CLAP (Linux, macOS), VST3 via `pluginval test` (Linux, macOS), AU via `pluginval test` (macOS) | bit voor bit gelijk (de dub-echo: wow/flutter = sin per sample, saturatie, zelfoscillatie) | `tools/ci-null.sh`, blok 512 en wisselend, ook na een state-rondje |
| **Faust 2.90, double** (`-double`) | bit voor bit gelijk, ook door het `.clap` in onze schil | Linux: bit voor bit gelijk; macOS: zie `stackproef/faust/uitslag-darwin-arm64.json` in CI | `stackproef/faust/proef.mjs` |
| **Faust 2.90, float** (de standaard) | bit voor bit gelijk | **−46,8 dBFS** verschil (ruis), vanaf sample 509; 43 dB boven de grens | idem |

Wat dat betekent:
- Faust in float laat browser en plugin uit elkaar lopen zodra coëfficiënten bewegen: de WASM-kant krijgt zijn
  transcendente functies van JavaScript (`Math`), de native kant van `libm` (`sinf`, `cosf`, `powf`). In double
  vielen ze hier toevallig samen; dat is geen garantie (andere libm, andere argumenten).
- Faust-float zit ook met vaste coëfficiënten tot **−72 dBFS** naast dezelfde keten in double (Q 6 bij 1500 Hz):
  precisieverlies in de biquad, geen stackverschil.
- Onze stack garandeert gelijkheid door constructie (CLAUDE.md regel 2): geen libm, `-ffp-contract=off`, geseed toeval.

## Bijkomende vondsten in de proef (die een keuze voor Faust ook zouden treffen)
- De WASM-build van de Faust-compiler (faustwasm 0.19.0, compiler 2.90.0) heeft **geen C++-backend**. Wie Faust
  voor beide kanten wil, heeft twee compilers nodig, en die liepen hier al uiteen in versie (2.90.0 tegen 2.90.8).
- De native Faust-compiler uit de bron bouwen kost ~2,5 min in CI (met cache daarna ~0).
- Faust heeft eigen plugin-architecturen (`faust2clap`, `faust2au`, `faust2juce`, `faust2vst` in `tools/faust2appls/`);
  die zijn niet getoetst. Wel getoetst: Faust-C++ in onze CLAP-schil (`dsp/ontwerpen/faust_proef_delay.h`) haalt de
  null-test door het binary.

## Licenties en formaten (uit de LICENSE-bestanden, zie BRONNEN.md)
| Kandidaat | Licentie | AU | Zelfde DSP in de browser | Opmerking |
|---|---|---|---|---|
| **Onze stack** | eigen code; CLAP MIT, clap-wrapper MIT, VST3 SDK **MIT** (sinds 3.8), AudioUnitSDK Apache 2.0 | ja (clap-wrapper) | ja, door constructie | geen copyleft, geen kosten |
| Faust | compiler LGPL 2.1; gegenereerde code van jou **[architecturen niet nagelopen]** | via `faust2au` of onze schil | ja, maar float loopt uiteen (gemeten) | twee compilers nodig |
| JUCE 9.0.4 | AGPLv3 of commercieel | ja | niet officieel **[niet bevestigd]** | AGPL: bij publiceren de bron erbij; voor eigen gebruik geen kosten |
| iPlug2 (v1.0.0-beta) | zlib-achtig | ja | ja via WAM **[niet gebouwd]** | |
| Cmajor 1.0.3209 | GPLv3 of commercieel; gegenereerde C++ is van jou | via export | ja **[niet gebouwd]** | |
| nih-plug (Rust) | ISC; VST3-bindings GPLv3 | **nee** | niet officieel | valt af: AU is verplicht |
| DPF | ISC | ja | **[niet bevestigd]** | |

Niet gebouwd in de proef: JUCE, iPlug2, Cmajor, DPF. Reden: de opdracht vraagt twee kandidaten; de tweede (Faust)
is gekozen omdat hij als enige in deze lijst één taal voor browser én plugin belooft zonder dat je C++ schrijft. Die
belofte is hierboven gemeten.

## Wat dit kost aan moeite en wat Clay moet leren
- Onze stack: alles is C++ die Claude schrijft; Clay leest geen C++. De bouwstraat (CI, validators, null-test) staat.
  Risico: wie een nieuw blok schrijft moet de regels van CLAUDE.md volgen (geen libm, per sample); de toetsen vangen
  het als dat misgaat (blokgrootte, null, RTSan).
- Faust: een DSP-taal die Clay zelf zou kunnen lezen en aanpassen. Als dat later belangrijk wordt: Faust in **double**,
  en elk Faust-blok door dezelfde null-test (met bewegende parameters) vóór het in de BIB komt.
