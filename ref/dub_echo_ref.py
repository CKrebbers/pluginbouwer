"""dub_echo_ref.py — een ONAFHANKELIJKE float64-referentie van de dub-echo (PROMPT.md "Bewijs").

Andere code dan dsp/ontwerpen/dub_echo.h: Python, de math-module van het platform (libm) in plaats van pb_math.h,
en de formules opnieuw uitgeschreven vanuit de bronnen (Zavalishin/Simper voor de TPT-filters, Laakso voor Lagrange,
Parker 2016 voor ADAA). Wat hier staat is niet gekopieerd uit de C++; waar beide hetzelfde doen, doen ze dat omdat
de bron het zo zegt.

Twee modi:
  render(x, sr, p)                  de keten zoals ontworpen (ADAA), per sample, voor de vergelijking met de WASM
  render(x, sr, p, ideaal=16)       de saturatie ZONDER anti-aliasing op 16× de samplerate, dan terug naar sr:
                                    de "ideale" bandbegrensde saturatie, de meetlat voor aliasing en harmonischen
Gebruik vanaf de commandoregel (door tests/dubecho.test.mjs):
  python3 ref/dub_echo_ref.py in.f64 uit.f64 sr '{"Time":..}' [ideaal]
  (ruwe float64-bestanden, één kanaal)
"""
import json
import math
import sys

import numpy as np

STANDAARD = {"Time": 375.0, "Feedback": 0.5, "Tone": 3000.0, "Low cut": 100.0, "Resonance": 0.70703125,
             "Saturation": 6.0, "Flutter": 0.0, "Mix": 0.375, "Hiss": 0.0}


def lagrange3(buf, schrijfpos, d):
    """Lees d samples terug (d ≥ 1) met 4-punts Lagrange: punten op i-1, i, i+1, i+2 rond i = floor(d)."""
    i = math.floor(d)
    f = d - i
    pts = (-1, 0, 1, 2)
    y = 0.0
    n = len(buf)
    for k in pts:
        h = 1.0
        for j in pts:
            if j != k:
                h *= (f - j) / (k - j)
        y += h * buf[(schrijfpos - (i + k)) % n]
    return y


class Svf:
    """TPT state-variable filter (Simper 2013, naar Zavalishin hfst. 4)."""

    def __init__(self, fc, q, sr):
        g = math.tan(math.pi * min(max(fc, 1.0), 0.49 * sr) / sr)
        self.k = 1.0 / max(q, 0.05)
        self.a1 = 1.0 / (1.0 + g * (g + self.k))
        self.a2 = g * self.a1
        self.a3 = g * self.a2
        self.ic1 = 0.0
        self.ic2 = 0.0

    def laag(self, x):
        v3 = x - self.ic2
        v1 = self.a1 * self.ic1 + self.a2 * v3
        v2 = self.ic2 + self.a2 * self.ic1 + self.a3 * v3
        self.ic1 = 2 * v1 - self.ic1
        self.ic2 = 2 * v2 - self.ic2
        return v2


class HoogEenPool:
    def __init__(self, fc, sr):
        g = math.tan(math.pi * min(max(fc, 1.0), 0.49 * sr) / sr)
        self.G = g / (1 + g)
        self.s = 0.0

    def hoog(self, x):
        v = (x - self.s) * self.G
        y = v + self.s
        self.s = y + v
        return x - y


# asymmetrie van de tape (17 okt 2026, ref/referentie/RAPPORT.md §2): tanh(u + β) − tanh(β), gedeeld door 1 − tanh²(β)
BETA = 0.099609375
TB = math.tanh(BETA)
INORM = 1.0 / (1.0 - TB * TB)
LCB = math.log(math.cosh(BETA))


def logcosh(x):
    a = abs(x)
    return a + math.log1p(math.exp(-2 * a)) - math.log(2.0)


def render(x, sr, p, ideaal=0):
    p = {**STANDAARD, **p}
    if p["Flutter"] != 0:
        raise ValueError("de referentie kent geen bandbeweging: zet Flutter op 0")
    if p.get("Hiss", 0) != 0:
        raise ValueError("de referentie kent geen bandruis: zet Ruis op 0")
    g = 10 ** (p["Saturation"] / 20)
    fb, mix = p["Feedback"], p["Mix"]
    d = p["Time"] * 1e-3 * sr - (0 if ideaal else 0.5)
    n = len(x)
    buf = [0.0] * (1 << int(math.ceil(math.log2(d + 8))))
    pos = 0
    hp, lp = HoogEenPool(p["Low cut"], sr), Svf(p["Tone"], p["Resonance"], sr)
    x1, F1 = 0.0, 0.0
    terug = 0.0
    uit = np.zeros(n)
    # ideaal: de saturatie op ideaal×sr, met een eigen lus? Nee: de lus blijft op sr. We vervangen alleen
    # de saturatie door "tanh op 16× sr, bandbegrensd terug naar sr". Dat kan per sample niet causaal; daarom
    # wordt de ideale modus alleen gebruikt zonder terugkoppeling (fb = 0), waar de saturatie vóór de lijn zit
    # en als geheel kan worden voorbewerkt.
    if ideaal:
        if fb != 0:
            raise ValueError("ideaal alleen met Feedback 0")
        sat = ideale_saturatie(np.asarray(x, dtype=float), g, ideaal)
    for t in range(n):
        u = x[t] + terug
        if ideaal:
            w = sat[t]
        else:
            ug = u * g
            F = logcosh(ug + BETA) - LCB - TB * ug
            dd = ug - x1
            w = ((F - F1) / dd if abs(dd) > 1e-6 else math.tanh(0.5 * (ug + x1) + BETA) - TB) * INORM / g
            x1, F1 = ug, F
        pos = (pos + 1) % len(buf)
        buf[pos] = w
        r = lagrange3(buf, pos, d)
        nat = lp.laag(hp.hoog(r))
        terug = fb * nat
        uit[t] = (1 - mix) * x[t] + mix * nat
    return uit


def sinc_laagdoor(n_taps, fc_rel):
    """Venster-sinc (Blackman-Harris 4-term), fc_rel = afsnijfrequentie / samplerate."""
    m = np.arange(n_taps) - (n_taps - 1) / 2
    h = 2 * fc_rel * np.sinc(2 * fc_rel * m)
    a = [0.35875, 0.48829, 0.14128, 0.01168]
    w = a[0] - a[1] * np.cos(2 * np.pi * np.arange(n_taps) / (n_taps - 1)) + a[2] * np.cos(4 * np.pi * np.arange(n_taps) / (n_taps - 1)) - a[3] * np.cos(6 * np.pi * np.arange(n_taps) / (n_taps - 1))
    h *= w
    return h / h.sum()


def fftconv(a, b):
    n = len(a) + len(b) - 1
    m = 1 << int(math.ceil(math.log2(n)))
    return np.fft.irfft(np.fft.rfft(a, m) * np.fft.rfft(b, m), m)[:n]


def ideale_saturatie(x, g, os):
    """tanh(g·x)/g, gerekend op os×sr en bandbegrensd terug: wat een saturatie zonder aliasing zou geven.
    Opwaarts: nullen invoegen + laagdoor (tot 0,45·sr); neerwaarts: laagdoor (0,45·sr) + elke os-de sample.
    Lineaire fase; de vertraging van beide filters wordt eraf gehaald."""
    # 256·os taps, afsnijden op 0,42·sr: overgangsband ~1,5 kHz (Blackman-Harris), zodat de 5e harmonische van
    # 5 kHz (25 kHz) al in de stopband zit. Met 64·os taps en 0,45·sr lekte hij door (meetlat −69,7 dB).
    taps = 256 * os + 1
    h = sinc_laagdoor(taps, 0.42 / os)
    op = np.zeros(len(x) * os)
    op[::os] = x * os
    op = fftconv(op, h)[(taps - 1) // 2:][: len(x) * os]
    y = (np.tanh(g * op + BETA) - TB) * INORM / g
    y = fftconv(y, h)[(taps - 1) // 2:][: len(x) * os]
    return y[::os]


if __name__ == "__main__":
    x = np.fromfile(sys.argv[1], dtype="<f8")
    sr = float(sys.argv[3])
    p = json.loads(sys.argv[4])
    ideaal = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    render(x, sr, p, ideaal).astype("<f8").tofile(sys.argv[2])
