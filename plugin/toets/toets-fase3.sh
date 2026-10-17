#!/bin/sh
# toets-fase3.sh — de eigen toets van fase 3 (frontpaneel), na `cmake --build <build>` met PB_TOETSEN=ON.
#   sh plugin/toets/toets-fase3.sh <build-dir> <uitmap>        (Linux: met PB_VOOR="xvfb-run -a")
# pb-paneel opent het paneel in een echt venster (Cocoa of X11), bedient het met de muis en toetst wat de plugin
# aan de host meldt: eerst met flush op de mainthread (zoals een host na request_flush), dan met process op een
# eigen audiothread (zoals een DAW die speelt). De schermafbeeldingen komen in <uitmap> en <uitmap>/audio.
set -e
PAKKET="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
UIT="${2:-$PAKKET/uit/paneel}"
[ -n "$BUILD" ] || { echo "gebruik: toets-fase3.sh <build-dir> [uitmap]" >&2; exit 2; }
CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
[ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }
mkdir -p "$UIT" "$UIT/audio"

echo "### Frontpaneel ($(uname -m))"
$PB_VOOR "$BUILD/pb-paneel" "$CLAPPAD" "$UIT"
$PB_VOOR "$BUILD/pb-paneel" "$CLAPPAD" "$UIT/audio" --audio
echo "paneel: slepen, Shift + slepen, dubbelklik en host-waarden geslaagd, met flush en met een audiothread ($(uname -m))" | tee /dev/stderr | som
