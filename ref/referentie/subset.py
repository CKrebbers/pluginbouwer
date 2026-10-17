"""Haalt de delen van het Akai-archief die de meting nodig heeft eruit (gestreamd, één keer door het archief).

Gebruik: python3 -I subset.py <archief.tar.gz> <uitmap>
Neemt: SinesFadedShort… en LogSweeps… (input, target, trajectory) en Silence_…/Test (input, target), beide banden.
Bestandsnamen worden plat: <subset>__<map>__<bestand>, zonder haken (een release-asset mag geen [ ] hebben).
Bron: Aalto Magnetic Tape Recorder Dataset, CC-BY-4.0 (Mikkonen, Wright, Välimäki, DAFx 2023), Zenodo 8085482.
"""
import os
import re
import shutil
import sys
import tarfile

archief, uit = sys.argv[1], sys.argv[2]
os.makedirs(uit, exist_ok=True)
WIL = re.compile(r'^(?:\./)?(SinesFadedShortContinuousPulse100|LogSweepsContinuousPulse100|Silence)_AKAI_IPS\[(3\.75|7\.5)\]_(SCOTCH|MAXELL)/(Train|Test)/([^/]+\.(?:wav|npy))$')
with tarfile.open(archief, 'r|*') as tar:
    for m in tar:
        if not m.isfile():
            continue
        r = WIL.match(m.name)
        if not r or (r.group(1) == 'Silence' and r.group(4) != 'Test'):
            continue
        naam = f'{r.group(1)}__{r.group(2)}ips-{r.group(3)}__{r.group(4)}__{r.group(5)}'
        with tar.extractfile(m) as f, open(os.path.join(uit, naam), 'wb') as g:
            shutil.copyfileobj(f, g, 1 << 20)
        print(naam, m.size, flush=True)
