# ARCHITECTUUR — Dub-echo

## Lagen
```
dsp/                 DE DSP (gegeven, niet herschrijven): pb_*.h + de keten als constante (ontwerpen/keten_dub_echo.h)
plugin/              de CLAP-schil (fase 0–2) en het frontpaneel (fase 3); clap-wrapper maakt VST3 en AUv2
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
- Parameteroverdracht: alleen via CLAP-events (`process` en `params.flush`). Geen gedeelde variabelen tussen threads.

## State
Binair, klein en vooruit-compatibel: `"PBST"`, versie (u32), aantal (u32), dan per parameter (id u32, waarde f64).
Laden gaat per id: een onbekend id (van een nieuwere versie) wordt overgeslagen, een ontbrekend id houdt zijn
standaard. Parameter-id's veranderen nooit.
- Na `state.load` roept de schil `host_params->rescan(CLAP_PARAM_RESCAN_VALUES)` aan (clap-validator
  state-reproducibility eist het). Kapotte state (leeg, andere kop, te kort) geeft false en verandert niets.
- `state.load` komt op de mainthread. Is de plugin niet actief, dan krijgt het Ontwerp de waarden meteen; is hij actief,
  dan zet hij een atomische vlag en neemt de eerstvolgende plek die het Ontwerp bezit (process, `params.flush`, of
  `activate` na een deactivate) de waarden over.
- Getoetst door `gereedschap/pb-state-toets` met een bouw zonder parameter N (`build/toets/zonder-N.clap`, gebouwd met
  `-DPB_ZONDER_PARAM=N`): oud → nieuw, nieuw → oud, laden terwijl actief, versie 2 met onbekend id, kapotte state.

## Parameters als tekst
`params.value_to_text`/`text_to_value` (plugin/src/param_tekst.h) tonen gewone eenheden: "375 ms", "3.00 kHz",
"265 Hz", "6.0 dB", "0.71 Q", en 0..1-knoppen als procent ("37.5 %"). Alleen de tekst rondt af, de waarde niet.
Lezen kent ook "1.2 s", "3k", een komma en een kaal getal (in de getoonde eenheid; "50" bij Mix = 50 %).
Tekst → waarde → tekst geeft dezelfde tekst; de tekst hangt niet af van de locale van de host. Geen enkele parameter
heeft stappen (`STAPPEN`); zou er een komen, dan toont en leest hij hele getallen.

## Frontpaneel (fase 3)
```
paneel.h/.cpp     indeling, tekenen en muis; platformvrij (design.md: 8 knoppen, 270°, naam boven, waarde onder)
tekenaar.h/.cpp   software-tekenbord (vlakken, ringen, bogen, tekst via stb_truetype) in een pixelbuffer
venster.h         het venster van de host: paneel_mac.mm (Cocoa NSView) of paneel_x11.cpp (X11-kindvenster)
ui_wachtrij.h     lock-vrije ring paneel (mainthread) → process/flush
fonts/            IBM Plex Mono en Silkscreen (SIL OFL 1.1), door CMake als bytes in de plugin gebakken
```
- Het paneel leest alleen de spiegel `waarde[]` (wat `params.get` ook geeft), met een timer van ±30 Hz (Cocoa:
  NSTimer; X11: timer-support van de host). Het raakt het Ontwerp en de audiothread nooit.
- Een knop stuurt **via de host**: het paneel zet BEGIN / WAARDE / EIND in de wachtrij en roept
  `host_params->request_flush`. Wie het Ontwerp op dat moment bezit (process op de audiothread, of `params.flush`)
  haalt de berichten op, pusht ze als `CLAP_EVENT_PARAM_GESTURE_BEGIN` / `PARAM_VALUE` / `GESTURE_END` naar de host
  (clap-wrapper maakt er beginEdit/performEdit/endEdit van in VST3 en AU-parametergebaren in AUv2) en past de waarde
  toe zoals een event van de host. Een CLAP-host stuurt een uitvoerevent niet terug, dus de plugin past hem zelf toe.
- Tot de host de waarde terugmeldt in `waarde[]` toont het paneel wat het stuurde; zet de host intussen zelf iets
  anders (automatisering, state), dan wint de host.
- Slepen omhoog = meer, 200 punten = het hele bereik (lineair, zoals clap-wrapper normaliseert); Shift = 10× fijner;
  dubbelklik = standaard. Minimaal 560 × 220 punten; groter schaalt alles uniform. Maat in punten op Cocoa, in
  pixels op X11 (`gui.set_scale`).
- De NSView-klasse krijgt bij het laden een naam die uniek is per binary (AU, VST3 en CLAP in één proces botsen
  anders).
- Getoetst door `gereedschap/pb-paneel` (CLAP-host: opent het paneel, bedient het met nagemaakte muis-events, maakt
  PNG's) op Linux/X11 en macOS/Cocoa, onder RTSan (gebaren door een real-time process) en onder TSan (paneel en
  audiothread tegelijk; een kanariebouw `PB_UI_KANARIE` laat het paneel de DSP zelf zetten en MOET falen).
- Bekend: de AUv2-host van clap-wrapper v0.16.0 doet niets met `request_flush`. Speelt een AU-host niet (geen Render),
  dan blijft een knopverandering in de wachtrij tot de volgende Render; het paneel toont de nieuwe waarde wel al.

## Presets (fase 4)
Een preset is een state-blob plus naam, als JSON naast de plugin (`presets/<naam>.json` met de parameterwaarden
per naam). Fabriekspresets komen uit de varianten van de tool.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
