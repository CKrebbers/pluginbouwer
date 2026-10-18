# PROMPTS — Toetsenpad (v3.1 (18 okt 2026: instrument, en wat de verse gesprekken van proef F4 en G1–G3 vonden))

Per fase één prompt. Geef een vers Claude Code-gesprek dit pakket (alle bestanden) en de prompt van de fase.
Een fase is klaar als het stopcriterium waar is en de poorten in CI groen zijn. Uitslagen per fase: hoeveel van de
losse verse gesprekken (pass@3) het haalden, met de oorzaak als het misging.

---
## Fase 0 — skelet, CI en alle validators
**Prompt**
> Lees CLAUDE.md, SPEC.md, ARCHITECTUUR.md. Bouw het skelet van de plugin "Toetsenpad": een CLAP-plugin in
> `plugin/` (C++17) met CMake, en clap-wrapper v0.16.0 via FetchContent (met de patches uit `patches/`, in naamvolgorde) voor VST3 en AUv2
> (`make_clapfirst_plugins`; de CLAP-headers komen mee met clap-wrapper, haal ze niet apart op; AU maker `Vrve`, subtype `PTgg`, type `aumu`). De plugin gebruikt het Ontwerp uit
> `dsp/ontwerpen/keten_toetsenpad.h` (include-pad `dsp/`); in deze fase is het een instrument zonder audio-ingang (SPEC.md "Instrument": 0 ingangen, 1 stereo-uitgang, features instrument en synthesizer, AU-type `aumu` via `AUV2_INSTRUMENT_TYPE`) en schrijft process stilte (de DSP aansluiten is fase 1, zodat de null-test het bewijst).
> Schrijf `.github/workflows/ci.yml` met: Linux (bouwen met clang, clap-validator 0.4.1 uit de bron via cargo,
> pluginval v2.0.0-beta3 strictness 10 op de VST3 onder xvfb), RTSan (clang 20 via apt.llvm.org, `gereedschap/pb-render`
> gebouwd met `-fsanitize=realtime`, met een kanarie die moet falen; nog zonder `--state-rondje`, de state komt in fase 1), en macOS arm64 (bouwen, ad-hoc signeren,
> installeren in ~/Library/Audio/Plug-Ins, `killall -9 AudioComponentRegistrar`, `auval -strict -v aumu PTgg Vrve`,
> pluginval strictness 10 met `--rtcheck enabled` op AU (met `--repeat 3`) en VST3 (de VST3 van een instrument via `gereedschap/pluginval-rt.sh`, valkuil 22), de plugins als artefact
> `macos-keten_toetsenpad`; een artefactnaam mag geen `:` `/` `*` `?` `"` `<` `>` `|` bevatten, zet de matrixnaam dus niet ongefilterd in de naam). Push en maak CI groen. Zet niets uit om groen te worden; een valkuil uit CLAUDE.md eerst.

**Stopcriterium**: CI groen op de laatste commit: clap-validator 0 mislukt, pluginval 10 geslaagd (VST3 Linux, AU en
VST3 macOS), auval "AU VALIDATION SUCCEEDED", RTSan-bouw draait schoon en de kanarie faalt zoals bedoeld.
**Uitslag**: **3/3** (18 okt 2026, pakket v3, tak proef/g1–g3). G2 en G3 de eerste push groen, G1 in twee (eigen fout: een state-rondje vóór er state was; de prompt zegt nu dat state pas in fase 1 komt). Gevonden: PIC (nu valkuil).

---
## Fase 1 — DSP aansluiten; null-test door het binary
**Prompt**
> Wat de schil uit een eerdere fase al heeft, hoef je niet opnieuw te bouwen: toets het. Sluit het Ontwerp uit `dsp/` aan in process: parameters als CLAP-params (id = nummer in SPEC.md, bereik en standaard
> uit `PARAMS`), events op hun sample (blok splitsen bij elk event), `latency` en `tail` uit het Ontwerp, state zoals
> ARCHITECTUUR.md. Voeg aan CI de null-test toe: `sh gereedschap/ci-null.sh clap <build>` (pb-render, blok 512,
> wisselende blokken, state-rondje) en `sh gereedschap/ci-null.sh pluginval <plugin> <pluginval> <label>` voor de VST3
> (Linux en macOS) en de AU (macOS). Verander `dsp/` en `testsignalen/` niet. Als een render "gelijk aan de
> standaardwaarden" zegt: lees valkuil 1.
> Dit is een instrument: geef `verwerk` een stille invoer (ARCHITECTUUR.md "Instrument"). Bouw de note port en de noten
> precies zoals ARCHITECTUUR.md "Noten" (`dsp/pb_noten.h` is gegeven; valkuil 19). `ci-null.sh clap` speelt dan ook
> `testsignalen/noten/` (als CLAP-noten en als MIDI-bytes, blok 512 en wisselend).

**Stopcriterium**: alle null-tests in CI ≤ −90 dBFS (verwacht: bit voor bit), ook de noten (CLAP en MIDI), en de validators van fase 0 nog groen.
**Uitslag**: **3/3**, alle drie de eerste code-push groen; noten als CLAP en als MIDI bit voor bit. G1 vond een echte fout die ook in de Pluginbouwer zat: reset liet de poort open en een noot hangen (nu gerepareerd en in ARCHITECTUUR).

---
## Fase 2 — parameters, automatisering, state
**Prompt**
> Wat de schil uit een eerdere fase al heeft, hoef je niet opnieuw te bouwen: toets het. Maak de parameters af: tekst ↔
> waarde volgens de tabel "Tekst ↔ waarde" in SPEC.md, `params.flush` buiten process. Eis in CI met
> `node gereedschap/cv-eis.mjs <clap-validator> <plugin.clap> '^state-reproducibility' '^param-fuzz' '^param-conversions'`
> dat die toetsen van clap-validator PASSED zijn (SKIPPED telt niet), en maak een eigen toets (in CI, door het binary):
> (a) een met de hand gemaakte state-blob zonder de laatste parameter (`aantal` = N − 1, zoals een oudere build hem
> schreef) laadt en die parameter houdt zijn standaard; (b) een blob met een extra onbekend id en versie 2 laadt;
> (c) andere magic, versie 0 of een afgekapte blob geeft false en verandert niets. Er hoeft geen tweede build te zijn.
> Daarnaast: `value_to_text` van de standaard geeft precies de tekst uit de tabel, en `text_to_value` daarvan geeft
> een waarde met weer dezelfde tekst (SPEC.md, "Rondreis"). Automatisering in een DAW verandert niets aan de null-test.
**Stopcriterium**: clap-validator en pluginval 10 groen, state-toets groen.
**Uitslag**: **3/3**, alle drie de eerste code-push groen. Gevonden: de grenzen van de tekst op de afgeronde waarde (999,7 Hz = "1,00 kHz"), × en geen eenheid = procent, tekst ↔ waarde zonder locale (alle drie nu in het pakket).

---
## Fase 3 — UI
**Prompt**
> Bouw het frontpaneel uit design.md en `frontpaneel.svg` (de mockup; CLAP gui-extensie, clap-wrapper geeft het door aan VST3/AU). Knoppen sturen
> parameters via de host, nooit rechtstreeks de DSP. Geen audiothread-toegang vanuit de UI.
**Stopcriterium**: pluginval 10 zonder `--skip-gui-tests` groen op macOS; een schermafbeelding in CI als artefact.
**Uitslag**: **3/3**, in twee à drie pushes. Alle drie liepen op dezelfde val: de pixeltoets via de kleurruimte van het scherm. Ook: de maat in design.md, de naamtape te smal voor Silkscreen, de lettertypen niet in het pakket, unieke Objective-C-klassen per binary (alles nu in design.md).

---
## Fase 4 — presets
**Prompt**
> Fabriekspresets uit `presets/` (JSON met parameterwaarden per naam) als CLAP preset-discovery én als AU factory presets
> (ARCHITECTUUR.md "Presets", valkuilen 15 en 16). Eis in CI dat de drie preset-toetsen van clap-validator PASSED zijn
> (SKIPPED telt niet; `node gereedschap/cv-eis.mjs <clap-validator> <plugin.clap> '^preset-discovery'`) en dat elke presetnaam in de uitvoer van `auval -strict -v` staat.
**Stopcriterium**: clap-validator preset-tests groen (PASSED); auval toont de presets.
**Uitslag**: **3/3**; G2 en G3 de eerste code-push groen, G1 in twee (eigen fout: een .clap is op macOS een bundel). Pakketfout: presets/ ontbrak (nu vier presets die Noot en Poort niet zetten). Ook: clap-validator laadt maar één preset; de presetpatch kreeg NewCustomPresetSet.

---
## Fase 5 — formaten compleet
**Prompt**
> Stereo- én mono-uitgang (audio-ports-config: 0 → 2 en 0 → 1; een instrument heeft geen ingang), bypass zoals de host
> het wil (naar stilte), 44,1–192 kHz, blokgrootte 1..4096. Breid de CI-matrix uit (samplerates in de null-test, ook de noten).
> Mono = de DSP met één kanaal (kanaal 0), geen (L+R)/2; de verwachte uitvoer komt van de WASM met één kanaal.
> Lees valkuil 17 (elk formaat kiest zijn kanalen anders). De verwachte uitvoer bij andere samplerates maak je met `wasm/graaf.wasm` (die
> doet `testsignalen/` eerst bit voor bit na). Bypass: `CLAP_PARAM_IS_BYPASS`, de DSP rekent door, 10 ms overvloeien,
> en ook de droge invoer door `pb::uitvoer` (geen subnormale float naar de host; de fuzzer van clap-validator vangt het).
**Stopcriterium**: CI groen met de uitgebreide matrix.
**Uitslag**: **3/3**, alle drie de eerste code-push groen. Gevonden: "a && b" midden in een CI-stap faalt niet (ook in onze CI; nu valkuil), het bypass-id (drie verschillende keuzes; nu id 1000), een preset laat de bypass staan, mono = kanaal 0, AU BypassEffect uit geeft een Reset.

---
## Fase 6 — signeren en installeren
**Prompt**
> Maak een installatiepakket (`.pkg` via pkgbuild) met de ad-hoc gesigneerde AU, VST3 en CLAP, dat ze op de juiste
> plek zet (`/Library/Audio/Plug-Ins/{Components,VST3,CLAP}`). Het `.pkg` zelf kan niet ad-hoc gesigneerd worden
> (`pkgbuild --sign`/`productsign` eisen een betaald Developer ID Installer-certificaat): het blijft ongesigneerd,
> en LEERPAD.md zegt wat Clay bij het openen doet (macOS 15: Privacy en beveiliging → "Open toch"; rechtsklik → Open
> werkt daar niet meer). Zet `BundleIsRelocatable` op false in de component-plist (`pkgbuild --analyze`; de sleutel
> ontbreekt soms: toevoegen, niet zetten), anders installeert Installer in een bestaande bundel elders op de schijf.
> Het bewijs staat in `PackageInfo` van het pakket: geen `<bundle>` binnen `<relocate>`; pkgbuild schrijft altijd een
> lege `<relocate/>`, dus toets niet op "geen relocate" (proef G1). Een lokbundel elders bewijst weinig:
> `lsregister` registreert geen plugin-bundels (`-10811`).
> De tweede installatie meldt "Upgrading at base path /" en "The upgrade was successful." (niet "The install was
> successful"); zet `set -o pipefail` als je `installer` door `tee` stuurt, anders telt alleen de exitcode van tee
> (proef G3).
> postinstall: `killall -9 AudioComponentRegistrar`. Toets in een aparte job op een verse runner die niets bouwt:
> installeren (twee keer: de tweede meldt "upgrade"), plek en handtekening van alle drie, dan `auval -strict`.
> Notarisatie alleen als optie in de docs, met de kosten.
**Stopcriterium**: het pakket installeert in CI op een schone runner; auval slaagt daarna.
**Uitslag**: **3/3**; G2 de eerste code-push groen, G1 in drie (pkgbuild schrijft altijd een lege <relocate/>), G3 in twee (de tweede installatie zegt "The upgrade was successful"). Beide nu in de prompt.

---
## Fase 7 — test in Logic (checklist voor Clay)
- [ ] Plug-in Manager: PB Toetsenpad "geslaagd" (na LEERPAD.md "Installeren"), bij de instrumenten (AU Instruments → Varve).
- [ ] Een nieuw spoor "Software-instrument", PB Toetsenpad als instrument.
- [ ] Spelen met het keyboard (of Musical Typing, Cmd+K): elke toets klinkt, de toon klopt (A onder de middelste C = 220 Hz).
- [ ] Legato: houd een toets vast, druk een tweede, laat de tweede los: de toon gaat terug naar de eerste zonder nieuwe aanslag.
- [ ] Een MIDI-regio afspelen, ook op een ander tempo: noten op tijd, geen hangende noot na stop.
- [ ] Een knop automatiseren (Filter): geen tikken.
- [ ] Bouncen met de staart (Bestand → Bounce, "Include Audio Tail"): het loslaten klinkt uit tot het einde.
- [ ] Project bewaren, Logic sluiten, opnieuw openen: alle knoppen staan waar ze stonden.