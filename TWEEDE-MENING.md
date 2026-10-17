# Tweede mening: bandsaturatie met hysterese (CHOWTape) naast onze tanh

Vraag: wat mist ons tapeblok (`dsp/pb_tape.h`, tanh met ADAA-1) ten opzichte van een fysisch bandmodel? Als
vergelijking het Jiles-Atherton-model zoals CHOWTape het gebruikt (Chowdhury, DAFx 2019), in CHOWTape's
standaardstand (drive 0,5, saturation 0,5, bias 0,5).

- Code: `ref/tweede-mening/ja_tape.py` (eigen code naar de vergelijkingen in het artikel; geen code van CHOWTape, dat
  GPLv3 is) en `ref/tweede-mening/vergelijk.py`. Uitslag: `ref/tweede-mening/uitslag.json`.
- Methode: periodieke sinus (100 Hz, 1 kHz, 5 kHz) op −40 tot 0 dBFS, 8× overbemonsterd met FFT, inlopen over één
  periode, terug via een ideaal laagdoorlaat. Zo meet je het model, niet de aliasing.
- Niet gedaan: CHOWTape zelf bouwen en renderen (JUCE, een uur bouwen, en de rest van zijn keten — wow, verlies,
  ruis — staat er dan omheen). Dit vergelijkt alleen de saturatie.

## Uitslag (1 kHz; 100 Hz en 5 kHz wijken hooguit 0,2 dB en 0,3° af)

| ingang | JA: H3 | JA: fase | JA: versterking | tanh 6 dB: H3 | tanh 6 dB: versterking | tanh 12 dB: H3 |
|---|---|---|---|---|---|---|
| −40 dBFS | −65,4 dB | −0,2° | −2,74 dB | −89,6 dB | 0,00 dB | −77,6 dB |
| −24 dBFS | −50,0 dB | −0,8° | −2,65 dB | −57,6 dB | −0,03 dB | −45,7 dB |
| −18 dBFS | −44,7 dB | −1,4° | −2,57 dB | −45,7 dB | −0,14 dB | −34,1 dB |
| −12 dBFS | −39,3 dB | −2,6° | −2,42 dB | −34,1 dB | −0,52 dB | −23,5 dB |
| −6 dBFS | −31,7 dB | −4,4° | −2,29 dB | −23,5 dB | −1,81 dB | −15,5 dB |
| 0 dBFS | −22,7 dB | −5,4° | −2,70 dB | −15,5 dB | −5,04 dB | −11,3 dB |

Geen van beide maakt even harmonischen (beide symmetrisch; H2 onder −98 dB, rekenruis).

## Wat het zegt
1. **Vervorming bij zacht spelen.** Bij hysterese groeit H3 ongeveer 1 dB per dB ingang (de wet van Rayleigh); bij
   tanh 2 dB per dB. Gevolg: op −40 dBFS geeft de band nog −65 dB H3, onze tanh −90 dB. Bij hard spelen is het
   omgekeerd: tanh vervormt sterker. Het "korrelige" van band bij zachte passages heeft ons model niet.
2. **Geheugen.** De grondtoon loopt tot 5,4° achter, afhankelijk van het niveau (de hystereselus). tanh heeft geen
   geheugen: 0°. Per doorgang klein; in de terugkoppeling van een echo telt het per herhaling op.
3. **Compressie.** Het JA-model in deze stand comprimeert nauwelijks (versterking binnen 0,5 dB over 40 dB); onze tanh
   comprimeert vanaf −12 dBFS duidelijk. In de echo betekent dat: onze zelfoscillatie wordt harder begrensd.
4. **Frequentie.** In deze stand bijna frequentieonafhankelijk (H3 binnen 0,2 dB van 100 Hz tot 5 kHz).

## Besluit
Geen wijziging aan `dsp/`: de dub-echo is getoetst en klinkt zoals ontworpen, en "warm en begrensd" is wat SPEC.md
vraagt. Wel een bewuste beperking, nu met getallen: ons tapeblok mist hysterese (vervorming bij zachte niveaus en
niveau-afhankelijke fase). Een blok "Bandhysterese" (JA, RK4, ≥ 2× overbemonsterd; CPU niet gemeten) staat als kandidaat in TASKS.md "Bibliotheek later"; of het hoorbaar verschil maakt in
een dub-echo hoort een ABX-toets te beslissen, met Clay's oren, niet deze tabel.
