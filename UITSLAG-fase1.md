Stopcriterium gehaald.

# Uitslag fase 1 — Toetsenpad (proef g1)

Laatste CI-run met code (commit `7101af8`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39155002038
(jobs linux, rtsan en macos alle drie success). De commit met dit verslag verandert alleen docs; zijn run draait dezelfde jobs.

## Wat ik deed
- `plugin/src/toetsenpad.cpp`:
  - **process** splitst het blok bij elk event (parameter, noot, MIDI): eerst `verwerk` tot de sample van het event,
    dan het event toepassen. `verwerk` krijgt een stille invoer: één buffer met nullen per kanaal, gereserveerd in activate.
  - **params**: de 4 parameters uit `PARAMS` (id = nummer uit SPEC.md, bereik en standaard uit `PARAMS`),
    automatiseerbaar en continu. `get_value` en `state.save` lezen een atomaire spiegel per parameter, die de audiothread
    bijwerkt. `flush` past events toe; als de plugin actief is gebeurt dat op de audiothread, anders op de mainthread.
    `value_to_text`/`text_to_value` volgen de tabel in SPEC.md al: komma als decimaalteken, geen locale.
  - **noten** precies zoals ARCHITECTUUR.md "Noten": `pb::NootStapel`, noot-aan/-uit/choke, MIDI 0x9n/0x8n en CC 120/123.
    Daarna stap 2 (`zetParam(0, nootVerhouding(n))` en `zetParam(1, 1)`, of `zetParam(1, 0)`). Een noot zet de spiegel niet.
    Een key buiten 0..127 wordt genegeerd. De note port gebruikt alleen het CLAP-dialect (valkuil 19).
  - **state**: `"PBST"`, versie 1, aantal, per parameter (id u32, waarde f64), little-endian. Laden gaat per id en begint
    vanaf de standaarden. Een onbekend id wordt overgeslagen. Andere magic, versie 0 of een te korte blob geeft false en
    verandert niets. Daarna `rescan(VALUES)`, en `request_flush` als de plugin actief is. Laden terwijl de plugin actief is
    zet de waarden in de spiegel en zet een atomaire vlag; process of flush neemt ze over vóór de events van het blok.
  - **latency** komt uit het Ontwerp (0).
  - **tail**: `staartSec()` wordt aan het eind van process en in activate omgerekend naar samples (−1 → `INT32_MAX`) en
    atomisch bewaard; `tail.get` leest die waarde.
  - **transport**: per blok `zetTransport` (deze keten heeft geen sequencer, dus het verandert niets).
- `.github/workflows/ci.yml`:
  - `actions/setup-node@v4` (Node 22) in linux en macos.
  - Linux: `ci-null.sh clap build` en `ci-null.sh pluginval` op de VST3, onder xvfb.
  - macOS: `ci-null.sh clap build`, en `ci-null.sh pluginval` op de geïnstalleerde VST3 en AU (absolute paden, valkuil 18).
  - RTSan rendert nu ook met `--state-rondje` (fase 0 had dat weggelaten omdat de state toen nog ontbrak).
  - De null-tests staan achteraan in elke job.
- `dsp/` en `testsignalen/` niet veranderd. Geen toets of validator uitgezet.

## Poorten (bewijs uit de logs van run 39155002038)
| poort | uitslag | bewijs |
|---|---|---|
| null-test CLAP, blok 512 / wisselend / state-rondje (5 gevallen elk), Linux x86_64 | groen | elk geval `−∞ (bit voor bit gelijk) … ja` |
| null-test CLAP, automatisering, blok 512 en wisselend, Linux | groen | `automatie (events midden in blokken) \| −∞ (bit voor bit gelijk)` |
| null-test noten CLAP en MIDI, blok 512 en 3,128,1,4096,77, Linux | groen | 4× `noten (CLAP/MIDI), blok … \| −∞ (bit voor bit gelijk) \| ja` |
| dezelfde null-tests CLAP, macOS arm64 | groen | `### Null-test CLAP (blok512, arm64)` enz., alles `bit voor bit gelijk`, ook de 4 notenrenders |
| null-test VST3 Linux (pluginval test) | groen | `### Null-test vst3-linux`: 5× `bit voor bit gelijk` |
| null-test VST3 macOS (pluginval test) | groen | `### Null-test vst3-macos`: 5× `bit voor bit gelijk` |
| null-test AU (pluginval test) | groen | `### Null-test au`: 5× `bit voor bit gelijk` (patch van valkuil 1 werkt; geen "gelijk aan de standaardwaarden") |
| clap-validator 0.4.1 | groen | `clap-validator: 44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped (exit 0)` (fase 0: 21 passed; de param- en state-toetsen draaien nu ook) |
| pluginval 10 VST3 Linux | groen | `pluginval VST3 Linux: exit 0, laatste regel: SUCCESS` |
| RTSan, plugin | groen | stap "RTSan door het .clap" success: 8 renders (noten CLAP/MIDI, automatisering, state-rondje; blok 512 en wisselend) |
| RTSan-kanarie moet falen | groen | `RTSan-kanarie gevangen zoals bedoeld (exit 43)`, `malloc` ← `pluginProcess` ← `doeProcess` |
| pluginval 10 `--rtcheck enabled --repeat 3` AU | groen | `pluginval AU: exit 0, laatste regel: SUCCESS` |
| pluginval 10 VST3 macOS via pluginval-rt.sh | groen | `SUCCESS`, `real-time-schendingen: 0, waarvan pluginval-host (MidiEventList::addEvent): 0` |
| auval -strict | groen | stap "auval -strict (bewijs)" success; die stap eist exit 0 én `AU VALIDATION SUCCEEDED`. De regel zelf viel buiten de laatste regels die de GitHub-tool teruggaf (valkuil 18): de null-tests staan er nu achter. |

## Pushes
Eén push tot CI groen was (`7101af8`). Vooraf had ik lokaal (Linux, clang 18) al gedraaid: `ci-null.sh clap` (alles
bit voor bit), `ci-null.sh pluginval` op de VST3 onder xvfb (bit voor bit), pluginval 10 validate (SUCCESS) en
clap-validator 0.4.1 (0 failed). Lokaal kon ik de AU en RTSan niet toetsen. RTSan kon niet omdat er geen clang 20 is.

## Onduidelijk of fout in het pakket
- **Reset en noten.** ARCHITECTUUR.md "Noten" 4 zegt alleen: `reset` maakt de stapel leeg. Dan blijft Poort op 1 staan
  als er een noot klonk, en die noot blijft hangen (Logic: stop of reset). Ik doe na het legen stap 2, maar alleen als
  er een noot in de stapel zat. Zonder noten blijft Poort wat de knop zegt, zodat de null-test van pluginval (Poort
  0,5, mogelijk een reset vóór het renderen) niet verandert. Graag een zin in ARCHITECTUUR.md die dit vastlegt.
- **Bewijs van auval valt weg.** Met de null-tests erbij is het macOS-log zo lang dat de regel
  `AU VALIDATION SUCCEEDED` buiten de staart valt die de GitHub-tool teruggeeft (valkuil 18). De stap faalt zelf als
  de regel ontbreekt, dus groen blijft bewijs. Toch zou het helpen als de auval-stap één samenvattende regel helemaal
  achteraan in het log zette.
- Verder was alles duidelijk; de valkuilen 1, 11, 18 en 19 waren al geregeld (patches en fase 0) of gewoon te volgen.
