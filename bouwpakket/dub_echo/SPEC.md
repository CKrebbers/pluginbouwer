# SPEC — Dub-echo

Gegenereerd door de Pluginbouwer uit het ontwerp `dub_echo`. De DSP staat
vast in `dsp/` (één bron voor browser en plugin); deze SPEC zegt wat de plugin eromheen moet doen en waaraan hij
getoetst wordt.

## Doel
Tape-echo voor dub: saturatie en een filter in de terugkoppeling. Boven terugkoppeling 1 gaat hij vanzelf door, en de band houdt hem in toom.

## Doelgeluid
**Nog niet vastgelegd.** Er is (nog) geen referentieopname van een echte tape-echo. Tot die er is, toetst
de plugin alleen tegen zijn eigen ontwerp (null-test) en tegen de theorie; "klinkt als een tape-echo" is niet bewezen.
Wat Clay kan leveren: een opname van een tape-echo (droog en nat, zelfde fragment), met de instellingen erbij.

Over het voorbeeld (Roland RE-201): één opneemkop, drie weergavekoppen, de echotijd via de bandsnelheid
(Repeat Rate), intensity = terugkoppeling, bass/treble op de echo. **Niet bevestigd**: tijden in ms, bandsnelheid,
kopafstanden, wow/flutter-getallen. Gezocht (15 okt 2026): websearch op service notes en specificaties; het
service-handboek staat op archive.org ("Roland_RE-101_RE-201_Service_Manual") en ManualsLib (999287), maar die waren
vanuit de bouwomgeving niet te openen. De bereiken hieronder zijn ontwerpkeuzes, geen RE-201-waarden.

## Keten (uitvoervolgorde per sample)
1. band (Bandbeweging)
2. in (Ingang)
3. som (Optellen): a ← in.uit, b ← terug.uit (vorige sample: terugkoppeling)
4. tape (Tapeverzadiging): in ← som.uit
5. lijn (Vertraging): in ← tape.uit, beweging ← band.beweging
6. laag (Laag weg): in ← lijn.uit
7. toon (Filter (SVF)): in ← laag.uit
8. mix (Mix): droog ← in.uit, nat ← toon.laag
9. uit (Uitgang): in ← mix.uit
10. terug (Volume): in ← toon.laag

Terugkoppeling: ja — een kabel naar een eerdere knoop leest de uitgang van de vorige sample (z⁻¹).

## Parameters (plugin-id = het nummer; nooit hergebruiken of hernummeren)
| id | naam | bereik | eenheid | standaard | glijtijd | bij draaien | wat het doet |
|---|---|---|---|---|---|---|---|
| 1 | Tijd | 20 – 1200 | ms | 375 | 250 ms | glijden | Hoe lang tot de echo komt. Draaien laat de toon glijden, zoals een band die sneller gaat |
| 2 | Terugkoppeling | 0 – 1.25 | — | 0.5 | 30 ms | glijden | Hoeveel van de echo weer de band op gaat. Boven 1 gaat hij vanzelf door (zelfoscillatie) |
| 3 | Toon | 300 – 12000 | Hz | 3000 | 20 ms | glijden | Hoe donker elke herhaling wordt: het filter in de lus haalt hoog weg |
| 4 | Laag weg | 20 – 1000 | Hz | 100 | 20 ms | glijden | Haalt laag uit de lus zodat de herhalingen niet dichtslibben |
| 5 | Resonantie | 0.5 – 4 | Q | 0.70703125 | 20 ms | glijden | Een piek op de toonfrequentie in de lus; hoger = fluitender herhalingen |
| 6 | Verzadiging | 0 – 24 | dB | 6 | 20 ms | glijden | Hoe vroeg de band dichtloopt: warmer, en het remt de zelfoscillatie |
| 7 | Haperen | 0 – 1 | — | 0.25 | 20 ms | glijden | Wow en flutter: hoe onrustig de band loopt |
| 8 | Mix | 0 – 1 | — | 0.375 | 20 ms | glijden | Hoeveel echo je hoort naast het origineel |

- CLAP-parameters zijn waarden in hun eigen eenheid (min..max hierboven), niet 0..1; clap-wrapper normaliseert lineair
  voor VST3 en AU. Grenzen en standaarden zijn exact in float32, omdat een AU float doorgeeft: zo krijgt elk formaat
  bit voor bit dezelfde waarde.
- "glijden": de waarde schuift per sample naar het doel (een tape-echo laat dan de toon glijden; zo bedoeld).

## Budgetten
| wat | budget | geschat/gemeten |
|---|---|---|
| CPU per instantie | ≤ 5 % van één kern | 1,70 % (WASM-ketenmotor in Node, Intel(R) Xeon(R) Processor @ 2.10GHz, linux x64, Node v22.22.0; native is meestal sneller) |
| latency | 0 samples | 0 samples (fracties worden in de vertraging gecompenseerd) |
| staart | gemeld aan de host | oneindig bij een lusversterking ≥ 1, anders tot −90 dB (formule in pb_graaf.h staartSec) |
| geheugen | vooraf, nooit in process | vertragingslijnen op de hoogste samplerate van activate |

## Toleranties (per meting, met reden)
| meting | grens | reden | gemeten |
|---|---|---|---|
| null-test browser (WASM) vs gebouwd binary, per testsignaal | piek ≤ −90 dBFS | de opdracht van de Pluginbouwer; in de praktijk 0 (bit voor bit) door de regels van CLAUDE.md | bit voor bit (CLAP, VST3, AU; Linux en macOS) |
| zelfoscillatie (chaotisch) | terts-spectrum en omhullende binnen 1 dB | 1 ULP groeit in een lus; statistisch wat je hoort | 0,000 dB |
| lineair deel tegen de formules | ≤ 0,01 dB, ≤ 0,05° | kubische term van tanh op −60 dBFS, float32-uitvoer | 0,0008 dB, 0,005° |
| tegen een onafhankelijke float64-referentie (Python) | ≤ −120 dBFS | libm tegen pb_math, float32-uitvoer | −180,6 / −162,6 dBFS |
| harmonischen (ESS) tegen ideale saturatie op 16× | H3 en H5 binnen 1 dB | de opdracht van de Pluginbouwer | H3 −0,27 dB, H5 −0,68 dB |
| intermodulatie (SMPTE 60 Hz + 7 kHz) | binnen 1 dB van ideaal | de opdracht van de Pluginbouwer | 0,06 dB |
| aliasing, keten zoals je hem hoort (toon 3 kHz, 12 dB) | ≤ −60 dB onder de grondtoon | ruim onder de ruisvloer van een tape-echo **[RE-201-waarde niet bevestigd]** | −142,9 dB |
| stabiliteit (324 combinaties) | nooit NaN/Inf, piek ≤ 8 | saturatie houdt de lus onder 1/g | piek 5,04 |
| staart | onder −90 dBFS binnen de gemelde tijd | host mag na de staart stoppen met process | fb 0.3: 1,60 s ≤ 2,04 s; fb 0.8: 7,24 s ≤ 9,68 s; fb 0.95: 26,04 s ≤ 41,11 s |

Ter informatie (geen eis): aliasing van de saturatie los bij 5 kHz/12 dB: naïef −28,1 dB, ADAA
−33,5 dB; met de toon helemaal open −58,7 dB. Daar is 2× oversampling de
volgende stap als het hoorbaar is.

## Validators (CI, het log is het bewijs)
pluginval strictness 10 (AU en VST3; met `--rtcheck enabled` op macOS), auval -strict, clap-validator, RTSan met een
kanarie die moet falen, ASan/UBSan, null-test door het gebouwde binary (pluginval test en pb-render).
