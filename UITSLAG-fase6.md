Stopcriterium gehaald.

# Uitslag fase 6 — Dub-echo (tak proef/f2)

Laatste CI-run met code (commit 11eff22): run 121 —
https://github.com/CKrebbers/pluginbouwer/actions/runs/39048460324. Alle vier de jobs zijn groen: Linux, RTSan,
macOS en de nieuwe `macos-pkg`. De commit met dit bestand verandert alleen docs (dit bestand en TASKS.md).

Stopcriterium (PROMPTS.md §6): "het pakket installeert in CI op een schone runner; auval slaagt daarna". Dat is
gehaald in job `macos-pkg` van run 121. Het bewijs staat hieronder.

## Wat ik deed
- **`gereedschap/maak-pkg.sh`** maakt met `pkgbuild` het pakket `PB-Dub-echo.pkg` (id
  `nl.varve.pluginbouwer.dub-echo.pkg`, versie uit `project(... VERSION)`). Het zet de plugin voor alle gebruikers
  neer:
  - `/Library/Audio/Plug-Ins/Components/PB-Dub-echo.component`
  - `/Library/Audio/Plug-Ins/VST3/PB-Dub-echo.vst3`
  - `/Library/Audio/Plug-Ins/CLAP/PB-Dub-echo.clap`
  
  Verder:
  - Het script eist eerst `codesign --verify --strict` op elke bundel. De bundels zijn al ad-hoc gesigneerd in de
    bestaande stap. Het kopiëren gebeurt met `ditto`.
  - Een `postinstall`-script doet `killall -9 AudioComponentRegistrar` (valkuil 5).
  - In de componentenlijst staat `BundleIsRelocatable` uit en `BundleOverwriteAction` op `upgrade`. Zie hieronder
    waarom.
- **CI, job `macos`**: na het ad-hoc signeren maakt hij het pakket en bewaart het als artefact `macos-pkg`. De
  bestaande stappen (installeren in `~/Library`, auval, pluginval enzovoort) zijn ongewijzigd.
- **CI, nieuwe job `macos-pkg`** (`needs: macos`, een verse `macos-15`-runner waar niets gebouwd is):
  1. het artefact ophalen;
  2. eisen dat de runner schoon is;
  3. `sudo installer -pkg … -target /`;
  4. controleren dat de bundels op de juiste plek staan en dat de handtekening klopt;
  5. `auval -strict`;
  6. nog een keer installeren (bijwerken over de bestaande installatie) en opnieuw auval.
- **Docs:**
  - LEERPAD.md: installeren met het pakket (terminal of "Open toch"), bijwerken, verwijderen, en de oude handmatige
    weg.
  - LEERPAD.md: notariseren als optie, met de stappen en de kosten (Apple Developer Program, € 99/jaar, US$ 99; het
    bedrag in euro is gemarkeerd als niet door Apple bevestigd).
  - ARCHITECTUUR.md: nieuwe sectie "Installatiepakket".
  - SPEC.md: validators.
  - `dsp/` en `testsignalen/` zijn niet aangeraakt.

## Poorten (run 121)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| runner is schoon vóór installatie | groen | "schoon: geen PB-Dub-echo.* in /Library/Audio/Plug-Ins of ~/Library/Audio/Plug-Ins, geen PDca in auval -a" |
| pakket installeert | groen | "installer: The install was successful."; `pkgutil --pkg-info`: "package-id: nl.varve.pluginbouwer.dub-echo.pkg", "version: 0.1.0"; `pkgutil --files`: "Library/Audio/Plug-Ins/CLAP/PB-Dub-echo.clap", ".../Components/PB-Dub-echo.component", ".../VST3/PB-Dub-echo.vst3" |
| juiste plek, ad-hoc gesigneerd | groen | per bundel "/Library/Audio/Plug-Ins/…: valid on disk", "satisfies its Designated Requirement", "Signature=adhoc" |
| **auval na installatie** | groen | "aufx PDca Vrve  -  Varve: PB-Dub-echo"; "Reported Channel Capabilities (explicit): [2, 2]  [1, 1]  [1, 2]"; "HAS FACTORY PRESETS" met de 5 presets; "AU VALIDATION SUCCEEDED." |
| bijwerken over een bestaande installatie | groen | "installer: Upgrading at base path /", "The upgrade was successful.", daarna weer "AU VALIDATION SUCCEEDED." |
| poorten fase 0–5 | groen | alle stappen van de jobs Linux, RTSan en macOS in run 121 zijn "success": auval -strict, pluginval strictness 10 `--rtcheck` (AU en VST3), clap-validator (ook fuzz en de subsets), null-tests en null-matrices (CLAP, VST3, AU), eigen toetsen fase 2–5, RTSan schoon, en de kanarie faalt zoals bedoeld |

## Waar het schuurde / wat onduidelijk of fout was
- **"ad-hoc gesigneerd" kan niet voor een .pkg.**
  - `pkgbuild --sign` en `productsign` vragen een "Developer ID Installer"-certificaat; een ad-hoc-variant zoals
    `codesign --sign -` bestaat voor pakketten niet.
  - Ik heb het daarom zo opgevat: de bundels in het pakket zijn ad-hoc gesigneerd, het pakket zelf niet.
    `pkgutil --check-signature` zegt "Status: no signature".
  - Gevolg voor Clay: een gedownload pakket opent niet met een dubbelklik. Het werkt met
    `sudo installer -pkg … -target /` of via Systeeminstellingen → Privacy en beveiliging → "Open toch". Op macOS 15
    werkt de oude truc (rechtsklik → Open) niet meer. Beide wegen staan in LEERPAD.md.
  - De prompt zou dit beter kunnen zeggen.
- **De eerste poging faalde in CI** (run 112/113): `PlistBuddy Set :0:BundleIsRelocatable` gaf "Does Not Exist".
  - `pkgbuild --analyze` schreef die sleutel hier niet.
  - Opgelost met Delete + Add.
- **BundleIsRelocatable moet uit.** Een standaard-componentpakket is "relocatable": installer zoekt dan via de
  bundel-id of de bundel al ergens anders staat, en zet de update daar neer. Op Clay's Mac staat er misschien al een
  kopie in `~/Library/Audio/Plug-Ins` (de handmatige weg van eerder). Dan zou het pakket die bijwerken en niets in
  `/Library` zetten.
- **Twee kopieën.** Een eerder handmatig geïnstalleerde AU in `~/Library` blijft staan; het pakket haalt hem niet
  weg. LEERPAD.md zegt dat Clay die moet verwijderen. Een preinstall-script dat in de home-map van de gebruiker
  wist, vond ik te ingrijpend voor een installatiepakket.
- **Geen tweede volledige validatie op de schone runner.** Alleen auval (het stopcriterium) en `codesign --verify`
  draaien daar. pluginval en de null-tests draaien in de job `macos` op dezelfde bundels, die bit voor bit dezelfde
  bestanden zijn: ditto plus de pkg-payload.
- **Kosten notarisatie.** Het bedrag in euro komt uit Nederlandse berichten (iCulture), niet van een Apple-pagina.
  Daarom blijft het in LEERPAD.md gemarkeerd als niet bevestigd.
- **Kleinigheid:** auval waarschuwt op de schone runner, net als eerder, voor "Tail Time not supported" en
  "MusicDeviceMIDIEvent bij type aufx". Dat zijn waarschuwingen van clap-wrapper, geen fouten, en niet nieuw in deze
  fase.
