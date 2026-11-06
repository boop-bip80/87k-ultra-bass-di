# Native ngspice NJF Level-1 DC oracle

`analysis/spice_reference/verify_njf_level1_oracle.py` runs real ngspice
against a deterministic intrinsic N-channel JFET Level-1 model and compares
its drain current to an independently implemented channel equation. The
comparison includes cutoff, linear/triode, saturation, forward gate voltage,
and negative drain bias (reverse source-drain conduction).

Run with an installed ngspice:

```sh
python3 -m unittest discover -s analysis -p 'test_njf_level1_oracle.py' -v
python3 analysis/spice_reference/verify_njf_level1_oracle.py --ngspice /usr/bin/ngspice
```

The reference model uses `VTO=-3 V`, `BETA=1 mA/V²`, `LAMBDA=0`,
`RS=RD=0`, `IS=1e-30 A`, and zero capacitors. With
`u = Vgs - VTO`, `Id=0` for `u <= 0`,
`Id = BETA * Vds * (2*u - Vds)` for `0 <= Vds <= u`, and
`Id = BETA*u²` for `Vds >= u`. Negative `Vds` uses the equivalent
source-drain-swapped bias and negated current. This matches the intrinsic
Level-1 DC channel expressions in the ngspice device manual; current from
forward-biased PN junctions is deliberately suppressed by the tiny `IS`.

The parser fails closed on missing, extra, unsorted, malformed, and nonfinite
rows. The executable verifies the entire 160-point sweep, with an absolute
error allowance of `1e-12 A` plus `1e-7` relative to analytic magnitude.

This is a device-law oracle, **not** evidence about full frontend accuracy,
diode current, transient capacitances, temperature dependence, noise,
nonzero series resistances, or channel-length modulation.
