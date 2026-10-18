Stopcriterium gehaald.

# Uitslag fase 2 — proef G2 (tak `proef/g2`)

Laatste CI-run met code (groen, alle drie de jobs): https://github.com/CKrebbers/pluginbouwer/actions/runs/39158312879
(commit `17d3842`). Deze commit verandert alleen dit verslag en het vinkje in TASKS.md.

## Wat ik deed
- `plugin/src/plugin.cpp`, tekst ↔ waarde (SPEC.md "Tekst ↔ waarde"):
  - `value_to_text`: eenheidloos (Noot, Poort) × 100 in hele procenten ("100 %", "125 %"); Filter hele Hz onder 1000,
    daarna kHz met 2 decimalen, vanaf 10 000 met 1 ("3,00 kHz", "12,0 kHz"); Loslaten 1 decimaal onder 100, daarna hele
    ms. De grens kiest op de **afgeronde** waarde (999,7 Hz → "1,00 kHz", niet "1000 Hz"; 99,97 ms → "100 ms").
    "-0 %" wordt "0 %". Niet-eindige waarde → false.
  - `text_to_value`: komma én punt, met of zonder eenheid, hoofdletters maakt niet uit, "k"/"kHz" voor kHz, procenten
    voor eenheidloos ("38", "38 %", "38%"); buiten het bereik klemmen; onleesbaar (leeg, "veel", "300 ms" bij Filter,
    "1,2,3 ms") = false.
  - Beide zonder `printf`-decimaalteken/`strtod`: die volgen de locale van de host (een Nederlandse macOS-locale kan een
    komma geven of verwachten). Het getal wordt zelf gelezen: een gehele mantisse en één vermenigvuldiging of deling met
    een exacte macht van tien, dus correct afgerond ("0,38" geeft precies 0.38, "900 Hz" precies 900).
  - `params.flush` buiten process: was er al (neemt een klaargezette state over en past de events toe met dezelfde
    `pasToe` als process); de toets roept hem nu aan met de plugin niet actief en actief zonder process.
- `plugin/toetsen/pb-statetoets.cpp` (doel `pb-statetoets` in `plugin/CMakeLists.txt`): een CLAP-host die het gebouwde
  `.clap` laadt (dlopen, zoals pb-render) en 171 controles doet:
  - **(a)** blob met `aantal` = N − 1 (zonder Loslaten, met de hand gemaakt): load = true, de drie waarden komen
    binnen, Loslaten = 450 (standaard), ook al stond hij ervoor op 1007,5.
  - **(b)** versie 2, een extra onbekend id 1000 midden in de lijst: load = true, alle vier de waarden kloppen.
  - **(c)** magic "PBSX", versie 0, 1 byte te kort, een hele parameter te kort, afgekapt midden in de kop, leeg: load =
    false, `get_value` van alle parameters ongewijzigd én `state.save` byte voor byte als ervoor.
  - Elk geval met de plugin niet actief en actief (actief: daarna `params.flush` met een lege lijst, buiten process).
    Na een geslaagde load moet de plugin `rescan(CLAP_PARAM_RESCAN_VALUES)` vragen (de host telt het).
  - Daarna rendert dezelfde instantie een noot (4 blokken van 512) en moet de uitvoer bit voor bit gelijk zijn aan een
    verse instantie die dezelfde waarden via `params.flush` kreeg: zo is getoetst dat de DSP de geladen waarden kreeg,
    niet alleen de spiegel van `get_value`.
  - Tekst: de standaard van elke parameter geeft precies de tabeltekst ("100 %", "0 %", "900 Hz", "450 ms") en
    `text_to_value` daarvan terug precies de standaard; de voorbeelden uit de kolom "regel"; klemmen; onleesbaar;
    en per parameter 1001 waarden tekst → waarde → tekst blijft dezelfde tekst.
  - Ik heb de toets zelf laten falen (lokaal, niet gepusht): met de controle op versie 0 weggehaald en een geladen
    waarde × 0,999 in het Ontwerp faalden 16 controles (versie 0 laadde, en de render week af terwijl `get_value`
    nog klopte). De toets vangt dus ook een fout die alleen in de DSP zit.
- `.github/workflows/ci.yml`:
  - linux: na clap-validator (exit 0 en "0 failed", zoals in fase 0) moet elke state- en param-toets **PASSED** zijn
    (SKIPPED telt niet): `state-invalid-empty`, `state-invalid-random`, `state-reproducibility-basic`, `-binary`,
    `-buffered`, `param-conversions`, `param-default-values`, `param-set-events`, `-no-cookies`, `-wrong-namespace`,
    `param-fuzz-basic`, `-bounds`, `-sample-accurate`, `-modulation`. Laatste stap: `pb-statetoets`.
  - macos: `pb-statetoets` door het gesigneerde, geïnstalleerde `.clap`, vóór auval (dat blijft de laatste stap).

## Poorten (run 39158312879)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| clap-validator 0.4.1 (Linux) | groen | `44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped`, `clap-validator exit: 0` |
| clap-validator state-reproducibility en param-fuzz | groen | `clap-validator state-reproducibility-basic: PASSED`, `…-binary: PASSED`, `…-buffered: PASSED`, `param-fuzz-basic: PASSED`, `-bounds`, `-sample-accurate`, `-modulation: PASSED`, `param-conversions: PASSED` (14 regels, alle PASSED) |
| state- en teksttoets Linux | groen | `pb-statetoets: 171 controles, 171 geslaagd, 0 mislukt` |
| state- en teksttoets macOS arm64 | groen | `pb-statetoets: 171 controles, 171 geslaagd, 0 mislukt` (o.a. `(a) aantal N-1, zonder Loslaten [actief]: Loslaten = 450 (verwacht 450)`, `(c) versie 0 [actief]: state.save na de load is byte voor byte als ervoor`, `value_to_text(Filter, 900) = "900 Hz"`) |
| pluginval 10 VST3 Linux | groen | `SUCCESS` |
| pluginval 10 VST3 macOS (`pluginval-rt.sh`) | groen | stap geslaagd |
| pluginval 10 AU macOS (`--rtcheck enabled --repeat 3`) | groen | stap geslaagd (grep `^SUCCESS`) |
| auval -strict | groen | `Values: Minimum = 25 %, Default = 100 %, Maximum = 400 %` … `Minimum = 100 Hz, Default = 900 Hz, Maximum = 8,00 kHz` … `Minimum = 10,0 ms, Default = 450 ms, Maximum = 4000 ms`, `Test MIDI PASS`, `AU VALIDATION SUCCEEDED.` |
| null-tests (fase 1) Linux | groen | alle rijen `−∞ (bit voor bit gelijk)`, ook `automatie (events midden in blokken)` 2× en noten CLAP/MIDI bij blok 512 en wisselend |
| null-test AU/VST3 macOS | groen | `PASSED: stukjes-laag` / `sweep-kwart` … `max abs diff 0 <= tolerance 3.16228e-05` |
| RTSan schoon en kanarie | groen | beide stappen geslaagd |

## Pushes
Twee: de code (`17d3842`, meteen groen in alle drie de jobs) en dit verslag. Vóór de push heb ik lokaal (Linux x86_64)
gebouwd en `pb-statetoets`, clap-validator 0.4.1 (dezelfde 14 toetsen PASSED, de CI-stap ook letterlijk lokaal
gedraaid), pluginval strictness 10 op de VST3 (`SUCCESS`) en `ci-null.sh clap` (alles bit voor bit) gedraaid. macOS
en RTSan alleen in CI.

## Wat onduidelijk of fout was, en waar ik bijna vastliep
- Niet vastgelopen. Fase 1 had state en tekst al grotendeels; fase 2 werd vooral toetsen plus twee echte fouten:
  - de Hz/kHz- en ms-grens keek naar de onafgeronde waarde ("1000 Hz" voor 999,7);
  - tekst ↔ waarde hing van de C-locale af (`strtod` na het vervangen van komma door punt breekt in een host die een
    komma-locale zet). Beide niet door clap-validator of pluginval gevangen; het hoort misschien bij de valkuilen.
- "Automatisering in een DAW verandert niets aan de null-test": voor CLAP bewijst het geval `testsignalen/automatie/`
  (720 events, ook midden in blokken) dat bit voor bit. Voor VST3/AU kan `pluginval test` geen automatisering sturen;
  daar is het niet door een binary getoetst, alleen door dezelfde `pasToe` die CLAP gebruikt (clap-wrapper zet AU- en
  VST3-automatisering om in dezelfde CLAP-events).
- SPEC zegt niet wat er op de grens gebeurt (999,7 Hz, 99,97 ms); ik koos "op de afgeronde waarde", zodat de tekst
  nooit "1000 Hz" of "100,0 ms" is.
- clap-validator 0.4.1 heeft geen `state-reproducibility-flush` (die naam komt in latere versies); de drie
  state-reproducibility-toetsen die hij wel heeft staan nu als PASSED geëist.
- Niets uitgezet of overgeslagen.
