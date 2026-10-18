#!/bin/sh
# installatietoets.sh <pkg> <naam> — fase 6, op een verse macOS-runner die niets bouwt: het installatiepakket
# installeren (twee keer: de tweede meldt "upgrade"), dan plek, eigenaar en handtekening van alle drie de bundels.
# auval -strict draait daarna als eigen stap in CI. Elke "FOUT" laat het script falen.
set -u
PKG=$1
NAAM=$2
ID=nl.varve.pluginbouwer.toetsenpad.pkg
L=/Library/Audio/Plug-Ins
fout=0
faal() { echo "installatietoets: FOUT, $*"; fout=1; }
bundels() { echo "Components/$NAAM.component"; echo "VST3/$NAAM.vst3"; echo "CLAP/$NAAM.clap"; }

echo "== verse runner: nog niets van $NAAM geïnstalleerd"
bundels | while IFS= read -r b; do
  for d in "$L" "$HOME$L"; do [ ! -e "$d/$b" ] || { echo "installatietoets: FOUT, $d/$b bestaat al"; exit 1; }; done
done || fout=1
pkgutil --pkg-info "$ID" >/dev/null 2>&1 && faal "pakket $ID staat al als geïnstalleerd"

echo "== het pakket"
pkgutil --check-signature "$PKG" || true   # verwacht: geen handtekening (ad-hoc kan niet voor een .pkg)
spctl --assess --type install -vv "$PKG" 2>&1 || true   # Gatekeeper weigert het: daarom de stappen in LEERPAD.md
X=$(mktemp -d)/uit
pkgutil --expand "$PKG" "$X"
INFO=$(find "$X" -name PackageInfo | head -1)
cat "$INFO"
# pkgbuild schrijft altijd <relocate/>; een relocatable bundel staat erin als <relocate><bundle id=…/></relocate>
voor_info=$fout
grep -q "<relocate/>" "$INFO" || faal "PackageInfo heeft geen lege <relocate/>"
sed -n '/<relocate>/,/<\/relocate>/p' "$INFO" | grep -q "<bundle" && faal "PackageInfo noemt een relocatable bundel (BundleIsRelocatable niet false)"
grep -q '<postinstall file=' "$INFO" || faal "PackageInfo noemt geen postinstall"
for b in "$NAAM.component" "$NAAM.vst3" "$NAAM.clap"; do
  grep -qF "Plug-Ins/" "$INFO" && grep -qF "$b\"" "$INFO" || faal "PackageInfo noemt de bundel $b niet"
done
[ "$fout" -eq "$voor_info" ] && echo "installatietoets: PackageInfo met een lege <relocate/> (geen bundel relocatable), met postinstall en alle drie de bundels"

echo "== eerste installatie"
sudo installer -pkg "$PKG" -target / 2>&1 | tee installatie-1.log
grep -q "The install was successful" installatie-1.log || faal "de eerste installatie meldt geen \"The install was successful\""
grep -qi "upgrad" installatie-1.log && faal "de eerste installatie meldt al een upgrade"

echo "== tweede installatie, met een kopie van de AU elders op de schijf en de AU in /Library zonder binary"
# Met BundleIsRelocatable = true zou Installer de kopie elders bijwerken en de bundel in /Library kapot laten.
ELDERS="$HOME/Elders/$NAAM.component"
mkdir -p "$HOME/Elders"
ditto "$L/Components/$NAAM.component" "$ELDERS"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$ELDERS" 2>&1 || true
voor=$(cd "$ELDERS" && find . -type f -exec shasum {} + | sort | shasum)
BIN=$(find "$L/Components/$NAAM.component/Contents/MacOS" -type f | head -1)
[ -n "$BIN" ] || faal "geen binary in de AU"
sudo rm -f "$BIN"
sudo installer -pkg "$PKG" -target / 2>&1 | tee installatie-2.log
grep -q "The upgrade was successful" installatie-2.log || faal "de tweede installatie meldt geen \"The upgrade was successful\""
[ -f "$BIN" ] && echo "installatietoets: de binary van de AU staat weer in /Library" || faal "de upgrade zette de AU niet terug in /Library (relocated?)"
na=$(cd "$ELDERS" && find . -type f -exec shasum {} + | sort | shasum)
[ "$voor" = "$na" ] && echo "installatietoets: de kopie elders is niet aangeraakt" || faal "de upgrade schreef in de kopie elders"
rm -rf "$HOME/Elders"

echo "== plek, eigenaar en handtekening"
pkgutil --pkg-info "$ID" || faal "pkgutil kent $ID niet"
pkgutil --files "$ID" > pakketbestanden.txt
for b in $(bundels | tr ' ' '#'); do
  b=$(echo "$b" | tr '#' ' ')
  p="$L/$b"
  if [ -d "$p" ]; then echo "installatietoets: $p staat er"; else faal "$p ontbreekt"; continue; fi
  [ ! -e "$HOME$L/$b" ] || faal "ook een kopie in $HOME$L/$b"
  grep -qxF "Library/Audio/Plug-Ins/$b" pakketbestanden.txt || faal "pkgutil --files noemt Library/Audio/Plug-Ins/$b niet"
  eigenaar=$(stat -f '%Su:%Sg' "$p")
  [ "$eigenaar" = "root:wheel" ] || [ "$eigenaar" = "root:admin" ] || faal "$p is van $eigenaar"
  codesign --verify --deep --strict --verbose=2 "$p" 2>&1 || faal "codesign --verify $p"
  codesign -dv "$p" > handtekening.txt 2>&1
  cat handtekening.txt
  grep -q "Signature=adhoc" handtekening.txt && echo "installatietoets: $b ad-hoc gesigneerd en geldig ($eigenaar)" || faal "$p is niet ad-hoc gesigneerd"
done

echo "== postinstall"
if sudo grep -F "PB Toetsenpad postinstall" /var/log/install.log; then
  n=$(sudo grep -cF "PB Toetsenpad postinstall" /var/log/install.log)
  echo "installatietoets: postinstall draaide $n keer"
  [ "$n" -ge 2 ] || faal "postinstall draaide niet bij beide installaties"
else
  faal "postinstall staat niet in /var/log/install.log"
fi

[ "$fout" -eq 0 ] || { echo "installatietoets: MISLUKT"; exit 1; }
echo "installatietoets: geslaagd (twee keer geïnstalleerd: install en upgrade; plek, eigenaar en ad-hoc-handtekening van AU, VST3 en CLAP; postinstall)"
