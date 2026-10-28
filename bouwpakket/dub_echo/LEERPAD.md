# LEERPAD — wat Clay zelf doet

## Installeren (gratis, geen Apple Developer-account)
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-keten_dub_echo` (een zip). Een artefact van
   GitHub bewaart geen Unix-rechten of symlinks: laat CI daarom de bundels zelf nog eens zippen (`ditto -c -k
   --keepParent` of `zip -y`) en pak díe zip uit.
2. Uitpakken. macOS zet een quarantainevlag op downloads: `xattr -dr com.apple.quarantine "PB Dub echo.component"`
   (en hetzelfde voor .vst3 en .clap).
3. Kopieer `PB Dub echo.component` naar `~/Library/Audio/Plug-Ins/Components/`.
4. Terminal: `killall -9 AudioComponentRegistrar` (anders ziet Logic hem niet meteen).
5. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Dub echo. Status moet "geslaagd" zijn.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren | Apple Developer Program **[bedrag niet bevestigd, ±€ 99/jaar]** | niet nodig voor eigen gebruik | distributie |
| AUv3 op iPad/iPhone | idem (gratis account: 7 dagen geldig) | gratis account, elke week opnieuw | blijvend op je toestel |
| CI op macOS | 0 (publieke repo) | — | een privérepo telt macOS-minuten zwaarder **[factor niet bevestigd]** |

## Wat je kunt leren (niet nodig om te bouwen)
- Wat een null-test is: twee renders van elkaar aftrekken; stilte = gelijk.
- Een terugkoppeling: hoe de lusversterking (terugkoppeling × filter × saturatie) bepaalt of hij uitsterft of doorgaat.
