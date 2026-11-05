# Round 4 — circuit-consistency and evidence-boundary audit

_Date: 2026-11-05. Scope: independent 12-agent engineering review. **Retail Darkglass B7K Ultra parity remains UNVERIFIED.**_

## Implemented in this continuation

- Added a [third Black Mirror VII building-doc revision](https://pcbguitarmania.com/wp-content/uploads/2022/07/Black-Mirror-VII-1.6v-Building-Docs.pdf) to the source ledger. Its BOM agrees with version 1.2v that IC6 is TL074ACN, while version 1.1v lists TL072ACP; version 1.6v explicitly uses an unbuffered CD4049UBE at IC3. These are descriptions of an **independent DIY clone**, not Darkglass factory drawings.
- `SourceLoadingTest` now compares **complex magnitude and phase** with the independent circuit-impedance oracle at the existing 32 source/frequency/sample-rate cases; fitted plugin loading remains untouched.
- Capture comparison refuses `take.output=xlr` (and any non-`quarter_inch` port) because `MasterOut` models the quarter-inch analog jack, not a balanced DI driver. The capture **validator** may still preserve either port for future research.
- Capture comparator includes full-take tone stationarity checks before treating the first ~3 seconds of harmonic analysis as a stable single-tone measurement. It also retains earlier ADC clipping, late-sweep Welch-H1, and input-voltage attribution gates.
- CI's capture job now installs `ngspice` and runs `analysis/spice_reference/verify_coupled_stage.py` rather than checking only that the text netlist can be emitted. That tool checks **27** separate AC magnitude/phase points (drawn 48/96 kHz and a component perturbation).
- `JfetStage.h` comments now identify old H2/H3 numerical targets as unarchived composite fits, not evidence that the actual transistor produced a uniquely known harmonic phase.

## What was **actually executed**

| Check | Evidence | Status |
|---|---|---|
| C++17 standalone SallenKeyLPFTest | Direct compiled/linked executable; independently derived rail-feedback KCL cases 44.1–192 kHz | **PASS** |
| C++17 standalone RecoveryBridgedTTest | Direct executable; embedded IC4_B nonlinear-feedback oracle | **PASS** |
| C++17 standalone TrebleAttackTest | Direct executable; Q2/C4 magnitude and phase at 100, 1000 and 10000 Hz | **PASS** |
| C++17 standalone MasterOutTest | Direct executable; finite receiver-load transfer against independent analog oracle | **PASS** |
| Python comparator (before later whole-tone safeguard) | 12 selected in-memory synthetic tests, including XLR refusal | **PASS** |
| Latest whole-tone safeguard and added test | Source committed; expanded execution environment startup interrupted | **NOT RUN** |
| SourceLoadingTest's **new phase** assertions | Source committed; runtime requires chowdsp_wdf and complete build | **NOT RUN** |
| `ngspice` CI job | Workflow committed, but no workflow run shown by the GitHub API at audit time | **NOT RUN** |
| Whole CTest (29 declared entries), JUCE Windows/macOS/Linux builds, independent retail WAV A/B | No complete run or authenticated hardware fixture available | **NOT RUN** |

The C++ executions were performed from exact GitHub source using anonymous memory-backed Linux binaries, without altering repository files. The isolated PASS results do **not** imply the full plugin build is green.

## Key discovered but **unfixed** physics gaps

| Stage | Independent issue | Why it is not yet enabled as a new production model |
|---|---|---|
| J201/Q1 source feedback | Current nonlinear map is applied after a *linearized* source-degeneration stage; actual Q1/source R6–C3 and drain compliance are coupled nonlinear equations | Missing device/revision captures and Q1/Q2 stage references; typical J201 parameters span large production tolerances |
| CMOS D1/D2 clamps | `Clipper` solves node W without diode currents, then hard-clamps; under a synthetic -8 V impulse the resulting nodal residual was ~354.5 µA | A diode-current-coupled Newton solve is feasible, but its `Is` and ideality factor are unmeasured, and a true B7K transistor/rail topology is not authenticated |
| Baxandall at output saturation | Rail limiting happens after ideal-summing-node state updates, violating passive feedback KCL; a +5 V synthetic pulse showed a **121 mV** difference on release in an independently solved passive network | Needs opt-in rail-aware passive-state solver with 7/8-node regression and calibrated op-amp dynamics |
| Mid-band peak EQ at saturation | Same feedback issue, including C31-on path; a +5 V synthetic pulse showed a **~0.595 V** release difference relative to a finite-gain passive solve | Needs independent passive MNA oracle at pot extremes and a documented, bounded op-amp model |
| Full XLR output | Official Ultra features a balanced DI; the plugin currently models a single quarter-inch output only | Balanced amplifier/transformer, loading, ground lift, digital cab-sim paths and retail revision are not authenticated |
| Manufacturer input loading | The [official B7K Ultra manual](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual) states nominal **1 MΩ** input, whereas Black Mirror VII's R1=R2=1 MΩ/C1=100 nF circuit approaches **500 kΩ** at 1 kHz | A nominal spec cannot identify exact retail topology; requires frequency-dependent input impedance measurement on the target unit |
| Clone-derived mid selector modes | The schematic preset uses identical capacitances in multiple selector positions; the Ultra's published six mid-frequency options are engineered user-facing behavior, not reconstructed clone wiring | Clone topology has fewer/different switch options; blindly inventing components would make source claims worse |

The 1.6v source lists IC3 CD4049UBE, but no identified silicon die transfer curve. The [1N4148 manufacturer datasheet](https://www.vishay.com/docs/81857/1n4148.pdf) does not uniquely identify the Shockley parameters of a physical diode. [J201 device specifications](https://central.aeminc.com/datasheets/J201.PDF) describe part-to-part spread, not the precise installed devices. The [TI TL072 datasheet](https://www.ti.com/lit/ds/symlink/tl072.pdf) constrains common-mode voltage and slew/GBW under its stated test conditions; these cannot be treated as a static ideal 9 V op-amp.

## Hardware-parity acceptance gate

A claim of 1:1 retail parity remains **blocked** until the following are supplied and independently checked:

1. Identify the **specific retail pedal version/revision** and hardware serial, supply voltage, controls, and output-port configuration.
2. Acquire authentic dry (measured at **loaded pedal input**), quarter-inch output, and optionally XLR output WAVs with signed hashes, calibrated volts/FS, shared clocks, controlled source/load impedances, calibrated fixture latency and fully documented knob/switch positions.
3. Reproduce response over low/high input levels, all control positions, attack/recovery, harmonics, noise floor, transients and separate output ports; compare error bands and independent repeat captures across multiple physical units.
4. Run and publish complete CTest, `ngspice` comparisons and cross-platform plugin CI for a specific commit, plus a blinded audio A/B. Do not reuse the pre-Round-3 numerical residuals as revalidated results.

**No raw, authenticated retail B7K Ultra reference captures are committed.** This round makes the clone-derived model and diagnostics more rigorous, but it does not—and cannot—turn a third-party schematic into an authenticated Darkglass production circuit.
