# LEERPAD — wat Clay zelf doet

## Installeren (gratis, geen Apple Developer-account)
1. Download in GitHub bij de laatste groene CI-run het artefact `macos-keten_toetsenpad` (een zip). Een artefact van
   GitHub bewaart geen Unix-rechten of symlinks: laat CI daarom de bundels zelf nog eens zippen (`ditto -c -k
   --keepParent` of `zip -y`) en pak díe zip uit.
2. Uitpakken. macOS zet een quarantainevlag op downloads: `xattr -dr com.apple.quarantine "PB Toetsenpad.component"`
   (en hetzelfde voor .vst3 en .clap).
3. Kopieer `PB Toetsenpad.component` naar `~/Library/Audio/Plug-Ins/Components/`.
4. Terminal: `killall -9 AudioComponentRegistrar` (anders ziet Logic hem niet meteen).
5. Logic: Instellingen → Plug-in Manager → "Reset & Rescan Selection" op PB Toetsenpad. Status moet "geslaagd" zijn.

## Kosten en het gratis alternatief
| wat | kost | gratis alternatief | wanneer je wél moet betalen |
|---|---|---|---|
| signeren voor je eigen Mac | 0 | ad-hoc (`codesign --sign -`), CI doet het | als anderen hem moeten kunnen installeren zonder terminal |
| notariseren | Apple Developer Program **[bedrag niet bevestigd, ±€ 99/jaar]** | niet nodig voor eigen gebruik | distributie |
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
