# LEERPAD — wat Clay zelf doet

## Installeren met het pakket (gratis, geen Apple Developer-account)
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-pkg` (een zip met `PB-Dub-echo-<versie>.pkg`)
   en pak het uit.
2. Het pakket is niet gesigneerd (dat kost een betaald certificaat, zie hieronder). macOS weigert het daarom na een
   download ("Apple kan niet controleren …"). Kies één van de twee:
   - Terminal: `xattr -d com.apple.quarantine ~/Downloads/PB-Dub-echo-*.pkg`, dan dubbelklikken; of
   - dubbelklikken, het venster sluiten, dan Systeeminstellingen → Privacy en beveiliging → "Open toch".
3. Het installatieprogramma vraagt je wachtwoord en zet alle drie de formaten neer, voor alle gebruikers:
   - AU: `/Library/Audio/Plug-Ins/Components/PB Dub-echo.component`
   - VST3: `/Library/Audio/Plug-Ins/VST3/PB Dub-echo.vst3`
   - CLAP: `/Library/Audio/Plug-Ins/CLAP/PB Dub-echo.clap`
   Een oudere versie op die plekken haalt hij eerst weg. Daarna laat hij macOS de AU's opnieuw zoeken
   (`killall -9 AudioComponentRegistrar`); dat hoef je dus niet zelf te doen.
4. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Dub-echo. Status moet "geslaagd" zijn.
5. Had je eerder met de hand een kopie in `~/Library/Audio/Plug-Ins/…` gezet? Haal die weg, anders ziet Logic twee
   versies en weet je niet welke hij laadt.

**Verwijderen**: de drie bundels hierboven weggooien (`sudo rm -rf "/Library/Audio/Plug-Ins/Components/PB Dub-echo.component"`
enz.), dan `sudo pkgutil --forget nl.varve.pluginbouwer.dub-echo.pkg`.

**Zonder pakket** kan het ook nog: het artefact `macos-keten_dub_echo` uitpakken, `xattr -dr com.apple.quarantine` op
de bundel, de bundel naar `~/Library/Audio/Plug-Ins/Components/` kopiëren en `killall -9 AudioComponentRegistrar`.

## Notariseren (optie, niet nodig voor je eigen Mac)
Ad-hoc gesigneerd is genoeg zolang jij de enige gebruiker bent. Wil je de plugin aan anderen geven zonder dat zij stap 2
hierboven moeten doen, dan heb je nodig:
- lidmaatschap van het **Apple Developer Program: US$ 99 per jaar** (in de eurozone € 99 per jaar; Apple toont de
  prijs in je eigen munt bij het aanmelden, controleer dat daar). Zonder lidmaatschap geen Developer ID-certificaten.
- twee certificaten (inbegrepen in het lidmaatschap): **Developer ID Application** voor de bundels
  (`codesign --sign "Developer ID Application: …" --options runtime --timestamp`) en **Developer ID Installer** voor
  het pakket (`pkgbuild --sign "Developer ID Installer: …"` of `productsign`).
- notariseren (gratis binnen het lidmaatschap): `xcrun notarytool submit PB-Dub-echo.pkg --wait …`, dan
  `xcrun stapler staple PB-Dub-echo.pkg`.
In CI moeten het certificaat en een app-specifiek wachtwoord dan als GitHub-secrets in de repo; het pakket bevat
daarvoor nu niets (geen betaalde diensten).

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| een gesigneerd pakket en notariseren | Apple Developer Program: US$ 99/jaar (eurozone € 99/jaar) | ad-hoc gesigneerde bundels in een ongesigneerd .pkg; quarantainevlag zelf weghalen | distributie aan anderen |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Wat je kunt leren (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.
