// proef_mod.dsp — de scherpere proef van fase B: een laagdoor waarvan de afsnijfrequentie ELKE sample beweegt
// (LFO van 0,7 Hz). Dan rekent Faust per sample sin/cos/tan: native met libm, in de browser met Math van V8.
import("stdfaust.lib");
mod = hslider("Mod", 0.5, 0, 1, 0.0001);
fc = 1000 * pow(4, mod * os.osc(0.7));
laagdoor(f, Q) = fi.tf2(b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)
with {
  w0 = 2*ma.PI*f/ma.SR; c = cos(w0); al = sin(w0)/(2*Q);
  b0 = (1-c)/2; b1 = 1-c; b2 = b0; a0 = 1+al; a1 = -2*c; a2 = 1-al;
};
process = laagdoor(fc, 4), laagdoor(fc, 4);
