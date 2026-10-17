#!/bin/sh
# toets-fase5.sh — de eigen toets van fase 5 (in CI en lokaal): kanaalindelingen (audio-ports-config,
# configurable-audio-ports), mono en mono naar stereo bit voor bit als stereo, in-place, en de bypass (pb-toets formaten),
# na `cmake --build <build>` met PB_TOETSEN=ON. De null-test over de samplerates staat in gereedschap/ci-null.sh.
#   sh plugin/toets/toets-fase5.sh <build-dir>
set -e
BUILD="$1"
[ -n "$BUILD" ] || { echo "gebruik: toets-fase5.sh <build-dir>" >&2; exit 2; }
CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
[ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }
echo "### Formaten (kanaalindelingen, in-place, bypass, $(uname -m))"
"$BUILD/pb-toets" formaten "$CLAPPAD"
echo "formaten: stereo, mono en mono naar stereo (ook in-place) bit voor bit; bypass precies de invoer, met overvloeier" | som
