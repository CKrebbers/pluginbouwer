"""vergelijk.py — Jiles-Atherton (CHOWTape-standaard) tegen onze tanh, op periodieke sinussen.

Methode: een sinus van een geheel aantal perioden in N = 4800 samples bij 48 kHz; 8× overbemonsterd met FFT
(nul-opvullen: exact voor een periodiek signaal), twee perioden door het model (de eerste is inlopen), van de tweede
de FFT; terug naar 48 kHz door alles boven 24 kHz weg te laten (ideaal laagdoorlaat: geen aliasing in de vergelijking).
Gemeten per niveau en frequentie: versterking van de grondtoon, faseverschuiving, H2..H7 t.o.v. de grondtoon.
Uitvoer: ref/tweede-mening/uitslag.json (wordt door TWEEDE-MENING.md geciteerd).
"""
import json, math, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from ja_tape import JATape, Tanh

FS, OS, N = 48000, 8, 4800


def meet(model_maker, f, amp):
    k = round(f * N / FS)                       # bin; f wordt k·10 Hz
    n = np.arange(N * OS)
    x = amp * np.sin(2 * np.pi * k * n / (N * OS))
    m = model_maker(FS * OS)
    y = np.empty(2 * N * OS)
    for p in range(2):
        for i in range(N * OS):
            y[p * N * OS + i] = m.stap(x[i])
    Y = np.fft.rfft(y[N * OS:])[: N // 2 + 1] / (N * OS / 2)
    X1 = amp
    g1 = abs(Y[k]) / X1
    fase = math.degrees(np.angle(Y[k]) - (-np.pi / 2))     # sin heeft fase −90° in de FFT
    fase = (fase + 180) % 360 - 180
    harm = {}
    for h in range(2, 8):
        if h * k <= N // 2:
            harm[f"H{h}"] = 20 * math.log10(max(abs(Y[h * k]), 1e-15) / abs(Y[k]))
    return {"f": k * FS / N, "amp": amp, "versterking_db": 20 * math.log10(g1), "fase_graad": fase, **harm}


def main():
    modellen = {
        "ja_chowtape_standaard": lambda fs: JATape(fs, drive=0.5, sat=0.5, bias=0.5),
        "tanh_6db": lambda fs: Tanh(fs, 6.0),
        "tanh_12db": lambda fs: Tanh(fs, 12.0),
    }
    niveaus_db = [-40, -24, -18, -12, -6, 0]
    freqs = [100, 1000, 5000]
    uit = {"methode": "periodieke sinus, 8x overbemonsterd via FFT, ideaal laagdoorlaat terug", "fs": FS, "metingen": {}}
    for naam, mk in modellen.items():
        rij = []
        for f in freqs:
            for L in niveaus_db:
                r = meet(mk, f, 10 ** (L / 20))
                r["niveau_dbfs"] = L
                rij.append(r)
                print(f"{naam:24s} {f:5d} Hz {L:4d} dBFS  versterking {r['versterking_db']:7.2f} dB  fase {r['fase_graad']:7.2f}°  H2 {r.get('H2', float('nan')):7.1f}  H3 {r.get('H3', float('nan')):7.1f}  H5 {r.get('H5', float('nan')):7.1f}", flush=True)
        uit["metingen"][naam] = rij
    pad = os.path.join(os.path.dirname(__file__), "uitslag.json")
    with open(pad, "w") as fh:
        json.dump(uit, fh, indent=1)
    print("geschreven:", pad)


if __name__ == "__main__":
    main()
