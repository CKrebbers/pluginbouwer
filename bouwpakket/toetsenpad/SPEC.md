# SPEC — Toetsenpad

Gegenereerd door de Pluginbouwer uit het ontwerp `toetsenpad`. De DSP staat
vast in `dsp/` (één bron voor browser en plugin); deze SPEC zegt wat de plugin eromheen moet doen en waaraan hij
getoetst wordt.

## Doel
Zelf spelen: twee zaagtanden een octaaf uit elkaar, een filter dat per noot opent, chorus en galm. Speel met a s d f g h j k l ; ' of met de knoppen onder Horen.

## Doelgeluid
**Geen referentieopname.** Een instrument uit de tool: de plugin toetst tegen zijn eigen ontwerp
(null-test, ook met noten), niet tegen een bestaand apparaat.

## Instrument
- **Geen audio-ingang.** Eén stereo-uitgang (en mono, fase 5). CLAP-features `instrument` en `synthesizer`, AU-type
  `aumu` (staat als `PB_AU_TYPE` in `dsp/ontwerpen/keten_toetsenpad.h`, met `PB_INSTRUMENT`). De keten heeft geen
  ingangsknoop; geef `verwerk` een stille invoer (of de nulbuffer van de host), het Ontwerp leest hem niet.
- **Noten.** Eén note port (ingang), alleen het CLAP-dialect (valkuil 19). Noot-aan en noot-uit (CLAP-noten, en
  MIDI-bytes 0x90/0x80 die de AU-wrapper doorgeeft) worden op hun sample de parameters **Noot** (index 0) en
  **Poort** (index 1): zie ARCHITECTUUR.md "Noten" voor de precieze regel. Die twee blijven ook gewone
  parameters (automatiseerbaar), maar een noot verandert de waarde die de host ziet niet.

## Keten (uitvoervolgorde per sample)
1. toetsen (Toetsen)
2. omh (Omhulling (ADSR)): poort ← toetsen.poort
3. o2 (Oscillator): toon ← toetsen.toon
4. o1 (Oscillator): toon ← toetsen.toon
5. som (Optellen): a ← o1.uit, b ← o2.uit
6. filter (Filter (SVF)): in ← som.uit
7. vca (Vermenigvuldig): a ← filter.laag, b ← omh.omhulling
8. chorus (Chorus / flanger): in ← vca.uit
9. mix (Mix): droog ← vca.uit, nat ← chorus.uit
10. galm (Galm): in ← mix.uit
11. mix2 (Mix): droog ← mix.uit, nat ← galm.uit
12. vol (Volume): in ← mix2.uit
13. uit (Uitgang): in ← vol.uit

Terugkoppeling: nee.

## Parameters (plugin-id = het nummer; nooit hergebruiken of hernummeren)
| id | naam | bereik | eenheid | standaard | glijtijd | bij draaien | wat het doet |
|---|---|---|---|---|---|---|---|
| 1 | Noot | 0.25 – 4 | × | 1 | 5 ms | glijden |  |
| 2 | Poort | 0 – 1 | — | 0 | 0 ms | glijden |  |
| 3 | Filter | 100 – 8000 | Hz | 900 | 20 ms | glijden |  |
| 4 | Loslaten | 10 – 4000 | ms | 450 | 20 ms | glijden |  |

- CLAP-parameters zijn waarden in hun eigen eenheid (min..max hierboven), niet 0..1; clap-wrapper normaliseert lineair
  voor VST3 en AU. Grenzen en standaarden zijn exact in float32, omdat een AU float doorgeeft: zo krijgt elk formaat
  bit voor bit dezelfde waarde.
- "glijden": de waarde schuift per sample naar het doel (een tape-echo laat dan de toon glijden; zo bedoeld).

## Tekst ↔ waarde (paneel, host, clap `value_to_text`/`text_to_value`)
| id | naam | label op het paneel | standaard als tekst | regel |
|---|---|---|---|---|
| 1 | Noot | Noot | 100 % | eenheidloos: × 100, hele procenten ("38 %"); boven 1 gaat het door ("125 %") |
| 2 | Poort | Poort | 0 % | eenheidloos: × 100, hele procenten ("38 %"); boven 1 gaat het door ("125 %") |
| 3 | Filter | Filter | 900 Hz | onder 999,5: hele Hz ("300 Hz"); vanaf 999,5 kHz met 2 decimalen ("1,00 kHz"), vanaf 9995 met 1 ("12,0 kHz") |
| 4 | Loslaten | Loslaten | 450 ms | onder 99,95: 1 decimaal ("20,0 ms"); vanaf 99,95 hele ms ("100 ms", "375 ms") |

- De grenzen gelden voor de afgeronde tekst: 999,7 Hz wordt "1,00 kHz" en 99,97 ms wordt "100 ms" (nooit "1000 Hz" of
  "100,0 ms"). Elke andere eenheid dan Hz, ms, dB of Q (ook `×` en geen eenheid) is eenheidloos: procenten.
- Rondreis: `text_to_value(value_to_text(v))` geeft een waarde die weer dezelfde tekst geeft; hij hoeft niet precies v te
  zijn, want de tekst is afgerond (de standaard 0,70703125 is "Q 0,71" en komt terug als 0,71).
- Decimaalteken een komma (Nederlands), spatie voor de eenheid. `text_to_value` neemt komma én punt, met of zonder
  eenheid ("375", "375 ms", "0,375 s" hoeft niet), "k" voor kHz, en voor eenheidloos procenten ("38", "38 %"). Buiten
  het bereik = klemmen, onleesbaar = false.
- Stapjes: geen; elke parameter is continu (geen CLAP_PARAM_IS_STEPPED).

## Budgetten
| wat | budget | geschat/gemeten |
|---|---|---|
| CPU per instantie | ≤ 5 % van één kern | 5,17 % (WASM-ketenmotor in Node, Intel(R) Xeon(R) Processor @ 2.10GHz, linux x64, Node v22.22.0; native is meestal sneller) |
| latency | 0 samples | 0 samples (fracties worden in de vertraging gecompenseerd) |
| staart | gemeld aan de host | 50 ms |
| geheugen | vooraf, nooit in process | vertragingslijnen op de hoogste samplerate van activate |

## Toleranties (per meting, met reden)
| meting | grens | reden | gemeten |
|---|---|---|---|
| null-test browser (WASM) vs gebouwd binary, per testsignaal | piek ≤ −90 dBFS | de opdracht van de Pluginbouwer; in de praktijk 0 (bit voor bit) door de regels van CLAUDE.md | bit voor bit (CLAP, VST3, AU; Linux en macOS) in de repo van de Pluginbouwer |
| noten (15 noten, legato, buiten bereik, dubbel) als CLAP-noten én als MIDI, blok 512 en wisselend | piek ≤ −90 dBFS | een noot hoort op zijn sample; een verschil is een noot op het verkeerde moment of de verkeerde toon | bit voor bit in de repo |


## Validators (CI, het log is het bewijs)
pluginval strictness 10 (AU en VST3; met `--rtcheck enabled` op macOS), auval -strict (type `aumu`), clap-validator, RTSan met een
kanarie die moet falen, null-test door het gebouwde binary (pluginval test en pb-render).
