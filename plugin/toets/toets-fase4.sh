#!/bin/sh
# toets-fase4.sh — de eigen toets van fase 4 (in CI en lokaal): de fabriekspresets uit presets/*.json via CLAP
# preset-discovery en preset-load (pb-toets presets), na `cmake --build <build>` met PB_TOETSEN=ON.
#   sh plugin/toets/toets-fase4.sh <build-dir>
# De verwachting (bestand, naam, waarden) komt rechtstreeks uit presets/*.json (node), niet uit de plugin.
set -e
PAKKET="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
[ -n "$BUILD" ] || { echo "gebruik: toets-fase4.sh <build-dir>" >&2; exit 2; }
CLAPPAD="${CLAP:-$(ls -d "$BUILD"/plugins/*.clap 2>/dev/null | head -1)}"
[ -n "$CLAPPAD" ] || { echo "geen .clap gevonden (zet CLAP=…)" >&2; exit 2; }
som() { if [ -n "$GITHUB_STEP_SUMMARY" ]; then cat >> "$GITHUB_STEP_SUMMARY"; else cat > /dev/null; fi; }
UIT="$PAKKET/uit/presets"; mkdir -p "$UIT"
node -e '
const fs = require("fs"), map = process.argv[1];
for (const f of fs.readdirSync(map).filter((f) => f.endsWith(".json")).sort()) {
  const j = JSON.parse(fs.readFileSync(map + "/" + f, "utf8"));
  console.log([f, j.naam, ...Object.entries(j.waarden).map(([k, v]) => k + "=" + v)].join("\t"));
}' "$PAKKET/presets" > "$UIT/verwacht.tsv"
echo "### Fabriekspresets (CLAP preset-discovery en preset-load, $(uname -m))"
"$BUILD/pb-toets" presets "$CLAPPAD" "$UIT/verwacht.tsv"
echo "presets: $(wc -l < "$UIT/verwacht.tsv") fabriekspresets gevonden en geladen, bit voor bit gelijk aan dezelfde waarden via flush" | som
