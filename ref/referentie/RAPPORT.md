# De dub-echo naast een echte bandmachine

Bron: **Aalto Magnetic Tape Recorder Dataset**. Mikkonen, Wright en Välimäki, "Neural Modeling of Magnetic Tape
Recorders", DAFx 2023, Zenodo 8085482, licentie CC-BY-4.0.
- Machine: een Akai GX-4000D op 7,5 ips (Maxell-band) en op 3,75 ips (Scotch-band).
- Signaal: 44,1 kHz float32. Links staat de audio, rechts een pulstrein van 100 pulsen per seconde.
- Gebruikt: korte gefade sinussen (110 s), log-sweeps (310 s) en stilte. Per bestand hoort er een vertragingstraject bij.

Gemeten op 17 okt 2026 met `ref/referentie/meet.py`. De ruwe getallen staan in `ref/referentie/uitslag.json` (een kopie van `uit/uitslag.json`). Opnieuw meten:
- de data ophalen: `referentie.yml` met stap `subset`, als release-assets van `referentie-data`, niet in git;
- dan draaien: `python3 -I ref/referentie/meet.py <map> ref/referentie/uit`.

**Let op:** dit is een bandrecorder, geen tape-echo. Een RE-201 heeft een korte lus, een andere motor en eigen
koppen, en beweegt meestal méér. De Akai is dus een **ondergrens** voor beweging en vervorming, geen doel.

## 1. Beweging (wow en flutter)

### Hoe er gemeten is
- Elke puls van de pulstrein is in de opname teruggezocht, tot op een fractie van een sample. Daarvoor is per puls
  vergeleken met de gemiddelde pulsvorm, met een parabool door de beste drie posities.
- Het traject in de dataset zelf rondt af op hele samples. Dat geeft een ruis van 0,2 % in de snelheid, groter dan
  de flutter. Daarom is het niet gebruikt.
- De vertraging per puls (100 per s) geeft de snelheidsafwijking ε = −dd/dt, tussen 0,2 en 47,5 Hz.
- Controle op de methode: dezelfde meting op de digitale invoerpulsen geeft precies 0.

### De echte Akai

| | totaal RMS | piek | wow (0,2–6 Hz) RMS | wow-piek | flutter (6–47,5 Hz) RMS | flutter-piek |
|---|---|---|---|---|---|---|
| 7,5 ips Maxell (sinussen) | 0,151 % | 0,38 % | 0,083 % | 2,6 Hz | 0,127 % | 7,76 Hz |
| 7,5 ips Maxell (sweeps) | 0,136 % | 0,34 % | 0,089 % | 3,2 Hz | 0,103 % | 7,69 Hz |
| 3,75 ips Scotch (sinussen) | 0,150 % | 0,39 % | 0,079 % | 2,5 Hz | 0,125 % | 7,76 Hz |
| 3,75 ips Scotch (sweeps) | 0,140 % | 0,36 % | 0,082 % | 1,6 Hz | 0,113 % | 7,72 Hz |

Wat dit zegt:
- Op beide snelheden ongeveer 0,14–0,15 %.
- De flutter is een smalle piek op 7,7 Hz, de omwenteling van de capstan of de motor. Die piek is gelijk op 7,5 en
  3,75 ips, dus hij hoort bij de machine, niet bij de band.
- De wow ligt verspreid tussen 1,5 en 3 Hz.

### Ons model (`TapeBeweging`, gemeten op dezelfde manier: 100 per s, dezelfde band)

| | totaal RMS | wow RMS | flutter RMS |
|---|---|---|---|
| tijd 375 ms, haperen 0,25 (de standaard) | 0,32 % | 0,075 % | 0,32 % |
| tijd 375 ms, haperen 1 | 1,30 % | 0,30 % | 1,26 % |
| tijd 1200 ms, haperen 0,25 | 1,04 % | 0,24 % | 1,01 % |
| tijd 1200 ms, haperen 1 | 4,16 % | 0,96 % | 4,04 % |

Bevindingen:
1. **De standaard beweegt 2,2 × zoveel als de Akai.** Dat komt bijna helemaal door de flutter: 6,5 Hz tegen 7,7 Hz,
   en 2,5 × zo sterk. De wow is vergelijkbaar.
2. **Ons model schaalt met de delaytijd, de echte band niet.**
   - De dub-echo moduleert de vertraging zelf: d = T·(1 + haperen·b(t)). De toonhoogteafwijking is dan T·b′(t), dus
     een lange echo jankt meer dan een korte.
   - Bij een band hangt de afwijking af van de bandsnelheid: toonhoogte ≈ (1 + ε(t)) / (1 + ε(t − T)), los van T.
   - Bij 1200 ms is onze standaard 7 × de Akai.
3. Voorstel, niet gebouwd (het verandert de klank en de verwachte uitvoer van alle toetsen):
   - laat `TapeBeweging` een snelheid ε(t) geven, met een spectrum zoals hierboven;
   - leid de vertraging af uit de band die tussen de koppen ligt;
   - schaal "haperen" zo dat 0,25 ongeveer een Akai is en 1 een versleten tape-echo.
   - Eerst een ABX-toets of het verschil hoorbaar is. Staat in TASKS.md (Bibliotheek later).

## 2. Saturatie (harmonischen tegen het niveau)

### Hoe er gemeten is
- Gebruikt: stoten van een kwart seconde op een willekeurige toon (40 Hz – 7 kHz) en een willekeurig niveau.
- Vensters: 8192 samples onder 500 Hz, 2048 samples erboven.
- Gemeten per venster: H2, H3 en H5 ten opzichte van H1. Elke harmonische is gecorrigeerd voor de frequentierespons
  (zie §3) en telt alleen als hij 6 dB boven de ruis ligt.
- Tonen tussen 300 Hz en 4 kHz zijn samengenomen, met per bak van 2 dB invoerniveau de mediaan.
- Elk model heeft er één niveauverschuiving bij gefit (de ijking van de band is onbekend).
- Modellen: onze tanh (zonder ADAA) en Jiles-Atherton (`ref/tweede-mening/ja_tape.py`), op 1 kHz.

### H3 (dB onder H1) tegen het invoerniveau (dBFS)

| invoer | −26 | −24 | −22 | −20 | −18 | −16 | −14 | −12 | −10 | −8 |
|---|---|---|---|---|---|---|---|---|---|---|
| 7,5 ips Maxell | −56,6 | −58,3 | −59,5 | −58,3 | −55,1 | −53,4 | −50,0 | −46,4 | −42,2 | −37,9 |
| 3,75 ips Scotch | −46,4 | −44,7 | −44,0 | −42,7 | −40,7 | −41,3 | −32,4 | −29,2 | −23,0 | −20,0 |

| fit van H3 (300 Hz–4 kHz, 10 punten) | verschuiving | RMS-fout |
|---|---|---|
| 7,5 ips: tanh | −4,0 dB | 5,6 dB |
| 7,5 ips: Jiles-Atherton | −9,0 dB | **3,1 dB** |
| 3,75 ips: tanh | +4,0 dB | 4,0 dB |
| 3,75 ips: Jiles-Atherton | +6,75 dB | **3,0 dB** |

Hoe H3 verloopt:
- Boven ongeveer −18 dBFS stijgt H3 bij de echte band met ongeveer 2 dB per dB: 1,7 op 7,5 ips en 2,1 op 3,75 ips.
  Dat is de helling van tanh.
- Daaronder vlakt H3 af. Op 7,5 ips blijft hij rond −57 tot −60 dB. Tanh gaat daar door met 2 dB per dB; JA vlakt
  af tot ongeveer 1 dB per dB (Rayleigh).
- Daardoor past **Jiles-Atherton beter op beide banden**.
- Een deel van de vlakke onderkant kan ook van de elektronica komen. Met alleen deze opnames zijn die twee niet
  uit elkaar te halen.

### H2 (dB onder H1)
De echte band geeft een duidelijke tweede harmonische:
- 7,5 ips: −49 dB bij −34 dBFS invoer, oplopend tot −32 dB bij −8 dBFS;
- 3,75 ips: −48 tot −34 dB.

Onze tanh is symmetrisch en maakt geen H2. JA is in dit model ook symmetrisch (−116 dB of lager).
Mogelijke bronnen:
- asymmetrie in de bias;
- de record- of playback-elektronica.

Bij −20 dBFS ligt de H2 boven de H3 (−41 tegen −58 dB op 7,5 ips). Een kleine asymmetrie (een even term) zou dat
nadoen. Niet gebouwd: eerst ABX.

## 3. Frequentierespons en ruis

### Respons (dB ten opzichte van 1 kHz, uit de sweeps)

| Hz | 20 | 31 | 50 | 99 | 157 | 250 | 500 | 1k | 2k | 4k | 6,3k | 8k | 10k | 12,7k | 16k | 20k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 7,5 ips | −6,8 | −1,6 | −1,1 | −0,3 | +1,7 | +0,9 | +0,3 | 0 | −0,1 | −0,2 | +0,1 | −0,1 | +0,1 | −3,6 | −4,4 | −8,9 |
| 3,75 ips | −8,3 | −5,7 | −3,7 | −0,3 | 0,0 | −0,2 | −0,2 | 0 | +1,0 | +1,5 | −0,8 | −3,8 | −7,7 | −12,7 | −18,2 | −23,6 |

Wat dit zegt:
- **7,5 ips:** vlak (±1,7 dB) van 30 Hz tot 10 kHz, met een kopbult op 157 Hz.
- **3,75 ips:** een lichte bult rond 3–4 kHz, dan valt hij snel weg: −3,8 dB op 8 kHz, −18 dB op 16 kHz.
- **Onze lus** (toon 3 kHz standaard) is na één herhaling al donkerder dan 3,75 ips. Dat is bedoeld (een tape-echo
  wordt per herhaling donkerder), maar het is goed om te weten: één doorgang over de Akai is helderder.

### Ruis (stilte op band, de invoer is digitaal stil, −113 dBFS)
- **Totaal:** −61,4 dBFS RMS op 7,5 ips en −63,4 dBFS op 3,75 ips. De som van de tertsen geeft hetzelfde.
- **Laag:** brom op 50, 100 en 200 Hz (het net). Op 7,5 ips liggen de tertsen tot 160 Hz op −70 tot −75 dBFS.
- **Midden:** tussen 400 Hz en 2 kHz ongeveer −85 dBFS per terts.
- **Hoog:** de sis loopt op naar −78 tot −76 dBFS per terts boven 10 kHz.
- **Onze dub-echo:** maakt geen ruis. Een blok "bandruis" kan dit spectrum als doel nemen (Bibliotheek later).

## 4. Wat dit verandert

Nu (deze commit):
- Niets aan de DSP. Alle verwachte uitvoer en alle toetsen blijven zoals ze zijn.

Voorstellen (in TASKS.md, elk eerst met een ABX-toets):
- **Beweging:** naar snelheid (los van T) en de Akai als ijkpunt voor haperen 0,25.
- **Saturatie:** Jiles-Atherton, plus een kleine even term voor H2.
- **Bandruis:** als blok, met het spectrum hierboven.
