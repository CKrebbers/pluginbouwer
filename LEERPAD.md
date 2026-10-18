# LEERPAD — wat Clay zelf doet

## Installeren met het pakket (gratis, geen Apple Developer-account)
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-pkg` (een zip) en pak het uit: daarin staat
   `PB Toetsenpad.pkg`. Het pakket zet alle drie de formaten op de vaste plek voor alle gebruikers:
   `/Library/Audio/Plug-Ins/Components` (AU), `/Library/Audio/Plug-Ins/VST3` en `/Library/Audio/Plug-Ins/CLAP`.
2. Dubbelklik het `.pkg`. Het pakket is **niet gesigneerd** (dat kan alleen met een betaald certificaat, zie
   hieronder), dus macOS 15 weigert het eerst: "PB Toetsenpad.pkg" kan niet worden geopend. Klik **Gereed** (niet "Verplaats
   naar prullenmand").
3. Open **Systeeminstellingen → Privacy en beveiliging**, scrol naar **Beveiliging**: daar staat dat
   "PB Toetsenpad.pkg" is geblokkeerd. Klik **Open toch** en bevestig met je wachtwoord of Touch ID. (Rechtsklik → Open
   werkt op macOS 15 niet meer om dit te omzeilen.) Dit hoeft alleen binnen een uur na de eerste poging.
4. Het installatieprogramma start: Ga door → Installeer → wachtwoord. Het pakket herstart na afloop zelf de AU-registry
   (`killall -9 AudioComponentRegistrar`), dus je hoeft niets in Terminal te doen. Een nieuwere versie installeer je
   op dezelfde manier over de oude heen ("upgrade").
5. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Toetsenpad. Status moet "geslaagd" zijn.

De plug-ins zelf zijn ad-hoc gesigneerd (`codesign --sign -`); dat is genoeg voor je eigen Mac. Verwijderen: gooi de
drie bundels uit de mappen van stap 1 weg en `sudo pkgutil --forget nl.varve.pluginbouwer.toetsenpad.pkg`.

## Installeren zonder pakket (met de hand)
1. Download het artefact `macos-keten_toetsenpad` (een zip). Een artefact van GitHub bewaart geen Unix-rechten of
   symlinks: CI zipt de bundels daarom zelf nog eens (`ditto -c -k --keepParent`); pak díe zips uit.
2. macOS zet een quarantainevlag op downloads: `xattr -dr com.apple.quarantine "PB Toetsenpad.component"`
   (en hetzelfde voor .vst3 en .clap).
3. Kopieer `PB Toetsenpad.component` naar `~/Library/Audio/Plug-Ins/Components/`.
4. Terminal: `killall -9 AudioComponentRegistrar` (anders ziet Logic hem niet meteen).
5. Logic: als stap 5 hierboven. Heb je ook het pakket geïnstalleerd, dan staat de AU twee keer op de schijf; houd er één.

## Optie: signeren en notariseren (alleen voor verspreiden, kost geld)
Niet nodig voor je eigen Mac. Wel als anderen het pakket moeten openen zonder de stap "Open toch":
- Lid worden van het **Apple Developer Program: US$ 99 per jaar** (in de eurozone ±€ 99 per jaar; Apple's actuele prijs
  staat op developer.apple.com/programs). Een gratis Apple-account kan geen Developer ID-certificaten maken.
- Twee certificaten (zit in het lidmaatschap, geen extra kosten): **Developer ID Application** voor de drie bundels
  (`codesign --sign "Developer ID Application: …" --options runtime --timestamp`) en **Developer ID Installer** voor het
  pakket (`pkgbuild --sign "Developer ID Installer: …"` of achteraf `productsign`).
- Notariseren: `xcrun notarytool submit "PB Toetsenpad.pkg" --apple-id … --team-id … --password <app-specifiek
  wachtwoord> --wait`, daarna `xcrun stapler staple "PB Toetsenpad.pkg"`. Apple controleert dan op malware; het pakket
  opent daarna op elke Mac zonder waarschuwing.
- In CI zouden de certificaten en het wachtwoord als GitHub-secrets moeten; dat is nu niet ingericht.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren (en het `.pkg` signeren) | Apple Developer Program, US$ 99/jaar (±€ 99) | niet nodig voor eigen gebruik: "Open toch" | distributie aan anderen |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Wat je kunt leren (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.
