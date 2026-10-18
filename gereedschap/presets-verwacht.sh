#!/bin/sh
# presets-verwacht.sh — de fabriekspresets uit presets/*.json als tabel voor de toetsen (los van plugin/presets.cmake):
#   sh gereedschap/presets-verwacht.sh <map met presets> > verwacht.tsv
# Per regel: sleutel <tab> naam <tab> parameternaam <tab> waarde; een preset zonder waarden geeft één regel met lege
# parameternaam en waarde. Gebruikt jq.
set -eu
map="$1"
for f in "$map"/*.json; do
  k=$(basename "$f" .json)
  jq -r --arg k "$k" '. as $p
    | if (.waarden | length) == 0 then [$k, $p.naam, "", ""]
      else (.waarden | to_entries[] | [$k, $p.naam, .key, (.value | tostring)]) end
    | @tsv' "$f"
done
