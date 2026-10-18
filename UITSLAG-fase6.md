Stopcriterium gehaald.

# Uitslag fase 6 — Toetsenpad (proef g1)

Laatste CI-run met code: https://github.com/CKrebbers/pluginbouwer/actions/runs/39166873002 (commit `5d48b8b`). De
jobs linux, rtsan, macos en installatie zijn alle vier success. De commit met dit verslag verandert alleen docs
(dit bestand en TASKS.md).

## Wat ik deed
- **`gereedschap/maak-pkg.sh`** (draait in job macos, na "Ad-hoc signeren"):
  - zet de ad-hoc gesigneerde bundels in een root `Library/Audio/Plug-Ins/{Components,VST3,CLAP}` en controleert
    eerst de handtekening van elke bundel;
  - maakt de component-plist met `pkgbuild --analyze`. Voor elke bundel (ook ChildBundles, als die er zijn) doet hij
    `Delete` en dan `Add BundleIsRelocatable bool false` met PlistBuddy. Zo werkt het ook als de sleutel ontbreekt.
    Hij faalt als niet elke bundel op false staat;
  - draait `pkgbuild --root … --component-plist … --scripts … --identifier nl.varve.pluginbouwer.toetsenpad.pkg
    --version <uit CMake> --install-location /`. Het `.pkg` blijft ongesigneerd;
  - het artefact heet `macos-installatiepakket` en bevat `PB-Toetsenpad-0.1.0.pkg`.
- **`gereedschap/pkg-scripts/postinstall`**: `killall -9 AudioComponentRegistrar || true; exit 0`. Zonder `|| true`
  zou de installatie mislukken als de registrar niet draait (killall geeft dan 1).
- **Job `installatie`** (`needs: macos`, `runs-on: macos-15`): bouwt niets. Hij doet checkout (alleen voor de
  hulpscripts), node, en download-artifact. Daarna draait **`gereedschap/installatietoets.sh`**:
  - een verse runner: geen van de drie bundels staat in `/Library` of `~/Library`, en pkgutil kent het pakket niet;
  - het pakket: `pkgutil --check-signature` en `spctl` (alleen ter info), en PackageInfo uit `pkgutil --expand`.
    `<relocate>` noemt geen enkele bundel; er is een postinstall; alle drie de bundels staan erin;
  - de eerste installatie (`sudo installer -pkg … -target /`) moet "The install was successful" melden, zonder upgrade;
  - de tweede installatie moet "The upgrade was successful" melden. Vooraf haal ik de binary van de AU in `/Library`
    weg en zet ik een kopie van de AU in `~/Elders` (`lsregister -f`). Na de upgrade moet de binary terug zijn in
    `/Library` en moet de kopie onaangeroerd zijn (shasum van alle bestanden);
  - plek: alle drie staan in `/Library/Audio/Plug-Ins/…`, van `root:wheel`, in `pkgutil --files`, en niet in
    `~/Library`;
  - handtekening: `codesign --verify --deep --strict` en `Signature=adhoc`;
  - postinstall: twee regels in `/var/log/install.log`;
  - daarna `auval -strict -v aumu PTgg Vrve` op de geïnstalleerde AU, met dezelfde preset- en kanaaltoets als in macos.
- **LEERPAD.md**: installeren met het `.pkg` op macOS 15. Je dubbelklikt, klikt op "Gereed", gaat naar
  Systeeminstellingen → Privacy en beveiliging → "Open toch" en geeft je wachtwoord. Rechtsklik → Open werkt daar niet
  meer. Daarnaast: oude kopieën in `~/Library` eerst weggooien, de weg via de Terminal (`sudo installer`),
  verwijderen (`rm` + `pkgutil --forget`), en de oude handmatige weg. **Notariseren** staat er alleen als optie in, met
  de kosten (Apple Developer Program, US$ 99 / € 99 per jaar), de twee Developer ID-certificaten en
  `notarytool`/`stapler`. Het pakket doet het niet. In de kostentabel staat het bedrag nu ingevuld.
- `.github/workflows/ci.yml`: een kopcommentaar voor fase 6, de stap "Installatiepakket maken" en de upload in macos,
  en de nieuwe job installatie.

## Poorten (run 39166873002)
| poort | uitslag | bewijs uit het log |
|---|---|---|
| linux: clap-validator, pluginval VST3, null-tests, state-, paneel-, preset-, formaattoets | groen | job linux success, alle stappen success |
| rtsan: plugin schoon, de kanarie gevangen | groen | job rtsan success |
| macos: signeren, auval, pluginval AU (rtcheck, repeat 3) en VST3, alle null-tests, toetsen, pb-scherm | groen | job macos success; samenvatting: `pb-presettoets: 63 toetsen, 63 ok, 0 fout`, `pb-formaattoets: 147 toetsen, 147 ok, 0 fout`, bus-indelingen mono en stereo, AU 0 -> 2 en 0 -> 1 |
| fase 6: het pakket wordt gemaakt | groen | stap "Installatiepakket maken" success; artefact `macos-installatiepakket` (810305 bytes) |
| fase 6: installeren op een schone runner (macOS 15.7.9, image macos-15-arm64), twee keer | groen | `installer: Installing at base path /`, `installer: The install was successful.`, daarna `installer: Upgrading at base path /`, `installer: The upgrade was successful.` |
| fase 6: BundleIsRelocatable false | groen | PackageInfo: `relocatable="false"` en een lege `<relocate/>`; `de binary van de AU staat weer in /Library`, `de kopie elders is niet aangeraakt` |
| fase 6: plek en handtekening van alle drie | groen | per bundel `/Library/Audio/Plug-Ins/{Components,VST3,CLAP}/PB Toetsenpad.* staat er`, `valid on disk`, `satisfies its Designated Requirement`, `Signature=adhoc`, `(root:wheel)` |
| fase 6: postinstall | groen | twee keer `package_script_service[…]: ./postinstall: PB Toetsenpad postinstall: killall -9 AudioComponentRegistrar`; `postinstall draaide 2 keer` |
| fase 6: auval -strict na installatie | groen | `AU VALIDATION SUCCEEDED.`, de presets Donker, Helder, Kort en droog en Lange staart, `[0, 2]  [0, 1]` |
| fase 6: het .pkg is ongesigneerd (zo bedoeld) | info | `Status: no signature`; `spctl`: `rejected`, `source=no usable signature` (daarom de stappen in LEERPAD.md) |

## Pushes tot groen: 3
1. `c553358` (fase 6) gaf run 39164888006. linux, rtsan en macos werden groen, en het pakket installeerde al goed
   (install, upgrade, plek, handtekening, postinstall). installatie faalde toch op mijn eigen toets
   "PackageInfo heeft een `<relocate>`". Mijn aanname was fout: pkgbuild schrijft **altijd** een element
   `<relocate/>`, ook als er niets relocatable is. Een relocatable bundel staat erin als
   `<relocate><bundle id=…/></relocate>`.
2. `37e0b84` (alleen LEERPAD.md) gaf run 39164982816. Die faalde om dezelfde reden.
3. `5d48b8b` toetst op een `<bundle` binnen `<relocate>…</relocate>` en eist de lege `<relocate/>`. Die gaf run
   39166873002, helemaal groen.

## Wat onduidelijk of zwak is
- **De relocation-val is waarschijnlijk niet scherp.** `lsregister -f` op de kopie in `~/Elders` gaf `failed to scan
  …: -10811`. Mogelijk wist LaunchServices of Spotlight dus niet van de kopie. Dan zou Installer hem ook met
  `BundleIsRelocatable = true` niet gevonden hebben. Het harde bewijs is daarom PackageInfo: geen enkele bundel onder
  `<relocate>`. Dat zegt Installer zelf, en het volgt rechtstreeks uit de component-plist. Een scherpere toets kan
  een tegenproef zijn: een tweede pakket met `true`, dat de kopie wél moet raken. Die heb ik niet gebouwd.
- Cosmetisch: `cat PackageInfo` eindigt zonder regeleinde, dus de regel "installatietoets: PackageInfo met een lege
  `<relocate/>` …" plakt in het log achter `</pkg-info>`. Om dezelfde reden staat "from spotlight" (stderr van
  lsregister) vóór de eerste regel van installer. Aan de uitslag verandert dat niets.
- PROMPTS.md noemt de lege `<relocate/>` niet. Wie de toets zo bouwt als ik eerst deed (geen `<relocate` in
  PackageInfo), krijgt een rode run, terwijl het pakket goed is.
- Het bedrag voor het Apple Developer Program (US$ 99 / € 99 per jaar) komt van openbare bronnen, niet van een
  bevestiging op developer.apple.com. LEERPAD.md zegt dat Clay het daar zelf moet nakijken.
- Wat de toets niet ziet: de dialoog van Gatekeeper bij een dubbelklik (de CI-download heeft geen quarantainevlag).
  De stappen in LEERPAD.md voor "Open toch" zijn dus niet in CI getoetst; dat doet Clay in fase 7.
