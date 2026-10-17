# VERSLAG — Dub-echo

Wat vastliep, wat ik zag en wat ik probeerde (CLAUDE.md: "Loopt iets vast: beschrijf het hier").

## Fase 4: clap-validator 0.4.1 blijft hangen op een preset-container met meer dan één preset
**Wat ik zag.** De eerste opzet had één preset-provider met één locatie van soort `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN`
en alle vijf presets als container (elk met een load_key). Dat is geldige CLAP. Lokaal liepen `preset-discovery-crawl`
en `preset-discovery-load` van clap-validator 0.4.1 vast: "Timed out after 45 seconds".
`preset-discovery-descriptor-consistency` slaagde wel.

**Oorzaak (in clap-validator, niet in de plugin).** In `src/plugin/preset_discovery/metadata_receiver.rs` neemt
`begin_preset` de mutex `this.result` (`let result = this.result.lock().unwrap();`) en houdt hem vast tot het eind van
de functie. Heeft de preset een load_key, dan roept hij daarbinnen `this.flush_preset()` aan. Bij de tweede preset
staat er een vorige klaar, en `flush_preset` neemt dezelfde mutex nog eens. Een `std::sync::Mutex` is niet reentrant,
dus de thread blokkeert op zichzelf. Bij de eerste preset is er niets te flushen en keert `flush_preset` terug
vóór de lock. Daarom gaat één preset per container goed. Upstream master (okt 2026, commit b2f1d9b) heeft dezelfde code.

**Wat ik probeerde.**
- Een bouw die maar één preset meldt: de drie preset-tests van clap-validator slagen (PASSED). Dat bevestigt de oorzaak.
- De validator patchen wilde ik niet. De poort is "clap-validator 0.4.1 uit de bron", en een aangepaste validator
  toetst iets anders.

**Oplossing.** Elke fabriekspreset krijgt een eigen provider. Elke provider heeft één locatie in de plugin en één
preset (een container van één, met een load_key). Voor een host is dat even geldig: de AU-patch en een DAW lopen
gewoon alle providers af. De validator komt zo niet op het pad met de dubbele lock.

**Wat dit kost.** clap-validator houdt per locatie maar één resultaat bij (een map met de locatie als sleutel), en alle
locaties zijn "in de plugin". Daardoor laadt `preset-discovery-load` alleen de preset van de laatste provider; de crawl
ziet ze wel allemaal. Dat alle vijf presets laden en klinken zoals hun waarden, toetst `gereedschap/pb-preset-toets`
in CI.

## Fase 5: clap-wrapper v0.16.0 geeft kanaalindelingen niet door aan de AU
**Wat ik zag.** De AUv2-wrapper leidt SupportedNumChannels en ValidFormat alleen af uit `audio-ports` zoals die bij
PostConstructor zijn (stereo). `audio-ports-config` kent hij niet; de VST3-wrapper gebruikt `configurable-audio-ports`
(setBusArrangements), niet `audio-ports-config`.
**Wat ik deed.** De plugin biedt beide extensies aan (plus `audio-ports-config-info`). Voor de AU een vijfde patch,
`patches/clap-wrapper-auv2-toegestane-kanalen-uit-audio-ports-config.patch` (kop: herkomst en wat hij doet). Getoetst
door auval (kanaalindelingen) en `gereedschap/pb-au-render` (null-test door de AU in elke indeling).

## Fase 5: pluginval test lost relatieve paden op tegen de map van de config
De eerste CI-run (39046625176) faalde in de VST3-matrix: de verwijzing `uit-matrix/verwacht/...` in een config onder
`uit-matrix/vst3-linux/.../cfg/` werd `.../cfg/uit-matrix/verwacht/...`. Lokaal had ik een absoluut pad gebruikt.
Opgelost door de matrixmap in de scripts absoluut te maken.

## Fase 6: pkgbuild --analyze op macOS 15 schrijft geen BundleIsRelocatable
**Wat ik zag.** `PlistBuddy -c "Set :0:BundleIsRelocatable false"` faalde met `Entry ... Does Not Exist`: de
component-plist van `pkgbuild --analyze` (macos-15-runner) bevat die sleutel niet.
**Wat ik deed.** `plutil -replace "0.BundleIsRelocatable" -bool NO` zet hem ook als hij ontbreekt. Dat hij werkt,
toetst de CI-job `pakket` met een lokvogel: een kopie van de drie bundels in ~/Downloads blijft onaangeroerd en de
installer schrijft in /Library.

## Fase 6: een .pkg kan niet ad-hoc gesigneerd worden
`codesign --sign -` werkt op de bundels, maar `pkgbuild --sign`/`productsign` vragen een "Developer ID Installer"-
certificaat (betaald). Het pakket bevat dus ad-hoc gesigneerde bundels en is zelf ongesigneerd; LEERPAD.md legt uit hoe
Clay het opent ("Open toch") en wat notariseren kost.
