# Hypothetical Q1 drain rail diagnostic (research only)

`JfetTrebleRailDiagnostic` runs the **existing** `JfetStage::processCoupled` and `TrebleAttack` paths without JUCE and probes the incremental Q1 drain node. It maps this to a *hypothetical* absolute drain voltage using `Vdrain = 4.5 V + Vdrain,incremental`, then counts samples outside `[0 V, 9 V]` for three attack modes, four amplitudes and 44.1/48/96/192 kHz. The input is bounded for each amplitude and combines 197 Hz, 1319 Hz and sparse impulses. The executable separately asserts finite values, coupled-port agreement and small source/drain/port KCL residuals. Reported rail excursions **do not change exit status** and the target is deliberately **not registered with CTest**.

The assumed **9 V supply** and **4.5 V quiescent Q1 drain DC bias** are illustrative only. Actual drain bias, rail headroom, nonlinear clipping, downstream loading and a retail pedal's node waveform have **not** been measured or established. In particular, an incremental small-signal model need not enforce absolute transistor supply limits. Exceeding the mapped hypothetical rails is a **physical-plausibility research red flag**, neither a retail measurement nor proof that hardware would produce that voltage or that this simulation has a validated DC operating point.

Run from the repository root:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic tests/JfetTrebleRailDiagnostic.cpp -o /tmp/JfetTrebleRailDiagnostic
/tmp/JfetTrebleRailDiagnostic
```

Or build the standalone target `JfetTrebleRailDiagnostic` with CMake. A nonzero status means a numerical/finite/KCL invariant failed, not that a hypothetical rail was exceeded.
