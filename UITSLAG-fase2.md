Stopcriterium gehaald.

# Uitslag fase 2 — Toetsenpad (proef g1)

Laatste CI-run met code (commit `c2cd03f`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39156671997
(jobs linux, rtsan en macos alle drie success). De commit met dit verslag verandert alleen docs; zijn run draait dezelfde jobs.

## Wat ik deed
Fase 1 had de parameters, `params.flush` en de state al grotendeels zoals ARCHITECTUUR.md. Fase 2 maakte ze af en
toetst ze:
- `plugin/src/toetsenpad.cpp`:
  - **value_to_text** rondt eerst af op de precisie die getoond wordt en kiest daarna de grens uit de tabel. Daarvoor
    gaf 999,7 Hz "1000 Hz" (moet "1,00 kHz" zijn), 9999,7 Hz "10,00 kHz" ("10,0 kHz") en 99,96 ms "100,0 ms"
    ("100 ms"). Een −0 wordt "0 %", geen "-0 %". NaN geeft false.
  - **text_to_value** leest de cijfers als geheel getal met een deler (een macht van 10) en deelt één keer. Zo wordt
    "38 %" precies 0.38 en "3,00 kHz" precies 3000; eerst ging het via een optellende 0,1-reeks. Er is geen locale.
    Komma en punt werken allebei, met of zonder eenheid, "k" voor kHz, procenten met of zonder "%". Een waarde buiten
    het bereik wordt geklemd, onleesbare tekst geeft false.
  - `params.flush` (al uit fase 1) verwerkt events buiten process: als de plugin actief is op de audiothread, anders op
    de mainthread. Eerst neemt hij een klaargezette state over. Er is niets aan veranderd.
- `plugin/toets/statetoets.cpp` → `build/pb-statetoets <plugin.clap>`: de eigen toets. Het is een kleine CLAP-host
  (dlopen, zoals pb-render) die rescan en request_flush telt en een stream geeft die 5 bytes per read levert.
  - **(a)** Een blob versie 1 met aantal = N − 1, zonder Loslaten. De instantie stond vooraf op andere waarden
    (Loslaten 4000). Load geeft true. Noot, Poort en Filter krijgen de blobwaarden, Loslaten krijgt de standaard 450.
  - **(b)** Een blob versie 2 met N + 2 paren, met onbekende id's 9999 (ertussen) en 77 (erachter). Load geeft true en
    de bekende waarden komen aan.
  - **(c)** Een blob die niet mag laden: magic "PBSX", versie 0, één byte te kort, één parameter te kort, afgekapt
    midden in de kop, of leeg. Load geeft false. get_value blijft gelijk, een nieuwe save is byte voor byte gelijk aan
    die van ervoor, er komt geen rescan of request_flush, en de render verandert niet.
  - Elk geval draait inactief en actief (laden tussen twee process-aanroepen in). Er volgt een render met een noot
    aan en uit, zodat Loslaten te horen is. Die render moet bit voor bit gelijk zijn aan een referentie-instantie die
    dezelfde waarden via flush kreeg. Na een geslaagde load moet er een rescan komen, en als de plugin actief is ook
    een request_flush.
  - **Tekst**: value_to_text van de standaard geeft precies de tekst uit de tabel ("100 %", "0 %", "900 Hz",
    "450 ms"). text_to_value daarvan geeft de standaard exact terug (`==`). Verder toetst hij 20 voorbeelden en
    grenzen uit de kolom "regel", 27 invoerteksten (ook klemmen en onleesbaar), afkappen binnen de buffer, en
    tekst → waarde → tekst over het hele bereik (4 × 401 waarden).
  - **flush** inactief en actief: get_value ziet de nieuwe waarde, er wordt geklemd, een onbekend id wordt
    overgeslagen, en de render is gelijk aan dezelfde waarden als event in process.
  - Lokaal heb ik twee fouten in de plugin gezet om te zien of de toets ze vangt: versie 0 accepteren, en laden laten
    beginnen vanaf de huidige waarden in plaats van de standaarden. De toets gaf toen 20 FOUT, onder meer
    "(a) … Loslaten = 4000 (verwacht 450)" en "(c) versie 0 … load geeft true". Daarna heb ik de fouten teruggedraaid.
- `plugin/CMakeLists.txt`: het doel `pb-statetoets`.
- `.github/workflows/ci.yml`:
  - clap-validator: behalve "0 failed" moet nu elk van deze 11 toetsen PASSED zijn (SKIPPED telt niet):
    state-reproducibility-basic, -binary en -buffered, state-invalid-empty en -random, param-fuzz-basic, -bounds en
    -sample-accurate, param-set-events, param-conversions en param-default-values.
  - pb-statetoets draait als laatste stap in linux en in macos, met `set -o pipefail`, zodat `tee` een fout niet
    verbergt.
- `dsp/` en `testsignalen/` zijn niet veranderd. Geen toets of validator uitgezet.

## Poorten (bewijs uit de logs van run 39156671997)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1, Linux | groen | `clap-validator: 44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped (exit 0)` |
| clap-validator state-reproducibility / param-fuzz | groen | `clap-validator state-reproducibility-basic: PASSED`, `…-binary: PASSED`, `…-buffered: PASSED`, `state-invalid-empty/-random: PASSED`, `param-fuzz-basic/-bounds/-sample-accurate: PASSED`, `param-set-events`, `param-conversions`, `param-default-values: PASSED` |
| state- en teksttoets (eigen, door het .clap), Linux x86_64 | groen | `pb-statetoets: 212 toetsen, 212 ok, 0 fout` |
| idem, macOS arm64 | groen | `pb-statetoets: 212 toetsen, 212 ok, 0 fout` |
| pluginval 10 VST3 Linux | groen | `pluginval VST3 Linux: exit 0, laatste regel: SUCCESS` |
| pluginval 10 `--rtcheck enabled --repeat 3` AU | groen | `Running tests 3 times` … `pluginval AU: exit 0, laatste regel: SUCCESS` |
| pluginval 10 VST3 macOS via pluginval-rt.sh | groen | `SUCCESS`, `real-time-schendingen: 0, waarvan pluginval-host (MidiEventList::addEvent): 0` |
| auval -strict | groen | `AU VALIDATION SUCCEEDED.` (staat dit keer wel in de staart van het log) |
| null-tests CLAP (blok 512, wisselend, state-rondje, automatisering, noten CLAP/MIDI), Linux en macOS | groen | alle `### Null-test CLAP …`-tabellen `bit voor bit gelijk` (de automatisering is het geval "events midden in blokken") |
| null-tests VST3 Linux/macOS en AU (pluginval test) | groen | `### Null-test vst3-linux`, `vst3-macos`, `au`: alles `bit voor bit gelijk` |
| RTSan plugin / kanarie | groen | stap "RTSan door het .clap" success; `RTSan-kanarie gevangen zoals bedoeld (exit 43)` (`malloc` ← `pluginProcess` ← `doeProcess`) |

"Automatisering in een DAW verandert niets aan de null-test": de automatie-null-test (720 events, ook midden in
blokken) blijft bit voor bit gelijk. De nieuwe toets laat ook zien dat een waarde via flush, via state of als event in
process precies dezelfde render geeft.

## Pushes
Eén push tot CI groen was (`c2cd03f`). Vooraf heb ik lokaal (Linux, clang 18) gedraaid:
- pb-statetoets: 212/212;
- clap-validator 0.4.1 uit de bron: 35 passed, 0 failed, en de nieuwe awk-controle op die uitvoer;
- pluginval 10 op de VST3 onder xvfb: SUCCESS;
- `ci-null.sh clap`: alles bit voor bit.

De AU en RTSan kon ik lokaal niet toetsen.

## Onduidelijk of fout in het pakket
- **Grenzen in de teksttabel.** "onder 1000: hele Hz; vanaf 1000 kHz" zegt niet of de grens geldt voor de waarde of
  voor de afgeronde waarde. Ik koos de afgeronde waarde (999,7 → "1,00 kHz"), omdat "1000 Hz" de regel zelf breekt.
  Een voorbeeld in de tabel zou het vastleggen.
- **"Noot" heeft eenheid "×"** in `PARAMS`, maar volgens de teksttabel is hij eenheidloos (procenten). De schil
  behandelt alles wat geen "Hz" of "ms" is als procenten. Dat klopt hier, maar bij een ontwerp met "dB" of "%" zou
  dat stil misgaan. Een kolom "tekstregel" in `PARAMS`, of een zin in SPEC.md, zou dat voorkomen.
- **"Toets met clap-validator (state-reproducibility, param-fuzz)"**: de gewone `validate` draait die toetsen al
  sinds fase 1. Zonder een eis dat ze PASSED zijn, zou een SKIPPED ongemerkt groen blijven (zoals in fase 4 voor de
  presets). Ik heb die eis toegevoegd. Een zin daarover in de prompt van fase 2 zou helpen.
- clap-validator breekt lange toetsnamen in zijn boom af over twee regels. Een grep op "naam PASSED" werkt daarom niet
  voor elke toets; de awk-controle leest het blok van "┌─ naam" tot "└── uitslag". Voor de 11 toetsen hier past de
  naam op één regel.
- Verder niets geblokkeerd.
