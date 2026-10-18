#!/usr/bin/env python3
# presets-lijst.py — de fabriekspresets uit presets/*.json als platte lijst, voor de toetsen in CI (fase 4).
#
#   python3 gereedschap/presets-lijst.py <map presets>          per regel: sleutel<TAB>naam<TAB>naam=waarde;...
#   python3 gereedschap/presets-lijst.py <map presets> --namen  per regel alleen de naam
#
# Volgorde: op sleutel (de bestandsnaam zonder .json), zoals plugin/presets.cmake ze in het binary zet.
import json
import os
import sys


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--namen"):
        sys.exit("gebruik: presets-lijst.py <map presets> [--namen]")
    map_ = sys.argv[1]
    for bestand in sorted(f for f in os.listdir(map_) if f.endswith(".json")):
        with open(os.path.join(map_, bestand), encoding="utf-8") as f:
            p = json.load(f)
        sleutel = bestand[: -len(".json")]
        if len(sys.argv) == 3:
            print(p["naam"])
        else:
            waarden = ";".join(f"{k}={float(v)!r}" for k, v in p["waarden"].items())
            print(f"{sleutel}\t{p['naam']}\t{waarden}")


main()
