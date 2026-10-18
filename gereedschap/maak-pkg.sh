#!/bin/sh
# maak-pkg.sh <map met de gesigneerde bundels> <uit.pkg> — het installatiepakket van fase 6 (pkgbuild).
# Zet "PB Toetsenpad".component/.vst3/.clap in /Library/Audio/Plug-Ins/{Components,VST3,CLAP}. De bundels moeten al
# ad-hoc gesigneerd zijn (codesign --sign -); pkgbuild kopieert ze ongewijzigd. Het .pkg zelf blijft ongesigneerd:
# pkgbuild --sign en productsign eisen een betaald Developer ID Installer-certificaat (LEERPAD.md).
# BundleIsRelocatable gaat op false voor elke bundel in de component-plist (pkgbuild --analyze; de sleutel ontbreekt
# soms en wordt dan toegevoegd), anders installeert Installer in een bestaande bundel met dezelfde id elders op de schijf.
set -e
BRON="$1"; UIT="$2"
ID=nl.varve.pluginbouwer.toetsenpad.pkg
HIER=$(cd "$(dirname "$0")" && pwd)
WERK=$(mktemp -d "${TMPDIR:-/tmp}/pb-pkg.XXXXXX")
ROOT="$WERK/root"; P="$ROOT/Library/Audio/Plug-Ins"
mkdir -p "$P/Components" "$P/VST3" "$P/CLAP" "$WERK/scripts"
# ditto bewaart symlinks, rechten en de handtekening
ditto "$BRON/PB Toetsenpad.component" "$P/Components/PB Toetsenpad.component"
ditto "$BRON/PB Toetsenpad.vst3" "$P/VST3/PB Toetsenpad.vst3"
ditto "$BRON/PB Toetsenpad.clap" "$P/CLAP/PB Toetsenpad.clap"
for b in "$P"/*/*; do codesign --verify --strict --verbose=1 "$b"; done
cp "$HIER/pkg/postinstall" "$WERK/scripts/postinstall"
chmod 755 "$WERK/scripts/postinstall"
VERSIE=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$P/Components/PB Toetsenpad.component/Contents/Info.plist" 2>/dev/null \
  || /usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$P/Components/PB Toetsenpad.component/Contents/Info.plist")

pkgbuild --analyze --root "$ROOT" "$WERK/component.plist"
# elke bundel (ook geneste, onder ChildBundles) krijgt BundleIsRelocatable = false; ontbreekt de sleutel, dan erbij
python3 -I - "$WERK/component.plist" <<'PY'
import plistlib, sys
pad = sys.argv[1]
with open(pad, 'rb') as f: lijst = plistlib.load(f)
def zet(b):
    actie = 'gezet' if 'BundleIsRelocatable' in b else 'toegevoegd'
    b['BundleIsRelocatable'] = False
    print('component-plist: %s BundleIsRelocatable = false (%s)' % (b.get('RootRelativeBundlePath'), actie))
    for k in b.get('ChildBundles', []): zet(k)
for b in lijst: zet(b)
with open(pad, 'wb') as f: plistlib.dump(lijst, f)
PY
plutil -p "$WERK/component.plist"

pkgbuild --root "$ROOT" --component-plist "$WERK/component.plist" --scripts "$WERK/scripts" \
  --identifier "$ID" --version "$VERSIE" --install-location / "$UIT"
echo "pakket: $UIT ($ID $VERSIE)"
pkgutil --payload-files "$UIT" | grep -E "\.(component|vst3|clap)$"
rm -rf "$WERK"
