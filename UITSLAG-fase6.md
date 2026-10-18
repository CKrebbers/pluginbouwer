Stopcriterium gehaald.

# Uitslag fase 6 — signeren en installeren (proef F4, tak `proef/f4`)

Laatste CI-run: https://github.com/CKrebbers/pluginbouwer/actions/runs/39165156943 (commit 7d8bef1, alle vier de jobs
groen). Run https://github.com/CKrebbers/pluginbouwer/actions/runs/39165068905 (commit fd245ef, de commit met de
code) was ook al groen in alle vier de jobs. Het bewijs hieronder komt uit die run. De commit met dit verslag start
nog een run; die verandert geen code.

## Wat ik deed
- **`gereedschap/maak-pkg.sh`** (draait in de macos-job direct na "Ad-hoc signeren"): `ditto` de drie gesigneerde
  bundels in een wortel `Components/`, `VST3/`, `CLAP/`, controleert hun handtekening, `pkgbuild --analyze` geeft de
  component-plist en per bundel gaat `BundleIsRelocatable` eruit en komt hij er als `false` weer in (PlistBuddy
  Delete + Add: toevoegen, niet zetten). Het script eist 3 bundels in de plist. postinstall:
  `killall -9 AudioComponentRegistrar` (faalt nooit). `pkgbuild --install-location /Library/Audio/Plug-Ins`,
  identifier `nl.varve.pluginbouwer.dub-echo.pkg`, versie uit `project()` (0.1.0), `--min-os-version 11.0`. Daarna
  pakt het script het `.pkg` uit en eist dat `count(//relocate/bundle)` in `PackageInfo` 0 is. Het `.pkg` blijft
  ongesigneerd. Het wordt als artefact `macos-installatiepakket` geüpload.
- **Job `installeren`** (macos-15, `needs: macos`; geen checkout en geen bouwstap, alleen download-artifact):
  1. het pakket bekijken: `pkgutil --check-signature` ("no signature"), 0 bundels in relocate, de runner heeft nog
     niets van ons in `/Library` of `~/Library`;
  2. een lokbundel (dezelfde .component, dezelfde identifier) in `/Users/Shared/elders`;
  3. `sudo installer` twee keer: de eerste moet "The install was successful" zonder "upgrad" geven, de tweede
     "The upgrade was successful";
  4. plek, eigenaar en handtekening van alle drie (`codesign --verify --deep --strict` en `Signature=adhoc`), de
     lokbundel onaangeroerd (sha256 en markeerbestand), niets in `~/Library`, `pkgutil --pkg-info`/`--files`;
  5. postinstall in `/var/log/install.log`, `auval -a` ziet `aufx PDca Vrve`;
  6. `auval -strict -v aufx PDca Vrve`.
- **Docs**: LEERPAD.md (installeren met het pakket: macOS 15 → Gereed → Systeeminstellingen → Privacy en beveiliging
  → "Open toch"; rechtsklik → Open werkt daar niet meer; alternatief `sudo installer` in Terminal; verwijderen;
  notarisatie als optie met de kosten en de stappen), ARCHITECTUUR.md (sectie "Installatiepakket"), TASKS.md.

## Poorten (uit het log van run 39165068905)
| poort | uitslag | bewijs |
|---|---|---|
| pakket gemaakt, BundleIsRelocatable false | groen | `component-plist (3 bundels):`, 3 × `"BundleIsRelocatable" => 0`, `pkgbuild: Wrote package to …/PB-Dub-echo.pkg`, `bundels in <relocate> van PackageInfo: 0` |
| pakket ongesigneerd, schone runner | groen | `Status: no signature`, `bundels in relocate: 0`, `runner schoon` |
| eerste installatie | groen | `installer: Installing at base path /`, `installer: The install was successful.` |
| tweede installatie meldt upgrade | groen | `installer: Upgrading at base path /`, `installer: The upgrade was successful.` |
| plek en handtekening van alle drie | groen | voor `/Library/Audio/Plug-Ins/Components/PB Dub-echo.component`, `…/VST3/PB Dub-echo.vst3`, `…/CLAP/PB Dub-echo.clap`: `valid on disk`, `satisfies its Designated Requirement`, `Signature=adhoc`, `eigenaar: root:wheel drwxr-xr-x`; `lokbundel elders onaangeroerd`; `location: Library/Audio/Plug-Ins` |
| postinstall | groen | install.log: `Executing script "postinstall" in …/nl.varve.pluginbouwer.dub-echo.pkg…` (twee keer); `auval -a`: `aufx PDca Vrve  -  Varve: PB Dub-echo` |
| auval -strict na installatie | groen | `AU VALIDATION SUCCEEDED.`, exitcode 0; `Tail Time … PASS`; `[2, 2]  [1, 1]  [1, 2]`; 5 factory presets |
| eerdere poorten (fase 0–5) | groen | Linux: alle clap-validator-eisen, pluginval VST3 `SUCCESS`, matrices; RTSan/TSan/kanarie; macOS: auval, pluginval AU `--repeat 3` en VST3 `SUCCESS`, null-tests en matrices `bit voor bit: 183 / 90 / 24 / 24 renders; … (geen)`, state 134/134, preset 46/46, AU-preset 18/18, paneel 35/35, gui 52/52 |

## Pushes
Twee pushes, CI groen bij de eerste: fd245ef (script, CI-job) en 7d8bef1 (alleen docs). Geen herstel nodig.

## Wat zwak of onduidelijk is
- **De lokbundel bewijst weinig.** `lsregister -f` op de .component gaf `failed to scan …: -10811` (LaunchServices
  registreert geen plugin-bundels), dus het is niet zeker dat Installer de lokbundel had gevonden als
  `BundleIsRelocatable` wél true was geweest. Ik heb geen tegenproef gedaan (een pakket met true). Wat vaststaat is
  het pakket zelf: `PackageInfo` heeft geen `<relocate>`-bundel; dat is wat Installer gebruikt om te verplaatsen.
- Ik weet niet of `pkgbuild --analyze` de sleutel deze keer zelf al schreef; het script verwijdert en voegt toe,
  dus het maakt niet uit.
- Notarisatiekosten: € 99 per jaar (US$ 99) uit Nederlandse bronnen (iCulture); niet op Apple's eigen prijspagina
  bevestigd, dat staat zo ook in LEERPAD.md.
- De stappen in Privacy en beveiliging op macOS 15 heb ik niet zelf kunnen zien (geen scherm op de runner); ze
  volgen de prompt en Apple's beschrijving. Clay moet ze in fase 7 bevestigen.
- auval geeft de bekende waarschuwing `AU implements MusicDeviceMIDIEvent but is of type 'aufx'` (clap-wrapper, al
  zo sinds eerdere fasen); geen fout.
