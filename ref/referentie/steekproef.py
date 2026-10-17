"""Eén keer door het Akai-archief (tar.gz, gestreamd): lijst per map, en een steekproef om de meting op te bouwen.

Gebruik: python3 -I steekproef.py <archief.tar.gz> <uitmap>
- <uitmap>/lijst.txt      elk bestand met zijn grootte
- <uitmap>/mappen.txt     per map: aantal, MB, voorbeelden
- <uitmap>/klein/...      elk bestand < 2 MB in zijn geheel (metadata, trajecten)
- <uitmap>/wav/...        van elke map hooguit 2 WAV's, ingekort tot 10 s (kop blijft, data-chunk afgekapt)
Geen tar -tv lezen (de kolommen verschillen per tar); tarfile geeft naam en grootte zelf.
Bron: Aalto Magnetic Tape Recorder Dataset, CC-BY-4.0 (Mikkonen, Wright, Välimäki, DAFx 2023).
"""
import collections
import os
import struct
import sys
import tarfile

archief, uit = sys.argv[1], sys.argv[2]
KLEIN = 2_000_000
WAV_PER_MAP = 2
WAV_SEC = 10
TOTAAL_MAX = 80_000_000

os.makedirs(uit, exist_ok=True)
per = collections.defaultdict(lambda: [0, 0, []])
wav_n = collections.Counter()
totaal = 0


def veilig(naam):
    delen = [d for d in naam.split('/') if d not in ('', '.', '..')]
    return os.path.join(*delen) if delen else None


def kort_wav(f, doel):
    """Kop van de RIFF overnemen, data-chunk afkappen op WAV_SEC. Werkt voor PCM én float (format 3)."""
    kop = f.read(12)
    if len(kop) < 12 or kop[:4] not in (b'RIFF', b'RF64') or kop[8:12] != b'WAVE':
        return 0
    stukken = []
    bytes_per_sec = None
    while True:
        h = f.read(8)
        if len(h) < 8:
            return 0
        cid, n = h[:4], struct.unpack('<I', h[4:])[0]
        if cid == b'data':
            if not bytes_per_sec:
                return 0
            keep = min(n if n != 0xFFFFFFFF else 1 << 62, bytes_per_sec * WAV_SEC)
            data = f.read(keep)
            break
        body = f.read(n + (n & 1))
        if cid == b'fmt ':
            bytes_per_sec = struct.unpack('<I', body[8:12])[0]
        if cid != b'ds64':
            stukken.append(h[:4] + struct.pack('<I', n) + body)
    inhoud = b''.join(stukken) + b'data' + struct.pack('<I', len(data)) + data
    os.makedirs(os.path.dirname(doel), exist_ok=True)
    with open(doel, 'wb') as g:
        g.write(b'RIFF' + struct.pack('<I', 4 + len(inhoud)) + b'WAVE' + inhoud)
    return 12 + len(inhoud)


with open(os.path.join(uit, 'lijst.txt'), 'w') as lijst, tarfile.open(archief, 'r|*') as tar:
    for m in tar:
        if not m.isfile():
            continue
        naam = m.name
        lijst.write(f'{m.size}\t{naam}\n')
        map_ = naam.rsplit('/', 1)[0] if '/' in naam else '.'
        p = per[map_]
        p[0] += 1
        p[1] += m.size
        if len(p[2]) < 4:
            p[2].append(naam.rsplit('/', 1)[-1])
        pad = veilig(naam)
        if not pad or totaal > TOTAAL_MAX:
            continue
        if m.size < KLEIN and not naam.lower().endswith('.wav'):
            f = tar.extractfile(m)
            doel = os.path.join(uit, 'klein', pad)
            os.makedirs(os.path.dirname(doel), exist_ok=True)
            with open(doel, 'wb') as g:
                g.write(f.read())
            totaal += m.size
        elif naam.lower().endswith('.wav') and wav_n[map_] < WAV_PER_MAP:
            f = tar.extractfile(m)
            n = kort_wav(f, os.path.join(uit, 'wav', pad))
            if n:
                wav_n[map_] += 1
                totaal += n

with open(os.path.join(uit, 'mappen.txt'), 'w') as g:
    for m, (n, grootte, vb) in sorted(per.items()):
        regel = f'MAP {m}  {n} bestanden  {grootte / 1e6:.0f} MB  bv. {", ".join(vb)}'
        print(regel)
        g.write(regel + '\n')
print('steekproef', round(totaal / 1e6, 1), 'MB')
