#!/bin/sh
# maak-pkg.sh <plugins-map> <uit.pkg> — installatiepakket (pkgbuild) met de AU, VST3 en CLAP.
# De bundels moeten al ad-hoc gesigneerd zijn (codesign --sign -); dit script controleert dat en zet ze dan in
#   /Library/Audio/Plug-Ins/Components   (AU)
#   /Library/Audio/Plug-Ins/VST3         (VST3)
#   /Library/Audio/Plug-Ins/CLAP         (CLAP)
# Het .pkg zelf is niet gesigneerd: een pakket kan niet ad-hoc gesigneerd worden (productsign en pkgbuild --sign
# willen een "Developer ID Installer"-certificaat). Zie LEERPAD.md voor het openen en voor notarisatie.
set -eu
plugins="$1"
uit="$2"
naam="PB Dub-echo"
id="nl.varve.pluginbouwer.dub-echo.pkg"
hier="$(cd "$(dirname "$0")" && pwd)"

versie="$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$plugins/$naam.component/Contents/Info.plist")"
werk="$(mktemp -d)"
trap 'rm -rf "$werk"' EXIT
root="$werk/root"
mkdir -p "$root/Library/Audio/Plug-Ins/Components" "$root/Library/Audio/Plug-Ins/VST3" "$root/Library/Audio/Plug-Ins/CLAP"

for paar in "component:Components" "vst3:VST3" "clap:CLAP"; do
  ext="${paar%%:*}"; map="${paar#*:}"
  b="$plugins/$naam.$ext"
  codesign --verify --strict --verbose "$b"
  # ditto behoudt de handtekening, de rechten en de symlinks in de bundel
  ditto "$b" "$root/Library/Audio/Plug-Ins/$map/$naam.$ext"
done
# geen quarantaine of andere xattrs in de payload
xattr -cr "$root"

# Valkuil: pkgbuild maakt bundels standaard "relocatable". Vindt de installer dezelfde bundel-id ergens anders
# (bv. een uitgepakte zip in Downloads), dan schrijft hij dáár over in plaats van in /Library. Dus uitzetten.
pkgbuild --analyze --root "$root" "$werk/component.plist"
plutil -p "$werk/component.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i:RootRelativeBundlePath" "$werk/component.plist" >/dev/null 2>&1; do
  # plutil -replace zet de sleutel ook als pkgbuild --analyze hem weglaat (macOS 15 laat BundleIsRelocatable weg)
  plutil -replace "$i.BundleIsRelocatable" -bool NO "$werk/component.plist"
  plutil -replace "$i.BundleIsVersionChecked" -bool NO "$werk/component.plist"
  plutil -replace "$i.BundleOverwriteAction" -string upgrade "$werk/component.plist"
  i=$((i + 1))
done
echo "bundels in het pakket: $i (alle niet-relocatable)"
[ "$i" -ge 3 ] || { echo "FOUT: pkgbuild --analyze vond $i bundels, verwacht 3"; exit 1; }
plutil -p "$werk/component.plist"

mkdir -p "$(dirname "$uit")"
pkgbuild --root "$root" --component-plist "$werk/component.plist" --scripts "$hier/pkg-scripts" \
  --identifier "$id" --version "$versie" --install-location / "$uit"
echo "pakket: $uit (versie $versie)"
pkgutil --payload-files "$uit"
