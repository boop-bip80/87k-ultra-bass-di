# Independent Q1 J201 ngspice source-feedback verification

**Status (2026-11-05):** static provenance/fixture tests passed (50 Python
suite tests in the integrated Round 6 checkout). A locally unpacked Ubuntu `ngspice-42` binary (not installed
system-wide) executed all **32 native NJF DC/AC cases** and returned PASS.
Worst DC bias error was `1.4805713e-11 V`, magnitude error was
`4.4196478e-10 dB`, and phase error was `9.4034519e-10°`. The GitHub CI
workflow has an ngspice-install step and this diagnostic is registered as
**advisory** (`continue-on-error`) after the existing coupled-recovery stage
test; **no green GitHub CI result is asserted here**.

## Target and external provenance

The model under examination is one **isolated portion of PCB Guitar Mania's
Black Mirror VII v1.6 DIY clone**, not a production Darkglass board:

- [PCB Guitar Mania Black Mirror VII 1.6v build documentation](https://pcbguitarmania.com/wp-content/uploads/2022/07/Black-Mirror-VII-1.6v-Building-Docs.pdf):
  Q1 is J201, R6 is 3.3 kΩ, C3 is 220 nF in the source-degeneration branch.
- The [ngspice manual](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf)
  describes its independent Level-1 `NJF` transistor primitive, including
  `VTO`, `BETA`, `LAMBDA`, gate junctions and device parasitics.
- `analysis/spice_reference/b7k_spice.py` carries the project's existing
  illustrative third-party J201 parameter set; the new isolated script
  statically cross-checks its R6/C3 constants with that full clone netlist.

## Cross-check method

`analysis/spice_reference/verify_jfet_source.py` constructs the real
SPICE primitive elements, with Q1 drain held at +9 V, gate grounded at DC
and driven by a unity AC small-signal source, R6/C3 from source to ground.
It intentionally sets channel-length modulation and intrinsic gate
capacitances to zero to isolate the physically meaningful source-feedback
subproblem. It does **not** reuse the C++ waveshaper, test oracle code or MNA
solver, and no JFET is approximated by a behavioral current source inside
the netlist.

The independent Python side uses a self-biased saturation-condition equation
solely to predict the primitive's operating point and small-signal transfer.
NGSPICE must reproduce DC source bias to 0.2 mV and source/gate AC response
to 0.015 dB / 0.1° on a 20–8000 Hz grid at 48/96 kHz with the exact bilinear
prewarp used by trapezoidal C3. In addition to the clone-drawn R6/C3 and
illustrative beta/VTO, a deliberately perturbed R6/C3/beta/VTO fixture is
checked to reject accidental agreement at one tuned operating point.

The AC deck deliberately uses a three-point linear sweep spanning 1% above
each requested frequency and scores the *first* row only. During a real
`ngspice-42` smoke test, a two-point `ac lin 2` deck produced only **one**
`wrdata` row; it was replaced with three AC points. The parser now requires
three ordered frequencies and precisely three numeric columns
(`frequency`, `real(H)`, `imag(H)`) per row. `wr_singlescale` and 15-digit
export precision are explicitly requested to keep the table unambiguous.

```bash
# Does not require ngspice; prints a concrete independent netlist.
python3 analysis/spice_reference/verify_jfet_source.py --print-netlist

# Requires ngspice; exit code 2 if absent, 1 on simulator/error mismatch,
# and 0 only after all primitive simulations match their circuit oracle.
python3 analysis/spice_reference/verify_jfet_source.py
```

The standalone fail-closed contract tests cover parser behavior, J201 bias,
component provenance, netlist structure, frequency behavior and missing-ngspice
exit status. They run in the existing `analysis/test_*.py` CI discovery.

The same ngspice-42 audit exposed a pre-existing failure in the **required**
`verify_coupled_stage.py` check: its `ac lin 2` netlist also yielded a single
data row. That deck now uses a three-point AC sweep and strict table decoding.
All **27 native ngspice coupled-stage comparisons** (drawn 48/96 kHz and
perturbed 96 kHz) passed after the repair, with maximum magnitude error
`8.679e-14 dB` and phase error `2.103e-13°`. The new
`analysis/test_spice_coupled_contract.py` checks malformed or incomplete AC
tables, netlist structure, and missing-simulator behavior. These verify only
the DIY clone-derived idealized circuit, **not** retail pedal parity.

## Limits

- The full Q1/Q2 drain path is **not** represented; drain is forced to an ideal
  +9 V source, and the Q2 active load and treble-network node are absent.
- The J201 parameters are *typical illustrative values*, not measured from
  the user's pedal or authenticated against a production PCB.
- Small-signal AC and DC verification do not establish large-signal overload,
  drain/source compliance, thermal behavior or plugin sound quality.
- A green SPICE result shows an independently solved mathematical subcircuit,
  **not verified retail B7K Ultra parity**.
