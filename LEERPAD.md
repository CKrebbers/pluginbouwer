# LEERPAD — wat Clay zelf doet

## Installeren met het pakket (gratis, geen Apple Developer-account)
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-pkg` (een zip) en pak het uit. Je krijgt
   `PB-Dub-echo.pkg`.
2. Het pakket is niet gesigneerd (een .pkg kan niet ad-hoc gesigneerd worden; zie hieronder), dus macOS weigert het
   met een dubbelklik. Kies één van twee:
   - Terminal (het kortst): `sudo installer -pkg ~/Downloads/PB-Dub-echo.pkg -target /` en je wachtwoord.
   - Finder: dubbelklik, "Gereed" bij de waarschuwing, dan Systeeminstellingen → Privacy en beveiliging →
     "Open toch" bij PB-Dub-echo.pkg, en het installatieprogramma volgen.
3. Het pakket zet de plugin voor alle gebruikers neer:
   - `/Library/Audio/Plug-Ins/Components/PB-Dub-echo.component` (AU, voor Logic)
   - `/Library/Audio/Plug-Ins/VST3/PB-Dub-echo.vst3`
   - `/Library/Audio/Plug-Ins/CLAP/PB-Dub-echo.clap`

   en doet na afloop zelf `killall -9 AudioComponentRegistrar`. De bundels erin zijn ad-hoc gesigneerd.
4. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Dub-echo. Status moet "geslaagd" zijn.
5. Bijwerken: installeer gewoon het nieuwe pakket eroverheen.
6. Verwijderen:
   `sudo rm -rf /Library/Audio/Plug-Ins/{Components,VST3,CLAP}/PB-Dub-echo.* && sudo pkgutil --forget nl.varve.pluginbouwer.dub-echo.pkg`
7. Heb je eerder met de hand in `~/Library/Audio/Plug-Ins` geïnstalleerd (zie hieronder)? Haal die kopieën dan weg;
   anders ziet Logic twee keer dezelfde AU en is onduidelijk welke hij laadt.

Zelf een pakket maken op je Mac (na een build): `sh gereedschap/maak-pkg.sh build/plugins 0.1.0 PB-Dub-echo.pkg`.

## Installeren met de hand (zonder pakket)
1. Download het artefact `macos-keten_dub_echo` (een zip) en pak het uit.
2. macOS zet een quarantainevlag op downloads: `xattr -dr com.apple.quarantine PB-Dub-echo.component`
   (en hetzelfde voor .vst3 en .clap).
3. Kopieer `PB-Dub-echo.component` naar `~/Library/Audio/Plug-Ins/Components/`.
4. Terminal: `killall -9 AudioComponentRegistrar` (anders ziet Logic hem niet meteen).
5. Logic: zoals stap 4 hierboven.

## Optie: notariseren (alleen nodig als anderen hem installeren)
Voor jezelf is het niet nodig. Wil je het pakket aan anderen geven zonder dat zij stap 2 moeten doen, dan moet het:
1. lid zijn van het Apple Developer Program: **€ 99 per jaar** (VS: US$ 99; het bedrag in euro komt uit
   Nederlandse nieuwsberichten, niet van Apple zelf: **[controleer op developer.apple.com/programs]**);
2. de bundels signeren met een "Developer ID Application"-certificaat en *hardened runtime*
   (`codesign --options runtime --timestamp --sign "Developer ID Application: …"`);
3. het pakket signeren met een "Developer ID Installer"-certificaat (`pkgbuild … --sign "Developer ID Installer: …"`
   of `productsign`);
4. insturen bij Apple en het bewijs aan het pakket hangen:
   `xcrun notarytool submit PB-Dub-echo.pkg --apple-id … --team-id … --password <app-specifiek wachtwoord> --wait`
   en `xcrun stapler staple PB-Dub-echo.pkg`.

Het insturen zelf kost niets extra. In CI kan het ook, met de certificaten en het wachtwoord als GitHub-secrets; dat
zit nu bewust niet in de CI.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren (zie "Optie: notariseren") | Apple Developer Program, € 99/jaar **[bedrag in euro niet door Apple bevestigd]** | niet nodig voor eigen gebruik: pakket met ad-hoc gesigneerde bundels | distributie aan anderen |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Wat je kunt leren (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.
