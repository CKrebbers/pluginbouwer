# LEERPAD — wat Clay zelf doet

## Installeren met het installatiepakket (gratis, geen Apple Developer-account)
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-pkg` (een zip) en pak het uit: daarin zit
   `PB Toetsenpad 0.1.0.pkg`. Het pakket zet de AU, VST3 en CLAP (ad-hoc gesigneerd) in
   `/Library/Audio/Plug-Ins/Components`, `/VST3` en `/CLAP`, en herstart na afloop de AU-registratie
   (`killall -9 AudioComponentRegistrar`).
2. Dubbelklik het `.pkg`. Het pakket is **niet gesigneerd** (dat kan alleen met een betaald Developer ID
   Installer-certificaat), dus macOS 15 weigert het eerst: "Apple kan niet controleren of … vrij is van malware".
   Klik **Gereed** (niet "Verplaats naar prullenmand").
3. Open **Systeeminstellingen → Privacy en beveiliging** en scrol naar **Beveiliging**. Daar staat dat
   "PB Toetsenpad 0.1.0.pkg" is geblokkeerd, met de knop **Open toch**. Klik erop, bevestig met je wachtwoord (of
   Touch ID) en klik nog eens **Open**. Rechtsklik → Open, de oude truc, werkt op macOS 15 niet meer. De knop blijft
   ongeveer een uur staan; is hij weg, dubbelklik het pakket opnieuw.
   Liever de Terminal: `sudo installer -pkg ~/Downloads/"PB Toetsenpad 0.1.0.pkg" -target /` (dan vraagt macOS niets).
4. Doorloop de Installer (je wachtwoord is nodig: hij schrijft in `/Library`). Een nieuwere build installeer je op
   dezelfde manier over de oude heen; Installer noemt dat een upgrade.
5. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Toetsenpad. Status moet "geslaagd" zijn.

Weghalen: verwijder de drie bundels uit `/Library/Audio/Plug-Ins/…` en daarna
`sudo pkgutil --forget nl.varve.pluginbouwer.toetsenpad.pkg`.

## Installeren met de hand (zonder pakket)
1. Download het artefact `macos-keten_toetsenpad` (een zip). Een artefact van GitHub bewaart geen Unix-rechten of
   symlinks: CI zipt de bundels daarom zelf nog eens (`ditto -c -k --keepParent`); pak díe zips uit.
2. macOS zet een quarantainevlag op downloads: `xattr -dr com.apple.quarantine "PB Toetsenpad.component"`
   (en hetzelfde voor .vst3 en .clap).
3. Kopieer `PB Toetsenpad.component` naar `~/Library/Audio/Plug-Ins/Components/` (.vst3 naar `…/VST3`, .clap naar
   `…/CLAP`). Niet naast het pakket gebruiken: twee exemplaren van dezelfde AU verwarren Logic.
4. Terminal: `killall -9 AudioComponentRegistrar` (anders ziet Logic hem niet meteen).
5. Logic: Plug-in Manager, zoals hierboven.

## Optie: notariseren (kost geld, niet nodig voor je eigen Mac)
Met een lidmaatschap van het **Apple Developer Program** (99 USD per lidmaatschapsjaar, in euro's € 99 per jaar;
controleer het bedrag bij het inschrijven op developer.apple.com/programs) krijg je een *Developer ID Application*- en
een *Developer ID Installer*-certificaat. Dan kan CI:
1. de bundels signeren met `codesign --sign "Developer ID Application: …" --options runtime --timestamp`;
2. het pakket signeren met `pkgbuild --sign "Developer ID Installer: …"` (of `productsign`);
3. het pakket notariseren met `xcrun notarytool submit … --wait` en er `xcrun stapler staple` op zetten.
Het pakket opent dan op elke Mac met een dubbelklik, zonder "Open toch". Voor je eigen Mac is het gratis pad (ad-hoc
gesigneerde plugins in een ongesigneerd pakket) genoeg. Stop je het lidmaatschap, dan blijven al genotariseerde
pakketten werken, maar kun je geen nieuwe meer signeren.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren en het `.pkg` signeren | Apple Developer Program: 99 USD / € 99 per jaar (zie "Optie: notariseren") | ongesigneerd pakket + "Open toch" | distributie aan anderen |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Wat je kunt leren (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.
