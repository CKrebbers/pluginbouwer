#!/bin/sh
# maak-pkg.sh <map met de ad-hoc gesigneerde bundels> <uit.pkg> <versie>
# Maakt het installatiepakket (fase 6): de .component, .vst3 en .clap naar /Library/Audio/Plug-Ins/{Components,VST3,CLAP}.
# Het .pkg zelf blijft ongesigneerd (pkgbuild --sign/productsign eisen een betaald Developer ID Installer-certificaat).
set -eu
BRON=$1
UIT=$2
VERSIE=$3
NAAM="PB Dub-echo"
WERK=$(mktemp -d)
WORTEL="$WERK/wortel"
mkdir -p "$WORTEL/Components" "$WORTEL/VST3" "$WORTEL/CLAP" "$WERK/scripts"

# ditto houdt rechten, symlinks en de handtekening (_CodeSignature) heel
ditto "$BRON/$NAAM.component" "$WORTEL/Components/$NAAM.component"
ditto "$BRON/$NAAM.vst3" "$WORTEL/VST3/$NAAM.vst3"
ditto "$BRON/$NAAM.clap" "$WORTEL/CLAP/$NAAM.clap"
for b in "$WORTEL"/*/"$NAAM".*; do
  codesign --verify --deep --strict "$b"
done

# component-plist: elke bundel BundleIsRelocatable = false, anders installeert Installer in een bestaande bundel met
# dezelfde identifier elders op de schijf. pkgbuild --analyze schrijft de sleutel niet altijd: toevoegen, niet zetten.
PLIST="$WERK/componenten.plist"
pkgbuild --analyze --root "$WORTEL" "$PLIST"
n=0
while /usr/libexec/PlistBuddy -c "Print :$n" "$PLIST" > /dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Delete :$n:BundleIsRelocatable" "$PLIST" > /dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :$n:BundleIsRelocatable bool false" "$PLIST"
  n=$((n + 1))
done
echo "component-plist ($n bundels):"
plutil -p "$PLIST"
if [ "$n" -lt 3 ]; then echo "FOUT: pkgbuild --analyze vond $n bundels, verwacht 3"; exit 1; fi

# postinstall: AudioComponentRegistrar opnieuw laten zoeken (valkuil 5), anders ziet Logic de AU niet meteen
cat > "$WERK/scripts/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod 755 "$WERK/scripts/postinstall"

pkgbuild --root "$WORTEL" --component-plist "$PLIST" --scripts "$WERK/scripts" \
  --identifier nl.varve.pluginbouwer.dub-echo.pkg --version "$VERSIE" \
  --install-location /Library/Audio/Plug-Ins --min-os-version 11.0 "$UIT"

# controle in het pakket zelf: geen enkele bundel mag in <relocate> staan
CONTROLE="$WERK/uitgepakt"
pkgutil --expand "$UIT" "$CONTROLE"
reloc=$(xmllint --xpath 'count(//relocate/bundle)' "$CONTROLE/PackageInfo")
echo "bundels in <relocate> van PackageInfo: $reloc"
test "$reloc" = 0
pkgutil --payload-files "$UIT" | grep -E '\.(component|vst3|clap)$'
rm -rf "$WERK"
echo "maak-pkg: $UIT"
