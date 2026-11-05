# B7K Ultra nominal 1 MΩ input: measured specification versus unknown circuit

**Status: public specification comparison and offline behavioral what-if.
Not a reverse-engineered retail B7K Ultra input stage.**

The manufacturer's [Microtubes B7K Ultra manual](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual)
states nominal **1 MΩ input impedance**. The manufacturer's separate
[Microtubes B7K non-Ultra manual](https://www.darkglass.com/en-eu/pages/microtubes-b7k-manual)
states **500 kΩ**. These product specifications must not be interchanged.

PCB Guitar Mania's independently developed [Black Mirror VII v1.6 BOM](https://pcbguitarmania.com/wp-content/uploads/2022/07/Black-Mirror-VII-1.6v-Building-Docs.pdf)
specifies R1 = R2 = 1 MΩ, C1 = 100 nF. Interpreted as the drawn
R1-to-ground and C1-in-series-with-R2 input, its frequency-dependent port
impedance is

\[
 Z_{\mathrm{clone}}(s) = R_1 \parallel \left(R_2+\frac{1}{sC_1}\right).
\]

That clone approaches **500 kΩ** at audio frequencies well above the
1.59 Hz coupling corner, despite the Ultra's 1 MΩ nominal specification.
This is a discrepancy between public sources, **not evidence that the retail
Ultra has a different resistor arrangement or uses the non-Ultra circuit**.
The manufacturer does not specify the complete complex input impedance
versus frequency, revision-specific netlist, or measurement frequency.

## Experimental reference implemented

`InputBufferNominalPortProbe` in `src/dsp/InputBuffer.h` is **offline-only**.
It preserves the existing `InputBuffer` ideal-source high-pass, but assumes
an **independent constant real input-port resistance** `Znom` (default
1 MΩ). With an externally known real Thevenin source resistance `Rs`:

\[
 H_{\mathrm{nom}}(s)
 =
 \frac{Z_{\mathrm{nom}}}{R_s+Z_{\mathrm{nom}}}
 \frac{sR_2C_1}{1+sR_2C_1}.
\]

This is a behavioral sensitivity model, not a proposed Ultra schematic.
Unlike the clone probe, a constant resistive input load here is decoupled
from the retained high-pass. That implicit isolation has **not been observed**
in the hardware; it merely makes the single manufacturer-specified nominal
value useful for a reproducible *what-if* calculation.

At 1 kHz with an **illustrative** 100 kΩ purely resistive open-circuit source,
the nominal-port response is **0.7558 dB greater** than the clone-network
response. Neither a 100 kΩ bass pickup nor a constant 1 MΩ port over the
audio band has been demonstrated on a specific physical unit.

Never insert the reference ahead of the plugin's recorded-audio input: an
audio interface has already loaded and converted its analog pickup source.
Adding hypothetical pedal loading then would double-count or invent that
loading. Use only with an independently known *open-circuit* source voltage
and source impedance for bench comparison.

## Verification and next experiment

`UltraNominalPortReferenceTest` registers 84 combinations of four sample
rates, seven source resistances, and three nominal ports; the independent
bilinear-domain recurrence agrees to **3.05e-15 V** maximum error.
Additional checks verify exactly identical `Rsource=0` behavior to the
unchanged live input, invalid-parameter rejection, reset invariance, and
large-finite-number arithmetic.

To replace this approximation with evidence:

1. Identify the exact B7K Ultra generation/revision and document its condition.
2. Measure complex input admittance from 20 Hz to 20 kHz with known calibrated
   source resistances or an impedance analyzer, keeping the unit's supply and
   bypass/distortion settings recorded.
3. Measure transfer magnitude, phase, and nonlinear response from a known
   open-circuit source through the physical pedal and a calibrated acquisition
   chain. Record gains and loading, not only a normal DI waveform.
4. Distinguish across-unit differences and component tolerances; withhold
   circuit-topology parity claims without direct schematic/netlist evidence.

The physically documented clone probe stays untouched and remains the sole
reference for public Black Mirror VII component values.
