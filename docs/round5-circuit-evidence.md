# Round 5 — circuit-equation fidelity, independent oracles, and parity limits

**Status: numerical consistency improved; retail B7K Ultra parity NOT established.**

This round was an engineering challenge to the existing circuit claims. It changes
one objectively wrong *offline* diagnostic and introduces five deliberately
**default-disabled** circuit-research options. Fitted production DSP and the
user-visible plugin parameters retain their existing defaults.

## Concrete corrections

| Block / API | Defect or approximation corrected | Independent check | Qualification |
| --- | --- | --- | --- |
| `InputBufferSourceLoadProbe` | Finite-source offline WDF output inverted by 180° even though magnitude passed | 32 magnitude/phase checks; 36 independent time-domain source-resistance/rate cases with worst difference 6.11e-15 V; 3 ideal-source identity cases | Diagnostic only; normal input audio path remains unchanged |
| `Clipper::setDiodeCurrentModel(true)` | Legacy hard-limited node *after* KCL solved without D1/D2 currents | Independent scalar bisection with trapezoidal histories (18,000 samples), plus 486,000 adversarial samples across nine diode-parameter configurations and 12,000 physical-VTC samples; worst instantaneous KCL residual 2.36e-13 A in the parameter grid | Nominal Shockley Is=5 nA, nVT=52 mV, +rail=9 V are illustrative |
| `JfetStage::setExactFiniteDrainConductance(true)` | Approximate finite-`r_o` source feedback transfer and output impedance | Complex-frequency magnitude/phase and output-Z oracle | Small-signal correction only |
| `JfetStage::setPhysicalSourceFeedbackModel(true)` | Memoryless nonlinear J201 shaper following a *linearized* R6/C3 shelf | 24,000-sample independently solved implicit source-KCL oracle, plus default-disabled 15,000-sample bit-exact regression | Not a coupled drain/Q2 solve; J201 SPICE parameters are third-party exemplars |
| `MidBand::setRailAwarePassiveFeedback(true)` | Feedback capacitors historically evolved against an unclipped ideal output even when the rail clamp limited it | 640 configurations × 70 samples; worst independent nodal difference 2.23e-10 V | Uses assumed static open-loop gain; no GBW/slew/overload recovery |
| `Baxandall::setRailAwarePassiveFeedback(true)` | Same unclipped capacitor-state issue in the 7- and 8-node bass/treble circuit | 18 changing-control configurations; worst branch-stamped nodal difference 1.634e-12 V | RailClamp is a fitted static curve, not a measured TL07x transistor model |

The (486{,}000)-sample clipper stress oracle compares every *instantaneous*
equation using actual prior DSP state. Residue ADAA is an intentionally
high-sensitivity experimental mode: two separately rounded free-running
trajectories can diverge even when each solves its own KCL. Such divergence is
not evidence of a failed one-step KCL solver.

The parameter grid explicitly rejects positive rails below the active inverter
trip point plus 0.5 V. Switching to a physical-VTC table can change the trip
point; incompatible preconfigured diode-current modes automatically disengage.
This is numerical and topology sanity checking, not a diode-data-sheet fit.

These counts come from compiled native C++17 standalone tests in isolated
checkouts. The 18,000-sample clipper oracle also checks that a separate,
free-running reference tracks under ordinary drive levels. Older 24 standalone
tests ran green in the independent full-chain audit; the repository's 28 Python
analysis tests also passed. CMake/CTest and ngspice end-to-end jobs **were not**
executed locally because the corresponding executables were not installed.
The new tests are registered in `CMakeLists.txt`; GitHub CI must still run
them on its configured runners.

## Numerical and physical limitations

1. This is not an authenticated manufacturing schematic for a Darkglass B7K
   Ultra. The available public node values are traced from the Black Mirror VII
   third-party clone; manufacturer documentation covers controls and specifications.
2. The manufacturer describes **B7K Ultra nominal input impedance as 1 MΩ**,
   versus **500 kΩ for the non-Ultra B7K**. The clone's approximately 500 kΩ
   input loading should not be represented as confirmation of Ultra input parity.
3. One fitted VTC and an idealized Shockley diode equation omit temperature,
   measured diode I–V scatter, transistor parasitics and dynamic op-amp behavior.
   In particular, nonlinear J201 source feedback remains separate from drain
   compliance and the Q2 active-load nodal dynamics.
4. A compensated playback path is not an XLR output-circuit model. Balanced
   transformer/cabinet simulation, if present on a hardware revision, remains
   outside this unverified signal model.
5. The solver options are intentionally **not activated** in
   `FitParams::schematic()` or `FitParams::shipped()`. Moving them to the
   audible default requires authenticated topology and actual calibration data.

## What would constitute progress toward retail parity

Follow [the qualification protocol](retail-parity-qualification.md). At a minimum:
one identified retail hardware revision, non-destructive electrical input/output
checks; controlled, level-aligned WAV reamping across all switches and pot
extremes; held-out captures from multiple actual units; comparable FFT,
phase and distortion error bounds; noise, frequency, power and thermal checks;
and source-linked hardware acquisition metadata. A Black Mirror DIY schematic,
fitted plugin response, and simulated SPICE oracle alone cannot satisfy those
requirements.
