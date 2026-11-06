# Opt-in J201 Q2 source-follower DC bias oracle (research only)

`src/dsp/J201Q2ActiveLoad.h` computes **absolute** DC node voltages for the
*Black Mirror VII clone* J201 pair. It does not replace the runtime Q1 or treble
DSP models. Compile the standalone deterministic test with:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -I. tests/J201Q2ActiveLoadTest.cpp -o /tmp/j201-q2-test
/tmp/j201-q2-test
```

## DC network and KCL

The observed clone netlist has `J2 v9 q2g outg`: Q2 is a **source follower**
(drain = +9 V, source = `outg`), not a cascaded common-source amplifier. R9
(1 MΩ) and R10 (1 MΩ) bias Q2 gate at 4.5 V. Q1 gate sits at 0 V through R5;
its source goes to ground through R6 (3.3 kΩ), and its drain is `outg`.
At DC, all capacitors are open (including Q2's C4 gate/source feedback, and Q1
source bypass C3). The treble network loads `outg` through the series resistors
R7 (200 kΩ), R8 (470 kΩ), R11 (470 kΩ), yielding 1.14 MΩ to ground.

Let `Vs1` be Q1 source and `Vo` be Q1 drain/Q2 source; `Vg2 = 9*R10/(R9+R10)`.
The two simultaneous, finite-state Kirchhoff equations are:

```text
Id1(Vg=0, Vd=Vo, Vs=Vs1)       = Vs1/R6
Id2(Vg=Vg2, Vd=9, Vs=Vo)       = Id1 + Vo/(R7+R8+R11)
```

The solver nests monotonic 80-step bisections over `Vs1 ∈ [0,Vo]` and
`Vo ∈ [0,9]`, reports signed residuals in amperes, and validates the passive
network and JFET parameters. Device model: forward NJFET Level-1, `B=1`,
`lambda=0`, with `Vgst=Vgs−VTO`, `beta=Idss/VTO²`:

```text
Vgst <= 0 or Vds <= 0:  Id=0
0 < Vds < Vgst:          Id=beta*Vds*(2*Vgst−Vds)
Vds >= Vgst:             Id=beta*Vgst²
```

Independent reference: [ngspice NJF/PJF Level-1 model with Parker–Skellern
modification](https://nmg.gitlab.io/ngspice-manual/jfets/jfetmodels_njf_pjf/jfetlevel1modelwithparkerskellernmodification.html)
(for `B=1`, the unmodified triode/saturation equations above). J201 parameter
envelope is tested at `Idss` 0.2–1 mA and `Vgs(off)` −0.3 to −1.5 V; nominal
values 0.5 mA / −0.718 V are illustrative, not device-calibrated.

## Deliberate limits / integration contract

- `solveDc(Circuit, BiasPoint&)` is **opt-in** and has no side effects; all
  voltages are absolute, currents flow drain-to-source. Input failures return
  false without overwriting output. Include the header from a future processor
  only after mapping absolute runtime voltages onto the solved operating point.
- The current `JfetStage` and `TrebleAttack` DSP operate on **incremental**
  currents/voltages. A clamp of their incremental drain voltage to 0–9 V would
  be physically false; this module intentionally does not do so. A production
  active-load update needs a coupled dynamic node solver with absolute Q1/Q2
  terminal voltage state, capacitive KCL, and stable timestep/oversampling.
- Gate current, gate diode forward conduction, channel-length modulation,
  capacitance, temperature, reverse operation, bias resistor tolerances and
  uncertain device samples are outside this idealized DC model.
- `BiasPoint::q2GateSourceVolt = q2GateVolt - outputVolt`, and
  `q2GateForwardBiased` is true whenever that difference exceeds zero. In this
  case `solveDc` still reports the **mathematical channel-only KCL root**, but
  not a validated physical gate-bias operating point: its neglected gate-diode
  branch may carry current. An independent adversarial survey found this flag
  in 3 of 81 illustrative parameter combinations (worst +0.13727 V Q2 gate
  above source); the nominal combination is reverse-biased. The flag alone
  does not quantify the omitted gate current without a calibrated junction.
- The **clone** schematic/BOM revisions disagree in places and cannot be
  presented as an independently verified retail Black Mirror VII circuit.
  A retailer's internal voltages or charge pump cannot be inferred from it.
