# ARCHITECTUUR — Dub-echo

## Lagen
```
dsp/                 DE DSP (gegeven, niet herschrijven): pb_*.h + de keten als constante (ontwerpen/keten_dub_echo.h)
plugin/              de CLAP-schil (fase 0–2) en het frontpaneel (fase 3), clap-wrapper maakt VST3 en AUv2
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
- **Mainthread**: `init`, `activate` (hier wordt geheugen gereserveerd), state, parameters lezen en tekst, de gui.
- Geen locks, geen allocatie, geen syscalls in `process` (RTSan bewaakt het in CI).
- Parameteroverdracht: alleen via CLAP-events (`process` en `params.flush`). Geen gedeelde variabelen tussen threads.

## State
Binair, klein en vooruit-compatibel: `"PBST"`, versie (u32), aantal (u32), dan per parameter (id u32, waarde f64).
Laden gaat per id: een onbekend id (van een nieuwere versie) wordt overgeslagen, een ontbrekend id houdt zijn
standaard. Parameter-id's veranderen nooit.
Na het laden vraagt de plugin de host om `params.rescan(CLAP_PARAM_RESCAN_VALUES)` (clap-validator eist dat), en
als hij actief is ook `request_flush`; process of flush neemt de waarden dan over.
Getoetst (fase 2, `plugin/toets`): per parameter N een build zonder N (`PB_ZONDER_PARAM=N`, alleen als toets
gebouwd); de state daarvan laadt in de echte build met exact dezelfde waarden, N op zijn standaard en bit voor bit
hetzelfde geluid; en omgekeerd.

## Parameters: tekst en eenheden
`value_to_text`/`text_to_value` in gewone eenheden. De waarde wordt eerst afgerond op wat de tekst toont, dan pas
krijgt hij zijn eenheid, zodat tekst → waarde → tekst stabiel is:

| eenheid in PARAMS | tekst | invoer die ook werkt |
|---|---|---|
| ms | `20.0 ms` … `99.9 ms`, `100 ms` … `999 ms`, `1.000 s` … | `375`, `1,2 s`, `250ms` |
| Hz | `20.0 Hz` … `99.9 Hz`, `100 Hz` … `999 Hz`, `1.00 kHz` … | `3k`, `1,5 kHz`, `2500` |
| dB | `6.0 dB` | `12db`, `-3` (geklemd) |
| Q | `0.707` | `2 q` |
| geen (0..1, Terugkoppeling 0..1,25) | procenten: `37.5 %` | `50`, `110%` (een kaal getal is ook procent) |

Altijd een punt als decimaalteken, ook als de host een locale met komma zet; bij invoer mag een komma. Parameters
met `pb::STAPPEN` (nu geen) worden op hele getallen gezet. Waarden buiten het bereik worden geklemd.
`params.flush` werkt zonder process: vóór activate (mainthread) en actief zonder process (audiothread).

## Frontpaneel (fase 3)
`plugin/bron/paneel.cpp` tekent het paneel uit design.md zelf in een pixelbuffer (plat, zonder grafische
bibliotheek; IBM Plex Mono en Silkscreen zitten als OFL-lettertypes in de plugin, `plugin/extern`). Per platform
een venster: `venster_mac.mm` (NSView, klasse met een unieke naam per binary, NSTimer 30 Hz) en `venster_x11.cpp`
(kindvenster, events en hertekenen via de CLAP-extensies posix-fd-support en timer-support van de host).
clap-wrapper geeft de gui door aan de VST3 (IPlugView) en de AU (CocoaUI).
- Knop: 270° (−135° tot +135°), slepen omhoog/omlaag, 200 punten = het hele bereik, Shift = 10× fijner,
  dubbelklik = standaard. Tijd en de frequenties draaien logaritmisch (de host normaliseert wel lineair).
- Grootte: standaard 700 × 275, minimaal 560 × 220, vrij schaalbaar; de indeling schaalt gelijkmatig mee en staat in
  het midden. Op een Retina-scherm tekent hij op de pixelresolutie.
- **Threads**: het paneel draait alleen op de mainthread en raakt de DSP nooit. Het leest de atomische spiegel van de
  waarden en schrijft alleen per parameter tellers (gebaar begin/eind, nieuwe waarde) plus de waarde, dan
  `host->params->request_flush`. process (of flush) neemt die over als een event op sample 0 (DSP + spiegel) en meldt
  de host `PARAM_GESTURE_BEGIN`, `PARAM_VALUE`, `PARAM_GESTURE_END`; zo neemt de host het op als automatisering.
  Tot process/flush de waarde nam toont het paneel zijn eigen waarde (geen terugspringende knop).
- Getoetst met `plugin/toets/pb-paneel.cpp` (`toets-fase3.sh`): een host die het paneel echt opent, met de muis
  bedient en de events controleert, met flush en met process op een audiothread (ook onder RTSan), plus
  schermafbeeldingen als CI-artefact. pluginval draait zijn editor-toetsen (geen `--skip-gui-tests`).

## Presets (fase 4)
Een preset is een state-blob plus naam, als JSON naast de plugin (`presets/<naam>.json` met de parameterwaarden
per naam). Fabriekspresets komen uit de varianten van de tool.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
