// proef_delay.dsp — dezelfde keten als dsp/ontwerpen/proef_delay.h, in Faust (fase B van PROMPT.md).
// Laagdoor (RBJ, tf2) → fractionele delay (Lagrange 3e orde, de.fdelay3) → droog/nat.
import("stdfaust.lib");
freq = hslider("Frequentie", 4000, 20, 20000, 0.001);
q = hslider("Resonantie", 0.70703125, 0.25, 10, 0.0001);
tijd = hslider("Tijd", 250, 1, 1000, 0.0001);
mix = hslider("Mix", 0.5, 0, 1, 0.0001);

laagdoor(f, Q) = fi.tf2(b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)
with {
  w0 = 2*ma.PI*f/ma.SR; c = cos(w0); al = sin(w0)/(2*Q);
  b0 = (1-c)/2; b1 = 1-c; b2 = b0; a0 = 1+al; a1 = -2*c; a2 = 1-al;
};
d = tijd*0.001*ma.SR;
kanaal = laagdoor(freq, q) <: *(1-mix), (de.fdelay3(262144, d) : *(mix)) :> _;
process = kanaal, kanaal;
