# Uitslag proefbouw B (2026-10-15)

Faust 2.90.0 (WASM, @grame/faustwasm) en FAUST Version 2.90.8 (native), clang Ubuntu clang version 18.1.3 (1ubuntu1).
Keten: laagdoor → Lagrange-3 delay → droog/nat. Sets: geheel (48000 Hz, {"Frequentie":4000,"Resonantie":2.5,"Tijd":123.4375,"Mix":0.5}); fractie (48000 Hz, {"Frequentie":7000,"Resonantie":0.70703125,"Tijd":10.0078125,"Mix":0.75}); fractie-441 (44100 Hz, {"Frequentie":1500,"Resonantie":6,"Tijd":333.3125,"Mix":0.5}).

| variant | set | geval | browser (WASM) vs native | WASM vs gebouwd .clap | Faust vs onze stack |
|---|---|---|---|---|---|
| Faust float | geheel | ruis | −∞ (bit voor bit gelijk) | — | -83.0 dBFS |
| Faust float | geheel | stukjes | −∞ (bit voor bit gelijk) | — | -102.5 dBFS |
| Faust float | geheel | sweep | −∞ (bit voor bit gelijk) | — | -75.9 dBFS |
| Faust float | fractie | ruis | −∞ (bit voor bit gelijk) | — | -106.2 dBFS |
| Faust float | fractie | stukjes | −∞ (bit voor bit gelijk) | — | -126.2 dBFS |
| Faust float | fractie | sweep | −∞ (bit voor bit gelijk) | — | -102.6 dBFS |
| Faust float | fractie-441 | ruis | −∞ (bit voor bit gelijk) | — | -86.7 dBFS |
| Faust float | fractie-441 | stukjes | −∞ (bit voor bit gelijk) | — | -97.2 dBFS |
| Faust float | fractie-441 | sweep | −∞ (bit voor bit gelijk) | — | -72.2 dBFS |
| Faust double | geheel | ruis | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) |
| Faust double | geheel | stukjes | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | -596.7 dBFS |
| Faust double | geheel | sweep | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) |
| Faust double | fractie | ruis | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | -198.7 dBFS |
| Faust double | fractie | stukjes | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | -603.5 dBFS |
| Faust double | fractie | sweep | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) |
| Faust double | fractie-441 | ruis | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) |
| Faust double | fractie-441 | stukjes | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | -427.5 dBFS |
| Faust double | fractie-441 | sweep | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) | −∞ (bit voor bit gelijk) |

Platform: linux x64, Node v22.22.0.
