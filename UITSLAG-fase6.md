Stopcriterium gehaald.

# Uitslag fase 6 — Dub-echo (tak proef/f1)

Laatste CI-run met de code (commit 78c82c9): https://github.com/CKrebbers/pluginbouwer/actions/runs/39050118644
— alle vijf de jobs `success`: Linux, RTSan, TSan, macOS, en de nieuwe job "macOS pakket (installeren op een schone
runner, auval)". Na deze run kwamen alleen nog deze uitslag, TASKS.md en VERSLAG.md erbij (alleen docs).

Er waren twee pushes met code nodig:
1. **Runs 39048160888 en 39048238539 (commits 85b206f en 60db5be, de tweede alleen docs)**: de macOS-job faalde in
   `maak-pkg.sh`: `Set: Entry, ":0:BundleIsRelocatable", Does Not Exist`. pkgbuild --analyze op macOS 15 schrijft die
   sleutel niet.
2. **Run 39050118644 (commit 78c82c9)**: de sleutel gaat er nu op met `plutil -replace`. Alles groen.

## Wat ik deed
- **`gereedschap/maak-pkg.sh <plugins-map> <uit.pkg>`** maakt met `pkgbuild` één component-pakket:
  - identifier `nl.varve.pluginbouwer.dub-echo.pkg`, versie uit de Info.plist van de AU (0.1.0);
  - de AU naar `/Library/Audio/Plug-Ins/Components`, de VST3 naar `/Library/Audio/Plug-Ins/VST3`, de CLAP naar
    `/Library/Audio/Plug-Ins/CLAP`;
  - het controleert eerst dat elke bundel ad-hoc gesigneerd is (`codesign --verify --strict`) en kopieert met `ditto`;
  - `BundleIsRelocatable` uit, zodat de installer niet over een kopie elders (een uitgepakte zip in Downloads) schrijft;
  - `postinstall`: `killall -9 AudioComponentRegistrar` (valkuil 5), zodat Logic de AU meteen ziet.
- **CI**: de macOS-job maakt na "Ad-hoc signeren" het pakket en zet het als artefact `macos-pkg`. Een nieuwe job
  `pakket` op een verse macos-15-runner krijgt alleen dat artefact en:
  1. bewijst dat de runner schoon is (geen PB Dub-echo in /Library of ~/Library, auval kent `aufx PDca Vrve` niet);
  2. zet een lokvogel neer: dezelfde drie bundels in ~/Downloads;
  3. installeert met `sudo installer -pkg … -target /`;
  4. eist de drie bundels op hun plek, ad-hoc gesigneerd, zonder quarantaine, en de lokvogel onaangeroerd;
  5. draait `auval -strict` (plus de kanaalindelingen uit fase 5);
  6. draait pluginval (strictness 5) op de geïnstalleerde VST3 en AU.
- **Docs**: LEERPAD.md (installeren met het pakket, openen zonder Apple-handtekening, verwijderen; notariseren als
  optie met de kosten, € 99 per jaar voor het Apple Developer Program), ARCHITECTUUR.md ("Installatiepakket
  (fase 6)"), TASKS.md, VERSLAG.md.

## Poorten (run 39050118644)
| poort | uitslag | bewijs (log / stap) |
|---|---|---|
| schone runner vóór de installatie | groen | `schoon: geen PB Dub-echo in /Library of ~/Library, auval kent aufx PDca Vrve niet` |
| pakket installeert | groen | `installer: The install was successful.`, `package-id: nl.varve.pluginbouwer.dub-echo.pkg`, `version: 0.1.0`; `pkgutil --files`: `Library/Audio/Plug-Ins/CLAP/PB Dub-echo.clap`, `.../Components/PB Dub-echo.component`, `.../VST3/PB Dub-echo.vst3` |
| op de juiste plek, ad-hoc gesigneerd, geen quarantaine | groen | `ok: /Library/Audio/Plug-Ins/Components/PB Dub-echo.component (root:wheel drwxr-xr-x)`, idem VST3 en CLAP (de stap eist `Signature=adhoc` en `codesign --verify --strict`) |
| niet relocatable (lokvogel) | groen | `lokvogel in ~/Downloads onaangeroerd` (mtime en inode van de drie kopieën gelijk) |
| **auval na de installatie** | groen | `aufx PDca Vrve  -  Varve: PB Dub-echo`, `AU VALIDATION SUCCEEDED.`, `auval: [1, 1] [1, 2] [2, 2]`, `auval vond de AU in /Library/Audio/Plug-Ins/Components en keurde hem goed` |
| pluginval op de geïnstalleerde VST3 en AU | groen | `Started validating: /Library/Audio/Plug-Ins/VST3/PB Dub-echo.vst3` … `SUCCESS`; idem `.component` … `SUCCESS` |
| eerdere poorten (auval en pluginval 10 --rtcheck, alle null-tests en matrices, bypass, automatisering, state, presets CLAP en AU, paneel, clap-validator, RTSan, TSan en de kanaries) | groen | alle stappen `success` in deze run |

## Waar ik vastliep / wat onduidelijk of fout was in het pakket
- **"ad-hoc gesigneerd" kan niet voor een .pkg.** Een installatiepakket heeft geen ad-hoc-handtekening; `productsign`
  en `pkgbuild --sign` willen een Developer ID Installer-certificaat (betaald). Ik heb het gelezen als: de plugins in
  het pakket zijn ad-hoc gesigneerd, het pakket zelf niet. Gevolg voor Clay: Gatekeeper houdt het gedownloade .pkg
  tegen; LEERPAD.md zegt hoe je het opent ("Open toch" of `xattr -d com.apple.quarantine`). De opdracht zou dat
  kunnen zeggen.
- **pkgbuild --analyze op macOS 15** laat `BundleIsRelocatable` weg (een CI-ronde; VERSLAG.md). Een valkuil 10 in
  CLAUDE.md zou een volgend gesprek dit besparen: "pkgbuild: zet BundleIsRelocatable met plutil -replace, niet met
  PlistBuddy Set".
- **Waar installeren.** De opdracht zegt "de juiste plek". Een .pkg installeert als root, dus ik koos de
  systeemmappen `/Library/Audio/Plug-Ins/…` (alle gebruikers). LEERPAD.md zegt dat een oude handmatige kopie in
  `~/Library/Audio/Plug-Ins/Components` weg moet, anders ziet Logic er twee.
- **De prijs van notariseren** stond in LEERPAD.md als "niet bevestigd". € 99 per jaar komt uit Nederlandstalige bronnen
  (iCulture); de actuele prijs staat op developer.apple.com.
- De CI-job doet geen echte Logic-scan. Dat is fase 7 (Clay).
