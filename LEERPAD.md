# LEERPAD — wat Clay zelf doet

## Installeren met het installatiepakket (fase 6; gratis, geen Apple Developer-account)
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-installatiepakket` (een zip) en pak het uit:
   daarin staat `PB-Toetsenpad-<versie>.pkg`.
2. Had je de plugin eerder met de hand in `~/Library/Audio/Plug-Ins` gezet (de oude manier hieronder)? Gooi die drie
   bundels dan eerst weg, anders ziet Logic twee keer dezelfde plugin.
3. Dubbelklik het `.pkg`. Het pakket is **niet gesigneerd** (dat kan alleen met een betaald Developer ID
   Installer-certificaat), dus macOS 15 weigert het eerst: "… kan niet worden geopend". Klik op **Gereed** (niet op
   "Verplaats naar prullenmand").
4. Open **Systeeminstellingen → Privacy en beveiliging**, scrol naar beneden naar "Beveiliging". Daar staat
   "PB-Toetsenpad-….pkg is geblokkeerd …". Klik **Open toch**, geef je wachtwoord, en klik in het nieuwe venster nog
   eens **Open**. (Rechtsklik → Open, de oude truc, werkt op macOS 15 niet meer.) De knop staat er ongeveer een uur na
   de poging; zie je hem niet, dubbelklik het `.pkg` dan nog een keer.
5. Het installatieprogramma zet de AU, VST3 en CLAP in `/Library/Audio/Plug-Ins/Components`, `…/VST3` en `…/CLAP`
   (voor alle gebruikers van de Mac; daarom vraagt het je wachtwoord) en herstart zelf `AudioComponentRegistrar`.
   De plugins zelf zijn ad-hoc gesigneerd; dat is genoeg op je eigen Mac.
6. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Toetsenpad. Status moet "geslaagd" zijn.

Een nieuwe versie installeer je op dezelfde manier over de oude heen; het installatieprogramma meldt dan "De upgrade is
voltooid". In CI gebeurt precies dit (job `installatie`, een verse Mac die niets bouwt): twee keer installeren, dan
plek en handtekening van alle drie, dan `auval -strict`.

Liever de Terminal (zonder Systeeminstellingen): `sudo installer -pkg ~/Downloads/PB-Toetsenpad-0.1.0.pkg -target /`.

**Weer verwijderen**:
```
sudo rm -rf "/Library/Audio/Plug-Ins/Components/PB Toetsenpad.component" "/Library/Audio/Plug-Ins/VST3/PB Toetsenpad.vst3" "/Library/Audio/Plug-Ins/CLAP/PB Toetsenpad.clap"
sudo pkgutil --forget nl.varve.pluginbouwer.toetsenpad.pkg
killall -9 AudioComponentRegistrar
```

## Installeren met de hand (de oude manier, zonder pakket)
1. Download het artefact `macos-keten_toetsenpad` (een zip). Een artefact van GitHub bewaart geen Unix-rechten of
   symlinks: CI zipt daarom de bundels zelf nog eens (`ditto -c -k --keepParent`); pak díe zip uit.
2. macOS zet een quarantainevlag op downloads: `xattr -dr com.apple.quarantine "PB Toetsenpad.component"`
   (en hetzelfde voor .vst3 en .clap).
3. Kopieer `PB Toetsenpad.component` naar `~/Library/Audio/Plug-Ins/Components/`.
4. Terminal: `killall -9 AudioComponentRegistrar` (anders ziet Logic hem niet meteen).
5. Logic: zoals stap 6 hierboven.

## Notariseren (optie, niet nodig voor je eigen Mac)
Notariseren laat Apple het pakket scannen; daarna opent macOS het zonder stap 3 en 4 hierboven, ook op de Mac van een
ander. Daarvoor is nodig:
- lidmaatschap van het **Apple Developer Program: US$ 99 per jaar, in de eurozone € 99 per jaar** (zelf nakijken op
  developer.apple.com/programs; Apple past regionale prijzen soms aan). Het gratis Apple-account is niet genoeg.
- twee certificaten uit dat lidmaatschap: **Developer ID Application** (voor de plugins, in plaats van ad-hoc
  `codesign --sign -`, met `--options runtime --timestamp`) en **Developer ID Installer** (voor het `.pkg`:
  `productsign` of `pkgbuild --sign`).
- daarna `xcrun notarytool submit PB-Toetsenpad.pkg --wait` (met een app-specifiek wachtwoord of API-sleutel) en
  `xcrun stapler staple PB-Toetsenpad.pkg`. In CI zouden de certificaten en het wachtwoord als GitHub-secrets staan.
Dit pakket doet het niet: het kost geld en Clay heeft het niet nodig.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren | Apple Developer Program, US$ 99 / € 99 per jaar (zie "Notariseren") | niet nodig voor eigen gebruik | distributie |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Presets (fase 4)
De fabriekspresets staan in `presets/`, één bestand per preset, bijvoorbeeld `presets/donker.json`:
`{ "naam": "Donker", "uitleg": "…", "waarden": { "Filter": 350, "Loslaten": 600 } }`. Een parameter die je weglaat,
krijgt zijn standaard (SPEC.md). Een nieuwe preset is een nieuw bestand; de bestandsnaam (kleine letters, cijfers, `-`)
mag daarna niet meer veranderen, want hij is de sleutel waarmee een host de preset terugvindt. Een tikfout in een
parameternaam of een waarde buiten het bereik laat de bouw (en dus CI) falen, met de reden erbij.
In Logic staan ze in het presetmenu bovenin het pluginvenster; een verse plugin begint op "Untitled" (de standaarden).

 (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.

## Mono, stereo en bypass (fase 5)
- **Mono of stereo**: in Logic kies je bij een nieuw spoor "Software-instrument" de uitgang (Mono of Stereo) vóór je
  PB Toetsenpad laadt; beide werken. Mono is het linkerkanaal van de stereo-uitvoer (de chorus en galm spreiden dan
  niet meer over links en rechts).
- **Bypass**: de plugin heeft een eigen parameter "Bypass" (Aan/Uit) die VST3- en CLAP-hosts voor hun bypassknop
  gebruiken: de plugin rekent dan door en vloeit in 10 ms over naar stilte, en bij weer aanzetten hoor je meteen wat hij
  op dat moment speelt. Logic heeft voor AU-instrumenten een eigen bypass (de aan/uit-knop van de plugin-slot); wat
  Logic dan doet, bepaalt Logic zelf. Probeer het in fase 7: geen tik bij aan- en uitzetten.
- **Samplerate**: 44,1 tot 192 kHz klinken hetzelfde (in CI bit voor bit nagerekend tegen de tool).
