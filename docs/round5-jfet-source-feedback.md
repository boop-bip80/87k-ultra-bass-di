# Round 5 J201 source-feedback research branch

**Status: software-level circuit consistency demonstrated; retail Darkglass parity UNVERIFIED.**
Both new DSP modes default to OFF and do not alter `FitParams::schematic()` or
the currently fitted plugin preset. The circuit evidence refers to the public
**Black Mirror VII clone**, not an authenticated retail manufacturing drawing.

## Opt-in exact finite-drain-conductance small-signal model

`JfetStage::setExactFiniteDrainConductance(true)` replaces the approximate
source-degeneration transfer with the finite-Q1-conductance relations:

```
Zs(s)       = R6 / (1 + s R6 C3)
gds         = 1 / ro
Gm(s)       = gm / (1 + (gm + gds) Zs(s))
Zout,Q1(s)  = ro + (1 + gm ro) Zs(s)
Zout(s)     = Zout,Q1(s) || Rq2
```

The old approximation omitted `gds` in the feedback denominator and omitted
one extra `Zs` from Q1 output resistance. The correction is modest at the
nominal parameters; it does not assert the exact *measured* J201 output
resistance, which varies by device and bias. `SourceZ` continues to hand the
Thevenin network to `TrebleAttack`, unchanged in interface.

## Opt-in nonlinear source-node solve

`JfetStage::setPhysicalSourceFeedbackModel(true, beta, vto)` activates a
stateful, nonlinear **Q1 source-only** model for R6=3.3 kΩ and C3=220 nF.
The illustrative defaults (`beta=1.031 mA/V²`, `vto=-0.718 V`) come from a
publicly available *third-party SPICE model* cited in
`docs/nonlinear-component-modeling.md`, not a measurement of installed silicon.

The DC self-bias solves:

```
Vs,Q = R6 * beta * (abs(VTO) - Vs,Q)^2
Id,Q = Vs,Q / R6
Vov  = abs(VTO) - Vs,Q
```

For incremental gate potential `vg` and source potential `vs`, the source
feedback is implicit:

```
Id,AC(vg,vs) = beta * max(0, Vov + vg - vs)^2 - Id,Q
gc3         = 2 fs C3
F(vs)       = (1/R6 + gc3) vs - ieqC3 - Id,AC(vg,vs) = 0
ieqC3,next  = 2 gc3 vs - ieqC3
```

`F` is strictly increasing (`F' >= 1/R6 + gc3`). A guaranteed bracket and
48 bisections solve it deterministically; the return is the **inverting
Norton AC current** `-Id,AC`. No pre-shaper linear shelf is used in this
mode. The original fitted shaper and its ADAA remain untouched when OFF.

### What this model specifically does **not** include

- Source and drain potential coupling due to J201 channel-length modulation,
  body/substrate, junction capacitances, gate conduction or device spread.
- Q2 active-load saturation or finite rail headroom as a jointly solved
  nonlinear drain/source network.
- The instantaneous treble-ladder loading's feedback into the Q1 drain.
- Temperature, op-amp or board parasitics, or verified physical-pedal audio.

Thus the research mode is a useful source-node physics improvement but **not
full Q1/Q2 SPICE parity**. The fitted preset must not silently activate it.

## Reproducible checks

Build and run `tests/JfetPhysicalFeedbackTest.cpp` as a standalone C++17 test
or through CTest. The independent oracle uses an analytical *quadratic* DC
self-bias formula and a wide-bracket **110-bisection** nodal solver, not the
production solver's derivative bound. Verified:

- 15,000 samples: default and explicitly OFF output bitwise identical.
- Five complex-frequency cases: exact `Zout` expression matches `SourceZ`.
- Four digital-BLT frequency cases: sample-domain Norton-current magnitude
  **and phase** agree with the independent finite-gds transfer.
- 12 grids (four sample rates times three `beta` values), 2,000 samples each,
  including rail-scale impulses, asymmetric steps and sinusoids: independent
  source-KCL current and voltage agree within 2e-13 A / 2e-11 V.

These are **mathematical oracles**, not comparisons with a physical pedal.
