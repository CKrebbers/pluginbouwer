#!/bin/sh
# Fase 6: maakt een installatiepakket (.pkg) met de AU, VST3 en CLAP van Dub-echo.
# Gebruik: sh gereedschap/maak-pkg.sh <map met de drie bundels> <versie> <uit.pkg>
# De bundels moeten al ad-hoc gesigneerd zijn (codesign --sign -); dit script controleert dat.
# Het pakket zelf is niet gesigneerd: pkgbuild/productsign kennen geen ad-hoc-handtekening, daarvoor is een
# "Developer ID Installer"-certificaat nodig (betaald; zie LEERPAD.md).
set -eu

bron=$1
versie=$2
uit=$3
id=nl.varve.pluginbouwer.dub-echo.pkg
naam=PB-Dub-echo

werk=$(mktemp -d)
trap 'rm -rf "$werk"' EXIT
wortel=$werk/wortel
P=$wortel/Library/Audio/Plug-Ins
mkdir -p "$P/Components" "$P/VST3" "$P/CLAP" "$werk/scripts"

for paar in "component:Components" "vst3:VST3" "clap:CLAP"; do
  ext=${paar%%:*}
  map=${paar#*:}
  b=$bron/$naam.$ext
  codesign --verify --strict --verbose "$b"
  # ditto behoudt de handtekening en extended attributes van de bundel
  ditto "$b" "$P/$map/$naam.$ext"
  echo "$map/$naam.$ext: $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$b/Contents/Info.plist")"
done

# Na de installatie moet de AU-registrar de nieuwe component zien (CLAUDE.md, valkuil 5).
cat > "$werk/scripts/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod 755 "$werk/scripts/postinstall"

# Componentenlijst: niet verplaatsbaar. Anders "verhuist" installer de update naar een oudere kopie met dezelfde
# bundel-id (bv. de build-map of ~/Library/...), en komt er niets in /Library/Audio/Plug-Ins.
pkgbuild --analyze --root "$wortel" "$werk/componenten.plist"
PB=/usr/libexec/PlistBuddy
i=0
while $PB -c "Print :$i:RootRelativeBundlePath" "$werk/componenten.plist" >/dev/null 2>&1; do
  $PB -c "Set :$i:BundleIsRelocatable false" "$werk/componenten.plist"
  $PB -c "Delete :$i:BundleOverwriteAction" "$werk/componenten.plist" 2>/dev/null || true
  $PB -c "Add :$i:BundleOverwriteAction string upgrade" "$werk/componenten.plist"
  i=$((i + 1))
done
test "$i" -ge 3
plutil -p "$werk/componenten.plist"

pkgbuild \
  --root "$wortel" \
  --component-plist "$werk/componenten.plist" \
  --scripts "$werk/scripts" \
  --identifier "$id" \
  --version "$versie" \
  --install-location / \
  --ownership recommended \
  "$uit"

pkgutil --payload-files "$uit"
pkgutil --check-signature "$uit" || true
ls -la "$uit"
