# ARCHITECTUUR — Dub-echo

## Lagen
```
dsp/                 DE DSP (gegeven, niet herschrijven): pb_*.h + de keten als constante (ontwerpen/keten_dub_echo.h)
plugin/              de CLAP-schil (fase 0–2), clap-wrapper maakt VST3 en AUv2
plugin/ui/           het frontpaneel (fase 3): paneel.h (indeling, knop <-> waarde, zonder platformcode),
                     paneel_mac.mm (NSView, CoreGraphics/CoreText), lettertypen/ (Plex Mono, Silkscreen; OFL, ingebed)
gereedschap/         pb-render (CLAP-host), vergelijk.mjs (null-test + statistisch), ci-null.sh
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
Een preset is een state-blob plus naam, als JSON naast de plugin (`presets/<naam>.json` met de parameterwaarden
per naam). Fabriekspresets komen uit de varianten van de tool.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
