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
Een preset is een JSON-bestand naast de plugin (`presets/<naam>.json`: `naam`, `uitleg`, `waarden` per
parameternaam). Fabriekspresets komen uit de varianten van de tool.
- **Waar**: CMake kopieert `presets/*.json` bij elke build in de plugin: op macOS in elke bundel (CLAP, VST3, AU) onder
  `Contents/Resources/presets`, op Linux in de map `PB Dub-echo-presets` naast `PB Dub-echo.clap`. De plugin vindt de
  map via het pad dat `clap_entry.init` krijgt (absoluut gemaakt met `realpath`). Installeren = de bundel kopiëren.
- **CLAP preset-discovery** (`clap_entry.get_factory`): één provider met één locatie van het soort FILE (die map,
  fabrieksinhoud, alleen als hij bestaat) en het bestandstype `json`. Elk bestand is één preset, zonder laadsleutel:
  naam, plugin-id, maker en uitleg.
- **preset-load** (`from_location(FILE, pad, NULL)`, mainthread): het bestand lezen (een eigen kleine JSON-lezer,
  getallen zonder locale), en dan per naam via hetzelfde pad als een state: een onbekende naam wordt overgeslagen,
  een parameter die de preset niet noemt krijgt zijn standaard, en de plugin vraagt een rescan (plus `request_flush`
  als hij actief is). Daarna roept hij `host.preset_load.loaded` aan, of bij een fout `on_error` (de waarden blijven
  dan staan).
- **AU factory presets**: `patches/clap-wrapper-auv2-presets.patch` laat clap-wrapper in `PostConstructor` de
  preset-discovery-factory van de CLAP doorzoeken, zoals een host dat doet: een FILE-map recursief, gefilterd op de
  bestandstypes en gesorteerd op pad. `kAudioUnitProperty_FactoryPresets` krijgt die lijst; `PresentPreset` laadt
  een preset via preset-load en meldt de nieuwe waarden aan de luisteraars. De AU begint niet op een preset, maar
  op zijn standaardwaarden (`standaard.json` is daaraan gelijk; de toets controleert dat).
- **Waarom geen container met laadsleutels** (alles in één locatie van het soort PLUGIN, in de plugin gebakken):
  clap-validator 0.4.1 loopt daarop vast (zie CLAUDE.md, valkuil 10).
- Getoetst met `plugin/toets/toets-fase4.sh` (`pb-toets presets`). De verwachting komt uit `presets/*.json`, niet
  uit de plugin. Gecontroleerd worden:
  - de factory, de locatie en elke preset met naam en plugin-id;
  - elke waarde na het laden exact, de rest op zijn standaard;
  - bit voor bit hetzelfde geluid als dezelfde waarden via flush, ook als de preset geladen wordt terwijl de plugin
    actief is, en ook onder een komma-locale;
  - de foutgevallen.

## Formaten (fase 5)
- **Kanaalindelingen**: `audio-ports-config` geeft Stereo (id 0, 2-2, standaard), Mono (id 1, 1-1) en Mono naar stereo
  (id 2, 1-2); `audio-ports` volgt de gekozen indeling. Kiezen kan alleen als de plugin niet actief is. Daarnaast
  `configurable-audio-ports`, want daarmee kiest de VST3 van clap-wrapper (setBusArrangements); een verzoek moet op een
  van de drie indelingen uitkomen. De AUv2 van clap-wrapper v0.16.0 kijkt alleen naar de huidige poorten (stereo), dus
  `patches/clap-wrapper-auv2-stereo-en-mono.patch`: SupportedNumChannels en ValidFormat uit audio-ports-config, en
  Initialize kiest de indeling die bij de streamformaten van de host past (anders kAudioUnitErr_FormatNotSupported).
- **process**: de invoer wordt eerst gekopieerd (`droog`, 2 × blokMax, gereserveerd in activate; process werkt in
  stukken van ten hoogste blokMax). De DSP leest uit die kopie, nooit uit de hostbuffer: bij in-place schrijft de DSP
  per sample kanaal 0 vóór hij kanaal 1 leest, dus bij mono naar stereo (beide DSP-ingangen uit één buffer die ook
  uitgang 0 is) zou kanaal 1 de uitvoer lezen. Mono rekent één DSP-kanaal (de kanalen zijn onafhankelijk, de
  bandbeweging is gedeeld, dus bit voor bit het linkerkanaal van stereo); mono naar stereo kopieert dat naar kanaal 1.
- **Bypass**: parameter id 100, van de schil (de DSP kent hem niet). `CLAP_PARAM_IS_BYPASS`, zodat clap-wrapper hem
  koppelt aan VST3 `kIsBypass` en AU `kAudioUnitProperty_BypassEffect`. De DSP draait door; de uitgang vloeit per sample
  in 10 ms lineair naar de gekopieerde invoer (op 1 precies de invoer) en terug. Een event volgt die overvloeier; een
  state, preset of `params.flush` (er loopt dan geen audio) en activate laten hem springen. De state bewaart de bypass
  (id 100; een oudere state zonder bypass laadt met bypass uit). Presets raken hem niet.
- **Samplerates en blokken**: de DSP rekent op elke samplerate (geheugen in activate); getoetst 44,1–192 kHz, blok
  1..4096 en wisselend.

## Installatiepakket (fase 6)
- `gereedschap/maak-pkg.sh <bundelmap> <versie> <uit.pkg>` (macOS) maakt met `pkgbuild` één component-pakket
  (id `nl.varve.pluginbouwer.dub-echo.pkg`, versie uit `project()` in `plugin/CMakeLists.txt`). De wortel is
  `Library/Audio/Plug-Ins/{Components,VST3,CLAP}`, installatieplek `/`: dus voor alle gebruikers, met beheerdersrechten.
- **Signeren**: de bundels zijn al ad-hoc gesigneerd (`codesign --force --deep --sign -`) vóór ze in het pakket gaan;
  het script controleert dat (`codesign --verify --strict`). Een .pkg kan niet ad-hoc gesigneerd worden
  (`pkgbuild --sign`/`productsign` eisen een Developer ID Installer-certificaat); het pakket zelf is dus ongesigneerd.
  Notariseren staat als optie in LEERPAD.md.
- **Valkuil: relocatable bundels.** pkgbuild markeert bundels standaard als verplaatsbaar. Vindt Installer op de schijf
  al een bundel met dezelfde CFBundleIdentifier (bv. in een bouwmap of een oude kopie), dan werkt hij die bij in plaats
  van in `/Library/Audio/Plug-Ins` te installeren. Het script maakt daarom met `pkgbuild --analyze` een component-plist
  en zet `BundleIsRelocatable` overal op `false`.
- `preinstall` haalt een oude versie van de drie bundels weg (geen verouderde presets); `postinstall` doet
  `killall -9 AudioComponentRegistrar` (valkuil 5).
- CI: de macOS-job maakt het pakket na het signeren (artefact `macos-pkg`). De job "macOS arm64, schone runner" bouwt
  niets: hij controleert dat er nog geen PB Dub-echo is, installeert het pakket twee keer met `sudo installer` (de
  tweede keer als upgrade), controleert plek en ad-hoc handtekening van alle drie, en draait `auval -strict`.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
