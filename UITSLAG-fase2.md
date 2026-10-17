Stopcriterium gehaald.

# UITSLAG fase 2 — parameters, automatisering, state (tak proef/f3)

Laatste CI-run op de code: **run 58**, commit `772d039`, alle drie de jobs groen in de eerste push —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39038970074
(Deze UITSLAG-commit verandert alleen docs; CI draait er opnieuw op.)

## Wat ik deed
- `plugin/bron/plugin.cpp` (alleen de schil; `dsp/` en `testsignalen/` niet aangeraakt):
  - **tekst ↔ waarde in gewone eenheden**: Tijd `20.0 ms` … `999 ms`, vanaf 1 s `1.200 s`; Toon/Laag weg `20.0 Hz` …
    `999 Hz`, vanaf 1 kHz `3.00 kHz`; Verzadiging `6.0 dB`; Resonantie `0.707` (Q); de parameters zonder eenheid
    (Terugkoppeling, Haperen, Mix) als procenten `37.5 %`. De waarde wordt eerst afgerond op wat de tekst toont en
    pas dan krijgt hij zijn eenheid, zodat tekst → waarde → tekst altijd stabiel is (ook rond 99,96 ms / 999,6 Hz).
    Invoer: `375`, `1,2 s`, `3k`, `1,5 kHz`, `12db`, `50` (= 50 %); onbekende eenheden geweigerd, waarden buiten het
    bereik geklemd. Uitvoer altijd met een punt, ook als de host een locale met komma zet; de invoerparser hangt niet
    af van de locale (geen strtod).
  - **stapjes**: een parameter met `pb::STAPPEN` wordt op hele getallen gezet en zo getoond. In dit ontwerp heeft
    geen enkele parameter `STAPPEN`, dus nu heeft dat nergens effect.
  - **`params.flush` buiten process**: werkte al vóór activate (mainthread); nu ook getoetst terwijl de plugin actief
    is zonder process (audiothread), en flush rekent dan de staart bij. `actief` is nu `std::atomic<bool>`.
  - `PB_ZONDER_PARAM=<id>`: bouwt een schil die die parameter niet kent (niet in de lijst, niet in de state, events
    en get_value voor dat id genegeerd). Dat speelt "een oudere versie zonder parameter N" na; alleen voor de toets.
- `plugin/CMakeLists.txt`: optie `PB_TOETSEN` (standaard aan) bouwt `pb-toets` en per parameter-id (uitgelezen uit
  `dsp/ontwerpen/keten_dub_echo.h`) een kale CLAP `build/zonder/zonder-<id>.clap`; die worden nooit uitgeleverd.
- `plugin/toets/pb-toets.cpp` (een kleine CLAP-host, met `noinline` + `[[clang::nonblocking]]` rond process zoals
  pb-render) en `plugin/toets/toets-fase2.sh`:
  - `tekst`: bekende paren beide kanten op, rondgang over 2001 waarden per parameter, en hetzelfde onder
    `nl_NL.UTF-8` (decimale komma). Met `PB_EIS_KOMMA=1` faalt hij als zo'n locale ontbreekt (in CI op Linux en macOS gezet).
  - `state <oud> <nieuw> <id>` voor elke parameter (8×): state van de build zonder N laadt in de echte build; de
    waarden komen exact terug, N gaat naar zijn standaard (ook als N daarvoor anders stond), de host krijgt een
    rescan, en het geluid is bit voor bit gelijk aan de echte build met dezelfde waarden via flush, ook als de state
    komt terwijl de plugin al actief is (dan ook `request_flush`). Daarnaast: flush terwijl actief zonder process,
    achteruit (state van nieuw in oud: onbekend id overgeslagen, geluid gelijk), een toekomstige versie (versie 7,
    onbekend id 9999) en kapotte state (verkeerde kop, afgekapt, leeg: geweigerd, niets veranderd).
  - `automatisering`: ~1900 events op willekeurige samples (ook meer op één sample, ook buiten het bereik). De
    uitvoer is bit voor bit gelijk bij blok 512, wisselend 3,128,1,4096,77, blok 1, blok 4096 en bij blokken die
    precies op elk event gesplitst zijn. De toets controleert ook dat de automatisering echt iets aan de uitvoer verandert.
  - `render`: de null-test met de waarden als automatisering: elk testgeval met zijn waarden als events op sample 0
    en daarna steeds opnieuw midden in wisselende blokken, tegen de verwachte uitvoer uit de WASM (`vergelijk.mjs`).
    Dit is "automatisering in een DAW verandert niets aan de null-test".
  - Ik heb twee fouten met opzet ingebouwd om te zien of de toetsen ze vangen. Ontbrekend id houdt de oude waarde
    in plaats van de standaard: de state-toets wordt rood. Events op het blokbegin in plaats van op hun sample: de
    automatiseringstoets wordt rood.
- `.github/workflows/ci.yml`: de fase-2-toetsen in de Linux-job (na `localedef nl_NL.UTF-8`), de macOS-job, en met de
  RTSan-bouw (pb-toets met `-fsanitize=realtime`: events midden in blokken en een state-load terwijl actief, RTSan
  blijft stil). De bewijsstappen herhalen de clap-validator-regels van fase 2 en de uitslagen van de eigen toetsen.
- Docs: ARCHITECTUUR.md ("State" met rescan/request_flush en de state-toets; nieuw "Parameters: tekst en eenheden"),
  SPEC.md (verwijzing en eigen toetsen bij Validators), TASKS.md (fase 2 afgevinkt).

## Poorten (bewijs uit het log van run 58)
| poort | uitslag | bewijs |
|---|---|---|
| clap-validator 0.4.1 | groen | `clap-validator: 44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped` |
| clap-validator state-reproducibility | groen | `state-reproducibility-basic`, `-binary`, `-buffered`: `PASSED` (ook `state-invalid-empty/-random`) |
| clap-validator param-fuzz | groen | `param-fuzz-basic`, `-bounds`, `-sample-accurate`, `-modulation`: `PASSED` |
| clap-validator param-conversions (tekst ↔ waarde) | groen | `param-conversions` `PASSED` (lokaal gezien: de plugin ondersteunt de conversies; de toets slaat dan niet over) |
| state-toets (build zonder N → echte build), Linux | groen | `state-toets: 8 van 8 builds zonder parameter N geslaagd` (x86_64) |
| state-toets, macOS | groen | `state-toets: 8 van 8 builds zonder parameter N geslaagd` (arm64) |
| tekst-toets (ook komma-locale) | groen | `tekst: geslaagd` op x86_64 en arm64 (met `PB_EIS_KOMMA=1`) |
| automatisering op zijn sample | groen | `automatisering: geslaagd` op x86_64 en arm64 |
| null-test met de waarden als automatisering | groen | `### Null-test CLAP (waarden als automatisering, x86_64)` en `(…, arm64)`: alle 4 gevallen `−∞ (bit voor bit gelijk) … ja` |
| fase-2-toetsen met de RTSan-bouw | groen | stap "Fase 2-toetsen met de RTSan-bouw" geslaagd, geen RTSan-melding |
| pluginval 10, VST3 Linux | groen | `pluginval VST3 Linux: Strictness level: 10 SUCCESS` |
| pluginval 10 `--rtcheck enabled`, AU macOS | groen | `pluginval AU: Strictness level: 10 SUCCESS` |
| pluginval 10 `--rtcheck enabled`, VST3 macOS | groen | `pluginval VST3: Strictness level: 10 SUCCESS` |
| auval -strict | groen | `auval: AU VALIDATION SUCCEEDED.` |
| null-tests van fase 1 (CLAP blok512/wisselend/state Linux en macOS, VST3 Linux/macOS, AU macOS) | groen | overal 4× `ja`, `max abs diff 0`; zelfoscillatie `terts 0.000 dB, omhullende 0.000 dB` |
| RTSan schoon + kanarie faalt | groen | `Intercepted call to real-time unsafe function 'malloc' in real-time context!` met `#1 … process(…) plugin/bron/plugin.cpp:437` |
| artefact `macos-keten_dub_echo` | aanwezig | `Artifact macos-keten_dub_echo has been successfully uploaded!` |

Ik heb geen toets of validator uitgezet. Lokaal draaiden vóór de push al: bouwen, `toets-fase2.sh` (met
`nl_NL.UTF-8`), de CLAP-null-test, clap-validator 0.4.1 (uit de bron), pluginval 10 op de VST3 onder xvfb en de
VST3-null-test via `pluginval test`. Alles was groen.

## Opmerkingen, onduidelijk of fout in het pakket
- **"stapjes waar nodig"**: geen parameter in `PARAMS` heeft `pb::STAPPEN`, dus er is niets te stappen. De schil
  ondersteunt het wel (afronden en `CLAP_PARAM_IS_STEPPED`). Het pakket zegt niet welke parameters stapjes zouden
  moeten krijgen; ik heb de DSP-vlaggen gevolgd.
- **Parameters zonder eenheid** (`eenheid ""`): SPEC.md zegt "—". Ik toon ze als procenten, omdat dat in een DAW
  het gewoonst is (Mix 37,5 %). Een kaal getal bij de invoer is dan ook een percentage ("50" = 0,5). Het pakket
  legt dit niet vast; met een eenheid "%" in het ontwerp (de waarde zelf in procenten) zou het eenduidig zijn.
- **Decimaalteken**: Nederlandse tekst zou een komma gebruiken, maar ik geef altijd een punt. Zo blijft de uitvoer
  hetzelfde, wat de host ook met `setlocale` doet. Bij de invoer mag een komma.
- **AU/VST3 krijgen de eenheid in de tekst**, niet als apart label: clap-wrapper geeft geen eenheid door (pluginval
  meldt `label - ` leeg). In Logic zie je dus bv. "375 ms" als waardetekst. Dat heb ik niet in Logic gecontroleerd
  (fase 7).
- **"Een build zonder parameter N"** staat niet uitgewerkt in het pakket. Ik heb het opgelost met een
  compile-vlag in de schil, niet met een tweede versie van de DSP. De DSP rekent dan met N op zijn standaard, net
  als een oude versie zonder die knop. Een echte oudere versie zou ook een andere DSP kunnen hebben; dat kan deze
  toets niet nabootsen zonder `dsp/` te veranderen.
- **State-versie**: `stateLoad` kijkt niet naar het versienummer, alleen naar de magie en de (id, waarde)-paren.
  Daardoor laadt een toekomstige versie met dezelfde opbouw gewoon (getoetst met versie 7). Verandert het formaat
  zelf ooit, dan moet de versie wél gelezen worden. ARCHITECTUUR.md zegt daar niets over.
- clap-validator heeft geen aparte test "state-reproducibility-flush" in 0.4.1. Flush wordt gedekt door
  `param-set-events` (flush-run tegen process-run) en door de state-toets (flush vóór activate en actief zonder process).
