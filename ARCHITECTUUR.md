# ARCHITECTUUR — Dub-echo

## Lagen
```
dsp/                 DE DSP (gegeven, niet herschrijven): pb_*.h + de keten als constante (ontwerpen/keten_dub_echo.h)
plugin/              de CLAP-schil (fase 0–2, formaten fase 5) en het frontpaneel (fase 3); clap-wrapper maakt VST3 en AUv2
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
- **In de plugin gebakken.** CMake leest bij configure elk `presets/*.json` (`string(JSON)`) en schrijft per bestand
  één `PB_FABRIEKSPRESET(sleutel, naam, uitleg, aantal, {param, waarde}…)` in `build/gemaakt/fabriekspresets.inc`.
  Sleutel = bestandsnaam zonder `.json`, volgorde = bestandsnaam. `plugin/src/fabriekspresets.h` controleert bij het
  compileren (`static_assert`) dat elke parameternaam in `PARAMS` staat, elke waarde binnen het bereik ligt en geen
  naam of sleutel dubbel is. Een nieuw JSON-bestand in `presets/` is na een nieuwe configure een nieuwe preset.
- **CLAP preset-discovery.** `clap_entry.get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID)` (ook de `_COMPAT`-id) geeft
  per preset een provider (`<clap-id>.fabriekspreset.<sleutel>`) met één locatie van soort
  `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN` (location NULL, vlag fabrieksinhoud) en één preset met naam, load_key =
  sleutel, de CLAP-id van de plugin, maker en uitleg. Eén provider met alle presets in één container zou netter zijn,
  maar daarop blijft clap-validator 0.4.1 hangen (VERSLAG.md); voor een host zijn beide geldig.
- **Laden: preset-load** (`from_location(PLUGIN, NULL, sleutel)`, mainthread). Hetzelfde pad als `state.load`: alle
  waarden eerst op hun standaard, dan die uit de preset, in `waarde[]`; niet actief → meteen naar het Ontwerp, actief
  → `stateNieuw` en process neemt ze over. Daarna `params.rescan(VALUES)` en `host_preset_load.loaded`. Een onbekende
  sleutel of een bestandslocatie: false en `on_error`, er verandert niets.
- **AU factory presets.** clap-wrapper v0.16.0 kent ze niet; `patches/clap-wrapper-auv2-presets-uit-preset-discovery.patch`
  laat de AUv2-wrapper bij het maken van de instantie de preset-discovery van de CLAP doorlopen (alleen locaties van
  soort PLUGIN). Elke preset voor deze CLAP-id wordt een AU factory preset (`kAudioUnitProperty_FactoryPresets`),
  genummerd zoals de providers. `kAudioUnitProperty_PresentPreset` zetten laadt hem met preset-load; de naam gaat ook
  in de ClassInfo (wat Logic in het project bewaart). VST3 krijgt (nog) geen presets: clap-wrapper geeft ze daar niet
  door, en de opdracht vraagt alleen CLAP en AU.
- **Getoetst** door clap-validator (`preset-discovery-crawl`, `-descriptor-consistency`, `-load`), door
  `gereedschap/pb-preset-toets` (CLAP-host: discovery zoals een DAW, elke preset laden, en de preset klinkt bit voor
  bit als dezelfde waarden via `params.flush`, vóór activate en terwijl actief; ook onder RTSan), door
  `gereedschap/pb-au-presets` (macOS: elke AU factory preset zetten, PresentPreset en ClassInfo lezen) en door auval,
  dat de presets bij naam toont. `gereedschap/vergelijk-presets.mjs` legt de waarden uit beide toetsen naast
  `presets/*.json` (CLAP bit voor bit, AU als float32).

## Formaten (fase 5)
- **Kanaalindelingen.** Eén ingang en één uitgang, in drie indelingen: Stereo (2 → 2, standaard), Mono (1 → 1) en
  Mono naar stereo (1 → 2). Een CLAP-host kiest met `audio-ports-config` (`select`, mainthread, niet actief);
  `audio-ports-config-info` meldt de huidige. `configurable-audio-ports` neemt een verzoek per poort aan (alles of
  niets, alleen een indeling uit de lijst): zo zet clap-wrapper de VST3-busindeling door (`setBusArrangements`).
  Voor de AU leest `patches/clap-wrapper-auv2-toegestane-kanalen-uit-audio-ports-config.patch` de lijst uit
  `audio-ports-config`: SupportedNumChannels meldt [1, 1] [1, 2] [2, 2], en Initialize kiest de indeling die bij de
  streamformaten van de host past (stereo naar mono: `kAudioUnitErr_FormatNotSupported`).
- **process.** Het Ontwerp rekent min(in, uit) kanalen. Mono naar stereo: het Ontwerp rekent mono en uitgang 1 is een
  kopie van uitgang 0; twee kanalen met dezelfde invoer zijn in het Ontwerp bit voor bit gelijk (de toets vergelijkt
  met de motor op twee kanalen). Zo leest het Ontwerp nooit een ingang die het al overschreef. De DSP schrijft per
  sample eerst uitgang 0 en leest dan ingang 1: deelt een host ingang 1 met uitgang 0, dan kopieert process ingang 1
  eerst naar een vooraf gereserveerde buffer. Gewoon in-place (ingang c = uitgang c) is veilig.
- **Bypass.** Parameter 9 "Bypass" (0/1, `CLAP_PARAM_IS_BYPASS`, stappen): clap-wrapper maakt er `kIsBypass` (VST3)
  en `kAudioUnitProperty_BypassEffect` (AU) van, dus de bypassknop van de host werkt in elk formaat. Het Ontwerp
  rekent door (de staart en de echo blijven kloppen); de uitgang vloeit in 10 ms lineair over naar de droge ingang
  en terug. Na activate/reset springt hij op de eerste sample, zoals de smoothers. Vol aan = de ingang, bit voor bit.
  De bypass zit in de state (id 9; een oude state zonder id 9 = uit), niet in een preset: een preset laden laat hem
  staan.
- **Samplerates en blokken.** 44,1–192 kHz en blokken 1..4096: het geheugen komt in activate (`geheugenNodig(sr)`,
  de buffers voor bypass en kopie op maxFrames); process hakt een blok dat groter is dan maxFrames in stukken.
- **Getoetst.** `gereedschap/maak-matrix.mjs` maakt invoer en verwachte uitvoer op 44,1 / 48 / 88,2 / 96 / 176,4 /
  192 kHz, per indeling, met de ketenmotor van de tool (`wasm/graaf.wasm`); op de eigen samplerate geeft hij bit voor
  bit `testsignalen/*.verwacht.wav` (dat toetst hij eerst). Daartegen: `ci-null.sh matrix` (CLAP via pb-render
  `--indeling`, blok 512, wisselend 1..4096, `--in-place`), `ci-null.sh pluginval-matrix` (VST3 en AU via
  `pluginval test`, elke samplerate, blok 1 en 4096, Bypass), `ci-null.sh au-matrix` (de AU via
  `gereedschap/pb-au-render`: streamformaten zoals Logic, BypassEffect, en een geweigerde indeling),
  `ci-bypass.sh` (aan, aan- en uitzetten met overvloeiing). pluginval validate draait met alle samplerates en
  blokgroottes; `ci-formaten.sh` eist Mono en Stereo in de busindeling en de AU-indelingen in auval. RTSan rekent in
  elke indeling, in-place en met de bypass.

## Installatiepakket (fase 6)
- `gereedschap/maak-pkg.sh <plugins-map> <uit.pkg>` (macOS) maakt met `pkgbuild` een component-pakket, identifier
  `nl.varve.pluginbouwer.dub-echo.pkg`, versie uit de Info.plist van de AU. Payload: de AU in
  `/Library/Audio/Plug-Ins/Components`, de VST3 in `/Library/Audio/Plug-Ins/VST3`, de CLAP in
  `/Library/Audio/Plug-Ins/CLAP` (systeembreed; een .pkg installeert als root).
- De bundels zijn al ad-hoc gesigneerd (CI-stap "Ad-hoc signeren"); het script controleert dat (`codesign --verify
  --strict`) en kopieert met `ditto`. Het .pkg zelf is niet gesigneerd: een pakket kent geen ad-hoc-handtekening
  (`productsign`/`pkgbuild --sign` vragen een Developer ID Installer-certificaat). Zie LEERPAD.md, "Notariseren".
- `BundleIsRelocatable` staat uit (via `pkgbuild --analyze` en een component-plist). Anders installeert de installer
  over een kopie met dezelfde bundel-id die hij elders vindt (een uitgepakte zip in Downloads) in plaats van in
  /Library.
- `gereedschap/pkg-scripts/postinstall`: `killall -9 AudioComponentRegistrar`, zodat de AU meteen te zien is.
- **Getoetst** (CI-job `pakket`, een verse macos-15-runner die alleen het artefact `macos-pkg` krijgt): eerst bewijzen
  dat er niets geïnstalleerd is en auval de AU niet kent; een lokvogel (dezelfde bundels in ~/Downloads); `sudo
  installer -pkg`; de drie bundels staan op hun plek, ad-hoc gesigneerd, zonder quarantaine; de lokvogel is
  onaangeroerd; `auval -strict` slaagt (met de kanaalindelingen uit fase 5); pluginval (strictness 5) op de
  geïnstalleerde VST3 en AU.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
