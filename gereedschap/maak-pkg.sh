#!/bin/sh
# Fase 6: maakt het installatiepakket (.pkg, pkgbuild) met de ad-hoc gesigneerde AU, VST3 en CLAP.
# Gebruik: sh gereedschap/maak-pkg.sh <map met de drie bundels> <uit.pkg> <versie>
# Het .pkg zelf blijft ongesigneerd: pkgbuild --sign en productsign eisen een betaald Developer ID Installer-certificaat.
set -eu
BRON=$1; UIT=$2; VERSIE=$3
NAAM="PB Toetsenpad"
ID="nl.varve.pluginbouwer.toetsenpad.pkg"
WERK=$(mktemp -d)
trap 'rm -rf "$WERK"' EXIT

# de bundels moeten al ad-hoc gesigneerd zijn; een kapotte handtekening gaat het pakket niet in
for b in component vst3 clap; do
  codesign --verify --strict --verbose=2 "$BRON/$NAAM.$b"
done

# root: de plek op de schijf zoals Installer hem neerzet (install-location /)
R="$WERK/root"
mkdir -p "$R/Library/Audio/Plug-Ins/Components" "$R/Library/Audio/Plug-Ins/VST3" "$R/Library/Audio/Plug-Ins/CLAP"
ditto "$BRON/$NAAM.component" "$R/Library/Audio/Plug-Ins/Components/$NAAM.component"
ditto "$BRON/$NAAM.vst3" "$R/Library/Audio/Plug-Ins/VST3/$NAAM.vst3"
ditto "$BRON/$NAAM.clap" "$R/Library/Audio/Plug-Ins/CLAP/$NAAM.clap"

# postinstall (valkuil 5); killall faalt als de registrar niet draait, en een falend script laat de installatie falen
mkdir -p "$WERK/scripts"
cat > "$WERK/scripts/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod 755 "$WERK/scripts/postinstall"

# component-plist: BundleIsRelocatable false voor elke bundel (ook geneste), anders installeert Installer in een
# bestaande bundel met dezelfde identifier elders op de schijf. De sleutel ontbreekt soms: dan toevoegen.
# BundleIsVersionChecked false: elke CI-build heeft dezelfde CFBundleVersion; een herinstallatie moet altijd overschrijven.
pkgbuild --analyze --root "$R" "$WERK/component.plist"
echo "--- component-plist van pkgbuild --analyze"
plutil -convert xml1 -o - "$WERK/component.plist"
python3 -I - "$WERK/component.plist" "$NAAM" <<'EOF'
import plistlib, sys
pad, naam = sys.argv[1], sys.argv[2]
with open(pad, "rb") as f:
    lijst = plistlib.load(f)
def zet(d):
    d["BundleIsRelocatable"] = False
    d["BundleIsVersionChecked"] = False
    d.setdefault("BundleOverwriteAction", "upgrade")
    for k in d.get("ChildBundles", []):
        zet(k)
for d in lijst:
    zet(d)
# elke bundel moet erin staan; noemt --analyze er een niet, dan voegen we hem toe
gezien = {d["RootRelativeBundlePath"] for d in lijst}
for sub, ext in (("Components", "component"), ("VST3", "vst3"), ("CLAP", "clap")):
    p = f"Library/Audio/Plug-Ins/{sub}/{naam}.{ext}"
    if p not in gezien:
        print(f"toegevoegd aan de component-plist: {p}")
        lijst.append({"RootRelativeBundlePath": p, "BundleIsRelocatable": False,
                      "BundleIsVersionChecked": False, "BundleHasStrictIdentifier": True,
                      "BundleOverwriteAction": "upgrade"})
with open(pad, "wb") as f:
    plistlib.dump(lijst, f)
EOF
echo "--- component-plist na aanpassen"
plutil -convert xml1 -o - "$WERK/component.plist"
# controle: nergens meer een relocatable bundel
if plutil -convert xml1 -o - "$WERK/component.plist" | grep -A1 "<key>BundleIsRelocatable</key>" | grep -q "<true/>"; then
  echo "FOUT: nog een bundel met BundleIsRelocatable true"; exit 1
fi
[ "$(plutil -convert xml1 -o - "$WERK/component.plist" | grep -c "<key>BundleIsRelocatable</key>")" -ge 3 ] || {
  echo "FOUT: niet alle drie de bundels hebben BundleIsRelocatable"; exit 1; }

mkdir -p "$(dirname "$UIT")"
pkgbuild --root "$R" --component-plist "$WERK/component.plist" --scripts "$WERK/scripts" \
  --identifier "$ID" --version "$VERSIE" --install-location / --ownership recommended "$UIT"
echo "--- inhoud van $UIT"
pkgutil --payload-files "$UIT" | grep -E "\.(component|vst3|clap)$"
pkgutil --check-signature "$UIT" || true  # ongesigneerd, zoals bedoeld
