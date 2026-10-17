Stopcriterium gehaald.

# UITSLAG fase 6: signeren en installeren (tak proef/f3)

Laatste CI-run met code: **run 122**, commit `f80b21c`. Alle vier de jobs zijn groen:
https://github.com/CKrebbers/pluginbouwer/actions/runs/39048545213
Deze UITSLAG-commit verandert alleen docs (plus valkuil 13 in CLAUDE.md). CI draait er opnieuw op.

## Wat ik deed
- **`gereedschap/maak-pkg.sh <bundelmap> <versie> <uit.pkg>`** (macOS) maakt één pakket met `pkgbuild`:
  - id `nl.varve.pluginbouwer.dub-echo.pkg`;
  - de versie komt uit `project(… VERSION 0.1.0)` in `plugin/CMakeLists.txt`;
  - de wortel is `Library/Audio/Plug-Ins/{Components,VST3,CLAP}` en de installatieplek is `/`. Het pakket installeert
    dus voor alle gebruikers en vraagt om het beheerderswachtwoord.
- **Signeren**:
  - De bundels zijn ad-hoc gesigneerd (`codesign --sign -`, bestaande stap) voordat ze in het pakket gaan. Het script
    controleert dat met `codesign --verify --strict`.
  - Het `.pkg` zelf kan niet ad-hoc gesigneerd worden: `pkgbuild --sign` en `productsign` eisen een Developer ID
    Installer-certificaat, en dat is betaald. Het pakket is dus ongesigneerd (`Status: no signature`). Dat staat in
    LEERPAD, met wat Clay bij het openen moet doen.
- **Bundels niet verplaatsbaar**: met `pkgbuild --analyze` maakt het script een component-plist en zet
  `BundleIsRelocatable` op false. Anders installeert Installer in een bestaande bundel met dezelfde id, ergens op de
  schijf, in plaats van in de plugin-map (valkuil 13).
- **De scripts in het pakket**:
  - `preinstall` haalt een oude versie van de drie bundels weg;
  - `postinstall` doet `killall -9 AudioComponentRegistrar` (valkuil 5).
- **CI**:
  - De macOS-job maakt het pakket na het signeren en zet het als artefact `macos-pkg` klaar.
  - Een nieuwe job, **"macOS arm64, schone runner (.pkg installeren, auval)"**, start pas na de macOS-job, op een
    eigen verse runner die niets bouwt. Hij:
    1. controleert dat de runner schoon is;
    2. bekijkt het pakket;
    3. installeert het twee keer met `sudo installer` (de tweede keer als upgrade);
    4. controleert van alle drie de bundels de plek, de ad-hoc handtekening en de presets;
    5. draait `auval -strict`.
- **Docs**:
  - LEERPAD.md: installeren met het pakket, verwijderen, en notariseren als optie met de kosten (zie hieronder). De
    kostentabel is bijgewerkt.
  - ARCHITECTUUR.md: nieuwe sectie "Installatiepakket (fase 6)".
  - TASKS.md: fase 6 afgevinkt.
  - CLAUDE.md: valkuil 13.

## Poorten (bewijs uit het log van run 122)
| poort | uitslag | bewijs |
|---|---|---|
| **pakket installeert op een schone runner** | groen | job "schone runner": `schoon: geen PB Dub-echo in /Library of ~/Library, auval -a kent Vrve niet`; `installer: The install was successful.`; `installer: The upgrade was successful.`; `package-id: nl.varve.pluginbouwer.dub-echo.pkg version: 0.1.0 volume: /` |
| **alles op de juiste plek, ad-hoc gesigneerd** | groen | `geïnstalleerd en ad-hoc gesigneerd: /Library/Audio/Plug-Ins/Components/PB Dub-echo.component (5 presets)`, idem `…/VST3/PB Dub-echo.vst3 (5 presets)` en `…/CLAP/PB Dub-echo.clap (5 presets)`; elk `valid on disk` en `satisfies its Designated Requirement`; `codesign -dv` geeft `Signature=adhoc` (de stap faalt anders) |
| **auval slaagt daarna** | groen | `aufx PDca Vrve  -  Varve: PB Dub-echo`; `AU VALIDATION SUCCEEDED.`; `Reported Channel Capabilities (explicit): [2, 2]  [1, 1]  [1, 2]`; `HAS FACTORY PRESETS`, `ID: 0 Name: Lange dub` … `ID: 4 Name: Zelfoscillatie` |
| inhoud van het pakket | groen | `pkgutil --payload-files`: `./Library/Audio/Plug-Ins/VST3/PB Dub-echo.vst3`, `…/Components/PB Dub-echo.component`, `…/CLAP/PB Dub-echo.clap`; `Status: no signature` (zoals verwacht, zie boven) |
| fase 0–5 (null-tests CLAP/VST3/AU, matrix, state, tekst, automatisering, paneel, presets, formaten, auval, pluginval `--rtcheck` AU en VST3, pluginval VST3 Linux, clap-validator, RTSan en kanarie) | groen | jobs "Linux", "RTSan" en "macOS arm64" in run 122 zijn groen met alle stappen. Ik heb aan die stappen niets veranderd. Alleen in de macOS-job zijn twee stappen toegevoegd (pakket maken, artefact). |

Ik heb geen toets of validator uitgezet en niets aan `dsp/` of `testsignalen/` veranderd.

## Verloop (3 CI-ronden met code)
1. **Run 105/106**: `maak-pkg.sh` faalde op `PlistBuddy Set :0:BundleIsRelocatable` met "Does Not Exist". De
   component-plist van `pkgbuild --analyze` had die sleutel niet bij elke entry. Ik heb het veranderd in: weghalen
   (fout negeren) en dan `Add … bool false`.
2. **Run 115**: het pakket werd gemaakt en de eerste installatie slaagde. De tweede installatie meldt
   `The upgrade was successful.` en niet "install", en mijn grep eiste "install". Ik heb de grep aangepast.
3. **Run 122**: alles groen.

## Wat in het pakket onduidelijk of fout was, en wat opviel
- **"Ad-hoc gesigneerd .pkg" bestaat niet.** Een installatiepakket heeft een certificaat-identiteit nodig om te
  signeren; `codesign --sign -` werkt alleen voor code (bundels). Ik lees de opdracht daarom zo: ad-hoc gesigneerde
  bundels in een ongesigneerd pakket. Gevolg voor Clay: na een download blokkeert Gatekeeper het pakket. Hij moet dan
  de quarantainevlag weghalen of "Open toch" kiezen in Privacy en beveiliging. Dat staat stap voor stap in LEERPAD. Op
  macOS 15 werkt het oude trucje (rechtsklik → Open) daarvoor niet meer.
- **De kosten van notariseren**: LEERPAD had "[bedrag niet bevestigd, ±€ 99/jaar]". Openbare bronnen noemen voor het
  Apple Developer Program US$ 99 per jaar en € 99 in de eurozone. Ik heb geen actuele officiële Apple-pagina met het
  eurobedrag gezien. Het staat er nu als "US$ 99/jaar (eurozone € 99/jaar)", met de opmerking dat Apple de prijs in
  je eigen munt toont bij het aanmelden.
- **Installatieplek**: ik koos `/Library/Audio/Plug-Ins` (alle gebruikers). Een pakket kan ook naar
  `~/Library` installeren (een "domain"-keuze via productbuild met een distribution-bestand), maar dat maakt het
  ingewikkelder. Logic leest beide. De CI-job van fase 0–5 installeert nog met de hand in `~/Library` op zijn eigen
  runner; die twee raken elkaar niet. Clay moet wel een eerdere kopie in `~/Library` weghalen, anders ziet Logic er
  twee (staat in LEERPAD).
- **Niet getoetst**: dat `BundleIsRelocatable = false` echt helpt. Op de schone runner staat geen tweede kopie, dus
  het probleem kon daar niet optreden. Een bewuste toets daarvoor (een kopie van de bundel elders neerzetten en dan
  installeren) hangt af van wat Spotlight en LaunchServices geïndexeerd hebben. Die heb ik niet gemaakt. Wel is
  aangetoond dat de bestanden op de juiste plek staan, in `pkgutil --files` van de geïnstalleerde receipt.
- **De VST3 en de CLAP** worden op de schone runner alleen op plek en handtekening gecontroleerd, niet opnieuw met
  pluginval of clap-validator. Die validators draaien al op dezelfde bundels in de andere jobs. Het stopcriterium
  vraagt alleen om auval.
- **Voor Clay (fase 7)**: installeer met het `.pkg` uit het artefact `macos-pkg`. Haal eerst eventuele handmatige
  kopieën uit `~/Library/Audio/Plug-Ins` weg, en kijk dan in Logic's Plug-in Manager of PB Dub-echo "geslaagd" is.
