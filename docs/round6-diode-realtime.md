# Round 6 — Shockley clipper solve: numerical and real-time audit

**Research-only option; no claim of retail Darkglass circuit parity.**

The opt-in D1/D2 diode current model has a strictly decreasing node-W KCL
residual. A bracketed Newton/bisection solve is still required because normal
audio drive can make the diode differential conductance exponentially steep.
Do **not** reduce its 32-iteration safety cap on the strength of one benchmark.

## Change

The original current-mode root solver computed both the KCL residual and its
derivative separately on every Newton step, evaluating two `expm1()` and two
`exp()` calls. The new `solveDiodeFAndFp()` shares a single pair of
`expm1()` calls; the derivative for each untruncated diode branch follows
from the identity `exp(z) = expm1(z) + 1`. Above the existing +50 exponent
limit, the residual is numerically flat for that diode and its derivative is
set to zero. This keeps the old numerical protection consistent.

The legacy (default-disabled) solver retains its original residual, slope and
bracketing path. No physical component parameters or user-visible pedal
settings were changed.

## Independent verification

| Validation | Outcome |
| --- | --- |
| Existing `ClipperTest` | PASS |
| `ClipperDiodeTest`: independent stateful bisection oracle | 18,000 PASS; maximum node difference 1.25e-9 V |
| `ClipperDiodeAdversarialTest` | **607,500 PASS**, including 384 kHz; worst independent instantaneous KCL residual 9.38e-13 A |
| `ClipperDiodePhysicalAdversarialTest` | 12,000 PASS; worst KCL residual 6.83e-15 A |
| `ClipperDiodeRealtimeTest` | 100,000 samples; zero scalar/array `operator new` allocations under model / GRUNT / ADAA switching |
| Legacy opt-out bit-exact test | PASS |

The adversarial reference evaluates sample-by-sample KCL with aligned
capacitor history, so its results are not contaminated by exponential
divergence of two independently rounded ADAA Residue trajectories.

## CPU measurement (one host; never a release-wide guarantee)

Compiled twice on the same execution host with GCC C++17 `-O3 -march=native`,
250,000 samples per condition, three repeated timings (median used), spanning
44.1 / 48 / 96 / 192 / 384 kHz and ADAA Off / Full / Residue. A second
interleaved run of both builds was performed; the results below average the
two medians for each case:

| Mode | Prior mean | Fused mean | Change |
| --- | ---: | ---: | ---: |
| Research diode-current model, 15 conditions | 327.82 ns/sample | 280.66 ns/sample | **~14.4% less CPU time** (1.16× throughput) |
| Default legacy model, same conditions | 138.78 ns/sample | 140.27 ns/sample | within host timing noise |

Repeat the manual probe with:

```bash
cmake --build build --target ClipperDiodePerf --config Release
./build/ClipperDiodePerf
```

Executable location will vary by generator/OS; there is **no CTest timing
pass/fail gate**, because shared CI runner clock speeds are not controlled.

The options are still disabled by default. The production VTC, diode
`I_s` / `nV_T`, real amplifier output and downstream load are not
authenticated retail-pedal measurements. This pass improves numerical
consistency and CPU cost, not verified musical similarity or 1:1 circuitry.
