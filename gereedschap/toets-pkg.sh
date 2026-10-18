#!/bin/sh
# toets-pkg.sh <PB Toetsenpad.pkg> — de toets van fase 6 op een verse macOS-runner die niets bouwt: installeren (twee
# keer: de tweede meldt "upgrade"), plek en handtekening van alle drie, dan auval -strict. De AU-registry moet de
# postinstall van het pakket zelf verversen: dit script draait geen killall.
set -e
PKG="$1"; ID=nl.varve.pluginbouwer.toetsenpad.pkg
P=/Library/Audio/Plug-Ins; N="PB Toetsenpad"
LSREG=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
W=$(mktemp -d "${TMPDIR:-/tmp}/pb-pkgtoets.XXXXXX")

echo "--- vooraf: de runner is schoon"
for x in "$P/Components/$N.component" "$P/VST3/$N.vst3" "$P/CLAP/$N.clap" \
         "$HOME$P/Components/$N.component" "$HOME$P/VST3/$N.vst3" "$HOME$P/CLAP/$N.clap"; do
  test ! -e "$x" || { echo "FOUT: $x bestaat al vóór de installatie"; exit 1; }
done
if pkgutil --pkg-info "$ID" >/dev/null 2>&1; then echo "FOUT: $ID staat al als geïnstalleerd"; exit 1; fi
echo "geen $N op de runner, geen bon van $ID"

echo "--- het pakket"
pkgutil --check-signature "$PKG" || true   # verwacht: "Status: no signature" (ongesigneerd, LEERPAD.md)
pkgutil --check-signature "$PKG" 2>&1 | grep -q "no signature" || { echo "FOUT: het pakket heeft onverwacht een handtekening"; exit 1; }
pkgutil --expand "$PKG" "$W/uitgepakt"
cat "$W/uitgepakt/PackageInfo"
# BundleIsRelocatable = false: geen enkele bundel onder <relocate>
if tr -d '\n' < "$W/uitgepakt/PackageInfo" | grep -Eq '<relocate>.*<bundle .*</relocate>'; then
  echo "FOUT: PackageInfo noemt een verplaatsbare bundel onder <relocate>"; exit 1
fi
grep -q 'postinstall' "$W/uitgepakt/PackageInfo" || { echo "FOUT: PackageInfo noemt geen postinstall"; exit 1; }
echo "PackageInfo: geen verplaatsbare bundels, postinstall aanwezig"

# Lokaas: dezelfde AU (zelfde bundel-id) elders op de schijf, bij LaunchServices bekend. Met BundleIsRelocatable = true
# zou Installer daarin installeren in plaats van in $P/Components.
pkgutil --expand-full "$PKG" "$W/vol"
ELDERS=/Users/Shared/pb-elders; rm -rf "$ELDERS"; mkdir -p "$ELDERS"
ditto "$W/vol/Payload/Library/Audio/Plug-Ins/Components/$N.component" "$ELDERS/$N.component"
touch "$ELDERS/$N.component/Contents/lokaas"
"$LSREG" -f "$ELDERS/$N.component" || true
echo "lokaas: $ELDERS/$N.component"

echo "--- installatie 1"
sudo installer -pkg "$PKG" -target / -verboseR 2>&1 | tee "$W/inst1.log"
grep -q "The install was successful" "$W/inst1.log" || { echo "FOUT: installatie 1 meldt geen 'install was successful'"; exit 1; }
echo "installatie 1: install"

echo "--- installatie 2 (over de eerste heen)"
sudo installer -pkg "$PKG" -target / -verboseR 2>&1 | tee "$W/inst2.log"
grep -q "The upgrade was successful" "$W/inst2.log" || { echo "FOUT: installatie 2 meldt geen 'upgrade was successful'"; exit 1; }
echo "installatie 2: upgrade"

echo "--- plek"
pkgutil --pkg-info "$ID"
pkgutil --files "$ID" | grep -E "\.(component|vst3|clap)$" | tee "$W/bon.txt"
for x in "Library/Audio/Plug-Ins/Components/$N.component" "Library/Audio/Plug-Ins/VST3/$N.vst3" \
         "Library/Audio/Plug-Ins/CLAP/$N.clap"; do
  test -f "/$x/Contents/Info.plist" || { echo "FOUT: /$x ontbreekt"; exit 1; }
  grep -qx "$x" "$W/bon.txt" || { echo "FOUT: de bon van $ID noemt $x niet"; exit 1; }
  ls -ld "/$x"
  echo "plek goed: /$x"
done
test ! -e "$P/Components/$N.component/Contents/lokaas" || { echo "FOUT: het lokaas staat in de geïnstalleerde AU"; exit 1; }
if [ -n "$(find "$ELDERS/$N.component" -newer "$ELDERS/$N.component/Contents/lokaas" -type f 2>/dev/null)" ]; then
  echo "FOUT: Installer schreef in het lokaas $ELDERS (verplaatst)"; exit 1
fi
echo "niet verplaatst: het lokaas in $ELDERS is onaangeroerd"
rm -rf "$ELDERS"; "$LSREG" -u "$ELDERS/$N.component" 2>/dev/null || true
for x in "$HOME$P/Components/$N.component" "$HOME$P/VST3/$N.vst3" "$HOME$P/CLAP/$N.clap"; do
  test ! -e "$x" || { echo "FOUT: $x bestaat (verkeerde plek)"; exit 1; }
done

echo "--- handtekening"
for b in "$P/Components/$N.component" "$P/VST3/$N.vst3" "$P/CLAP/$N.clap"; do
  codesign --verify --strict --deep --verbose=2 "$b"
  codesign -dv "$b" 2>&1 | tee "$W/sig.txt"
  grep -q "Signature=adhoc" "$W/sig.txt" || { echo "FOUT: $b is niet ad-hoc gesigneerd"; exit 1; }
  echo "handtekening goed (ad-hoc, geldig): $b"
done

echo "--- postinstall"
sudo grep "PB Toetsenpad postinstall" /var/log/install.log | tail -n 4 || true
n=$(sudo grep -c "PB Toetsenpad postinstall" /var/log/install.log || true)
test "${n:-0}" -ge 2 || { echo "FOUT: install.log toont de postinstall geen twee keer ($n)"; exit 1; }
echo "postinstall draaide $n keer"

echo "--- auval -strict"
i=0
until auval -a 2>/dev/null | grep -q "aumu PTgg Vrve"; do
  i=$((i + 1)); test $i -lt 30 || { echo "FOUT: auval -a vindt aumu PTgg Vrve niet na de postinstall"; exit 1; }
  sleep 1
done
auval -a | grep "PTgg"
auval -strict -v aumu PTgg Vrve 2>&1 | tee "$W/auval.log"
grep -q "AU VALIDATION SUCCEEDED" "$W/auval.log"
echo "auval -strict na installatie uit het pakket: AU VALIDATION SUCCEEDED"
rm -rf "$W"
