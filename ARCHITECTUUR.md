# ARCHITECTUUR — Dub-echo

## Lagen
```
dsp/                 DE DSP (gegeven, niet herschrijven): pb_*.h + de keten als constante (ontwerpen/keten_dub_echo.h)
plugin/              de CLAP-schil (fase 0–2), clap-wrapper maakt VST3 en AUv2
plugin/presets.h     de fabriekspresets als tabel (fase 4; CMake maakt hem uit presets/*.json)
plugin/ui/           het frontpaneel (fase 3): paneel.h (indeling, knop <-> waarde, zonder platformcode),
                     paneel_mac.mm (NSView, CoreGraphics/CoreText), lettertypen/ (Plex Mono, Silkscreen; OFL, ingebed)
gereedschap/         pb-render (CLAP-host), vergelijk.mjs (null-test + statistisch), ci-null.sh,
                     presets-plat.mjs (presets/*.json plat, voor de presettoetsen)
presets/             de fabriekspresets (JSON: naam, uitleg, waarden per parameternaam)
testsignalen/        invoer + verwachte uitvoer (uit de WASM van de tool) + pluginval-configs
```

## De keten
10 knopen, uitgevoerd in een vaste volgorde per sample (SPEC.md "Keten"). De ketenmotor
(`dsp/pb_graaf.h`) leest de beschrijving `PB_KETEN`; `dsp/graaf_ontwerp.h` zet die achter het Ontwerp-contract:
`geheugenNodig`, `activeer`, `reset`, `zetParam`, `leesParam`, `verwerk`, `latentie`, `staartSec`, `PARAMS`, `META`.
- `zetParam(i, v)` en `leesParam(i)` nemen de INDEX in `PARAMS` (0..n−1), niet het parameter-id; de schil zoekt de
  index bij het id.
- `staartSec()` leest de toestand van de smoothers en hoort dus bij de audiothread: reken hem aan het eind van process
  (en in activate) uit en bewaar hem atomisch; `tail.get` leest die waarde.

## Threads
- **Audiothread**: alleen `process`. Events (parameterwaarden) worden op hun sample toegepast: het blok wordt bij elk
  event gesplitst; de DSP rekent per sample, dus de uitvoer is gelijk aan die zonder splitsen.
- **Mainthread**: `init`, `activate` (hier wordt geheugen gereserveerd), state, parameters lezen en tekst.
- Geen locks, geen allocatie, geen syscalls in `process` (RTSan bewaakt het in CI).
- **UI-thread** (= main thread): het paneel; zie "Frontpaneel".
- Parameteroverdracht: alleen via CLAP-events (`process` en `params.flush`); de UI via een wachtrij die process/flush leegt. Geen gedeelde variabelen tussen threads,
  behalve per parameter een atomische kopie van de hostwaarde (voor `get_value` en state save) en een atomische vlag
  voor een state load terwijl de plugin draait.
- `params.flush` buiten process: niet actief → main thread, waarden direct naar het Ontwerp; actief → audiothread
  tussen twee process-aanroepen (nooit tegelijk). Een event via flush vóór sample t geeft bit voor bit hetzelfde als
  datzelfde event op sample t in process (`plugin/toetsen/param-toets automatisering`).
- Uitvoer: de schil maakt een subnormale uitvoersample 0 (alleen bits, ná de DSP). De DSP poetst zijn invoer en
  toestand, maar zeer zachte normale invoer (~1e-38) komt via het droge pad subnormaal uit `Blok::uit`.

## Frontpaneel (fase 3)
- CLAP gui-extensie, alleen api `cocoa`, ingebed (macOS; build met `PB_MET_UI`). clap-wrapper hangt dezelfde NSView in
  de AU (`kAudioUnitProperty_CocoaUI`) en de VST3 (`IPlugView`). Op Linux heeft de plugin (nog) geen paneel.
- 720 × 270 punten, vrij schaalbaar vanaf 560 × 220; de inhoud schaalt gelijkmatig en staat in het midden.
- **De UI raakt de DSP nooit.** Hij leest de atomische hostkopie (30× per seconde, ook voor automatisering), en stuurt
  gebaar/waarde door een lock-free wachtrij (één schrijver: main thread; één lezer: process of flush) en vraagt
  `host_params.request_flush`. process/flush past elk bericht toe als een host-event op sample 0 (vóór de host-events)
  en meldt het de host via out_events als `PARAM_GESTURE_BEGIN` / `PARAM_VALUE` / `PARAM_GESTURE_END`. Een knop via
  de UI geeft daardoor bit voor bit hetzelfde geluid als dezelfde waarde als host-event (`plugin/toetsen/ui-toets`).
- Slepen: omhoog/rechts = meer, 200 punten = een hele slag, shift = 10× fijner; dubbelklik = standaard; scrollwiel.
  Tijd, Toon en Laag weg lopen logaritmisch op de knop (de host normaliseert zelf lineair). Een gebaar begint pas bij
  de eerste beweging. Is de wachtrij vol (de host roept lang niets aan), dan vervalt wat niet past; gebaren blijven heel.
- Getoetst: `ui-toets` (plugin-kant met een stub-view, Linux/macOS/RTSan) en `gui-toets` (macOS: het echte paneel met
  muis-events, kleuren uit design.md, maten, en PNG's die CI als artefact `macos-schermafbeelding` bewaart).

## State
Binair, klein en vooruit-compatibel: `"PBST"`, versie (u32), aantal (u32), dan per parameter (id u32, waarde f64).
Laden gaat per id: een onbekend id (van een nieuwere versie) wordt overgeslagen, een ontbrekend id houdt zijn
standaard. Parameter-id's veranderen nooit.
Getoetst in CI: voor elk id N een build zonder parameter N (`-DPB_ZONDER_PARAM=N`, alsof hij nog niet bestond); zijn
state laadt in de volledige build (bewaarde waarden exact, N op zijn standaard, geluid bit voor bit gelijk, ook als
hij laadt terwijl de plugin draait), en de state van de volledige build laadt andersom in de oude.

## Presets (fase 4)
Een preset is een naam plus parameterwaarden per naam, als JSON in het pakket (`presets/<sleutel>.json`:
`naam`, `uitleg`, `waarden`). Fabriekspresets komen uit de varianten van de tool.
- **In het binary, niet als losse bestanden.** CMake leest `presets/*.json` bij het configureren en maakt er een tabel
  van (`build/presets.cpp`, zie `plugin/presets.h`). Een onbekende parameternaam of een waarde buiten het bereik is een
  compileerfout (`static_assert` tegen `PARAMS`). Volgorde: `standaard` eerst (AU-presetnummer 0), de rest op
  bestandsnaam. De sleutel (bestandsnaam zonder `.json`) is de load_key en verandert nooit; de naam mag veranderen.
- **Laden = een state zonder blob**: elke parameter die de preset noemt krijgt die waarde (geklemd), elke andere zijn
  standaard; dan hetzelfde pad als state load (hostkopie, en de audiothread neemt het over aan het begin van process
  als de plugin draait; `params.rescan`). Een preset klinkt daardoor bit voor bit als dezelfde waarden als host-events.
- **CLAP**: de factory `clap.preset-discovery-factory/2` (ook onder het draft-id) heeft **per preset één provider**,
  met één locatie van soort `PLUGIN` (location `nullptr`, vlag `FACTORY_CONTENT`) en daarin die ene preset met zijn
  load_key. `clap.preset-load/2` (`from_location`) laadt hem en meldt `loaded` (of `on_error`) aan de host.
  Waarom niet één provider met een container van alle presets (de gewone vorm): clap-validator 0.4.1 (en master,
  okt 2026) loopt daarop vast. `MetadataReceiver::begin_preset` houdt de `result`-mutex vast en roept
  `flush_preset` aan, die dezelfde mutex pakt; vanaf de tweede preset in een container wacht hij op zichzelf
  (`preset-discovery-crawl` en `-load`: "Timed out after 45 seconds"). Met één preset per container gebeurt dat niet.
- **AU**: clap-wrapper v0.16.0 kent geen AU factory presets. `patches/clap-wrapper-presets-als-au-factory-presets.patch`
  geeft `WrapAsAUV2` `GetPresets` / `NewFactoryPresetSet`: bij het maken van de instantie doorloopt hij de
  preset-discovery-factory van de CLAP (alle providers, locaties van soort `PLUGIN`, presets voor dit plugin-id),
  en een gekozen preset gaat via `preset-load` naar de CLAP. Daarna zijn het de gewone AU-parameters
  (`params.rescan`), en draagt de ClassInfo (de state die Logic bewaart) de presetnaam. Een preset wint van
  parameters die de host vóór Initialize zette (valkuil 1).
- Getoetst: `preset-toets` (CLAP: ontdekken, laden, geluid; Linux, macOS en onder RTSan), clap-validator
  `preset-discovery-crawl` / `-descriptor-consistency` / `-load` (moeten slagen, niet overgeslagen), auval (toont de
  namen) en `au-preset-toets` (macOS: de AU-lijst, zetten vóór en na Initialize, parameters en ClassInfo).
  Beide eigen toetsen lezen `presets/*.json` zelf (via `gereedschap/presets-plat.mjs`), niet de tabel van CMake.
- VST3: clap-wrapper v0.16.0 geeft (nog) geen presets door aan VST3 (geen program list); niet gevraagd in fase 4.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
