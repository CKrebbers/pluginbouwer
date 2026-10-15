# PROMPT — de opdracht (versie 2, 15 okt 2026)

Dit is de opdracht zoals Clay hem gaf, herschreven tot een diepere versie (zie het gesprek van 15 okt).
Wie aan deze repo werkt, toetst zijn werk hieraan. Wijzigingen in de opdracht komen hier, met datum.

```
PLUGINBOUWER — één dub-echo van ontwerp tot AU in Logic, met bewijs dat browser en plugin hetzelfde klinken

DOEL
Een tool zoals mijn Stackbouwer, voor audioplugins: ontwerpen uit bouwblokken, horen en meten in de browser, en een
bouwpakket exporteren waarmee een vers Claude Code-gesprek de plugin bouwt. Dieper, niet breder: één plugin moet de
hele keten echt doorlopen voordat de bibliotheek groeit. Eerste plugin: een dub-echo (tape-echo) met tapesaturatie en
een filter in de feedback, die zelf kan oscilleren zonder te ontploffen.

VOLGORDE (niet omdraaien)
A. Pijplijn eerst. Eén biquad gaat de hele weg: één DSP-bron → WASM in een AudioWorklet → AU (en VST3/CLAP) in CI →
   auval + pluginval + clap-validator + VST3-validator → null-test browser vs native binary. Pas als dit groen is, komt B.
B. Proefbouw van de stack. Bouw dezelfde biquad + fractionele delay in twee kandidaat-stacks (bijvoorbeeld Faust, en
   kale C++ → Emscripten + CLAP met clap-wrapper voor AU/VST3; kijk ook naar iPlug2 met zijn web-target en Cmajor).
   Kies op wat de null-test, CI en de licentie zeggen. Leg de keuze vast in CLAUDE.md met de meetresultaten.
C. Het doelgeluid vastleggen vóór het DSP (zie RAUW MATERIAAL). Pas dan de dub-echo bouwen.
D. De ontwerper: alleen de blokken die de dub-echo nodig heeft (delay met fractionele lezing, tape-saturatie, SVF-filter,
   wow/flutter, feedback, mix) plus de catalogusstructuur (BIB) waar later alles in past. De rest van de bibliotheek
   staat als lijst in TASKS.md, niet gebouwd.
E. Bouwpakket exporteren en de prompts toetsen.

EERST LEZEN
- CKrebbers/varve-site: stackbouwer-bron/ (CLAUDE.md, SIMULATIE.md, src/): één catalogus (BIB), standen Ontwerpen en
  Bouwen, readout bovenin, toets tegen een onafhankelijke referentie, export naar een extern programma.
- werkbank/ in dezelfde repo: hergebruik de engine. Lessen: draaiknoppen, modulatie door te slepen, gewone woorden.
- CKrebbers/youtube-mixer (VARVE): src/engine/fx.js en CLAUDE.md "Bekende grenzen". Daar staat al wat een echo fout doet:
  delaytijd op één leeskop laat de toon glijden; Q van een lowpass is in dB, dus in een lus kan de versterking > 1
  worden; effecten wisselen zonder klik. Neem over wat klopt. Let op: bij een TAPE-echo is glijden juist gewenst. De
  spec zegt per parameter: glijden (tape) of overvloeien (digitaal).

RAUW MATERIAAL (primaire bronnen, geen samenvattingen; per bron: URL, versie/tag, datum van lezen)
- Specs als code: Apple AudioUnitSDK + AudioUnitProperties.h (TailTime, Latency, state), Steinberg VST3 SDK met
  validator, free-audio/clap + clap-wrapper + clap-validator, de broncode van pluginval (welke tests draait strictness 10).
- Licenties: het LICENSE-bestand op een tag van elke repo (JUCE, iPlug2, Faust, Cmajor, nih-plug, DPF, clap-wrapper,
  VST3 SDK). Per framework: licentie, formaten (AU ja/nee), en of hetzelfde DSP in de browser kan. Controleer dit nu
  (okt 2026), niet uit je geheugen.
- DSP: RBJ-cookbook (originele tekst), Zavalishin "The Art of VA Filter Design" (TPT-SVF, stabiel onder modulatie),
  Laakso et al. 1996 (fractionele delay), Dattorro 1997 "Effect Design" 1 en 2, Parker et al. 2016 (ADAA),
  Välimäki/polyBLEP, Farina 2000 (exponential sine sweep).
- Tape: Jiles–Atherton-hysterese; Chowdhury DAFx 2019 + CHOWTapeModel (GPL: als meetreferentie, niet overnemen).
- Doelgeluid: servicehandboek/schema van de Roland RE-201 (kopafstanden, bandsnelheid, intensity-pad, EQ in de lus).
  Vraag mij om een eigen opname of een opname van een echte tape-echo als referentie; tot die er is, staat in SPEC.md
  "doelgeluid: nog niet vastgelegd".
- Markeer elk getal dat je niet kon bevestigen, en waar je zocht.

ONTWERPEISEN DIE EEN PROF STELT
- Determinisme (anders is −90 dBFS onhaalbaar): geen -ffast-math, -ffp-contract=off, eigen benaderingen voor
  sin/tanh/exp (geen libm), denormals expliciet, toeval (flutter) uit een geseede generator op gehele getallen,
  zelfde code in WASM en native.
- Real-time veilig: geen allocatie, lock of syscall op de audiothread; RTSan in CI.
- Host-robuust: blokgrootte 1/7/4096 wisselend, samplerate 44,1–192 kHz en wissel midden in, mono/stereo, bypass,
  TailTime gemeld (een echo heeft een staart), latency correct gemeld, state van versie N laadt in N+1, parameters
  sample-nauwkeurig en gladgestreken (glijtijd in de spec).
- Stabiliteit: bij elke combinatie van feedback, filter en saturatie nooit NaN/Inf, en zelfoscillatie begrensd door de
  saturatie, niet door een harde clip.

BEWIJS, LOS VAN JOU EN MIJ (alles in CI, het log is het bewijs)
- Lineair: biquad en SVF tegen hun formule, fout ≤ [0,1 dB] en ≤ [1°] tot 0,9·Nyquist.
- Fractionele delay: amplitude- en fasefout van de interpolatie tegen Laakso, per gekozen methode.
- Niet-lineair: tegen een ONAFHANKELIJK geschreven float64-referentie (Python/NumPy, andere code) met 16× oversampling:
  harmonischen per orde via exponential sine sweep, IMD met twee tonen, aliasing in dB onder het signaal op een hoge
  sinus. Grens per meting in SPEC.md, met een reden.
- Null-test browser vs native: sample-uitgelijnd, latency-gecompenseerd, piek en RMS van het verschil ≤ [−90 dBFS],
  per testsignaal. De native kant gaat door het GEBOUWDE binary (bijvoorbeeld Plugalyzer), niet alleen door de
  DSP-bibliotheek. Met feedback < 1 en vensters van ≤ [10 s]. Voor zelfoscillatie (chaotisch: 1 ULP groeit) een
  statistische toets: spectrum en omhullende binnen [1 dB].
- Validators: pluginval strictness [10], auval -strict (na killall -9 AudioComponentRegistrar), clap-validator,
  VST3-validator, RTSan/ASan/UBSan, matrix van blokgroottes en samplerates. GitHub Actions op macOS arm64.
- Tweede mening: hysterese naast CHOWTapeModel op dezelfde signalen; verschil gerapporteerd, geen eis.
- Luisteren: een blinde ABX in de tool tegen de referentieopname. Ik doe hem; de tool telt.
- De prompts: per fase 3 losse verse gesprekken met alleen het bouwpakket. Eis 3/3 bouwt en haalt de poorten. Loopt er
  één vast: classificeer de oorzaak, verbeter de prompt (geversioneerd), draai opnieuw. Nooit de code met de hand.
- Intuïtief: een taaktoets voor mij ("maak vanaf leeg een dub-echo die zelf oscilleert"), tijd tot eerste geluid en
  waar ik vastliep worden gelogd. Geen oordeel achteraf.

WAT DE TOOL KAN (tooltekst in het Nederlands)
1. Bibliotheek (BIB): elk blok met wat het doet, CPU, latency, aliasing en bouwmoeite. Die getallen komen uit de
   meetharnas op het echte DSP, niet uit de hand geschreven. Nu alleen de blokken van de dub-echo; de rest als lijst.
2. Keten tekenen: blokken slepen, kabels leggen, draaiknoppen, modulatie door te slepen, pijltjes bij de laatste wijziging.
3. Horen: live in een AudioWorklet op dezelfde WASM die de plugin gebruikt, op mijn audio of testsignalen; mockup van het
   frontpaneel.
4. Meten: frequentierespons, harmonischen, aliasing, latency en CPU per blok, live, met dezelfde code als de CI-toets.
5. Verken: varianten, archetypes (eerst alleen dub-echo, veerreverb en sidechain-pomp als voorbeelden), schetsboek.
6. Bouwen-stand: framework en formaten met moeite, kosten, risico's en wat ik moet leren, gevuld met de uitkomst van B.
7. Export van het bouwpakket als bestanden.

MIJN SITUATIE
- MacBook Air M1 met Logic Pro: AU verplicht, arm64. Omarchy (Arch): VST3 en CLAP. AUv3 later.
- Ik programmeer geen C++. Wat ik zelf moet doen of leren staat apart (leerpad zoals in de Stackbouwer).
- Kostenbewust. Laat per kostenpost het gratis alternatief zien. Ik weet er al deze, controleer ze:
  - ad-hoc signeren voor eigen gebruik (geen Apple Developer nodig) en de quarantine-vlag van een download uit CI;
  - notarisatie en AUv3 op een eigen toestel kosten wel geld;
  - macOS-minuten op een privérepo tellen zwaarder dan Linux; een publieke repo is gratis.

HET BOUWPAKKET
- SPEC.md: doel, doelgeluid (referentie), parameters met bereik, eenheid, verloop, standaard, glijtijd en "glijden of
  overvloeien"; CPU- en latencybudget; tail time; de toleranties per meting met reden.
- ARCHITECTUUR.md: keten, threads, lock-vrije parameteroverdracht, state en versies, presets, determinismeregels.
- design.md, CLAUDE.md, TASKS.md.
- Testsignalen met verwachte uitvoer (WAV/CSV) uit de float64-referentie, plus het script dat ze maakt.
- PROMPTS.md: per fase één prompt met stopcriterium, de CI-poorten die groen moeten zijn, en de pass@3-uitslag:
  0 skelet, CI en alle validators · 1 DSP · 2 parameters, automatisering, state · 3 UI · 4 presets ·
  5 formaten AU/VST3/CLAP · 6 ad-hoc signeren en installeren (betaald signeren alleen als optie met kosten) ·
  7 test in Logic (checklist voor mij: laden, automatiseren, bouncen met staart, project opnieuw openen).

STOPCRITERIUM
- A en B groen: één biquad nult browser vs binary ≤ −90 dBFS, alle validators groen in CI, stackkeuze met metingen.
- De dub-echo ontworpen en gemeten in de tool, met het doelgeluid vastgelegd (of expliciet "nog niet").
- Bouwpakket geëxporteerd; fase 0 en 1 gebouwd door verse gesprekken met alleen die prompts, 3/3.
- De AU haalt auval -strict en pluginval [10] in CI; RTSan schoon.
- Null-test en de statistische zelfoscillatietoets binnen de tolerantie.
- Ik laad hem in Logic en de staart blijft staan bij een bounce.

WERKWIJZE
- Repo CKrebbers/pluginbouwer; lukt dat niet, een branch in varve-site.
- Eerst CLAUDE.md, design.md, TASKS.md. Een GitHub-checkpoint vóór elke grote stap (A, B, C, D, E).
- De tool als artifact én als losse pagina die buiten claude.ai werkt.
- Geen gradiënten, glassmorphism, Inter of ronde kaartjes.
- Bouw zonder te vragen. Stop alleen vóór iets onomkeerbaars of iets wat geld kost.
- Sluit af met: Wacht op mij · Veranderd · Gevonden · Staat alleen in Claude?
```
