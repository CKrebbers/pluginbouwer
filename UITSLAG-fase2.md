Stopcriterium gehaald.

# Uitslag fase 2 — Dub-echo (tak proef/f1)

Laatste CI-run (commit 8f5e0ac): https://github.com/CKrebbers/pluginbouwer/actions/runs/39039523860
— alle drie de jobs `success`. Er waren twee ronden nodig. De eerste run (39038804310, commit 6dc8e26) liet alle toetsen
slagen, maar het uploaden van de renders mislukte: `glij-1=200:800-1.wav … Contains the following character: Colon :`.
Dat lag aan de bestandsnamen, niet aan de plugin.

## Wat ik deed
- **Tekst ↔ waarde** (`plugin/src/param_tekst.h`, aangeroepen door `params.value_to_text`/`text_to_value`):
  - Tijd: "375 ms" (vanaf 100 ms hele ms), daaronder "93.8 ms".
  - Toon/Laag weg: "12.0 kHz", "3.00 kHz", "265 Hz", "81.3 Hz".
  - Verzadiging: "6.0 dB". Resonantie: "0.71 Q".
  - Terugkoppeling, Haperen en Mix (0..1): procent, "37.5 %".
  - Lezen kent ook "1.2 s", "3k", "3,5 kHz" en een kaal getal (in de getoonde eenheid).
  - Tekst → waarde → tekst geeft dezelfde tekst. Ik schrijf en lees de getallen zelf, zodat ze niet afhangen van de
    locale van de host.
  - Alleen de tekst rondt af, de waarde niet.
  - Stapjes: geen enkele parameter heeft de vlag `STAPPEN` in `PARAMS`, dus er zijn er geen nodig. Komt er een, dan toont
    en leest de code hele getallen.
- **`params.flush` buiten process**: dat werkte al sinds fase 1, nu met één randgeval erbij.
  - State die geladen wordt terwijl de plugin actief is, gaat nu ook via flush naar het Ontwerp, en via `activate` na een
    deactivate. Eerst gebeurde dat alleen in process.
  - Zonder die verandering ging state die vlak vóór een deactivate werd geladen verloren: activate wiste de vlag.
  - Uitgaande events zijn er niet, want er is nog geen GUI.
- **State-toets** (`gereedschap/pb-state-toets.cpp`):
  - CMake bouwt `build/toets/zonder-N.clap` voor N = 1..8 met `-DPB_ZONDER_PARAM=N`. Dat is de echte schil, maar zonder
    parameter N, zoals een oudere versie van de plugin.
  - Het hulpprogramma toetst per N:
    1. state oud → nieuw: elke waarde is bit voor bit gelijk, N houdt zijn standaard, en de host krijgt een rescan;
    2. de nieuwe bouw met die oude state klinkt bit voor bit als de oude bouw, en als de nieuwe bouw met dezelfde
       waarden via flush;
    3. state laden terwijl de plugin actief is, tussen twee blokken, klinkt gelijk aan flush op dat moment;
    4. nieuw → oud: het onbekende id wordt overgeslagen;
    5. state van versie 2 met een onbekend id 999 laadt; lege, afgekapte of andere kop wordt geweigerd zonder dat er iets
       verandert.
  - Mutatieproeven (lokaal, niet gecommit) vingen allebei hun fout: "aantal moet N zijn" en "process neemt geladen
    state niet over".
- **Automatisering verandert niets aan de null-test** (`pb-render --automatie K [--glij id=van:naar]`,
  `gereedschap/ci-automatie.sh`):
  - De parameters komen als events in process, op sample 0 en daarna elke 100 samples opnieuw, midden in blokken. Ze
    komen dus niet via flush vóór activate. De null-test geeft dan dezelfde verwachte uitvoer.
  - Glijdende automatisering (Tijd 200→800 ms, Toon 500→8000 Hz) geeft bit voor bit dezelfde uitvoer bij blok 512,
    wisselende blokken en blok 1.
- **CI**:
  - clap-validator noemt elf toetsen apart en eist PASSED: state-reproducibility-basic/binary/buffered,
    state-invalid-*, param-fuzz-basic/bounds/sample-accurate, param-conversions, param-set-events en
    param-default-values.
  - Ook geëist: dat param-conversions echt een tekstconversie toetste.
  - De automatisering en de state-toets draaien op Linux en macOS.
  - RTSan draait ook met `--automatie` en met de state-toets.
- `dsp/` en `testsignalen/` zijn niet veranderd. ARCHITECTUUR.md heeft nieuwe stukken over "State" en "Parameters als
  tekst". TASKS.md is bijgewerkt. Er is geen toets uitgezet.

## Poorten (run 39039523860)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1 (Linux) | groen | stap "clap-validator" slaagt. Die faalt als een van de elf genoemde toetsen niet PASSED is, of als "0 failed" ontbreekt. Lokaal met dezelfde versie en code: `44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped`, met state-reproducibility-basic/binary/buffered, param-fuzz-basic/bounds/sample-accurate en param-conversions allemaal `PASSED` |
| pluginval strictness 10, VST3 Linux | groen | stap slaagt. Lokaal: `Result: 537 tests passed, 0 tests failed` … `SUCCESS` |
| pluginval strictness 10 `--rtcheck`, AU en VST3 macOS | groen | beide stappen slagen |
| auval -strict | groen | stap slaagt (grept `AU VALIDATION SUCCEEDED`) |
| state-toets (bouw zonder N → mét, N = 1..8; Linux en macOS) | groen | bij elke N: `ok oud → nieuw: 7 waarden overgenomen, id N op zijn standaard …, rescan 1×`, `ok nieuw met oude state = oud: bit voor bit gelijk`, `ok state laden terwijl actief = flush op hetzelfde moment: bit voor bit gelijk`, `ok nieuw → oud: id N overgeslagen`, `ok state van versie 2 met onbekend id 999: geladen`, `ok kapotte state …: geweigerd` → `state-toets N=1..8: geslaagd` |
| null-test met automatisering (Linux en macOS) | groen | `### Null-test CLAP met automatisering (automatie-blok512 / automatie-wisselend)`: 4 gevallen `−∞ (bit voor bit gelijk) … ja`; `glij 1=200:800: blok 3,128,1,4096,77 = blok 512, bit voor bit`, `… blok 1 = blok 512, bit voor bit`, en hetzelfde voor `glij 3=500:8000` |
| null-tests fase 1 (CLAP Linux en macOS, VST3 Linux en macOS, AU) | groen | de stappen slagen. Lokaal: CLAP 3 varianten × 4 gevallen `ja`, VST3 Linux `PASSED … max abs diff 0` |
| RTSan (nu ook met automatisering en de state-toets) | groen | `RTSan: schoon`; `state-toets N=8: geslaagd` / `RTSan (state-toets): schoon` |
| kanarie | faalt zoals bedoeld | `RealtimeSanitizer: unsafe-library-call` / `` `malloc` in real-time context`` / `#1 … process … dub_echo.cpp` → `Kanarie gevangen door RTSan (exitcode 43), zoals bedoeld` |

## Waar ik op stuitte / wat in het pakket onduidelijk was
- **"Een build zonder parameter N"**: het pakket zegt niet hoe je die maakt. Een oudere commit bouwen mocht niet in
  deze proef, en is ook broos. Ik koos daarom voor een testvlag in de schil (`PB_ZONDER_PARAM`, net als `PB_KANARIE`).
  Een zin in PROMPTS/ARCHITECTUUR zou helpen.
- **"Stapjes waar nodig"**: er is geen parameter met `STAPPEN`. Ik nam aan dat dat zo bedoeld is: alle knoppen zijn
  continu en glijden.
- **"Automatisering in een DAW verandert niets aan de null-test"**: dit is niet in een echte DAW getoetst. Ik bootste
  het na met events in process (zie boven). Logic zelf is fase 7.
- **Eenheden**: SPEC zegt niet hoe "—"-parameters getoond moeten worden. Ik koos procent; dat staat nu in
  ARCHITECTUUR.md.
- **Naar VST3/AU**: clap-wrapper geeft een lege unit-label door (pluginval: `unit=""`). De eenheid zit daarom in de
  tekst zelf.
- **Bestandsnamen in `uit/`**: actions/upload-artifact weigert `:` (en `"<>|*?`). De eerste run faalde daarop; de
  glij-renders hebben nu veilige namen.
- **Lokaal niet gedraaid**: RTSan en macOS. Die zijn alleen in CI getoetst, en daar groen.
