Stopcriterium gehaald.

# Uitslag fase 6 — signeren en installeren (proef G3)

Laatste CI-run met de code (commit `0ab4348`): https://github.com/CKrebbers/pluginbouwer/actions/runs/39170046423.
Alle vier de jobs zijn groen: Linux, RTSan, macOS arm64 en de nieuwe job "macOS installatiepakket". Deze commit voegt
alleen dit verslag en de vinkjes toe; CI draait er opnieuw op.

## Wat ik deed
- **`gereedschap/maak-pkg.sh`** (nieuw): `sh maak-pkg.sh <map met bundels> <uit.pkg> <versie>`.
  - Eerst `codesign --verify --strict` op de drie ad-hoc gesigneerde bundels uit de macos-job. Een kapotte handtekening
    gaat het pakket niet in.
  - Dan een root met `Library/Audio/Plug-Ins/{Components,VST3,CLAP}` (`ditto`), en `--install-location /`.
  - De component-plist komt uit `pkgbuild --analyze`. Python (`plistlib`) zet daarin voor elke bundel, ook voor geneste:
    - `BundleIsRelocatable` = false (de sleutel **ontbrak** in de uitvoer van `--analyze`, dus hij is toegevoegd);
    - `BundleIsVersionChecked` = false. Elke CI-build heeft dezelfde CFBundleVersion (0.1.0), dus een nieuwere build
      moet altijd over de oude heen.
    Noemt `--analyze` een van de drie bundels niet, dan voegt het script hem toe. Daarna faalt het script als er ergens
    nog `true` staat of als er minder dan drie sleutels zijn.
  - postinstall: `killall -9 AudioComponentRegistrar 2>/dev/null || true; exit 0`. Zonder `|| true` laat een
    registrar die niet draait de hele installatie falen.
  - `pkgbuild --ownership recommended`, identifier `nl.varve.pluginbouwer.toetsenpad.pkg`. Het pakket zelf is
    ongesigneerd (`pkgutil --check-signature`: "Status: no signature").
- **`ci.yml`**:
  - De macos-job eindigt met de stap "Installatiepakket" en het artefact `macos-pkg`.
  - Nieuwe job **`installeer`** (`needs: macos`, verse `macos-15`): geen checkout en geen bouw, alleen
    `download-artifact` van het `.pkg`. De stappen:
    1. Eerst toetsen dat de runner echt vers is: geen bundel in `/Library` of `~/Library`, geen receipt.
    2. `sudo installer -verbose` twee keer, met `pipefail`.
    3. Plek, eigenaar root en `codesign --verify --strict --deep` voor alle drie. `codesign -dv` moet
       `Signature=adhoc` geven.
    4. `auval -a` moet Vrve tonen, dan `auval -strict -v aumu PTgg Vrve`. Er was geen handmatige `killall`: alleen die
       van de postinstall.
- **`LEERPAD.md`**:
  - Nieuw "Installeren met het installatiepakket": het pakket dubbelklikken, en op macOS 15 de weg via Privacy en
    beveiliging → "Open toch" (rechtsklik → Open werkt niet meer). Ook het alternatief `sudo installer` en weghalen
    met `pkgutil --forget`.
  - De handmatige installatie blijft staan als tweede weg.
  - Nieuw "Optie: notariseren": Apple Developer Program, 99 USD / € 99 per jaar, en de stappen (Developer ID
    Application/Installer, `notarytool`, `stapler`). De kostentabel is bijgewerkt. Het oude "[bedrag niet bevestigd]"
    is weg, maar het eurobedrag heb ik alleen in oudere bronnen gevonden: LEERPAD zegt dat je het bij het inschrijven
    controleert.

## Poorten (bewijs uit de logs van run 39170046423)
| poort | uitslag | bewijs |
|---|---|---|
| Fase 0–5: Linux (clap-validator, pluginval VST3, null-tests, samplerates) | groen | job 117801019115 success |
| Fase 0–5: RTSan + kanarie | groen | job 117801019266 success |
| Fase 0–5: macOS (auval, pluginval AU --repeat 3 en VST3, paneel, presets, null-tests AU/VST3/CLAP) | groen | job 117801019216 success |
| pkg gebouwd, BundleIsRelocatable false | groen | "--- component-plist na aanpassen": 3× `<key>BundleIsRelocatable</key><false/>`; vóór aanpassen ontbrak de sleutel; "pkgbuild: Adding top-level postinstall script", "Wrote package to pkg/PB Toetsenpad 0.1.0.pkg" |
| verse runner | groen | "verse runner: geen PB Toetsenpad, geen receipt" |
| eerste installatie | groen | "installer: Installing at base path /" … "installer: The install was successful." |
| tweede installatie = upgrade | groen | "installer: Upgrading at base path /" … "installer: The upgrade was successful." |
| plek en handtekening (alle drie) | groen | "…/Components/PB Toetsenpad.component: valid on disk", "satisfies its Designated Requirement", "Signature=adhoc", eigenaar root:wheel; hetzelfde voor `…/VST3/PB Toetsenpad.vst3` en `…/CLAP/PB Toetsenpad.clap`; receipt "volume: /", "location: " (leeg = de root, niet verplaatst) |
| auval -strict na installatie | groen | "aumu PTgg Vrve - Varve: PB Toetsenpad", "AU VALIDATION SUCCEEDED.", "auval exitcode: 0" (ook de presets Helder/Pluk/Wolk/Zacht, [0, 2] [0, 1], Tail Time PASS) |

## Pushes tot groen: 2
1. `eb72121`: alles behalve de installatiejob was groen. De tweede installatie was wél geslaagd, maar mijn toets
   zocht "The install was successful". Bij een upgrade schrijft `installer` "The upgrade was successful.". Een fout in
   de toets, niet in het pakket.
2. `0ab4348`: de toets eist nu "The upgrade was successful" en "Upgrading at base path /". Met `set -o pipefail` telt
   ook de exitcode van `installer` (zonder pipefail telde alleen die van `tee`). Groen.

## Onduidelijk of open in het pakket
- **De prompt zegt "de tweede meldt upgrade" maar niet welke tekst.** `installer` meldt hem op twee plekken
  ("Upgrading at base path /" en "The upgrade was successful."), en "The install was successful" verschijnt dan niet.
  Goed om in PROMPTS.md/CLAUDE.md als valkuil te zetten.
- **Het effect van de postinstall is niet direct te zien.** `installer -verbose` toont de uitvoer van het script niet.
  Het indirecte bewijs: auval vond de AU meteen, zonder handmatige `killall`. Op een verse runner draait de registrar
  misschien nog niet eens; de toets bewijst dus vooral dat het script de installatie niet laat falen.
- **"Open toch" is niet in CI te toetsen.** Gatekeeper grijpt alleen in bij een gedownload bestand met quarantainevlag
  en een dubbelklik; `sudo installer` omzeilt dat. Of de tekst in LEERPAD.md precies klopt met wat macOS 15 toont, moet
  Clay zelf zien.
- **`BundleIsVersionChecked` false** is mijn eigen keuze. De prompt noemt hem niet. Zonder die keuze zou een nieuwere
  build met hetzelfde versienummer volgens Apple wel overschrijven ("niet nieuwer" = gelijk mag), maar dat is een randgeval
  dat ik niet wilde laten afhangen van Installer.
- **Pakket en handmatige kopie naast elkaar.** Wie eerder via LEERPAD in `~/Library` installeerde, heeft na het pakket
  twee exemplaren. LEERPAD waarschuwt ervoor, maar het pakket haalt de oude niet weg.
- De europrijs van het Developer Program kon ik niet in een actuele Apple-bron bevestigen (zie LEERPAD).
