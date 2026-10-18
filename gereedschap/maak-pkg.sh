#!/bin/sh
# maak-pkg.sh <map-met-bundels> <naam> <versie> <uit.pkg> — fase 6: het installatiepakket (pkgbuild) met de ad-hoc
# gesigneerde AU, VST3 en CLAP. Ze komen in /Library/Audio/Plug-Ins/{Components,VST3,CLAP}. Elke bundel in de
# component-plist krijgt BundleIsRelocatable = false (de sleutel ontbreekt soms: weghalen en toevoegen, niet zetten),
# anders installeert Installer in een bestaande bundel met dezelfde identifier elders op de schijf. postinstall:
# gereedschap/pkg-scripts/postinstall. Het .pkg zelf blijft ongesigneerd (daarvoor is een betaald Developer ID
# Installer-certificaat nodig; LEERPAD.md zegt hoe Clay het opent).
set -eu
BRON=$1
NAAM=$2
VERSIE=$3
UIT=$4
HIER=$(cd "$(dirname "$0")" && pwd)
PB=/usr/libexec/PlistBuddy
WERK=$(mktemp -d)
ROOT="$WERK/root"
P="$ROOT/Library/Audio/Plug-Ins"
mkdir -p "$P/Components" "$P/VST3" "$P/CLAP" "$WERK/scripts"
ditto "$BRON/$NAAM.component" "$P/Components/$NAAM.component"
ditto "$BRON/$NAAM.vst3" "$P/VST3/$NAAM.vst3"
ditto "$BRON/$NAAM.clap" "$P/CLAP/$NAAM.clap"
for b in "$P/Components/$NAAM.component" "$P/VST3/$NAAM.vst3" "$P/CLAP/$NAAM.clap"; do
  codesign --verify --deep --strict "$b" || { echo "maak-pkg: $b is niet (geldig) gesigneerd"; exit 1; }
done
cp "$HIER/pkg-scripts/postinstall" "$WERK/scripts/postinstall"
chmod 755 "$WERK/scripts/postinstall"

PL="$WERK/component.plist"
pkgbuild --analyze --root "$ROOT" "$PL"

# $1 = het pad van een array in de plist ("" is de wortel); ook ChildBundles, in een subshell (eigen i)
vastzetten() {
  i=0
  while "$PB" -c "Print $1:$i:RootRelativeBundlePath" "$PL" >/dev/null 2>&1; do
    "$PB" -c "Delete $1:$i:BundleIsRelocatable" "$PL" >/dev/null 2>&1 || true
    "$PB" -c "Add $1:$i:BundleIsRelocatable bool false" "$PL"
    echo "maak-pkg: BundleIsRelocatable = false voor $("$PB" -c "Print $1:$i:RootRelativeBundlePath" "$PL")"
    if "$PB" -c "Print $1:$i:ChildBundles:0" "$PL" >/dev/null 2>&1; then (vastzetten "$1:$i:ChildBundles"); fi
    i=$((i + 1))
  done
}
vastzetten ""
aantal=$(grep -c "<key>RootRelativeBundlePath</key>" "$PL" || true)
vast=$(grep -A1 "<key>BundleIsRelocatable</key>" "$PL" | grep -c "<false/>" || true)
echo "maak-pkg: $aantal bundels in de component-plist, $vast met BundleIsRelocatable = false"
[ "$aantal" -ge 3 ] && [ "$aantal" -eq "$vast" ] || { cat "$PL"; echo "maak-pkg: niet elke bundel staat vast"; exit 1; }
if grep -A1 "<key>BundleIsRelocatable</key>" "$PL" | grep -q "<true/>"; then echo "maak-pkg: nog een relocatable bundel"; exit 1; fi

mkdir -p "$(dirname "$UIT")"
pkgbuild --root "$ROOT" --component-plist "$PL" --scripts "$WERK/scripts" \
  --identifier nl.varve.pluginbouwer.toetsenpad.pkg --version "$VERSIE" --install-location / "$UIT"
rm -rf "$WERK"
echo "maak-pkg: $UIT ($(du -h "$UIT" | cut -f1), versie $VERSIE, ongesigneerd)"
