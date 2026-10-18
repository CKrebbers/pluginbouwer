# LEERPAD — wat Clay zelf doet

## Installeren met het installatiepakket (fase 6; gratis, geen Apple Developer-account)
Het pakket zet alle drie de formaten op de juiste plek, voor alle gebruikers van de Mac:
`/Library/Audio/Plug-Ins/Components` (AU, voor Logic), `/Library/Audio/Plug-Ins/VST3` en `/Library/Audio/Plug-Ins/CLAP`.
De plugins zelf zijn ad-hoc gesigneerd; het `.pkg` is **ongesigneerd** (een pakket signeren kan alleen met een betaald
Developer ID Installer-certificaat), dus macOS houdt het de eerste keer tegen.
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-installatiepakket` (een zip) en pak het uit:
   je krijgt `PB-Dub-echo.pkg`.
2. Dubbelklik `PB-Dub-echo.pkg`. macOS 15 meldt dat het niet geopend kan worden ("Apple kan niet controleren …"); klik
   **Gereed** (niet "Verplaats naar prullenmand").
3. Open **Systeeminstellingen → Privacy en beveiliging**, scrol naar **Beveiliging**: daar staat
   "PB-Dub-echo.pkg is geblokkeerd …". Klik **Open toch**, bevestig met je wachtwoord of Touch ID en klik nog eens
   **Open toch**. (Rechtsklik → Open, wat vroeger werkte, werkt op macOS 15 niet meer.) Dit hoeft per download één keer.
4. Volg Installer (Ga door → Installeer, wachtwoord). Het pakket herstart zelf `AudioComponentRegistrar`, zodat Logic
   de AU meteen kan zien. Installeer je een nieuwere versie over een oude heen, dan heet dat in Installer "upgrade".
5. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Dub-echo. Status moet "geslaagd" zijn.

Liever de Terminal? `sudo installer -pkg ~/Downloads/PB-Dub-echo.pkg -target /` doet hetzelfde zonder de stap in
Privacy en beveiliging. Verwijderen: gooi de drie bundels uit de mappen hierboven weg en
`sudo pkgutil --forget nl.varve.pluginbouwer.dub-echo.pkg`.

## Installeren zonder pakket (met de hand, alleen voor jezelf)
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-keten_dub_echo` (een zip). Een artefact van
   GitHub bewaart geen Unix-rechten of symlinks: laat CI daarom de bundels zelf nog eens zippen (`ditto -c -k
   --keepParent` of `zip -y`) en pak díe zip uit.
2. Uitpakken. macOS zet een quarantainevlag op downloads: `xattr -dr com.apple.quarantine "PB Dub-echo.component"`
   (en hetzelfde voor .vst3 en .clap).
3. Kopieer `PB Dub-echo.component` naar `~/Library/Audio/Plug-Ins/Components/`.
4. Terminal: `killall -9 AudioComponentRegistrar` (anders ziet Logic hem niet meteen).
5. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Dub-echo. Status moet "geslaagd" zijn.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren (optie) | Apple Developer Program, € 99 per jaar (US$ 99) **[bedrag uit Nederlandse bronnen, niet op Apple's prijspagina bevestigd]** | niet nodig voor eigen gebruik: het ongesigneerde `.pkg` + "Open toch" | als anderen het pakket moeten kunnen openen zonder "Open toch" |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Wat je kunt leren (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.

## Notarisatie (optie, niet nodig voor je eigen Mac)
Wil je het pakket aan anderen geven zonder dat zij "Open toch" moeten zoeken, dan heb je het Apple Developer Program
nodig (€ 99 per jaar, zie de tabel). Daarmee krijg je twee certificaten: **Developer ID Application** (de bundels
signeren met `codesign --options runtime --timestamp --sign "Developer ID Application: …"` in plaats van `--sign -`)
en **Developer ID Installer** (`productsign --sign "Developer ID Installer: …"` op het `.pkg`). Daarna
`xcrun notarytool submit PB-Dub-echo.pkg --wait` (met een app-specifiek wachtwoord of API-sleutel) en
`xcrun stapler staple PB-Dub-echo.pkg`. In CI zet je de certificaten en het wachtwoord als GitHub-secrets. Niets in
het pakket hoeft daarvoor te veranderen behalve die twee signeerstappen; zolang je het alleen zelf gebruikt is het
geldweggooien.
