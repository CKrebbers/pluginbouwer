# PROMPTS — Dub-echo (v3 (17 okt 2026))

Per fase één prompt. Geef een vers Claude Code-gesprek dit pakket (alle bestanden) en de prompt van de fase.
Een fase is klaar als het stopcriterium waar is en de poorten in CI groen zijn. Uitslagen per fase: hoeveel van de
losse verse gesprekken (pass@3) het haalden, met de oorzaak als het misging.

---
## Fase 0 — skelet, CI en alle validators
**Prompt**
> Lees CLAUDE.md, SPEC.md, ARCHITECTUUR.md. Bouw het skelet van de plugin "Dub-echo": een CLAP-plugin in
> `plugin/` (C++17) met CMake, en clap-wrapper v0.16.0 via FetchContent (met de patches uit `patches/`, in naamvolgorde) voor VST3 en AUv2
> (`make_clapfirst_plugins`; de CLAP-headers komen mee met clap-wrapper, haal ze niet apart op; AU maker `Vrve`, subtype `PDca`, type `aufx`). De plugin gebruikt het Ontwerp uit
> `dsp/ontwerpen/keten_dub_echo.h` (include-pad `dsp/`); in deze fase geeft process de invoer door (de DSP aansluiten is fase 1, zodat de null-test het bewijst).
> Schrijf `.github/workflows/ci.yml` met: Linux (bouwen met clang, clap-validator 0.4.1 uit de bron via cargo,
> pluginval v2.0.0-beta3 strictness 10 op de VST3 onder xvfb), RTSan (clang 20 via apt.llvm.org, `gereedschap/pb-render`
> gebouwd met `-fsanitize=realtime`, met een kanarie die moet falen), en macOS arm64 (bouwen, ad-hoc signeren,
> installeren in ~/Library/Audio/Plug-Ins, `killall -9 AudioComponentRegistrar`, `auval -strict -v aufx PDca Vrve`,
> pluginval strictness 10 met `--rtcheck enabled` op AU (met `--repeat 3`) en VST3, de plugins als artefact
> `macos-keten_dub_echo`; een artefactnaam mag geen `:` `/` `*` `?` `"` `<` `>` `|` bevatten, zet de matrixnaam dus niet ongefilterd in de naam). Push en maak CI groen. Zet niets uit om groen te worden; een valkuil uit CLAUDE.md eerst.

**Stopcriterium**: CI groen op de laatste commit: clap-validator 0 mislukt, pluginval 10 geslaagd (VST3 Linux, AU en
VST3 macOS), auval "AU VALIDATION SUCCEEDED", RTSan-bouw draait schoon en de kanarie faalt zoals bedoeld.
**Uitslag**: **3/3** (15 okt 2026, pakket v1; tak proef/e1–e3, CI-runs 16, 17, 18 groen). Alle drie hadden twee CI-ronden nodig: pluginval `--rtcheck` op macOS ving calloc uit dyld in clap-wrapper (thread_local). Elk vond en patchte het los; sinds pakket v2 staat de patch erin (valkuil 7). Daarnaast: clap-validator kleurt altijd (valkuil 8); twee van de drie sloten de DSP al in fase 0 aan. Pakket v2 (17 okt, tak proef/f1–f3): **3/3**, elk de eerste push groen.

---
## Fase 1 — DSP aansluiten; null-test door het binary
**Prompt**
> Sluit het Ontwerp uit `dsp/` aan in process: parameters als CLAP-params (id = nummer in SPEC.md, bereik en standaard
> uit `PARAMS`), events op hun sample (blok splitsen bij elk event), `latency` en `tail` uit het Ontwerp, state zoals
> ARCHITECTUUR.md. Voeg aan CI de null-test toe: `sh gereedschap/ci-null.sh clap <build>` (pb-render, blok 512,
> wisselende blokken, state-rondje) en `sh gereedschap/ci-null.sh pluginval <plugin> <pluginval> <label>` voor de VST3
> (Linux en macOS) en de AU (macOS). Verander `dsp/` en `testsignalen/` niet. Als een render "gelijk aan de
> standaardwaarden" zegt: lees valkuil 1.

**Stopcriterium**: alle null-tests in CI ≤ −90 dBFS (verwacht: bit voor bit), de statistische toets (zelfoscillatie)
binnen 1 dB, en de validators van fase 0 nog groen.
**Uitslag**: **3/3** (15 okt 2026, op de fase 0 van dezelfde tak; CI-runs 22, 24, 25 groen, elk de eerste push). Alle null-tests bit voor bit (−∞ dBFS): CLAP Linux en macOS (blok 512, wisselend, state-rondje), VST3 Linux en macOS en AU macOS via `pluginval test`; zelfoscillatie terts en omhullende 0,000 dB. Gevonden: SPEC zei "genormaliseerd" (CLAP-params zijn eenheden), `zetParam` neemt een index, `staartSec` hoort bij de audiothread; nu in SPEC/ARCHITECTUUR. Pakket v2: **3/3** (twee de eerste push, één in twee rondes: rescan na state.load ontbrak; nu valkuil/ARCHITECTUUR).

---
## Fase 2 — parameters, automatisering, state
**Prompt**
> Maak de parameters af: tekst ↔ waarde volgens de tabel "Tekst ↔ waarde" in SPEC.md, `params.flush` buiten
> process. Toets met clap-validator (state-reproducibility, param-fuzz) en een eigen toets (in CI, door het binary):
> (a) een met de hand gemaakte state-blob zonder de laatste parameter (`aantal` = N − 1, zoals een oudere build hem
> schreef) laadt en die parameter houdt zijn standaard; (b) een blob met een extra onbekend id en versie 2 laadt;
> (c) andere magic, versie 0 of een afgekapte blob geeft false en verandert niets. Er hoeft geen tweede build te zijn.
> Daarnaast: `value_to_text` van de standaard geeft precies de tekst uit de tabel, en `text_to_value` daarvan terug
> geeft de waarde. Automatisering in een DAW verandert niets aan de null-test.
**Stopcriterium**: clap-validator en pluginval 10 groen, state-toets groen.
**Uitslag**: **3/3** (17 okt 2026, pakket v2; tak proef/f1–f3). Twee de eerste push, één in twee rondes (':' in een artefactnaam). Gevonden: de DSP gaf subnormale uitvoer bij zachte invoer (clap-validator-fuzz; nu pb::uitvoer in dsp/); 'eenheidloos' en 'state zonder parameter N' waren niet uitgewerkt (nu de tabel Tekst ↔ waarde en een blob met de hand).

---
## Fase 3 — UI
**Prompt**
> Bouw het frontpaneel uit design.md en `frontpaneel.svg` (de mockup; CLAP gui-extensie, clap-wrapper geeft het door aan VST3/AU). Knoppen sturen
> parameters via de host, nooit rechtstreeks de DSP. Geen audiothread-toegang vanuit de UI.
**Stopcriterium**: pluginval 10 zonder `--skip-gui-tests` groen op macOS; een schermafbeelding in CI als artefact.
**Uitslag**: **3/3** (17 okt 2026, pakket v2). Eén de eerste push, twee in twee à drie rondes. Gevonden: een race in clap-wrapper (AU SetParameter tegen Render; pluginval 'Parameter thread safety' crashte één op de drie runs; patch, nu valkuil 13 en pluginval AU --repeat 3); 'knoppen via de host' bij CLAP (valkuil 14); de mockup zat niet in het pakket (nu frontpaneel.svg).

---
## Fase 4 — presets
**Prompt**
> Fabriekspresets uit `presets/` (JSON met parameterwaarden per naam) als CLAP preset-discovery én als AU factory presets
> (ARCHITECTUUR.md "Presets", valkuilen 15 en 16). Eis in CI dat de drie preset-toetsen van clap-validator PASSED zijn
> (SKIPPED telt niet) en dat elke presetnaam in de uitvoer van `auval -strict -v` staat.
**Stopcriterium**: clap-validator preset-tests groen (PASSED); auval toont de presets.
**Uitslag**: **3/3** (17 okt 2026, pakket v2). Eén de eerste push, twee in twee à vier rondes. Alle drie vonden los van elkaar dat clap-validator 0.4.1 vastloopt op een preset-container met meer presets (valkuil 15) en bouwden een AU-presetpatch (clap-wrapper kent geen AU-fabriekspresets). ARCHITECTUUR zei ten onrechte 'een state-blob naast de plugin'.

---
## Fase 5 — formaten compleet
**Prompt**
> Mono én stereo (audio-ports-config), bypass zoals de host het wil, 44,1–192 kHz, blokgrootte 1..4096. Breid de
> CI-matrix uit (samplerates in de null-test). Let op bij mono in-place: de DSP schrijft per sample eerst kanaal 0;
> lees je beide DSP-ingangen uit één hostbuffer die ook uitgang 0 is, kopieer de invoer dan eerst. Drie indelingen:
> stereo (2 → 2), mono (1 → 1) en mono → stereo (1 → 2, Logic biedt die aan op een monospoor). Lees valkuil 17 (elk
> formaat kiest zijn kanalen anders). De verwachte uitvoer bij andere samplerates maak je met `wasm/graaf.wasm` (die
> doet `testsignalen/` eerst bit voor bit na). Bypass: `CLAP_PARAM_IS_BYPASS`, de DSP rekent door, 10 ms overvloeien,
> en ook de droge invoer door `pb::uitvoer` (geen subnormale float naar de host; de fuzzer van clap-validator vangt het).
**Stopcriterium**: CI groen met de uitgebreide matrix.
**Uitslag**: **3/3** (17 okt 2026, pakket v2). Eén de eerste push, twee in twee rondes. Alle drie moesten de AU-kanaalindelingen zelf patchen (clap-wrapper biedt de AU alleen stereo aan; valkuil 17). De mono-in-place-fout uit fase 1 bestond echt (F3 bewees het met een tegenproef). De fuzzer ving een subnormale sample in bypass.

---
## Fase 6 — signeren en installeren
**Prompt**
> Maak een installatiepakket (`.pkg` via pkgbuild) met de ad-hoc gesigneerde AU, VST3 en CLAP, dat ze op de juiste
> plek zet (`/Library/Audio/Plug-Ins/{Components,VST3,CLAP}`). Het `.pkg` zelf kan niet ad-hoc gesigneerd worden
> (`pkgbuild --sign`/`productsign` eisen een betaald Developer ID Installer-certificaat): het blijft ongesigneerd,
> en LEERPAD.md zegt wat Clay bij het openen doet (macOS 15: Privacy en beveiliging → "Open toch"; rechtsklik → Open
> werkt daar niet meer). Zet `BundleIsRelocatable` op false in de component-plist (`pkgbuild --analyze`; de sleutel
> ontbreekt soms: toevoegen, niet zetten), anders installeert Installer in een bestaande bundel elders op de schijf.
> postinstall: `killall -9 AudioComponentRegistrar`. Toets in een aparte job op een verse runner die niets bouwt:
> installeren (twee keer: de tweede meldt "upgrade"), plek en handtekening van alle drie, dan `auval -strict`.
> Notarisatie alleen als optie in de docs, met de kosten.
**Stopcriterium**: het pakket installeert in CI op een schone runner; auval slaagt daarna.
**Uitslag**: **3/3** (17 okt 2026, pakket v2). Alle drie in twee à drie rondes, steeds dezelfde valkuil: pkgbuild --analyze schrijft BundleIsRelocatable niet (toevoegen, niet zetten). Een .pkg kan niet ad-hoc gesigneerd worden (de prompt vroeg het; nu herschreven). Het pakket installeert op een schone runner en auval slaagt.

---
## Fase 7 — test in Logic (checklist voor Clay)
- [ ] Plug-in Manager: PB Dub-echo "geslaagd" (na LEERPAD.md "Installeren").
- [ ] Op een audiospoor laden; geluid gaat erdoor; knoppen doen wat SPEC.md zegt.
- [ ] Een knop automatiseren (Tijd van langzaam naar snel): de toon glijdt, geen tikken.
- [ ] Terugkoppeling boven 1: hij gaat vanzelf door en blijft begrensd.
- [ ] Bouncen met de staart: Bestand → Bounce, met "Staart van audio opnemen"/"Include Audio Tail" aan; de echo
      loopt door na het einde van de regio. (De plugin meldt een andere staart niet actief met `tail.changed`; de
      host vraagt hem op bij de bounce. Stopt de echo te vroeg, dan is dit de plek.)
- [ ] Project bewaren, Logic sluiten, opnieuw openen: alle knoppen staan waar ze stonden.
