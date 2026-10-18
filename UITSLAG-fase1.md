Stopcriterium gehaald.

# Uitslag fase 1 — proef G2 (tak `proef/g2`)

Laatste CI-run met code (groen, alle drie de jobs): https://github.com/CKrebbers/pluginbouwer/actions/runs/39156318496
(commit `e77c337`). Deze commit verandert alleen dit verslag en het vinkje in TASKS.md.

## Wat ik deed
- `plugin/src/plugin.cpp`:
  - **params**: 4 CLAP-parameters, id = nummer uit SPEC.md, bereik en standaard uit `Ontwerp::PARAMS`,
    `CLAP_PARAM_IS_AUTOMATABLE`, niet stepped. De schil zoekt de index bij het id (`zetParam` neemt de index).
    `get_value` leest een atomaire spiegel per parameter die de audiothread bijwerkt. `value_to_text`/`text_to_value`
    volgen al de tabel van SPEC.md (komma, "8,00 kHz", "10,0 ms", procenten); de toets daarvan is fase 2.
  - **process**: stille invoer (één nulbuffer per kanaal, gereserveerd in activate op `max_frames`), blok gesplitst
    bij elk event: tot de tijd van het event `verwerk`, dan het event toepassen. Parameter-events zetten Ontwerp en
    spiegel; transport (als de host het geeft) gaat naar `zetTransport` (alleen een sequencer leest het; hier is er
    geen). Na het blok wordt `staartSec()` atomisch bewaard voor `tail.get` (−1 → `INT32_MAX`).
  - **noten** precies volgens ARCHITECTUUR.md "Noten": `pb::NootStapel`, noot-aan/uit (key buiten 0..127 genegeerd,
    zonder stap 2), `NOTE_CHOKE` → `leeg`, MIDI 0x9n/0x8n (velocity 0 = uit) en CC 120/123; daarna altijd
    `zetParam(0, nootVerhouding(n))` + `zetParam(1, 1)` of alleen `zetParam(1, 0)`. De spiegel wordt door noten niet
    aangeraakt. `reset` maakt de stapel leeg na `ontwerp.reset()`. Note port: alleen `CLAP_NOTE_DIALECT_CLAP`.
  - **state**: `"PBST"`, versie 1, aantal, per parameter (id u32, waarde f64), little-endian. Laden per id; andere
    magic, versie 0 of te kort voor `aantal` = false zonder iets te veranderen; hogere versie laadt per id. Niet actief:
    direct in het Ontwerp; actief: waarden klaarzetten + atomaire vlag, process/flush neemt ze over vóór de events van
    dat blok. Daarna `rescan(CLAP_PARAM_RESCAN_VALUES)` en (als actief) `request_flush`.
  - **params.flush** neemt klaargezette state over en past de events toe (zelfde `pasToe` als process).
  - latency uit het Ontwerp (0), tail uit `staartSec()` (0,5 s standaard: loslaten 450 ms + 50 ms).
- `.github/workflows/ci.yml`:
  - linux: `ci-null.sh pluginval` op de VST3 (pluginval onder `xvfb-run -a`) en `ci-null.sh clap build`.
  - macos: `ci-null.sh clap build`, `ci-null.sh pluginval` op de geïnstalleerde VST3 en AU (absolute paden,
    valkuil 18). auval blijft de laatste stap.
  - rtsan: draait nu ook `--state-rondje` (blok 512 en wisselend), wat in fase 0 nog niet kon.

## Poorten (run 39156318496)
| poort | uitslag | bewijs |
|---|---|---|
| null-test CLAP Linux (5 gevallen × blok 512, wisselend, state-rondje) | groen | elke rij `−∞ (bit voor bit gelijk) … ja` |
| null-test CLAP automatisering (blok 512 en wisselend) | groen | `automatie (events midden in blokken) \| −∞ (bit voor bit gelijk)` 2× |
| null-test noten CLAP en MIDI, blok 512 en wisselend (Linux) | groen | `noten (CLAP), blok 512 \| −∞ (bit voor bit gelijk)`, idem MIDI en `blok 3,128,1,4096,77` |
| null-test VST3 Linux (pluginval test) | groen | tabel 5× bit voor bit; `PASSED: <geval>` / `max abs diff 0 <= tolerance 3.16228e-05` |
| null-test CLAP macOS arm64 | groen | stap geslaagd (het script faalt als `vergelijk.mjs` exit 1 geeft); de tabel staat vóór de laatste logregels die de GitHub-tools teruggeven |
| null-test VST3 macOS (pluginval test) | groen | stap geslaagd (idem) |
| null-test AU macOS (pluginval test) | groen | tabel `au-macos` 5× `−∞ (bit voor bit gelijk)`; `PASSED: impuls-96k … sweep-kwart`, `max abs diff 0` |
| clap-validator 0.4.1 | groen | stap geslaagd (exit 0 én grep op `0 failed`); lokaal op dezelfde code: `44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped` (params en state draaien nu mee) |
| pluginval 10 VST3 Linux | groen | stap geslaagd (grep `^SUCCESS`); lokaal ook `SUCCESS` |
| pluginval 10 VST3 macOS (`pluginval-rt.sh`) | groen | stap geslaagd |
| pluginval 10 AU macOS (`--rtcheck enabled --repeat 3`) | groen | stap geslaagd (grep `^SUCCESS`) |
| auval -strict | groen | 4 parameters met tekst (`Minimum = 10,0 ms, Default = 450 ms`), `Tail Time PASS`, `Test MIDI PASS`, `AU VALIDATION SUCCEEDED.` |
| RTSan schoon | groen | stap geslaagd, nu met state-rondje |
| RTSan-kanarie faalt | groen | `RealtimeSanitizer: unsafe-library-call` … `` `malloc` `` … `#1 plProcess … plugin.cpp`, `kanarie exit: 43` |

## Pushes
Twee: de code (`e77c337`, meteen groen in alle jobs) en dit verslag. Vóór de push heb ik lokaal (Linux x86_64)
gebouwd en `ci-null.sh clap` (alles bit voor bit, ook noten CLAP/MIDI en het state-rondje), `ci-null.sh pluginval`
op de VST3, pluginval strictness 10 op de VST3 en clap-validator 0.4.1 gedraaid. macOS en RTSan kon ik alleen in CI
toetsen.

## Wat onduidelijk of fout was, en waar ik bijna vastliep
- Niet vastgelopen. Valkuil 1 ("gelijk aan de standaardwaarden") kwam niet voor: de patch zat er sinds fase 0 al op.
- De GitHub MCP-tools geven alleen de staart van een log, en de zip met alle logs is vanuit deze sandbox niet te
  downloaden (403 van de proxy). Voor stappen midden in een job is "stap geslaagd" daarom het bewijs; de scripts
  falen zelf op een rode null-test, dus dat is geen zwakke poort, maar de tabel zelf heb ik voor macOS-CLAP/VST3 niet
  uit het log gelezen. Wie het per regel wil zien: de job summary (`GITHUB_STEP_SUMMARY`) bevat alle tabellen.
- `value_to_text`/`text_to_value` heb ik al volgens SPEC gemaakt (fase 2 vraagt de toetsen ervan); clap-validator
  `param-conversions` slaagt erop.
- Klein punt in het pakket: `ci-null.sh pluginval` zet `$PV` ongequote neer, zodat `"xvfb-run -a …/pluginval"` als
  pluginval-commando werkt; dat is handig maar nergens gedocumenteerd.
- Niets uitgezet of overgeslagen.
