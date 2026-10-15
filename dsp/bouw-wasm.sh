#!/bin/sh
# Bouwt dsp/<naam>.wasm uit dsp/pb_wasm.cpp met clang --target=wasm32, zonder libc (de DSP is vrijstaand).
# Gebruik: sh dsp/bouw-wasm.sh [ontwerp]   (ontwerp = bestandsnaam in dsp/ontwerpen/ zonder .h; standaard pijplijn_biquad)
set -e
cd "$(dirname "$0")"
ONTWERP="${1:-pijplijn_biquad}"
CLANG="${CLANG:-clang}"
mkdir -p wasm wasm/motor
if [ "$ONTWERP" = graaf ]; then
  # de ketenmotor voor de tool (dsp/pb_graaf_wasm.cpp); in een eigen map, want hij heeft een andere C-ABI (pg_*)
  "$CLANG" --target=wasm32 -std=c++17 -O2 -nostdlib -fno-exceptions -fno-rtti -fno-builtin-memset \
    -ffp-contract=off -fno-fast-math -fno-math-errno \
    -Wl,--no-entry -Wl,--export-dynamic -Wl,--export=__heap_base -Wl,-z,stack-size=1048576 \
    -o wasm/motor/graaf.wasm pb_graaf_wasm.cpp
  echo "dsp/wasm/motor/graaf.wasm ($(wc -c < wasm/motor/graaf.wasm) bytes)"
  exit 0
fi
# Let op: pb_wasm.cpp mag GEEN globale constructors hebben. Zonder libc ("command"-model) zet wasm-ld
# __wasm_call_ctors vóór elke geëxporteerde functie: elke aanroep zette dan alle instanties terug op hun
# standaardwaarden (gevonden 15 okt). tests/standaard.test.mjs leest een gezette waarde terug.
"$CLANG" --target=wasm32 -std=c++17 -O2 -nostdlib -fno-exceptions -fno-rtti -fno-builtin-memset \
  -ffp-contract=off -fno-fast-math -fno-math-errno \
  -DPB_ONTWERP="\"ontwerpen/$ONTWERP.h\"" \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--export=__heap_base -Wl,-z,stack-size=1048576 \
  -o "wasm/$ONTWERP.wasm" pb_wasm.cpp
echo "dsp/wasm/$ONTWERP.wasm ($(wc -c < "wasm/$ONTWERP.wasm") bytes)"
