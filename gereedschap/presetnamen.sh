#!/bin/sh
# presetnamen.sh <map> — de "naam" van elke presets/<sleutel>.json, één per regel, in de volgorde van de bestandsnamen
# (dezelfde volgorde als de tabel die CMake in het binary bakt). Voor de auval-toets in CI (fase 4).
set -e
node -e '
const fs = require("fs"), path = require("path");
const map = process.argv[1];
for (const f of fs.readdirSync(map).filter((f) => f.endsWith(".json")).sort())
  console.log(JSON.parse(fs.readFileSync(path.join(map, f), "utf8")).naam);
' "$1"
