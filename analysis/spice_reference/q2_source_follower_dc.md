# Independent Q1/Q2 DC reference — clone schematic only

`q2_source_follower_dc.cir` is an independent native ngspice-42 operating-point
fixture for the Black Mirror VII 1.6v clone schematic. It is **not** a
measurement or fidelity claim about a retail device. Q1 is the self-biased
J201 common-source stage, with R6 = 3.3 kΩ. Q2 is a **source follower**:
drain at 9 V, gate biased to half-supply through R9/R10 = 1 MΩ, and source at
Q1 drain / `outg`. R7 + R8 + R11 = 1.14 MΩ loads that source node to ground
at DC. Coupling capacitors are open circuits at DC.

Run the independent test:

```sh
python3 analysis/spice_reference/test_q2_source_follower_dc.py --ngspice /path/to/ngspice
```

For a portable ngspice binary with nonstandard shared libraries, add
`--lib-dir /path/to/library/directory`. Missing ngspice fails with exit 2.

| Node | Native ngspice-42 nominal | Simplified C++ Q1/Q2 oracle |
| --- | ---: | ---: |
| Q1 source `q1s` | 0.3836065 V | 0.375485448 V |
| Q2 gate `q2g` | 4.500003 V | 4.500000 V |
| Q2 source `outg` | 4.877378 V | 4.869116089 V |

The test permits **20 mV** between independent SPICE and simplified C++ DC
solutions because this native J201 model includes `LAMBDA=2m`, `RD=1`,
`RS=1`, and `IS=114.5f`, while the C++ square-law solver omits those terms.
Observed nominal differences are ~8.1–8.3 mV. This comparison verifies
topology, bias direction and reasonable agreement, not exact model identity,
device variation, transient dynamics, or the retail unit.
