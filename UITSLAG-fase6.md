Stopcriterium gehaald.

# Uitslag fase 6 — proef G2 (tak `proef/g2`)

Laatste CI-run met code (groen, alle vier de jobs): https://github.com/CKrebbers/pluginbouwer/actions/runs/39167229773
(commit `79aef1b`). Deze commit verandert alleen dit verslag en TASKS.md.

## Wat ik deed
- **`gereedschap/maak-pkg.sh <bundels> <uit.pkg>`** (macOS): zet de al ad-hoc gesigneerde `.component`, `.vst3` en
  `.clap` met `ditto` in een root `Library/Audio/Plug-Ins/{Components,VST3,CLAP}`, controleert daar de handtekening,
  maakt de component-plist met `pkgbuild --analyze` en zet in elke bundel (ook geneste onder `ChildBundles`)
  `BundleIsRelocatable` op false, met plistlib: ontbreekt de sleutel, dan wordt hij toegevoegd (het script zegt welk van
  de twee). Dan `pkgbuild --root … --component-plist … --scripts … --identifier nl.varve.pluginbouwer.toetsenpad.pkg
  --version <CFBundleShortVersionString> --install-location /`. Niet gesigneerd (kan niet zonder Developer ID Installer).
- **`gereedschap/pkg/postinstall`**: `killall -9 AudioComponentRegistrar` (en een regel in install.log als bewijs).
- **CI, job macos**: stap "Installatiepakket" direct na het ad-hoc signeren; artefact `macos-pkg`.
- **CI, nieuwe job `installatie`** (`needs: macos`, verse `macos-15`-runner): alleen checkout (voor het toetsscript) en
  het artefact, er wordt niets gebouwd. `gereedschap/toets-pkg.sh` toetst:
  vooraf schoon (geen bundels in `/Library` en `~/Library`, geen bon) → `pkgutil --check-signature` moet "no signature"
  zijn → PackageInfo: geen bundel onder `<relocate>`, postinstall aanwezig → installatie 1 ("install was successful") →
  installatie 2 ("upgrade was successful") → plek van alle drie en de bon (`pkgutil --files`) → `codesign --verify
  --strict --deep` en `Signature=adhoc` voor alle drie → postinstall twee keer in install.log → wachten tot `auval -a`
  de AU ziet (zonder eigen killall: de postinstall moet het doen) → `auval -strict -v aumu PTgg Vrve`.
- **LEERPAD.md**: installeren met het pakket (macOS 15: Gereed → Systeeminstellingen → Privacy en beveiliging → "Open
  toch"; rechtsklik → Open werkt niet meer), verwijderen, de handmatige weg als reserve, en notarisatie als optie:
  Apple Developer Program US$ 99/jaar (±€ 99), Developer ID Application + Installer, `notarytool` en `stapler`.

## Poorten (run 39167229773)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| pakket gebouwd, ongesigneerd | groen | macos: stap "Installatiepakket" geslaagd; installatie: `Status: no signature` |
| BundleIsRelocatable false | groen | PackageInfo: `relocatable="false"` en `<relocate/>` leeg; drie `<bundle … id="nl.varve.pluginbouwer.toetsenpad.{clap,auv2,vst3}">` |
| installeren op een schone runner | groen | `geen PB Toetsenpad op de runner, geen bon …`; `installer: The install was successful.` |
| tweede keer: upgrade | groen | `installer: Upgrading at base path /`, `installer: The upgrade was successful.` |
| plek van alle drie | groen | `plek goed: /Library/Audio/Plug-Ins/{Components,VST3,CLAP}/PB Toetsenpad.*` (root wheel), bon noemt alle drie |
| handtekening van alle drie | groen | `valid on disk`, `satisfies its Designated Requirement`, `Signature=adhoc` (×3) |
| postinstall | groen | install.log: `./postinstall: PB Toetsenpad postinstall: AudioComponentRegistrar herstart` (2×) |
| auval -strict na installatie | groen | `aumu PTgg Vrve  -  Varve: PB Toetsenpad`, `[0, 2]  [0, 1]`, vijf presets, `AU VALIDATION SUCCEEDED.` |
| alle poorten van fase 0–5 | groen | jobs linux, rtsan en macos ongewijzigd en geslaagd in dezelfde run |

## Pushes
Eén push met code (`79aef1b`), in één keer groen. Lokaal kon ik alleen `sh -n` en de YAML controleren (geen Mac);
de commando's (`pkgbuild --analyze`, de "upgrade"-melding van `installer`, `pkgutil --expand-full`) schreef ik uit de
documentatie en ze werkten de eerste keer.

## Onduidelijk of fout, en wat zwak is
- **Het lokaas tegen verplaatsen bewijst weinig.** Ik zette vóór de installatie een kopie van de AU in
  `/Users/Shared/pb-elders` en wilde die bij LaunchServices aanmelden, zodat Installer hem met verplaatsen aan zou zien
  voor de bestaande bundel. `lsregister -f` gaf `failed to scan …: -10811` (een AU is geen app); het lokaas was dus
  waarschijnlijk niet "bekend" en de controle "onaangeroerd" was vanzelf waar. Het echte bewijs is PackageInfo
  (`relocatable="false"`, lege `<relocate/>`). Een toets die faalt als BundleIsRelocatable true is, zou Spotlight
  (`mdimport`) of een eerdere installatie op een andere plek nodig hebben; dat heb ik niet gebouwd.
- **Toegevoegd of gezet?** Of `pkgbuild --analyze` de sleutel hier had, staat in het macos-log (stap
  "Installatiepakket"), dat de GitHub-tools niet meer tonen: die stap staat vooraan en valt buiten de laatste 5000 regels
  (valkuil 18). Het script werkt in beide gevallen.
- De prompt zegt "macOS 15"; Clay's MacBook kan ook macOS 26 hebben. De weg via Privacy en beveiliging → "Open toch"
  geldt daar ook, maar dat heb ik niet getoetst (CI kan geen Gatekeeper-dialoog tonen: `installer` op de opdrachtregel
  met sudo kijkt niet naar de quarantainevlag).
- Het pakket installeert voor alle gebruikers in `/Library`; de handmatige weg van LEERPAD zet de AU in `~/Library`. Wie
  beide doet heeft de AU twee keer; LEERPAD zegt nu "houd er één".
- Het bedrag van het Developer Program (US$ 99, ±€ 99) komt uit mijn kennis van Apple's prijs, niet uit een bron die ik in
  deze proef las; LEERPAD verwijst naar developer.apple.com/programs.
