Stopcriterium gehaald.

# Uitslag fase 2 — Dub-echo (tak proef/f2)

Laatste CI-run met code (commit 19d0b27): run 59 —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39039021477 — alle drie de jobs groen, **bij de eerste push**.
(De commit met dit bestand verandert alleen docs; zijn CI-run draait dezelfde code.)

## Wat ik deed
- `plugin/dub_echo.cpp`
  - **Tekst ↔ waarde in gewone eenheden**: Tijd `20.5 ms` / `375 ms`; Toon en Laag weg `450 Hz` / `3.00 kHz` / `12.0 kHz`;
    Verzadiging `6.0 dB`; Resonantie `0.71` (Q); Terugkoppeling, Haperen en Mix als percentage (`37.5 %`).
    Intypen kan met punt of komma, met of zonder eenheid (`0,4 s`, `3k`, `1,5 KHZ`, `110%`). Een waarde buiten het
    bereik wordt geklemd, een verkeerde eenheid geweigerd. Zonder printf-%f en strtod, zodat de locale van de host
    (komma als decimaalteken) niets verandert. Per tak wordt één keer afgerond en op dat gehele getal beslist welke
    vorm het wordt. Zo is tekst → waarde → tekst na één rondje altijd vast, ook op de grens 999,6 Hz → "1.00 kHz".
  - **Stapjes**: geen van de acht parameters is getrapt (in `PARAMS` heeft geen enkele de vlag `STAPPEN`). De schil
    ondersteunt het wel generiek: `CLAP_PARAM_IS_STEPPED`, afronden op hele getallen en een geheel getal als tekst.
  - **params.flush buiten process**: als de plugin niet actief is, gaan de waarden direct naar het Ontwerp; als hij
    actief is (audiothread, tussen twee process-aanroepen) ook, en daarna wordt de staart opnieuw uitgerekend.
  - **Subnormale uitvoer**: de `clap-validator fuzz` vond subnormale uitvoersamples. Oorzaak: invoer rond 1e-38 is
    nog een normale float, maar het droge pad × mix maakt hem subnormaal; de gegeven DSP poetst wel zijn invoer en
    toestand, maar niet `Blok::uit`. Lokaal nagebootst met alleen het Ontwerp: invoer 1,3e-38 → uitvoer 8e-39. De
    schil maakt nu ná de DSP elke subnormale uitvoersample tot (getekende) 0, en kijkt daarvoor alleen naar de bits.
    Er staat geen FTZ vóór de DSP (valkuil 6) en `dsp/` is niet aangeraakt. De verwachte uitvoer in `testsignalen/`
    bevat geen enkele subnormale sample (nageteld), dus de null-test blijft bit voor bit.
  - Met `-DPB_ZONDER_PARAM=<id>` bouw je een "oudere" versie waarin die parameter nog niet bestond. Hij heeft dan 7
    params, het id is onbekend voor events, get_value en state, en de DSP houdt hem op zijn standaard.
- `plugin/toetsen/param-toets.cpp` (nieuw), een kleine CLAP-host:
  - `state <volledig> <zonder-N> N`:
    - zet in de build zonder N alle parameters op een niet-standaardwaarde en bewaart de state;
    - laadt die state in de volledige build, waarin N eerst op een andere waarde stond. Elke bewaarde waarde moet
      exact terugkomen en N op zijn standaard;
    - rendert daarna en vergelijkt bit voor bit met een volledige build die dezelfde waarden via flush kreeg, één keer
      voor laden terwijl de plugin niet actief is en één keer terwijl hij actief is;
    - doet een tweede save→load-rondje, dat byte voor byte gelijk moet zijn;
    - laadt ook andersom de state van de volledige build in de oude, waarbij het onbekende id wordt overgeslagen.
  - `automatisering`:
    - een vlakke automatisering (elke ~37 samples alle parameters op hun waarde, op willekeurige plekken in het blok,
      en ook via flush) moet bit voor bit gelijk zijn aan de render zonder automatisering. Dat is "automatisering in
      een DAW verandert niets aan de null-test";
    - echte automatisering (Tijd in 200 stapjes, Toon springt, Terugkoppeling naar 1,1 en terug, Mix met twee events
      op één sample) moet bit voor bit gelijk zijn in blok 512, in wisselende blokken `3,128,1,4096,77`, in blokken van
      1 sample, en via `params.flush` tussen de blokken (buiten process);
    - get_value moet de automatisering volgen.
  - `tekst`: voorbeelden en grenzen, onzin weigeren, en 160 008 rondjes waarde → tekst → waarde → tekst over het bereik.
  - Of de toetsen ook kunnen falen, heb ik lokaal met twee mutaties nagegaan (beide gevangen):
    - "een ontbrekend id houdt de oude waarde" gaf `FOUT: id 3 (Toon): 9870.6, verwacht 3000` en 149 664 verschillende samples;
    - "events één sample te laat" gaf 229 056 verschillende samples in alle drie de varianten.
- `plugin/CMakeLists.txt`: optie `PB_TOETSEN` (standaard aan) bouwt `param-toets` en 8 builds
  `toetsen/PB-Dub-echo-zonder-N.clap` (N = 1..8). Onder `PB_RTSAN` krijgt param-toets ook `-fsanitize=realtime`.
- `.github/workflows/ci.yml` (er is niets weggehaald of uitgezet):
  - **Linux**: een stap die van clap-validator (JSON) eist dat state-reproducibility-basic/binary/buffered,
    param-fuzz-basic/bounds/sample-accurate/modulation, param-conversions en param-default-values *draaien* en
    slagen (overgeslagen telt als fout); `clap-validator fuzz --duration 120s`; en de eigen toetsen.
  - **macOS**: de eigen toetsen (arm64).
  - **RTSan**: param-toets `automatisering` en `state` met `-fsanitize=realtime`. Zo lopen events in process,
    `flush` op de audiothread en een state load terwijl de plugin actief is in een real-time context. In fase 1
    stuurde RTSan nog geen events.
- SPEC.md (tekstvorm, stapjes), ARCHITECTUUR.md (flush, atomische kopieën, subnormale uitvoer, de state-toets) en
  TASKS.md bijgewerkt. `dsp/` en `testsignalen/` zijn niet aangeraakt.

## Poorten (run 59)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| clap-validator 0.4.1 | groen | "44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped" |
| clap-validator state-reproducibility / param-fuzz | groen | stap "clap-validator state-reproducibility en param-fuzz": `state-reproducibility-basic success`, `-binary success`, `-buffered success`, `param-fuzz-basic success`, `-bounds success`, `-sample-accurate success`, `-modulation success`, `param-conversions success`, `param-default-values success`, "alle 9 geslaagd" |
| clap-validator fuzz 120 s | groen | "OK: fuzzing finished successfully" (14:02:36 → 14:04:36) |
| state-toets (zonder N → met), Linux | groen | voor N = 1..8: "state van de build zonder N: 96 bytes (7 parameters)", "ok: waarden na laden: elke bewaarde parameter exact terug, id N (…) op zijn standaard …", "GESLAAGD" |
| state-toets, macOS arm64 | groen | dezelfde 8 toetsen: "render na state laden (niet actief) …: bit voor bit gelijk", "render na state laden terwijl actief: bit voor bit gelijk", "GESLAAGD" |
| automatisering verandert niets aan de null-test | groen | Linux, macOS en onder RTSan: "vlakke automatisering in process (wisselende blokken): bit voor bit gelijk", "… via flush tussen blokken: bit voor bit gelijk", "automatisering via params.flush tussen de blokken (buiten process): bit voor bit gelijk", "blokken van 1 sample: bit voor bit gelijk" |
| tekst ↔ waarde | groen | "ok: 160008 rondjes waarde -> tekst -> waarde -> tekst stabiel" (Linux en macOS) |
| pluginval strictness 10, VST3 Linux/xvfb | groen | "Result: 537 tests passed, 0 tests failed", `SUCCESS` |
| auval -strict | groen | "AU VALIDATION SUCCEEDED." |
| pluginval 10 `--rtcheck enabled`, AU en VST3 macOS | groen | beide `SUCCESS` ("Time taken to run all tests: 59 secs" / "49 secs") |
| null-tests (fase 1) | groen, bit voor bit | CLAP Linux x86_64 en macOS arm64 (blok 512, wisselend, state), VST3 Linux en macOS, AU macOS: alle gevallen "−∞ (bit voor bit gelijk)"; zelfoscillatie "terts 0.000 dB, omhullende 0.000 dB"; 3× "PASSED" per pluginval-null-test |
| RTSan schoon + kanarie faalt | groen | "RTSan: geen overtreding in process", "RTSan: geen overtreding in process of flush"; kanarie: "Intercepted call to real-time unsafe function `malloc` … dub_echo.cpp:149", "Kanarie gevangen zoals bedoeld (exitcode 43)" |
| artefact `macos-keten_dub_echo` | groen | "Artifact macos-keten_dub_echo has been successfully uploaded! … Artifact ID is 12102944225" |

## Waar het schuurde / wat onduidelijk of fout was in het pakket
- **De DSP geeft subnormale uitvoer** bij heel zachte normale invoer, omdat `Blok::uit` in `pb_graaf.h` niet poetst.
  Dat zit in de gegeven DSP, dus ik heb het in de schil opgelost (zie hierboven). Beter zou zijn als de tool het in
  `Blok::uit` doet (dan ook in de browser), of als CLAUDE.md valkuil 6 zegt: "uitvoer poetsen mag, ná de DSP". De
  gewone `validate`-suite ziet het niet; alleen `clap-validator fuzz` vindt het (zijn signaal "Denormal" en sinussen
  die langzaam wegzakken).
- "Stapjes waar nodig": in dit ontwerp is geen enkele parameter getrapt. De prompt laat open of er toch één getrapt
  zou moeten zijn; ik heb het generiek gemaakt en niets veranderd.
- De parameters zonder eenheid (Terugkoppeling, Haperen, Mix) toon ik als percentage. SPEC zei "—"; de prompt noemt
  alleen ms, Hz en dB. Dat is een keuze van mij en staat nu in SPEC.md. Een kaal ingetypt getal betekent dan ook
  procent ("50" = 0,5), omdat de host dat getal zo toont.
- De prompt zegt niet hoe je "een build zonder parameter N" maakt. Ik heb een compile-define in de schil gebruikt en
  niet een tweede kopie van de DSP-header met 7 params, omdat `dsp/` vastligt. Zo'n build gedraagt zich naar buiten
  (params, events, state) als een oudere versie, maar zijn DSP heeft de parameter nog wel, op de standaardwaarde.
  Dat is het geval waar het om gaat: een parameter die er later bij kwam en zijn standaard moet krijgen.
- clap-wrapper geeft geen eenheidslabel door aan VST3 (pluginval: "label - "). De eenheid zit daarom in de tekst zelf.
  Hoe Logic de tekst van de AU toont, is pas in fase 7 te zien.
- RTSan bewaakt param-toets net zoals pb-render (dezelfde `noinline` + `[[clang::nonblocking]]`-wrapper). Er is
  geen aparte kanarie die via param-toets draait. De kanarie bewijst dat de werkwijze klopt, maar niet dat deze ene
  binary goed is geïnstrumenteerd.
- pluginval meldt "This plugin does not have a bypass parameter!!!" (alleen informatie, geen fout); dat hoort bij fase 5.
