#!/bin/sh
# pas-patches-toe.sh <map> — elke patch uit <map>, in naamvolgorde, op de huidige map (de bron van clap-wrapper).
# Een patch die er al op zit wordt overgeslagen, zodat een tweede configure niet faalt.
set -e
for p in "$1"/*.patch; do
  if git apply --reverse --check "$p" 2>/dev/null; then echo "patch zit er al op: $(basename "$p")"; continue; fi
  git apply "$p"
  echo "patch toegepast: $(basename "$p")"
done
