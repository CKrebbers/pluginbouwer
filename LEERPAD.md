# LEERPAD — wat Clay zelf doet

## Installeren (gratis, geen Apple Developer-account)
Met het installatiepakket (fase 6):
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-pkg` (een zip) en pak het uit. Daarin staat
   `PB Dub-echo.pkg`.
2. Het pakket is niet door Apple gesigneerd (dat kost geld, zie hieronder), dus macOS houdt het tegen bij dubbelklikken
   ("kan niet worden geopend"). Twee manieren om het toch te openen:
   - Systeeminstellingen → Privacy en beveiliging → onderaan bij "PB Dub-echo.pkg" op **Open toch** klikken;
   - of in Terminal: `xattr -d com.apple.quarantine ~/Downloads/"PB Dub-echo.pkg"` en daarna dubbelklikken.
3. Doorklikken en je wachtwoord geven. Het pakket zet voor alle gebruikers op deze Mac:
   - de AU in `/Library/Audio/Plug-Ins/Components/` (voor Logic),
   - de VST3 in `/Library/Audio/Plug-Ins/VST3/`,
   - de CLAP in `/Library/Audio/Plug-Ins/CLAP/`.

   Na afloop laat het macOS de AU-lijst opnieuw lezen (`killall -9 AudioComponentRegistrar`).
4. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Dub-echo. Status moet "geslaagd" zijn.

Staat er nog een oude kopie in `~/Library/Audio/Plug-Ins/Components/` (van de handmatige manier hieronder)? Gooi die
weg, anders ziet Logic er twee.

Verwijderen: gooi de drie bundels hierboven weg en typ `sudo pkgutil --forget nl.varve.pluginbouwer.dub-echo.pkg`.

Handmatig (zonder pakket): artefact `macos-keten_dub_echo` uitpakken,
`xattr -dr com.apple.quarantine "PB Dub-echo.component"`, naar `~/Library/Audio/Plug-Ins/Components/` kopiëren,
`killall -9 AudioComponentRegistrar`, en dan stap 4.

## Notariseren (optie, niet nodig voor je eigen Mac)
De plugins in het pakket zijn ad-hoc gesigneerd (`codesign --sign -`). Dat is genoeg voor jouw Mac. Wil je het pakket
aan anderen geven zonder dat zij stap 2 moeten doen, dan moet het via Apple:
- **Kosten:** het Apple Developer Program, **€ 99 per jaar** (in de VS $ 99; prijs op developer.apple.com controleren).
  Notariseren zelf is daarna gratis. Een gratis Apple-account kan geen Developer ID-certificaten maken.
- **Wat er dan verandert:**
  1. de bundels signeren met een "Developer ID Application"-certificaat en `--options runtime` (hardened runtime)
     in plaats van `--sign -`;
  2. het pakket signeren met een "Developer ID Installer"-certificaat (`productsign`, of `pkgbuild --sign`);
  3. `xcrun notarytool submit "PB Dub-echo.pkg" --apple-id … --team-id … --wait` (Apple controleert op malware,
     enkele minuten);
  4. `xcrun stapler staple "PB Dub-echo.pkg"`, zodat het ook offline opent.
- In CI moeten het certificaat (.p12) en een app-specifiek wachtwoord dan als GitHub-secrets erbij. Dat zit nu niet in
  het pakket: het is een betaalde stap en voor eigen gebruik overbodig.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren | Apple Developer Program, € 99/jaar (zie "Notariseren") | niet nodig voor eigen gebruik; het pakket opent via "Open toch" | als anderen het pakket zonder omweg moeten kunnen openen |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Wat je kunt leren (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.
