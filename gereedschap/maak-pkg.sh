#!/bin/sh
# Maakt het installatiepakket (.pkg) voor macOS met pkgbuild (fase 6).
#   sh gereedschap/maak-pkg.sh <map met de bundels> <versie> <uit.pkg>
# De bundels (.component, .vst3, .clap) moeten al ad-hoc gesigneerd zijn (codesign --sign -).
# Het pakket zet ze in /Library/Audio/Plug-Ins/{Components,VST3,CLAP} (voor alle gebruikers).
#
# Het .pkg zelf is niet gesigneerd: pkgbuild --sign en productsign eisen een "Developer ID Installer"-certificaat
# (betaald). Een ad-hoc handtekening bestaat niet voor een .pkg; ad-hoc gesigneerd zijn de bundels erin.
set -eu

bron="$1"; versie="$2"; uit="$3"
naam="PB Dub-echo"
id="nl.varve.pluginbouwer.dub-echo.pkg"

werk="$(mktemp -d)"
trap 'rm -rf "$werk"' EXIT
wortel="$werk/wortel"
mkdir -p "$wortel/Library/Audio/Plug-Ins/Components" "$wortel/Library/Audio/Plug-Ins/VST3" \
  "$wortel/Library/Audio/Plug-Ins/CLAP" "$werk/scripts"

# ditto houdt de bundels intact (symlinks, rechten, handtekening)
ditto "$bron/$naam.component" "$wortel/Library/Audio/Plug-Ins/Components/$naam.component"
ditto "$bron/$naam.vst3" "$wortel/Library/Audio/Plug-Ins/VST3/$naam.vst3"
ditto "$bron/$naam.clap" "$wortel/Library/Audio/Plug-Ins/CLAP/$naam.clap"
for b in "$wortel"/Library/Audio/Plug-Ins/*/"$naam".*; do
  codesign --verify --strict --verbose=1 "$b"
done

# Valkuil: pkgbuild maakt bundels standaard "relocatable". Staat er ergens anders op de schijf een bundel met
# dezelfde CFBundleIdentifier (bv. in de bouwmap), dan werkt Installer DIE bij in plaats van de plugin-map.
# Daarom een component-plist met BundleIsRelocatable = false voor elke bundel.
pkgbuild --analyze --root "$wortel" "$werk/componenten.plist"
plutil -p "$werk/componenten.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$werk/componenten.plist" > /dev/null 2>&1; do
  # niet elke entry heeft de sleutel al (CI: "Set: Entry … Does Not Exist"), dus weghalen en opnieuw toevoegen
  /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$werk/componenten.plist" > /dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$werk/componenten.plist"
  i=$((i + 1))
done
echo "pkgbuild --analyze: $i bundels, alle met BundleIsRelocatable = false"
plutil -p "$werk/componenten.plist"
# de AU moet er in elk geval bij zijn (een .clap wordt misschien niet als bundel herkend; dan is hij ook niet relocatable)
grep -q 'Components/PB Dub-echo.component' "$werk/componenten.plist" || { echo "FOUT: de AU staat niet in de component-plist"; exit 1; }

# preinstall: een oude versie eerst weg, zodat er geen verouderde bestanden (bv. presets) blijven staan.
cat > "$werk/scripts/preinstall" <<'EOF'
#!/bin/sh
for b in "Components/PB Dub-echo.component" "VST3/PB Dub-echo.vst3" "CLAP/PB Dub-echo.clap"; do
  rm -rf "$3/Library/Audio/Plug-Ins/$b"
done
exit 0
EOF
# postinstall: anders ziet Logic de AU pas na een herstart (CLAUDE.md valkuil 5).
cat > "$werk/scripts/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar > /dev/null 2>&1 || true
exit 0
EOF
chmod 755 "$werk/scripts/preinstall" "$werk/scripts/postinstall"

mkdir -p "$(dirname "$uit")"
pkgbuild --root "$wortel" --component-plist "$werk/componenten.plist" --scripts "$werk/scripts" \
  --identifier "$id" --version "$versie" --install-location / "$uit"

echo "pakket: $uit"
pkgutil --payload-files "$uit" | grep -E '\.(component|vst3|clap)$'
pkgutil --check-signature "$uit" || true
