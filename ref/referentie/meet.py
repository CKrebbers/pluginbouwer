"""meet.py — de dub-echo naast een echte bandmachine (Akai GX-4000D, Aalto Magnetic Tape Recorder Dataset, CC-BY-4.0).

Gebruik: python3 -I ref/referentie/meet.py <datamap> <uitmap>
<datamap> = de assets van release referentie-data (ref/referentie/subset.py maakt ze; plat: <subset>__<band>__<map>__<bestand>).

Drie vragen, elk met een getal naast ons model:
1. Beweging (wow en flutter): de vertraging per sample uit de trajecten -> snelheidsafwijking, spectrum, RMS en piek.
   Naast TapeBeweging (dsp/pb_tape.h) zoals de dub-echo hem gebruikt: vertraging = T·(1 + haperen·b(t)).
2. Saturatie: harmonischen tegen het niveau op de gefade sinussen (gecorrigeerd voor de frequentierespons uit de
   sweeps), en daarop onze tanh en Jiles-Atherton (ref/tweede-mening/ja_tape.py) gefit met één niveauverschuiving.
3. Ruis en frequentierespons: stilte -> ruisvloer per terts; sweeps -> respons per terts.
Alles in float64 met numpy; geen code van de dataset of van CHOWTape. Getallen zijn statistiek, geen bit-vergelijking.
"""
import glob
import json
import math
import os
import struct
import sys

import numpy as np

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HIER, '..', 'tweede-mening'))
from ja_tape import JATape, Tanh  # noqa: E402

FS = 44100


def lees_wav(pad):
    """float32 of PCM; geeft (sr, array[n, kanalen]) als float64."""
    with open(pad, 'rb') as f:
        kop = f.read(12)
        assert kop[:4] == b'RIFF' and kop[8:12] == b'WAVE', pad
        fmt = None
        while True:
            h = f.read(8)
            if len(h) < 8:
                raise ValueError('geen data-chunk: ' + pad)
            cid, n = h[:4], struct.unpack('<I', h[4:])[0]
            if cid == b'fmt ':
                b = f.read(n + (n & 1))
                fmt = struct.unpack('<HHIIHH', b[:16])
                if fmt[0] == 0xFFFE:
                    fmt = (struct.unpack('<H', b[24:26])[0],) + fmt[1:]
            elif cid == b'data':
                code, ch, sr, _, _, bits = fmt
                dt = {(3, 32): '<f4', (3, 64): '<f8', (1, 16): '<i2', (1, 32): '<i4'}[(code, bits)]
                x = np.fromfile(f, dtype=dt, count=n // (bits // 8)).astype(np.float64)
                if code == 1:
                    x /= float(2 ** (bits - 1))
                return sr, x.reshape(-1, ch)
            else:
                f.seek(n + (n & 1), 1)


def db(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), 1e-30))


def bestanden(map_, subset):
    uit = {}
    for p in sorted(glob.glob(os.path.join(map_, subset + '__*'))):
        _, band, deel, naam = os.path.basename(p).split('__')
        soort = naam.split('_')[0]  # input, target, trajectory
        uit.setdefault(band, {})[soort] = p
    return uit


# ---------------------------------------------------------------- 1. beweging

def snelheid_uit_vertraging(d, fs, decimeer=44):
    """d = vertraging in seconden per sample. Snelheidsafwijking eps = -dd/dt (dimensieloos), na laagdoorlaat (blokgemiddelde)."""
    m = len(d) // decimeer
    dd = d[:m * decimeer].reshape(m, decimeer).mean(axis=1)
    fs2 = fs / decimeer
    eps = -np.gradient(dd) * fs2
    return eps, fs2, dd


def spectrum_eps(eps, fs2):
    n = 1 << int(math.log2(min(len(eps), int(fs2 * 64))))
    hop = n // 2
    w = np.hanning(n)
    acc = None
    k = 0
    for i in range(0, len(eps) - n + 1, hop):
        s = np.abs(np.fft.rfft((eps[i:i + n] - eps[i:i + n].mean()) * w)) ** 2
        acc = s if acc is None else acc + s
        k += 1
    acc /= max(k, 1)
    f = np.fft.rfftfreq(n, 1 / fs2)
    return f, acc / (np.sum(w ** 2) * fs2 / 2)  # eenzijdige PSD


def band_rms(f, psd, lo, hi):
    m = (f >= lo) & (f < hi)
    return math.sqrt(np.sum(psd[m]) * (f[1] - f[0])) if m.any() else 0.0


def piek_freq(f, psd, lo, hi):
    m = np.where((f >= lo) & (f < hi))[0]
    return float(f[m[np.argmax(psd[m])]]) if len(m) else None


def beweging_kengetallen(eps, fs2):
    e = eps - np.mean(eps)
    f, psd = spectrum_eps(e, fs2)
    return {
        'rms_procent': float(np.sqrt(np.mean(e ** 2)) * 100),
        'piek_procent': float((np.percentile(e, 99.5) - np.percentile(e, 0.5)) / 2 * 100),
        'wow_rms_procent': band_rms(f, psd, 0.2, 6.0) * 100,
        'flutter_rms_procent': band_rms(f, psd, 6.0, 200.0) * 100,
        'wow_piek_hz': piek_freq(f, psd, 0.2, 6.0),
        'flutter_piek_hz': piek_freq(f, psd, 6.0, 200.0),
    }


def ons_b(n, fs, zaad=0x51CE):
    """TapeBeweging.stap() als numpy: 0,003·sin(2π·0,6t) + 0,0012·sin(2π·6,5t + 1) + 0,006·drift.
    De drift is gelijkmatige ruis in [−1, 1] door een eenpolig laagdoorlaat van 1,5 Hz. Statistisch gelijk aan dsp/
    (de xorshift van pb::Toeval is niet nagebouwd: dit vergelijkt spectra, geen samples)."""
    t = np.arange(n) / fs
    rng = np.random.default_rng(zaad)
    u = rng.uniform(-1, 1, n)
    k = 1.0 - math.exp(-2 * math.pi * 1.5 / fs)
    drift = np.empty(n)
    acc = 0.0
    # eenpolig filter in blokken (lfilter zonder scipy)
    a = 1 - k
    blok = 4096
    machten = a ** np.arange(1, blok + 1)
    for i in range(0, n, blok):
        seg = u[i:i + blok] * k
        m = len(seg)
        # y[j] = a^(j+1)·acc + sum_{l<=j} a^(j-l)·seg[l]
        c = np.cumsum(seg / machten[:m])
        y = machten[:m] * (c + acc / 1.0)
        drift[i:i + m] = y
        acc = y[-1]
    return 0.0030 * np.sin(2 * math.pi * 0.6 * t) + 0.0012 * np.sin(2 * math.pi * 6.5 * t + 1.0) + 0.0060 * drift


def meet_beweging(map_):
    uit = {'echt': {}, 'ons': {}}
    for subset in ('SinesFadedShortContinuousPulse100', 'LogSweepsContinuousPulse100'):
        for band, b in bestanden(map_, subset).items():
            if 'trajectory' not in b:
                continue
            d = np.load(b['trajectory'], mmap_mode='r').astype(np.float64).ravel()
            eenheid = 'samples' if np.median(np.abs(d)) > 1.0 else 'seconden'
            ds = d / FS if eenheid == 'samples' else d
            eps, fs2, dd = snelheid_uit_vertraging(ds, FS)
            r = beweging_kengetallen(eps, fs2)
            r.update({'eenheid_in_bestand': eenheid, 'vertraging_gem_ms': float(np.mean(ds) * 1000),
                      'vertraging_sd_ms': float(np.std(dd - np.polyval(np.polyfit(np.arange(len(dd)), dd, 1), np.arange(len(dd)))) * 1000),
                      'sec': len(d) / FS})
            uit['echt'][f'{band} ({subset.split("Continuous")[0]})'] = r
    n = FS * 120
    b = ons_b(n, FS)
    for T in (0.375, 1.2):
        for h in (0.25, 1.0):
            d = T * (1 + h * b)
            eps, fs2, _ = snelheid_uit_vertraging(d, FS)
            uit['ons'][f'tijd {int(T * 1000)} ms, haperen {h}'] = beweging_kengetallen(eps, fs2)
    return uit


# ---------------------------------------------------------------- 3a. frequentierespons (sweeps)

TERTSEN = [round(1000 * 2 ** (k / 3)) for k in range(-17, 14)]  # 20 Hz .. 20 kHz


def vertraging_samples(b):
    d = np.load(b['trajectory'], mmap_mode='r')
    m = float(np.median(np.asarray(d[::1000], dtype=np.float64)))
    return m if m > 1.0 else m * FS


def uitlijn(x, y, d):
    d = int(round(d))
    if d >= 0:
        return x[:len(x) - d] if d else x, y[d:]
    return x[-d:], y[:len(y) + d]


def respons(map_):
    uit = {}
    for band, b in bestanden(map_, 'LogSweepsContinuousPulse100').items():
        _, xi = lees_wav(b['input'])
        _, yt = lees_wav(b['target'])
        x, y = uitlijn(xi[:, 0], yt[:, 0], vertraging_samples(b))
        n = min(len(x), len(y))
        x, y = x[:n], y[:n]
        N = 1 << 15
        w = np.hanning(N)
        Pxx = np.zeros(N // 2 + 1)
        Pyy = np.zeros(N // 2 + 1)
        for i in range(0, n - N + 1, N // 2):
            X = np.fft.rfft(x[i:i + N] * w)
            Y = np.fft.rfft(y[i:i + N] * w)
            Pxx += np.abs(X) ** 2
            Pyy += np.abs(Y) ** 2
        f = np.fft.rfftfreq(N, 1 / FS)
        H = 10 * np.log10(np.maximum(Pyy, 1e-30) / np.maximum(Pxx, 1e-30))
        rij = {}
        for fc in TERTSEN:
            m = (f >= fc * 2 ** (-1 / 6)) & (f < fc * 2 ** (1 / 6)) & (Pxx > Pxx.max() * 1e-8)
            if m.any():
                rij[fc] = float(10 * np.log10(np.sum(Pyy[m]) / np.sum(Pxx[m])))
        ref = rij.get(1000, 0.0)
        uit[band] = {'terts_db_t_o_v_1kHz': {k: round(v - ref, 2) for k, v in rij.items()},
                     'versterking_1kHz_db': round(ref, 2), '_f': f, '_H': H - ref}
    return uit


def respons_bij(r, freq):
    f, H = r['_f'], r['_H']
    i = int(np.clip(np.searchsorted(f, freq), 1, len(f) - 1))
    lo, hi = max(i - 3, 0), min(i + 4, len(f))
    return float(np.median(H[lo:hi]))


# ---------------------------------------------------------------- 3b. ruis (stilte)

def ruis(map_):
    uit = {}
    for band, b in bestanden(map_, 'Silence').items():
        _, yt = lees_wav(b['target'])
        y = yt[:, 0] - np.mean(yt[:, 0])
        N = 1 << 14
        w = np.hanning(N)
        P = np.zeros(N // 2 + 1)
        k = 0
        for i in range(0, len(y) - N + 1, N):
            P += np.abs(np.fft.rfft(y[i:i + N] * w)) ** 2
            k += 1
        P /= k * np.sum(w ** 2) / 2  # vermogen per bin (sinus-geijkt)
        f = np.fft.rfftfreq(N, 1 / FS)
        terts = {}
        for fc in TERTSEN:
            m = (f >= fc * 2 ** (-1 / 6)) & (f < fc * 2 ** (1 / 6))
            if m.any():
                terts[fc] = round(float(10 * np.log10(np.sum(P[m]) / 2 + 1e-30)), 1)
        uit[band] = {'rms_dbfs': round(float(db(np.sqrt(np.mean(y ** 2)))), 1), 'terts_dbfs': terts}
    return uit


# ---------------------------------------------------------------- 2. saturatie (gefade sinussen)

def harmonischen_venster(xw, yw, w, N):
    X = np.abs(np.fft.rfft(xw * w))
    k0 = int(np.argmax(X[2:N // 2 - 2])) + 2
    tot = np.sum(X[2:] ** 2)
    piek = np.sum(X[max(k0 - 3, 0):k0 + 4] ** 2)
    if piek < 0.999 * tot:          # geen zuivere sinus in de invoer (overgang, twee tonen, stilte)
        return None
    # parabolische interpolatie voor f0
    a, b_, c = np.log(X[k0 - 1] + 1e-30), np.log(X[k0] + 1e-30), np.log(X[k0 + 1] + 1e-30)
    p = 0.5 * (a - c) / (a - 2 * b_ + c) if (a - 2 * b_ + c) != 0 else 0.0
    f0 = (k0 + p) * FS / N
    Y = np.abs(np.fft.rfft(yw * w)) ** 2
    norm = N * np.sum(w ** 2) / 4   # Parseval: de hoofdlob van een sinus met amplitude A heeft N·A²·Σw²/4
    ain = math.sqrt(piek / norm)
    h = []
    for m in range(1, 6):
        km = int(round(m * (k0 + p)))
        if km + 4 >= len(Y):
            h.append(None)
            continue
        h.append(math.sqrt(np.sum(Y[km - 4:km + 5]) / norm))
    vloer = math.sqrt(np.median(Y[2:]) * 9 / norm)
    return f0, ain, h, vloer


def meet_sinussen(map_, resp):
    uit = {}
    N = 8192
    w = np.hanning(N)
    for band, b in bestanden(map_, 'SinesFadedShortContinuousPulse100').items():
        _, xi = lees_wav(b['input'])
        _, yt = lees_wav(b['target'])
        d = vertraging_samples(b)
        x, y = uitlijn(xi[:, 0], yt[:, 0], d)
        n = min(len(x), len(y))
        rijen = []
        for i in range(0, n - N, N // 2):
            r = harmonischen_venster(x[i:i + N], y[i:i + N], w, N)
            if r is None:
                continue
            f0, ain, h, vloer = r
            if h[0] is None or h[0] < 1e-6:
                continue
            rij = {'f0': f0, 'in_db': db(ain), 'uit_db': db(h[0]), 'vloer_db': db(vloer)}
            for m in (2, 3, 5):
                if h[m - 1] is None:
                    continue
                corr = respons_bij(resp[band], m * f0) - respons_bij(resp[band], f0) if band in resp else 0.0
                rel = db(h[m - 1]) - db(h[0]) - corr
                rij[f'H{m}'] = rel if db(h[m - 1]) > rij['vloer_db'] + 6 else None   # onder de ruis: geen getal
            rijen.append(rij)
        uit[band] = rijen
    return uit


def curve(rijen, flo, fhi, harm='H3', stap=3.0):
    """mediaan van H per invoerniveau (bakken van `stap` dB) binnen flo..fhi."""
    bak = {}
    for r in rijen:
        if flo <= r['f0'] < fhi and r.get(harm) is not None:
            bak.setdefault(round(r['in_db'] / stap), []).append((r['in_db'], r[harm]))
    # sleutel = het mediane niveau in de bak (niet het afgeronde label: dat schoof de fit tot 1,5 dB op)
    return {round(float(np.median([a for a, _ in v])), 2): float(np.median([b for _, b in v]))
            for _, v in sorted(bak.items()) if len(v) >= 3}


def model_curve(maker, f, niveaus_db):
    """H3 en H5 van een model op een sinus (periodiek, 0,25 s inlopen, dan 0,25 s meten)."""
    uit = {}
    n_per = FS / f
    for L in niveaus_db:
        A = 10 ** (L / 20)
        m = maker()
        n = int(FS * 0.5)
        t = np.arange(n)
        x = A * np.sin(2 * math.pi * f * t / FS)
        y = np.array([m.stap(v) for v in x])
        seg = y[n // 2:]
        cyc = int(len(seg) / n_per)
        seg = seg[:int(round(cyc * n_per))]
        S = np.abs(np.fft.rfft(seg))
        k = cyc
        H1 = S[k]
        uit[L] = {f'H{h}': float(db(S[h * k]) - db(H1)) for h in (2, 3, 5)}
        uit[L]['versterking_db'] = float(db(H1 * 2 / len(seg)) - L)
    return uit


def fit(echt, model, harm='H3'):
    """Zoek c (dB) zodat model(L + c) zo goed mogelijk op echt(L) ligt; geeft c, rms-fout, en de punten."""
    mk = np.array(sorted(model))
    mv = np.array([model[k][harm] for k in mk])
    best = None
    for c in np.arange(-60, 40.01, 0.25):
        fouten = []
        for L, v in echt.items():
            q = L + c
            if q < mk[0] or q > mk[-1]:
                continue
            fouten.append(np.interp(q, mk, mv) - v)
        if len(fouten) >= max(3, len(echt) // 2):
            e = math.sqrt(np.mean(np.square(fouten)))
            if best is None or e < best[1]:
                best = (float(c), e, len(fouten))
    return best


def helling(curve_):
    ks = sorted(curve_)
    if len(ks) < 3:
        return None
    x = np.array(ks)
    y = np.array([curve_[k] for k in ks])
    return float(np.polyfit(x, y, 1)[0])


def main():
    data, uitmap = sys.argv[1], sys.argv[2]
    os.makedirs(uitmap, exist_ok=True)
    rapport = {'bron': 'Aalto Magnetic Tape Recorder Dataset (Mikkonen, Wright, Välimäki, DAFx 2023), Zenodo 8085482, CC-BY-4.0'}
    print('beweging …', flush=True)
    rapport['beweging'] = meet_beweging(data)
    print('respons …', flush=True)
    resp = respons(data)
    rapport['respons'] = {b: {k: v for k, v in r.items() if not k.startswith('_')} for b, r in resp.items()}
    print('ruis …', flush=True)
    rapport['ruis'] = ruis(data)
    print('sinussen …', flush=True)
    sin = meet_sinussen(data, resp)
    rapport['sinussen'] = {}
    niveaus = list(np.arange(-60, 24.01, 3.0))
    modellen = {
        'tanh (dub-echo, verzadiging 6 dB)': lambda: Tanh(FS, 6.0),
        'Jiles-Atherton (CHOWTape-standaard)': lambda: JATape(FS, 0.5, 0.5, 0.5),
    }
    print('modellen op 1 kHz …', flush=True)
    mcurves = {naam: model_curve(mk, 1000.0, niveaus) for naam, mk in modellen.items()}
    rapport['modellen_1kHz'] = {naam: {str(k): v for k, v in c.items()} for naam, c in mcurves.items()}
    for band, rijen in sin.items():
        frequenties = sorted({round(r['f0']) for r in rijen})
        per = {}
        for lo, hi in ((150, 400), (400, 1500), (1500, 4000)):
            c3 = curve(rijen, lo, hi, 'H3')
            c2 = curve(rijen, lo, hi, 'H2')
            c5 = curve(rijen, lo, hi, 'H5')
            fits = {naam: fit(c3, mc) for naam, mc in mcurves.items()} if lo == 400 else {}
            per[f'{lo}-{hi} Hz'] = {
                'H3': c3, 'H2': c2, 'H5': c5,
                'helling_H3_db_per_db_onderste_helft': helling(dict(list(c3.items())[:max(3, len(c3) // 2)])),
                'fit_H3': {naam: (None if f is None else {'verschuiving_db': f[0], 'rms_fout_db': round(f[1], 2), 'punten': f[2]})
                           for naam, f in fits.items()},
            }
        rapport['sinussen'][band] = {'vensters': len(rijen), 'frequenties_hz': frequenties[:60], 'per_band': per}
    for naam, c in mcurves.items():
        lage = {k: v['H3'] for k, v in c.items() if v['H3'] > -140 and k <= -12}
        rapport['modellen_1kHz'][naam]['helling_H3_laag'] = helling(lage)
    with open(os.path.join(uitmap, 'uitslag.json'), 'w') as f:
        json.dump(rapport, f, indent=1, default=lambda o: round(o, 3) if isinstance(o, float) else str(o))
    print(json.dumps({k: rapport[k] for k in ('beweging', 'ruis')}, indent=1)[:6000])
    for band, s in rapport['sinussen'].items():
        print(band, s['vensters'], 'vensters')
        for rng, p in s['per_band'].items():
            print(' ', rng, 'H3', {k: round(v, 1) for k, v in p['H3'].items()}, 'helling', p['helling_H3_db_per_db_onderste_helft'], p['fit_H3'])
            print(' ', rng, 'H2', {k: round(v, 1) for k, v in p['H2'].items()})


if __name__ == '__main__':
    main()
