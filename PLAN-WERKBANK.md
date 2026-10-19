# Plan: dieper experimenteren en ontwikkelen (19 okt 2026)

Clay vroeg: "veel dieper maken en op een veel technischere manier kunnen experimenteren en ontwikkelen", met
experimenteren als het belangrijkste. Hieronder het plan om dat in het bestaande systeem te bouwen, de onderdelen die al
bestaan en hergebruikt worden (§2), en wat er van buiten bruikbaar is, met de licentie nagekeken (§8).

## 1. Wat "experimenteren" hier betekent, en waarom wij het beter kunnen dan elders
Een experiment is een vraag over klank of techniek die je stelt, uitvoert, meet, beluistert en bewaart. Bijvoorbeeld:
"klinkt de ladder in de terugkoppeling beter dan de svf?" of "hoeveel aliasing kost de vouwer per vouwfactor?". Over een
maand moet het nog precies hetzelfde antwoord geven.

Dat laatste is ons voordeel. De motor rekent deterministisch (regel 2) en onafhankelijk van de blokgrootte (regel 3).
Een experiment geeft daarom elke keer, op elke machine, dezelfde getallen, bit voor bit. In Max, Jupyter of een DAW lukt
dat niet. Een experiment wordt zo een bestand in git: een wijziging aan `dsp/` die het antwoord verandert, ziet CI.

Vier lagen, elk bovenop wat er al staat. De vijf regels blijven, en elke laag eindigt met een toets in CI:

| laag | wat je ermee kunt | nieuw |
|---|---|---|
| X0 experimenten als bestanden | een vraag als bestand: ontwerp(en), signaal, raster van knopstanden, metingen → uitslag in git, opnieuw te draaien in CI | runner + formaat |
| X1 sondes en meetlab | in élke kabel van een ontwerp kijken: scope, spectrum, niveau, verschil A−B; live en in experimenten | 2 exports, Lab-tab |
| X2 het codeblok | een eigen blok in een paar regels code, binnen een halve seconde te horen, en zonder C++ mee in de plugin | taal, compiler (JS), tolk (C++) |
| X4 de werkbank-plugin | het ontwerp live in Logic, zonder bouwen: de tool stuurt, de plugin speelt | runtime-ontwerp, brug |

X3 (bouwstenen van buiten, §8) loopt vanaf X2 mee: één bouwsteen per ronde.

## 2. Wat er al is, en welke laag het gebruikt
| onderdeel | waar | gebruikt door |
|---|---|---|
| ketenmotor: 30 blokken, ontwerp als rij doubles, terugkoppeling via z⁻¹ | `dsp/pb_graaf.h` | alles; X1 leest `uitw`, X2 is een 31e blok, X4 laadt de rij tijdens het spelen |
| rij met ruimte achteraan: `leesBeschrijving` geeft het aantal gelezen getallen terug, de rest wordt niet gelezen | `dsp/pb_graaf.h` | X2: het codesegment staat achter de rij, oude rijen blijven bit voor bit gelijk |
| WASM-motor met `pg_*`-exports, `pg_knoop_uit` (één sample, kanaal 0) | `dsp/pb_graaf_wasm.cpp` | X1 breidt dit uit tot een buffer per sample |
| worklet met twee ketens A/B en 20 ms overvloeien, foutmeldingen terug | `tool/src/worklet.js` | X1 (sondedata terug), X2 (opnieuw compileren = keten in B, overvloeien), live A/B van varianten |
| meetbibliotheek: aliasing, harmonischen, ESS, respons, inloop, readout, spectrum, instrument-readout | `tool/src/meten.js` | X0 (de metingen van een experiment), X1 (dezelfde metingen op een sonde) |
| varianten met zaad en wildheid | `tool/src/verken.js` | X0 (raster of willekeurige verkenning met zaad) |
| tabs Horen, Meten, Verken, Schetsboek, ABX, Taaktoets | `tool/src/app.js` | X0 (de varianten van een uitslag openen in Schetsboek, dan ABX), X1 (tab Lab erbij) |
| signalen: sinus, impuls, ruis, sweep, stukjes; noten als events; hosttempo | `tool/src/signalen.js`, `testsignalen.js` | X0 |
| bit-exacte exp/sin/… in JS | `tool/src/wiskunde.js` | X2 (referentie per opcode in de toets) |
| render in Node, WAV lezen en schrijven, null-test, statistische null-test | `tests/lib/keten.mjs`, `tests/lib/pb.mjs`, `tools/null.mjs` | X0 (de runner), X2 en X4 (bewijs) |
| CPU per blok op de echte motor | `tools/meet-blokken.mjs` → `meting-blokken.json` | X2 (kosten van een codeblok per instructie) |
| float64-referentie en de Akai-meting | `ref/dub_echo_ref.py`, `ref/referentie/` (RAPPORT.md) | X0 (afstand tot de echte band als meting), X3 (orakel) |
| native hosts: CLAP-render, AudioToolbox-proef | `host/pb-render.cpp`, `host/au-proef.cpp` | X0 `--native`, X4 (de werkbank in CI) |
| van ontwerp naar header naar plugin | `tool/src/codegen.js`, `tools/keten-header.mjs`, `dsp/graaf_ontwerp.h` | X2 (codeblok mee in de plugin), X4 (`werkbank_ontwerp.h` naast `graaf_ontwerp.h`) |
| CI: null-test, blokgrootte, validators, RTSan | `.github/workflows/ci.yml` | elke laag eindigt hier |

## 3. X0 — experimenten als bestanden (eerst: dit vraagt niets nieuws van de motor)
**Formaat.** Eén bestand per vraag, `experimenten/<naam>.mjs`, dat een gewoon object exporteert:
```js
export default {
  vraag: 'Hoeveel aliasing kost de vouwer zonder oversampling, per vouwfactor?',
  ontwerp: 'voorbeeld:vouwer',            // of een ontwerp-object (hetzelfde JSON als de tool en het schetsboek)
  signaal: { soort: 'sinus', f: 2000, amp: 0.5, sec: 2 },
  raster: { 'vouwer.vouw': [1, 2, 4, 8] }, // of verken: { zaad: 7, wildheid: 'wild', aantal: 12 }
  meet: ['aliasing', 'harmonischen', 'niveau'],
  sondes: ['vouwer'],                      // na X1: meten op een knoop binnen het ontwerp
  verwacht: { aliasing: { max: -60 } },    // optioneel: dan is het ook een regressietoets
};
```
**Runner.** `node tools/experiment.mjs <naam>` doet het volgende:
- compileren met `keten.js`;
- per rastercel renderen met `renderKeten` op de WASM-motor in Node;
- meten met `meten.js`;
- schrijven naar `experimenten/uitslag/<naam>.json` (de getallen, in git) en `experimenten/uitslag/<naam>.md` (tabel en
  conclusie).

Daarnaast schrijft hij WAV's per cel naar `uit/` (niet in git).

`--native` rendert dezelfde cellen door het gebouwde `.clap` (`pb-render`) en zet de null-test erbij. `--tool` schrijft
de varianten als schetsboek-bestand, dat de tool opent; daarna kan Clay ze in de tab ABX blind vergelijken.

**CI.** `node tools/experiment.mjs --alle --controleer` draait alles opnieuw en vergelijkt met de uitslag in git. Getallen
moeten bit voor bit gelijk zijn; CPU-tijd telt niet mee. Verandert een wijziging aan `dsp/` een antwoord, dan faalt CI met
het verschil. Wie het nieuwe antwoord wil, schrijft de uitslag opnieuw en commit hem: de diff is dan het verslag.

**De eerste drie experimenten** komen uit vragen die al open staan:
1. **Aliasing per vervormer.** Tape, vouwer en vergruizer, per frequentie en niveau. Dit toetst wat ADAA belooft (CLAUDE.md)
   en is de nulmeting voor oversampling (§8).
2. **De dub-echo tegen de Akai.** De afstand per knopstand tot de metingen van `ref/referentie` (RAPPORT.md): welke knop
   brengt hem het dichtst bij de band.
3. **Samplerate.** Hetzelfde ontwerp op 44,1, 48 en 96 kHz, als respons en als harmonischen. Klinkt een ontwerp op 96 kHz
   nog zoals op 48? Een host kiest dat, niet wij.

**Stop.** De drie experimenten draaien in CI en hun uitslag is reproduceerbaar. Een opzettelijke wijziging in één blok
laat CI falen op het juiste experiment, met het juiste getal.

## 4. X1 — sondes en het meetlab
**Motor.** `pg_knoop_uit` geeft nu één sample van kanaal 0. Erbij komen twee exports:
- `pg_sonde(h, i, knoop, poort, kanaal)`: sonde i op een uitgang, of −1 = uit; hooguit 8 sondes;
- `pg_sonde_buf(h, i)`: een Float32-buffer met per sample de waarde van de laatste `pg_verwerk`.

De kopie gebeurt aan het eind van elke sample in `Graaf::verwerk`, achter `#ifdef PB_SONDES`. Alleen de WASM-bouw zet
die vlag: de plugin-code blijft letterlijk dezelfde. Het geheugen is vast, naast `inBuf`/`uitBuf`, dus er wordt niets
gealloceerd (regel 4).

**Bewijs** (`tests/sonde.test.mjs`):
- met sondes aan is de uitvoer bit voor bit gelijk aan zonder;
- een sonde op de knoop `uit` is de uitvoer;
- een sonde op een versterker is ingang × gain;
- ook in een terugkoppellus: de sonde ziet de waarde van déze sample, niet de z⁻¹-kopie.

**Worklet.** Per blok worden de sondebuffers in een ring gekopieerd. Elke ~1024 samples gaat er een bericht met
overgedragen buffers. SharedArrayBuffer vraagt COOP/COEP-headers, en die zijn er niet op `file://` en niet in het
artifact; berichten werken overal.

**Tab Lab.** Klik op een knoop of kabel in het ontwerp en er staat een sonde op. Te zien:
- scope met trigger op de stijgende nuldoorgang, en vast in de tijd voor transiënten;
- spectrum via `fft.js` en `gemiddeldSpectrum`;
- per knoop piek, RMS, DC en een teller voor NaN, inf en subnormaal (vangt de ontploffende lus vóór je hem hoort, zoals
  bij de veergalm);
- XY (L tegen R) voor stereo;
- een verschilscope A − B: de live null-test tussen twee varianten. Je hoort en ziet wat een knop echt verandert.

Tekenen gebeurt op een eigen canvas: de tool heeft nul afhankelijkheden, en lijnen tekenen is weinig code (§8: uPlot is
de reserve).

**Offline.** `renderKeten(…, { sondes })` geeft de sondes mee terug. Een experiment kan dan zeggen "meet het spectrum van
de terugkoppeling na de tape" in plaats van alleen de uitgang.

**Stop.** In Chrome toont een sonde op elk blok van elk voorbeeld een levend signaal (headless: `tools/headless/tool.mjs`
eist het). De bit-gelijkheid is groen in CI.

## 5. X2 — het codeblok
Het belangrijkste stuk voor dieper ontwikkelen: een nieuw idee hoeft niet meer langs een C++-blok, een nieuwe WASM en
een release. Het staat als code in het ontwerp, je hoort het meteen, en het gaat zo mee in de plugin.

**De taal** is klein, per sample, zonder lussen. Zo is hij constant in tijd en real-time veilig. Hij lijkt op gen~ codebox
en JSFX `@sample` (EEL2), die alleen als ontwerpvoorbeeld dienen. EEL2 zelf inbouwen kost meer dan een eigen tolk (§8).
Voorbeeld:
```
knop vouw 1 20 4          # knop 1: naam, min, max, standaard (moduleerbaar zoals elke knop)
staat fase = 0            # blijft tussen samples bestaan
lijn d 4800               # een vertragingslijn van 4800 samples (geheugen gereserveerd bij activeren)
y = sin(in1 * vouw) / vouw
d.schrijf(y)
uit1 = y + 0.3 * d.lees(2400.5)    # lees met interpolatie
uit2 = vorig(y)           # één sample terug
```
Ingangen en uitgangen zijn `in1`–`in2` en `uit1`–`uit3`, knoppen `knop` 1–4: precies de grenzen van een knoop (`MAX_IN`,
`MAX_UIT`, `MAX_KP`). Een codeblok is dus overal aan te sluiten en te moduleren zoals een gewoon blok.

De functies zijn sin, cos, tan, exp, log, tanh, sqrt, abs, min, max, klem, vloer, teken en rond, allemaal uit `pb_math.h`.
Daarbij komen `+ − × ÷ %`, vergelijkingen en `a ? b : c`.

`herhaal N` met een vaste N wordt bij het compileren uitgeschreven. Zo kan een kleine filterbank zonder echte lus.

**Compiler** in `tool/src/code.js`: van tekst naar een vaste reeks instructies met registers. Fouten krijgen regel en
kolom en staan in het knooppaneel. Hooguit 512 instructies, 64 staat-variabelen en 4 lijnen per blok.

**Rij.** Het codesegment komt achter de bestaande rij. De knoop krijgt soort `code` met `vast[0]` = begin en `vast[1]` =
lengte van zijn segment. Een rij zonder codeblok is ongewijzigd, dus oude rijen klinken bit voor bit hetzelfde: dezelfde
regel als de uitbreidingen van 15 en 17 okt.

**Tolk** in `dsp/pb_code.h`: één `switch` per instructie over een vaste registerbank; lijnen in het geheugen dat
`graafGeheugen` al reserveert. Delen door nul geeft 0, zodat het overal hetzelfde is en geen inf de lus in gaat. De
uitgang gaat zoals altijd door `pb::uitvoer` (NaN en ±8 worden stilte).

De volgorde van de bewerkingen ligt vast in de instructies en `-ffp-contract=off` staat al aan. Een codeblok is dus in
WASM en native bit voor bit gelijk, en kan **door het bouwpakket mee in de plugin, met de null-test intact**. Wat je in de
browser typt, klinkt zo in Logic.

**Snelheid** wordt gemeten, niet beloofd. `meet-blokken.mjs` krijgt codeblokken van 10, 50 en 200 instructies; de tool
toont de kosten per blok zoals nu. Is een codeblok te traag, dan **promoveert** het: codegen schrijft het als C++ in
dezelfde volgorde. De toets eist dat de C++ bit voor bit gelijk is aan de tolk, en daarna mag het een gewoon blok worden.

**Tool.** Het knooppaneel van een codeblok is een tekstvak met regelnummers, zonder editor-afhankelijkheid. 300 ms na het
typen volgt opnieuw compileren, dan gaat de keten naar slot B en vloeit 20 ms over: horen terwijl je typt. `sonde y` in
de code zet een sonde (X1) op een tussenwaarde.

**Bewijs** (`tests/code.test.mjs`):
- elke instructie tegen `wiskunde.js` (bit-exact);
- parserfouten met de juiste plek;
- fuzz: 10.000 willekeurige programma's met zaad geven nooit NaN uit de motor, crashen nooit en zijn blokgrootte-
  onafhankelijk;
- WASM tegen native bit voor bit (`native.test.mjs` krijgt de codevoorbeelden);
- een codeblok dat een bestaand blok nadoet (een eenpolig laagdoorlaat) is bit voor bit dat blok;
- RTSan op de plugin met een codeblok.

**Stop.** Clay typt in de tool een golfvormer, een eenpolig filter en een LFO, en hoort elk binnen een halve seconde.
Een van de drie gaat als plugin door het pakket, met de null-test groen.

## 6. X4 — de werkbank-plugin (na de Logic-proef)
Eén plugin die elk ontwerp speelt zonder opnieuw te bouwen. De tool stuurt de rij, de plugin speelt hem in Logic, met de
automatie, de opnames en de rest van de mix van Clay.

**Runtime-ontwerp** in `dsp/werkbank_ontwerp.h`, naast `graaf_ontwerp.h` (dat de rij als constante krijgt):
- een vaste lijst van **16 macro's**, omdat een DAW een vaste parameterlijst wil. Parameter i van het ontwerp is macro i,
  en de rest staat stil. Naam en bereik volgen het ontwerp alleen in het paneel; de host ziet "Macro 1–16".
- **twee motoren (A/B)** zoals in de worklet. Een nieuwe rij wordt buiten de audiothread klaargezet in de vrije motor
  (inlezen, geheugen uit een pool die in `activate` is gereserveerd voor het grootste ontwerp dat de motor toestaat). Een
  atomaire vlag laat de audiothread op een blokgrens wisselen met 20 ms overvloeien: geen allocatie, geen lock (regel 4).
  Past een rij niet in de pool, dan wordt hij geweigerd met een reden, en de oude blijft spelen.
- **de rij in de state.** Een Logic-project dat opnieuw opent, speelt hetzelfde experiment.

**Hoe de rij in de plugin komt: een brug, geen netwerk in de plugin.** Met `node tools/werkbank-brug.mjs` op de Mac:
- de brug serveert de tool zelf op `http://127.0.0.1:<poort>/`; dezelfde oorsprong, dus geen CORS en geen
  privénetwerk-regels van Chrome (§8);
- "naar werkbank" in de tool doet een POST naar de brug;
- de brug schrijft de rij atomisch naar `~/Library/Application Support/Pluginbouwer/werkbank/<naam>.json`;
- de plugin kijkt op een eigen draad elke 200 ms naar dat bestand, nooit op de audiothread.

Waarom zo:
- de plugin opent geen poort, dus geen botsing tussen twee instanties en geen firewallmelding;
- de map valt niet onder de privacybescherming van macOS: Bureaublad, Documenten en Downloads vragen toestemming;
- alles is in CI te toetsen met Node en een bestand.

Meerdere instanties kiezen elk een naam (`werkbank 1`, …) in hun paneel. Later kan de brug ook de andere kant op, met
meters uit de plugin naar de tool, maar dat is niet nodig voor de eerste versie.

**Bewijs** (CI):
- `pb-render --werkbank <rij.json>` speelt elk voorbeeld (17 plus de codevoorbeelden) bit voor bit zoals de WASM;
- een wissel tussen twee rijen tijdens het spelen geeft geen allocatie (RTSan) en het overvloeien is gemeten;
- state-rondje met de rij erin;
- pluginval, clap-validator en auval zoals de andere plugins;
- de brug: POST, het bestand, en weigering van een kapotte rij.

**Afhankelijk van** stap 1 van het plan van 18 okt (de AU in Logic). Zonder die meting bouwen we hier op een aanname.

**Stop.** Clay draait een knop in de tool en hoort het in Logic. Een nieuw codeblok gaat erheen en speelt binnen een
seconde. Het project opnieuw openen geeft hetzelfde geluid.

## 7. Volgorde, en waarom zo
1. **X0** (een paar dagen werk, geen risico): het formaat, de runner, de drie experimenten, CI. Dit gebruikt alleen wat er
   al is.
2. **X1**: twee exports en de Lab-tab. Daarna kan elk experiment binnen een ontwerp meten.
3. **X2**: het grootste stuk en de grootste winst. Na X1, zodat de tolk vanaf de eerste dag met sondes te debuggen is.
4. **X3**: één bouwsteen per ronde (§8), te beginnen met wat X0 nodig blijkt te hebben. Experiment 1 zegt of
   oversampling nodig is; zo ja, dan eerst halfband-oversampling.
5. **X4**: pas na de Logic-proef. Het is de enige laag die afhangt van iets wat Clay doet, en van macOS-gedrag dat we
   alleen in CI zien.

Experimenteren komt zo vóór de werkbank: X0–X2 werken in de tool en in Node, overal, zonder Logic.

## 8. Onderdelen van buiten (licenties gelezen in de bestanden zelf, 19 okt 2026)
Werkwijze: van elke repo de standaardtak ondiep gekloond, LICENSE en de kop per bestand gelezen, en libm-aanroepen
geteld. "Niet nagekeken" staat erbij waar iets niet uit de code kwam.

**Steengroeves voor algoritmes (X3: overzetten op `pb_math`, eerst als codeblok)**

| bron | licentie | waarvoor | wat er aangepast moet worden |
|---|---|---|---|
| Airwindows (airwindows/airwindows, 17 okt 2026) | MIT | ~500 effecten, per sample, kort; o.a. bandsimulaties: een tweede kandidaat naast de Akai (experiment 2) | coëfficiënten per blok zonder smoothing (blokgrootte-afhankelijk), dither-zaad uit `rand()`, ruisdither op de uitgang: alle drie eruit; veel `pow`/`sin` |
| Mutable Instruments (pichenettes/eurorack, STM32-modules + stmlib) | MIT (AVR-modules GPL, `stmlib/third_party` en `ui/event_queue.h` niet) | Rings/Elements (modale resonator, fysische modellen), Plaits (oscillatoren), Clouds (granulair) | rekent in blokjes van 12–24 met interpolatie over het blok: een vaste interne blokgrootte (met latentie) of per sample herschrijven; tabellen uit `resources/` |
| hiir (Laurent de Soras) | WTFPL v2 (sinds v1.11) | halfband-oversampling en Hilbert, zonder libm in het rekenpad | alleen het ontwerp van de coëfficiënten gebruikt libm: offline in `ref/` uitrekenen en als constanten vastleggen |
| Signalsmith DSP (signalsmith-audio/dsp) | MIT | interpolerende vertragingen, biquad-ontwerp, envelopes; vensters voor meten | `std::vector` alleen bij instellen; `std::complex` en enkele `cos`/`sqrt` |
| Q (cycfi/q) | MIT | toonhoogtedetectie, envelope-volgers, dynamiek | weinig libm; ringbuffers bij aanmaken |
| STK (thestk/stk) | MIT-achtig (verzoek om wijzigingen terug te sturen, niet bindend) | fysische modellen, klassieke filters | sommige klassen alloceren bij aanmaken |
| DaisySP (electro-smith/DaisySP), **alleen het MIT-deel** | MIT; de uit Csound afgeleide modules staan in DaisySP-LGPL (moogladder, reverbsc, comb, pluck, …): **niet** | eenvoudige synth- en effectbouwstenen | per bestand een toegestane lijst; `rand()` in snare/hihat/smooth_random vervangen |
| Cmajor-standaardbibliotheek (`standard_library/*.cmajor`) | ISC (de engine zelf GPLv3: niet) | algoritmes lezen | herschrijven |
| CloudSeedCore, el-visio/dattorro-verb | MIT | galm (FDN, Dattorro-plaat) | nog niet in detail gelezen |
| Gamma, madronalib | MIT | oscillatoren, filters | nog niet in detail gelezen |
| mhetrick/nonlinearcircuits | CC0 | chaotische modules | nog niet in detail gelezen |
| Faust-bibliotheken | **per functie**: veel STK/MIT, maar compressors.lib en delen van reverbs.lib GPL-3.0 en AGPL | alleen lezen, per functie `declare license` nakijken | — |

**Valkuilen, niet gebruiken**:
- chowdsp_utils: de DSP-modules (filters, waveshapers met ADAA, reverb, compressor) zijn GPLv3; alleen `math` e.d. is BSD.
- Surge `sst-basic-blocks`/`sst-filters`/`sst-effects`: GPLv3.
- DaisySP-LGPL.
- Soundpipe: MIT-bestand, maar 48 modules "uit Csound" (LGPL).
- de Cmajor-engine en de pluginlaag van ysfx (AGPL).
- de JSFX-implementatie van Cockos (LGPL, niet nagekeken).
- Valley Plateau (GPL, niet nagekeken).

**Ontwerpvoorbeelden (niets overnemen, wel leren)**
- **EEL2/JSFX** (WDL, zlib): het beste voorbeeld voor de taal van het codeblok. Het zelf inbouwen is mogelijk (er is een
  draagbare tolk, `EEL_TARGET_PORTABLE`), maar die is ~8× trager dan zijn eigen JIT. Hij reserveert zijn geheugen lui
  met `calloc` bij de eerste toegang, dus niet real-time veilig. Ook rekent hij met de libm-tabel. Patchen kost meer dan
  een eigen tolk van een paar honderd regels die meteen aan de vijf regels voldoet. **Besluit: eigen tolk, met de semantiek
  van EEL2 als voorbeeld** (alles is een double, geen lussen per sample).
- **Elementary + SRVB** (MIT): een JS-graaf die als instructies naar een C++-runtime gaat, in WASM en native, met
  herladen vanaf localhost in de plugin. Het dichtst bij X4, maar niet bit-exact.
- **WebCLAP/WCLAP** (MIT): CLAP-plugins als WASM, plus headers voor een host. Dezelfde WASM in browser én DAW zou de
  null-test triviaal maken. Bestuderen voor na X4; hoe ver de native brug is, is niet nagekeken.
- **ysfx** (bibliotheek Apache-2.0): hoe je EEL2 in een plugin hangt.

**Gereedschap**
- **cpp-httplib** (MIT, één header, sinds v0.62 met WebSocket): reserve als de brug ooit ín de plugin moet; dan op een
  eigen draad, met een lock-vrije rij naar de audiothread.
- **uPlot** (MIT, 73,7 kB, 31 kB gzip): reserve voor de Lab-tab als eigen canvas te veel code wordt. Hij is in te
  bakken zoals de WASM.
- **pocketfft** (BSD-3), **r8brain** (MIT), **dr_wav** (Unlicense/MIT-0): alleen voor metingen of referenties; we
  hebben eigen FFT en WAV-code, dus voorlopig niet nodig.

**Chrome en localhost** (spec WICG/local-network-access, gelezen 19 okt):
- PNA is nooit uitgerold. Sinds Chrome 142 geldt Local Network Access: een publieke pagina die 127.0.0.1 aanroept
  krijgt een toestemmingsvraag, ook voor een WebSocket, en een iframe alleen als de ouder het toestaat.
- Vanuit het claude.ai-artifact is de werkbank dus vrijwel zeker onbereikbaar (niet in een browser nagekeken).
- `file://` geldt als lokaal, maar vraagt CORS (oorsprong `null`).
- De brug van §6 serveert de tool zelf op 127.0.0.1. Dezelfde oorsprong, dus geen van die regels geldt; dat bevestigt
  de keuze.

**De eerste bouwstenen in X3** (volgorde naar wat de experimenten vragen):
1. hiir-oversampling, als experiment 1 aliasing laat zien die je hoort;
2. twee Airwindows-bandsimulaties als codeblok-voorbeeld, gemeten tegen de Akai in experiment 2;
3. de modale resonator van Rings (fysisch model, ontbreekt in de BIB);
4. interpolerende vertraging uit Signalsmith voor modulatie zonder ritsel;
5. een galm uit CloudSeed of Dattorro naast onze eigen.

## 9. Regels voor wat er van buiten komt
- **Overzetten, nooit koppelen.** Code van buiten gaat `dsp/` in, herschreven op `pb_math.h` (regel 1 en 2). Het origineel
  blijft het orakel: native gecompileerd met libm in `ref/`, met een grens met reden; niet bit voor bit, want libm rekent
  anders.
- **Eerst als codeblok, dan als C++.** Een algoritme van buiten wordt eerst een codeblok-voorbeeld: snel te proberen, en
  het experiment zegt of het iets toevoegt. Pas wat blijft promoveert naar C++, met het bewijs dat het gelijk is.
- **Licentie.** Alleen MIT, BSD, Apache 2.0, zlib, ISC, publiek domein of vergelijkbaar. Geen GPL of LGPL, ook niet "even
  om te proberen" (CLAUDE.md, KEUZE-STACK.md). Elke overgezette bron komt in `BRONNEN.md` (repo, commit, licentie,
  bestand). Het bouwpakket krijgt een `LICENTIES.md` met de verplichte vermeldingen, omdat MIT en BSD die ook in een
  binary eisen.
- **Eén bouwsteen per ronde**, met een experiment (X0) dat zegt wat hij oplevert.

## 10. Kosten en risico's
- **Kosten**: niets. Alles is open source met een vrije licentie. CI draait op Linux; macOS alleen voor X4, zoals nu.
- **Risico X2**: de tolk wordt te traag voor grote programma's. Opvang: meten vanaf de eerste versie, promoveren naar C++.
- **Risico X4**: Logic laadt de AU niet zoals we denken (open meting), of een wissel tijdens het spelen klikt. Opvang: X4
  pas na de Logic-proef, en het overvloeien wordt in CI gemeten.
- **Risico algemeen**: de tool groeit (app.js is 766 regels). Elke laag krijgt een eigen bestand (`code.js`, `lab.js`) en
  de headless toets loopt elke tab na.

## 11. Beslissingen voor Clay (met wat ik kies als je niets zegt)
1. **Volgorde X0 → X1 → X2 → X3 → X4.** Ik kies dit: experimenteren eerst, de werkbank na Logic.
2. **Sleutelwoorden van het codeblok in het Nederlands** (`knop`, `staat`, `lijn`, `vorig`, `herhaal`), wiskunde met de
   gewone namen (sin, tanh). Ik kies dit, in lijn met de tool.
3. **De brug** (`node tools/werkbank-brug.mjs`) moet op de Mac draaien als je de werkbank gebruikt. Node staat er al voor
   VARVE. Ik kies dit boven netwerk in de plugin.
4. **De eerste drie experimenten** uit §3. Heb je een eigen vraag, dan wordt die experiment 4.
