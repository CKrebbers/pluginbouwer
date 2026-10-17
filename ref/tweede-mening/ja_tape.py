"""ja_tape.py — tweede mening over de bandsaturatie: het Jiles-Atherton-hysteresemodel naast onze tanh.

Eigen code naar de vergelijkingen in J. Chowdhury, "Real-time Physical Modelling for Analog Tape Machines", DAFx 2019
(Langevin-functie, JA-differentiaalvergelijking dM/dt, afgeleide met de alfa-transformatie, RK4). Er is GEEN code van
CHOWTapeModel (GPLv3) overgenomen; alleen de getallen van zijn parameterafbeelding (drive/sat/bias → M_s, a, c, k,
alpha) worden als feit gebruikt, zodat "CHOWTape-standaard" iets concreets betekent.

Dit hoort niet bij de plugin en verandert niets aan de toetsen: het is een vergelijking, met een verslag in
TWEEDE-MENING.md. Draaien: python3 -I ref/tweede-mening/vergelijk.py
"""
import math

ALPHA = 1.6e-3


class JATape:
    """Eén kanaal; per sample. drive, sat, bias in 0..1 zoals de knoppen van CHOWTape (bias = 1 − width)."""

    def __init__(self, fs, drive=0.5, sat=0.5, bias=0.5):
        width = 1.0 - bias
        self.M_s = 0.5 + 1.5 * (1.0 - sat)
        self.a = self.M_s / (0.01 + 6.0 * drive)
        self.c = math.sqrt(1.0 - width) - 0.01
        self.k = 0.47875
        self.makeup = (1.0 + 0.6 * width) / (0.5 + 1.5 * (1.0 - sat))
        self.T = 1.0 / fs
        self.M = self.H = self.Hd = 0.0

    def _dMdt(self, M, H, Hd):
        Q = (H + ALPHA * M) / self.a
        if abs(Q) < 1e-3:
            L, Lp = Q / 3.0, 1.0 / 3.0
        else:
            coth = 1.0 / math.tanh(Q)
            L, Lp = coth - 1.0 / Q, 1.0 / (Q * Q) - coth * coth + 1.0
        Mdiff = self.M_s * L - M
        delta = 1.0 if Hd >= 0 else -1.0
        kap = (1.0 - self.c) if (delta > 0) == (Mdiff > 0) else 0.0
        f1 = kap * Mdiff / ((1.0 - self.c) * delta * self.k - ALPHA * Mdiff)
        f2 = Lp * self.c * self.M_s / self.a
        f3 = 1.0 - Lp * self.c * self.M_s * ALPHA / self.a
        return Hd * (f1 + f2) / f3

    def stap(self, H):
        T = self.T
        Hd = (1.75 / T) * (H - self.H) - 0.75 * self.Hd          # alfa-transformatie (alpha = 0,75)
        Hm, Hdm = 0.5 * (H + self.H), 0.5 * (Hd + self.Hd)
        k1 = T * self._dMdt(self.M, self.H, self.Hd)
        k2 = T * self._dMdt(self.M + 0.5 * k1, Hm, Hdm)
        k3 = T * self._dMdt(self.M + 0.5 * k2, Hm, Hdm)
        k4 = T * self._dMdt(self.M + k3, H, Hd)
        M = self.M + k1 / 6 + k2 / 3 + k3 / 3 + k4 / 6
        if M != M or abs(M) > 20:                                 # slecht geconditioneerd: zoals in het artikel, nul
            M, Hd = 0.0, 0.0
        self.M, self.H, self.Hd = M, H, Hd
        return M * self.makeup


class Tanh:
    """Onze saturatie als model (zonder ADAA: die is een anti-aliasing-benadering ervan): tanh(g·x)/g."""

    def __init__(self, fs, verzadiging_db=6.0):
        self.g = 10 ** (verzadiging_db / 20)

    def stap(self, x):
        return math.tanh(self.g * x) / self.g
