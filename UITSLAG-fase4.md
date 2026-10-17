Stopcriterium gehaald.

# Uitslag fase 4 — Dub-echo (tak proef/f2)

Laatste CI-run met code (commit c19eddf): run 81 —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39042445268 — alle drie de jobs groen, bij de **eerste** push.
De commit met dit bestand verandert alleen docs; zijn CI-run draait dezelfde code.

## Wat ik deed
- **Presets in het binary.** `plugin/CMakeLists.txt` leest bij het configureren `presets/*.json` en maakt
  `build/presets.cpp` (tabel volgens `plugin/presets.h`).
  - Een onbekende parameternaam of een waarde buiten het bereik is een compileerfout (`static_assert` tegen `PARAMS`).
  - Volgorde: `standaard` eerst (AU-presetnummer 0), de rest op bestandsnaam.
  - De bestandsnaam zonder `.json` is de load_key en verandert nooit.
- **Laden** (`dub_echo.cpp`, `presetLaden`) gaat langs hetzelfde pad als state load (`pasToe`, uit `stateLoad`
  gehaald):
  - genoemde parameters krijgen hun waarde (geklemd), de rest gaat naar zijn standaard;
  - de hostkopie wordt bijgewerkt;
  - draait de plugin, dan neemt de audiothread de waarden over aan het begin van process;
  - daarna `params.rescan` en `clap_host_preset_load.loaded` (bij een fout `on_error` en false).
- **CLAP preset-discovery**: de factory `clap.preset-discovery-factory/2` (ook onder het draft-id), en
  `clap.preset-load/2` (ook draft).
  - Er is **per preset één provider** (`nl.varve.pluginbouwer.dub-echo.preset.<sleutel>`).
  - Elke provider heeft één locatie van soort `PLUGIN` (location `nullptr`, `FACTORY_CONTENT`) met die ene preset:
    naam, load_key, plugin-id, uitleg en maker.
  - Waarom niet één provider met alle presets in een container, zie hieronder.
- **AU factory presets**: clap-wrapper v0.16.0 heeft ze niet, dus kwam er een derde patch:
  `patches/clap-wrapper-presets-als-au-factory-presets.patch`. De naam valt achteraan in naamvolgorde, zodat hij op
  de twee bestaande patches past.
  - `WrapAsAUV2` doorloopt bij het maken van de instantie de preset-discovery-factory van de CLAP: alle providers,
    locaties van soort `PLUGIN`, presets voor dit plugin-id.
  - `GetPresets` geeft die lijst aan de host.
  - `NewFactoryPresetSet` laadt de gekozen preset via `preset-load` (main thread), zet hem als huidige preset, en
    zet de naam die `SaveState` (ClassInfo) bewaart.
  - Een preset maakt waarden ongedaan die de host vóór Initialize zette (`_paramsBeforeInit` uit patch 1), anders
    zouden die bij Initialize over de preset heen komen.
  - De patch is generiek: niets in de wrapper weet van Dub-echo.
- **Toetsen** (er is niets uitgezet of weggehaald):
  - `gereedschap/presets-plat.mjs` zet `presets/*.json` plat. De eigen toetsen toetsen zo tegen de JSON zelf, niet
    tegen de tabel die CMake ervan maakte.
  - `plugin/toetsen/preset-toets.cpp` (Linux, macOS, en RTSan) eist:
    - ontdekken: providers, locaties, namen, uitleg, load_keys, plugin-id en volgorde;
    - laden: elke preset zet alle 8 parameters exact; een onbekende sleutel of locatie geeft false en `on_error`;
    - geluid: een preset geladen vóór activate of terwijl de plugin draait, is bit voor bit gelijk aan dezelfde
      waarden als host-events;
    - een preset midden in het renderen wordt overgenomen.
  - `plugin/toetsen/au-preset-toets.cpp` (macOS, via AudioToolbox op de geïnstalleerde AU) eist:
    - `FactoryPresets` heeft de juiste namen en volgorde;
    - na het zetten van `PresentPreset`, vóór en na Initialize, kloppen de presetnaam, alle AU-parameters, en de
      ClassInfo (naam en de waarden in de CLAP-state);
    - na een SetParameter vóór Initialize wint de preset;
    - een onbekend presetnummer geeft een fout.
  - CI:
    - een stap die eist dat clap-validator `preset-discovery-crawl`, `-descriptor-consistency` en `-load` laat
      slagen (niet overslaan);
    - een stap die eist dat elke presetnaam in de uitvoer van `auval -strict` staat;
    - de twee eigen toetsen, ook onder RTSan.
- ARCHITECTUUR.md ("Presets"), SPEC.md ("Fabriekspresets", "Validators") en TASKS.md bijgewerkt. `dsp/` en
  `testsignalen/` niet aangeraakt.

## Poorten (run 81)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| clap-validator preset-tests | groen | "3 tests run, 3 passed, 0 failed, 0 warnings, 0 skipped"; check-stap: "preset-discovery-crawl success", "preset-discovery-descriptor-consistency success", "preset-discovery-load success", "alle 3 geslaagd" |
| auval toont de presets | groen | "HAS FACTORY PRESETS / ID: 0 Name: Standaard / ID: 1 Name: Lange dub / ID: 2 Name: Slapback / ID: 3 Name: Versleten band / ID: 4 Name: Zelfoscillatie"; "auval toont: Standaard" … "Zelfoscillatie"; "AU VALIDATION SUCCEEDED." |
| au-preset-toets (macOS) | groen | "factory preset 0: Standaard" … "4: Zelfoscillatie"; "ok: preset 1 'Lange dub' (na SetParameter vóór Initialize, dan Initialize): PresentPreset, alle 8 parameters en de ClassInfo (naam en CLAP-state) kloppen"; elke preset vóór en na Initialize "ok"; "onbekend presetnummer: fout -10851"; `GESLAAGD` |
| preset-toets (Linux, macOS, RTSan) | groen | "provider 0: PB Dub-echo: Standaard (nl.varve.pluginbouwer.dub-echo.preset.standaard)" …; "preset 'Lange dub' laadt: alle 8 parameters exact"; "geluid 'Zelfoscillatie': preset = host-events, bit voor bit (204800 samples, 0 verschillend)"; "preset midden in het renderen: gelijk tot blok 100, daarna anders"; `GESLAAGD`; RTSan: "RTSan: geen overtreding in process na een preset" |
| clap-validator (alles) | groen | "44 tests run, 36 passed, 0 failed, 0 warnings, 8 skipped" (fase 3: 33 passed, 11 skipped; de 3 preset-tests draaien nu); fase-2-subset "alle 9 geslaagd"; fuzz "OK: fuzzing finished successfully" |
| pluginval 10, VST3 Linux/xvfb | groen | "Result: 537 tests passed, 0 tests failed", `SUCCESS` |
| pluginval 10 `--rtcheck`, AU en VST3 macOS (zonder `--skip-gui-tests`) | groen | AU `SUCCESS` ("Time taken to run all tests: 39 secs"); VST3 "Result: 537 tests passed, 0 tests failed", `SUCCESS` |
| auval -strict | groen | "AU VALIDATION SUCCEEDED." |
| null-tests (fase 1) | groen, bit voor bit | CLAP Linux/macOS (blok 512, wisselend, state), VST3 Linux/macOS, AU macOS: alle gevallen "−∞ (bit voor bit gelijk)"; zelfoscillatie "terts 0.000 dB, omhullende 0.000 dB" |
| eigen toetsen fase 2 en 3 | groen | elke aanroep `GESLAAGD` (tekst, automatisering, state zonder N voor N = 1..8, ui-toets, gui-toets) |
| RTSan schoon + kanarie faalt | groen | "RTSan: geen overtreding in process" / "… flush" / "… met UI-berichten" / "… na een preset"; kanarie: "Intercepted call to real-time unsafe function `malloc` … dub_echo.cpp:265", "Kanarie gevangen zoals bedoeld (exitcode 43)" |
| artefacten | groen | "Artifact macos-keten_dub_echo has been successfully uploaded!"; "Artifact macos-schermafbeelding has been successfully uploaded!" |

## Waar het schuurde / wat onduidelijk of fout was
- **clap-validator 0.4.1 loopt vast op een container met meer dan één preset.** Dit is een fout in de validator, niet
  in de plugin.
  - Mijn eerste versie had één provider met alle vijf presets als container (load_key per preset). Dat is de gewone
    vorm voor presets in de plugin.
  - `preset-discovery-crawl` en `-load` liepen daarop vast: "Timed out after 45 seconds".
  - Met gdb gevonden: `MetadataReceiver::begin_preset` (`src/plugin/preset_discovery/metadata_receiver.rs`) houdt de
    `result`-mutex vast en roept `flush_preset` aan, die dezelfde mutex pakt. Vanaf de tweede preset wacht hij dus op
    zichzelf (stack: `lock_contended ← flush_preset ← begin_preset ← providerMetadata`).
  - Master van clap-validator (okt 2026) heeft dezelfde code.
  - Ik heb de validator niet aangepast. De plugin heeft nu per preset een provider met een container van één.
    Dat is geldige CLAP, en de ongewijzigde validator draait alle drie de preset-tests en laat ze slagen.
  - Voor het pakket: dit zou valkuil 10 in CLAUDE.md kunnen worden. Wordt de validator gerepareerd, dan kan één
    provider met een container terug. Dat is netter voor hosts die locaties samenvoegen: vijf providers die elk een
    `PLUGIN`-locatie melden is ongewoon, al staat het niet verboden in de spec. Ik heb het niet in een CLAP-host als
    Bitwig of Reaper kunnen bekijken.
- **clap-wrapper v0.16.0 kent geen AU factory presets.** Zonder de derde patch zou Logic geen presetlijst tonen. De
  patch staat in `patches/` en gaat er net als de andere op via `pas-patches-toe.sh`. Een kandidaat om upstream aan te
  bieden.
- **VST3 krijgt geen presetlijst** (clap-wrapper geeft geen program list door). De prompt vraagt alleen CLAP en AU.
- **Waarschuwingen** (geen fouten; de poorten zijn groen):
  - auval "Preset name is not retained in retrieved class data" stond er in fase 3 (run 71) al. Het komt dus niet
    door de presets. Mijn eigen toets ziet de naam wel terug in de ClassInfo na het zetten van een preset.
    Waarschijnlijk zet de `RestoreState` van de wrapper de huidige AU-preset niet bij.
  - Nieuw is pluginval AU: "!!! WARNING: Current program is -1... Is this correct?". Een AU zonder gekozen
    fabriekspreset meldt nummer −1 ("Untitled"), en dat is de bedoelde stand. Een verse plugin staat wel gelijk aan
    "Standaard", maar de wrapper weet niet dat preset 0 de standaarden zijn, dus ik heb hem niet als huidige preset
    gezet.
- **ARCHITECTUUR.md zei "een state-blob plus naam, als JSON naast de plugin"**, maar de JSON in `presets/` bevat
  geen blob, alleen waarden per naam. Losse bestanden naast een Linux-.clap (één bestand, geen bundel) zijn lastig, en
  ze vragen JSON-parsing in de plugin. De presets zitten daarom in het binary. ARCHITECTUUR.md is aangepast.
- In Logic (fase 7) is dit de plek om na te kijken: de presetlijst bovenin het pluginvenster moet de vijf namen tonen,
  en een keuze moet de knoppen verzetten. Dat is in CI getoetst met AudioToolbox, niet in Logic zelf.
