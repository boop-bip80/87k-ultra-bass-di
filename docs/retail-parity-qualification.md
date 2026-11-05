# Retail B7K Ultra parity qualification — proposed gates, no certificate

**Status on 2026-11-05: UNVERIFIED.** There are no authenticated B7K Ultra production schematics or reproducible, physically sourced real-pedal capture datasets in this repository. This document specifies *tests that would have to pass*; the figures below are proposed engineering tolerances, **not manufacturer's published tolerances** and not current measurements. A passing software unit test or a good fit to the Black Mirror VII clone drawing cannot turn any hardware row green.

Source baseline: the [Darkglass Ultra manufacturer manual](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual) specifies a **1 MΩ** nominal input, whereas the [distinct non-Ultra B7K manual](https://www.darkglass.com/en-eu/pages/microtubes-b7k-manual) specifies **500 kΩ**. [PCB Guitar Mania's Black Mirror VII 1.6v build PDF](https://pcbguitarmania.com/wp-content/uploads/2022/07/Black-Mirror-VII-1.6v-Building-Docs.pdf) specifies R1=R2=1 MΩ in its clone input circuit, giving an approximately **500 kΩ** audio-band input in the project's published R/C topology. These are different products and different levels of evidence. See the [source-by-source ledger](schematic-provenance.md).

## Three different claims must not be conflated

| Claim | Required external reference | Present evidence | Present status |
| --- | --- | --- | --- |
| **Algorithmic consistency**: the solver follows its stated equations | An independent analytical/nodal/SPICE oracle for the **same documented assumptions** | Some independent stage oracles and partial ngspice comparisons | **Partial**, per test; not physical evidence |
| **Audio/electrical behavior parity**: plugin input/output matches an identified retail pedal within a predeclared envelope | Calibrated, independently reproducible real-unit measurements across controls, modes and levels | Historical calibration statements, but not their raw recordings or provenance | **UNVERIFIED** |
| **Physical-circuit parity**: the DSP captures the actual retail topology and device laws | An authenticated manufacturer circuit drawing or independently documented component identification plus PCB traces, device measurements and circuit-wide validation | A third-party **Black Mirror VII** DIY schematic, not a traced retail pedal | **UNVERIFIED** |

Even very good audio agreement would establish only performance in the tested region. It would **not** prove that the model reproduces the retail circuit's internal components, bias topology or branch currents.

## Gate order and hard stops

1. **P0 — Identity and provenance.** Record exact pedal name, Ultra generation/revision, serial and distinguishing controls, photos and acquisition chain, power rail under load, temperature, a signed acquisition log with UTC time, SHA-256 files and independent witness or reproducible bench log. Do not label a Black Mirror board or an ordinary B7K as an Ultra. If this gate is missing, all retail results remain `UNVERIFIED`.
2. **P1 — Fixture calibration.** Measure interface loopback, ADC/DAC voltage calibration, gain and polarity, probe impedance, common sample clock, latency uncertainty, source and output load, phantom-power isolation for XLR, and the *loaded voltage at the actual pedal input*. Require two independent fixture calibrations bracketing the session. A digital source WAV alone does not establish pedal input volts.
3. **P2 — Raw evidence integrity.** Keep playback stimulus, physically recorded loaded input and raw physical output as three distinct files per take; record per-file SHA-256 hashes and complete `schema_version: 2` metadata. Run `analysis/validate_hardware_capture.py` and preserve calibration logs. A complete manifest only reaches `REVIEW_REQUIRED`, never `PASS`; hashes are not hardware-authenticity signatures.
4. **P3 — Measurement repeatability.** Repeat each reference take at least three times, rotate order of a subset of knob settings, power-cycle between sessions, and sample at least two independently identified physical units of the *same stated revision*. Estimate within-unit and between-unit variation with confidence intervals. With no second unit, conclusions must say **single-unit behavior only**.
5. **P4 — Registered evaluation.** Before tuning to final captures, freeze the plugin commit, presets, level calibration, matching method and the tolerance table below; partition pedal units/settings into development and held-out evaluation sets. Do not move thresholds or gain-match independently after seeing mismatches. Publish **both** successes and failed cases.
6. **P5 — Circuit authentication (separate).** To claim *circuit* parity beyond behavior, supply a retail-origin circuit source or independent PCB tracing with bill of materials, component markings, switch connectivity, bias voltages, impedance loading, device I–V curves, tolerances and parasitics. Show coupled-node agreement with independent SPICE and physical node-probe measurements. A third-party schematic cannot satisfy this gate.

The existing [reference measurement protocol](reference-measurement-protocol.md) provides the concrete capture format and strict validation rules. The analysis comparator currently grades **quarter-inch output only**, not XLR or headphones.

## Proposed engineering acceptance envelopes (subject to fixture noise floor)

These limits are deliberately explicit so a future audit can fail a model. They are **project targets**, not objectively established retail tolerances. Every quantitative judgment must also report the calibrated measurement uncertainty and the observed spread of actual physical units; if physical-unit variability exceeds a tolerance, report both the nominal and variability-adjusted comparisons rather than silently loosening the gate.

| Property | Proposed held-out comparison | Required excitation/coverage | Important interpretation |
| --- | --- | --- | --- |
| Input impedance (Z_\mathrm{in}(f)) | Magnitude within **±10%** and phase within **±5°** of *measured* target over 30 Hz–10 kHz, or label non-conforming bands | 100 Ω / 1 kΩ resistive sources plus at least two characterized inductive pickup/cable equivalents | The Ultra **1 MΩ** nominal manual spec is not a full impedance trace; Black Mirror VII tends toward **500 kΩ** at audio frequencies; model/source mismatch must be reported |
| Small-signal transfer (quarter-inch) | Amplitude error RMS **≤0.5 dB**, max **≤1.5 dB** and unwrapped phase difference **≤5°** wherever output coherence ≥0.95, over 40 Hz–10 kHz | Two low-drive levels; clean/OD/bypass; neutral plus every selector position, five points per each of eight controls | Do not discard deep notches or phase unwrap failures to improve a score; tabulate excluded bins |
| Headroom and gain tracking | Clipping-onset input level within **1 dB**, bypass-relative gain within **0.5 dB** | Calibrated 1 kHz level steps, minimum/maximum Drive, Blend, Level and Master; repeat on bass-band tones | Never independently normalize the outputs before comparison |
| Nonlinear spectra | Individual H2–H6 level difference **≤2 dB** when **both** are above −55 dBc and measurable; THD curve deviation **≤2 dB** in its reported domain | Stable sine tones at 80, 400, 1000 and 3000 Hz, multiple physical input voltages and drive settings | Exclude harmonics above Nyquist and report noise-limited bins, not a misleading finite value |
| Intermodulation | Sideband-level differences **≤2 dB** for components above the calibrated noise floor | SMPTE 60 Hz + 7 kHz, 220 Hz + 660 Hz and bass-diatonic dyads at multiple levels | A matched 1 kHz tone does not prove nonlinear agreement |
| Transients, hysteresis and recovery | Matched event time within **1 ms** and response-envelope deviation **≤1 dB** over a stated 10–100 ms window | Level steps, short pulses and real DI bass phrases with **measured loaded input** | State history matters; test after long loud passages and after silence |
| Output loading and noise | Output impedance within **±10%** and broadband noise within **3 dB** in declared bandwidth | At least two 1/4-inch loads and measured output noise across control extremes | Device noise must be measured in real units; plugin silence is not hardware noise parity |
| Balanced XLR / cab simulation | **Not currently gradable** by repository comparator; build and validate separate electrical DI/driver/IR paths first | Balanced pin polarity, differential and common-mode gain, cable/ground-lift, output loads and cab-sim on/off | No quarter-inch match may be advertised as XLR parity |

All thresholds are **per condition**, not only an aggregate over easy presets. State the number of evaluated conditions, missing conditions, frequency bins outside the SNR/coherence criteria, worst-case failed setting and uncertainty intervals. A single overall “98% accurate” percentage is not a scientifically meaningful result.

### Control matrix and held-out tests

- One-factor sweep for all eight Ultra rotary controls at normalized positions `0, 0.25, 0.5, 0.75, 1` while holding other controls at a declared neutral setting. Mechanical angles should be recorded, not inferred from equal-valued tapers.
- Every actual physical Attack, Grunt and mid-frequency selector state, including the **250 / 500 / 1000 Hz** Lo-Mid and **750 / 1500 / 3000 Hz** Hi-Mid alternatives published by Darkglass; do not transplant clone-only capacitor modification labels to these positions.
- Interaction subsets on held-out tests: Drive × Blend × Level, both mid controls × their frequency switches, Bass × Treble, Master × clean/OD output paths, source impedance × input tone level; sample the full cross-product only if practical, then explicitly report any untested combinations.
- Independent hardware unit, input excitations and presets withheld from fitting. Report held-out errors separately from development-set errors.

## Current blockers and how to close them

| Blocker | Concrete next evidence | Can code-only work solve it? |
| --- | --- | --- |
| **No authenticated retail Ultra topology** | Permissioned original schematic or photographed/traced retail board with verified component references and node connectivity | **No**; DIY clone SPICE is valuable but not retail tracing |
| **No raw pedal captures/provenance** | Calibrated triple-WAV sessions, full manifest, session logs and physical unit identity | **No**; synthetic fixture test WAVs test tooling only |
| **Clone vs retail input loading discrepancy** | Independently sweep actual Ultra (Z_\mathrm{in}(f)) and compare with clone calculation | Partly; software can calculate the clone, not measure hardware |
| **Fitted DSP and schematic DSP disagree** | Keep separately labeled defaults, refit only against provenance-verified captures and held-out set | **Yes**, once external reference exists |
| **Incomplete nonlinear/rail models** | Independent KCL/SPICE checks of Q1, D1/D2, CD4049 and saturated EQ feedback, then physical I–V/circuit waveform measurements | **Partly**; equations can be verified, component parameters still need hardware |
| **XLR/cab-sim features not electrically validated** | Dedicated balanced-driver, loads, IR and phantom/ground-lift measurement fixture | **Partly**, but DI topology and physical reference still required |

**Permitted release language until all gates are supported:** “Black Mirror VII clone-informed bass-preamp plugin; optional circuit-equation research modes; retail B7K Ultra parity unverified.” Never describe a successful analytic/SPICE test as a measured percent match to a commercial unit.
