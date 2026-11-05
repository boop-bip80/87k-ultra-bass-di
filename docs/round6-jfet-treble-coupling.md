# Round 6 research: coupled J201 source/drain and loaded treble port

**Scope:** opt-in standalone circuit experiment, **not a shipped-plugin change**
and **not a claim of circuit parity with any physical Darkglass pedal**.

The previous physical-source J201 option solved the nonlinear R6/C3 source
voltage but treated the drain as an ideal incremental current source. The
TrebleAttack stage separately stamped a *linearized* Q1 output-impedance
network, `ro + (Rp || Cp)`. Both parts worked individually; however, nonlinear
Q1 source current did not depend on instantaneous drain potential.

## Current-sample coupled equation

At the start of a sample, the stage's *existing* precomputed 8-node ATTACK
matrix and prior capacitor/inductor histories define a Thevenin drain port:

```text
vd = Voc - Rth * Id
```

The passive network includes R7/R8/R11, C5/C9/C6/C7/C8, the Q2 `Rq2`
conductance, the C4 gate bootstrap, and the optional Q2 R-L active-load
equivalent. To avoid counting Q1 twice, opt-in drain-port mode **excludes** the
old, separately linearized Q1 `ro/Rp/Cp` branch from this matrix. The tiny
auxiliary node H is tied to its zero reference to keep the matrix invertible.

For the J201's incremental source and drain, the assumed device equation is:

```text
Gs  = 1/R6 + 2*fs*C3
vs  = (Id + Ieq3) / Gs
Id  = beta * [max(0, Vov + vg - vs)^2 - Vov^2] + gds * (vd - vs)
gds = 1/ro
```

Eliminating vs and vd gives one scalar equation:

```text
F(Id) = Id - beta*[max(0, Vov + vg - vs)^2 - Vov^2]
           - gds*(Voc - Rth*Id - vs) = 0
```

For positive `Rth`, `G_s`, `beta`, and `r_o`, its slope is
`1 + 2*beta*max(...)/G_s + gds*(Rth+1/G_s) >= 1`. The root is
unique, can be bracketed from `F(previous Id)` and that derivative lower bound, and is
found through safeguarded Newton/bisection without runtime allocation. A
warm-started 24-iteration fast budget takes an observed mean **2.64 iterations**,
maximum **5** over the ordinary regression grid. If an extreme nonlinear
transient exhausts that budget, the existing proven bracket is finalized with
up to 64 additional bisections. That fixed-cost fallback prevents a partially
converged drain/source current from being passed to the passive network.

The current is then injected into the unchanged passive treble network, whose
capacitor histories advance **once**. The J201's physical C3 history also
advances once with the same solved current. Nothing in the fitted, default
processing chain selects these interfaces.

## How to exercise the test API

```cpp
JfetStage q1;
TrebleAttack attack;
q1.prepare(sampleRate);
attack.prepare(sampleRate);
q1.setPhysicalSourceFeedbackModel(true);    // research beta/VTO
attack.setCoupledJfetDrainPort(true);       // removes legacy Q1 ro/Rp/Cp

// Each sample, in this order:
const auto port = attack.previewCoupledDrainPort();
const double i = q1.processCoupled(x, port.openCircuitVolts,
                                     port.theveninOhms);
const double voltageAtDrive = attack.process(i);
```

The prepared attack network and these calls must remain paired. Do not
mix the new Q1 current with the old Q1 linearized drain branch.

## Verification performed

`JfetTrebleCoupledTest` independently reconstructs gate C2 and source C3
trapezoidal histories, J201 self-bias, and a **110-iteration scalar-bisection**
current equation. It also checks the Thevenin preview against two independently
advanced copies of the actual full 8-node passive solver using `0` and
`+1 uA` injection (not a self-referential algebra assertion).

- **43,200 sample comparisons PASS**: 44.1/48/96/192 kHz, three ATTACK
  positions, Q2 gate-cap disabled, Q2 gate-cap enabled, and Q2 C4+R-L active
  load enabled.
- **1,476 independent port-perturbation checks PASS**.
- Maximum incremental-current difference: **1.01e-16 A**.
- Maximum source-voltage difference: **3.4e-14 V**.
- Maximum drain-voltage difference: **6.11e-13 V**.
- Maximum independent transistor-equation residual: **1.92e-17 A**.
- Legacy `JfetStage::process` vs the new API's unenabled fallback:
  **15,000 bit-for-bit identical samples**.
- Existing JFET, treble, and physical-feedback tests still pass in strict
  C++17 `-Wall -Wextra -Werror` standalone builds.
- **98,304 additional stateful adversarial samples PASS** in
  `JfetTrebleCoupledStressTest`: 8/44.1/48/96/192/384 kHz, four Q2 modes,
  dynamically switching ATTACK and Q2 parameters, 60 V impulse excursions,
  and 1,200 independent affine-port perturbations. Worst normalized original
  KCL residual was below `6e-16`; fallback reached 64 total iterations.

An optional `JfetTrebleCoupledBenchmark.cpp` measured **62 ns/sample** for
the new coupled path at 192 kHz on this execution host versus **339 ns/sample**
for the older, independently implicit-source method (O3/native, 300k samples).
Those numbers are machine-specific throughput diagnostics, not portable
real-time guarantees or validated plugin-host CPU figures.

## Limitations and integration handoff

1. The incremental **finite `gds` is constant**, not a complete J201
   channel-length-modulation/triode/reverse-conduction model. Q1 drain voltage
   headroom relative to its bias point is not verified. **Important measured
   non-parity:** independent adversarial stateful tests observed **13.16 V
   incremental drain swing with only 0.5 V stress-scale input** and **39.25 V
   with 1 V stress scale** during ATTACK/Q2 switching. Such voltage excursions
   are impossible in the nominal 9 V-powered hardware even before accounting
   for DC bias. The stress solver is algebraically consistent but its output
   is *not physically supply-compliant*. A bounded rail-aware nonlinear
   J201/Q2 replacement and measured circuit values remain necessary.
2. Q2 is included through the current **linearized R-L and C4 models**,
   not as a second nonlinear J201 transistor with drain/source/gate KCL.
3. The implemented coupling is a **research C++ API**. `PedalChain` presently
   computes one JFET current and may crossfade two ATTACK states. Simply using
   this API in that path would be incorrect during a crossfade: each treble
   network has a different instantaneous Thevenin port. A follow-up integration
   needs paired JFET source histories for the shadow and live ATTACK arms or
   another explicitly validated transition strategy.
4. No manufacturer B7K Ultra schematic, physical J201 characterization, or
   reproducible retail reference WAV is supplied. Do not enable or advertise
   these physical parameters as calibrated, and do not report simulated
   internal-consistency error as hardware sound-match error.
