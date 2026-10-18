#!/bin/sh
# cv-eis.sh — eist dat genoemde toetsen van clap-validator PASSED zijn (SKIPPED of FAILED telt niet).
#   sh gereedschap/cv-eis.sh <clap-validator.log zonder ANSI> <toets> [<toets> ...]
# De uitvoer van clap-validator 0.4.1 heeft per toets een regel "┌─ <naam>" en verderop "└── PASSED|FAILED|SKIPPED".
LOG="$1"; shift
fout=0
for t in "$@"; do
  u=$(awk -v t="$t" '
    index($0, "┌─ " t) && substr($0, index($0, "┌─ " t) + length("┌─ " t)) ~ /^[ \t]*$/ { zoek = 1; next }
    zoek && /└──/ { sub(/.*└── /, ""); split($0, a, " "); print a[1]; exit }' "$LOG")
  echo "clap-validator $t: ${u:-(niet gevonden)}"
  [ "$u" = PASSED ] || fout=1
done
exit $fout
