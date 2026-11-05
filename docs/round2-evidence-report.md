# Engineering evidence ledger — second audit pass

**Date:** 2026-11-05  
**Target:** `boop-bip80/87k-ultra-bass-di` (Obsidian B7000)  
**Hardware parity verdict:** **NOT ESTABLISHED**

This report describes verified *software/model* behavior and the evidence still needed to assess the
physical Darkglass B7K Ultra. It does not assign a percentage or claim 1:1 sound/circuit equivalence.
In particular, an analytical oracle and the DSP may agree exactly and still both represent an
incorrect or incomplete physical circuit.

## Which circuit is actually represented?

The document used for drawn component values is [PCB Guitar Mania's Black Mirror VII 1.1v building
documentation](https://pcbguitarmania.com/wp-content/uploads/2021/07/Black-Mirror-VII-1v1-Building-Docs-18-10-2022.pdf),
an *independent DIY board* advertised as based on the B7K Ultra. Its seven rotary controls omit the
Ultra's Master control. It therefore is **not** an authenticated schematic of a retail Darkglass PCB.
The [manufacturer's B7K Ultra manual](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual)
provides control behavior, nominal EQ frequencies, nominal input/output impedance, and power
requirements, **not** a circuit netlist. Details: [schematic provenance](schematic-provenance.md).

The shipped plugin defaults to fitted constants and several empirical filters; the separately
selectable `FitParams::schematic()` model follows the clone drawing much more closely. The former
has no committed raw pedal WAVs to re-verify its historical fit. Neither mode has a reproducible
retail-B7K-Ultra validation set in this repository.

## Engineering findings and narrow regression evidence

| Area | Finding or change | Reproducible evidence | What remains unknown |
| --- | --- | --- | --- |
| J201 numerical model | Two mathematically avoidable floating-point cancellation points in the waveshaper antiderivatives were stabilized; no fitted transistor parameters changed. | `tests/JfetStageTest.cpp`: relative tiny-input core primitive error ~122% before versus ~2.69×10⁻¹⁶ after. At ordinary normalized inputs the core old/new difference was ≤4.44×10⁻¹⁶. | J201 large-signal physical transfer, actual device/part variation, bias conditions, even-order hardware harmonics. |
| Input loading | The clone input impedance is `R1 || (R2 + 1/(s*C1))`, with R1=R2=1 MΩ and C1=100 nF: ~1 MΩ at DC, ~500 kΩ in the audio band. An **offline-only**, optional finite-resistance source-loading diagnostic was added without changing the plugin's normal audio path. | `tests/SourceLoadingTest.cpp`: 32 analytical frequency-response comparisons, three bit-exact default-path comparisons; 100 kΩ source at 1 kHz gives ~−1.584 dB. | Real Ultra input impedance versus frequency; pickup/source reactance, load interaction with recording interface, finite input capacitance. Manufacturer's *nominal* 1 MΩ does not specify frequency. |
| Coupled bridged-T and Sallen-Key | Independent nodal elimination checks DSP magnitude and phase of the loaded clone stage; perturbing each of eight R/C parts produces the expected change. | `tests/SchematicOracleExtensionTest.cpp`: 27 frequency/circuit cases + eight separate branch-sensitivity tests, with observed residual below 10⁻⁶ dB and 10⁻⁶° in these isolated calculations. `analysis/spice_reference/verify_coupled_stage.py` can check the reference values and optionally compare against ngspice. | Physical-pedal components/topology; the new optional ngspice perturbation comparison is **not run** when `ngspice` is unavailable. |
| Generic CD4049 model | MOSFET device-current reference points, supply/rail consistency, and full Hermite interpolation-interval slope bounds were checked; fitted VTC was not silently altered. | `tests/CmosInverterPhysicsTest.cpp`: eight independent LEVEL1 current cases; sample maximum KCL residual ~2.42×10⁻¹⁵ A and KVL residual ~3.30×10⁻¹³ V. | Whether the selected transistor equations and coefficients describe the actual unbuffered CMOS in the target unit, including regions outside measured fit data. |
| Op-amp rail clamp | Narrow/unequal estimated rails could make the previous parabolic knee emit an artificial DC output for zero input (e.g. −0.2/+2.0 V, knee 0.35 V). The effective knee is now limited to available rail headroom, preserving the requested knee for later rail restoration. | `tests/RailHeadroomExtendedTest.cpp` and existing `tests/RailClampTest.cpp` cover zero bias, monotonicity, limits, invalid parameters and setter ordering. | Genuine TL07x output-stage transfer, dynamic slew, rail sag and target pedal's supply/VD versus input/output loading. |
| CMOS clipper solver | No new Newton/bisection defect was demonstrated under extreme alternating levels; the current nonlinear parameters and solver were preserved. | New Test 9 in `tests/ClipperTest.cpp`: 36 rate/ADAA/GRUNT configurations at 48–384 kHz, 147,456 total samples including ±100 V diagnostic impulses; finite outputs and applicable VTC swing bounds passed. | Worst-case solver residual for every discontinuity, genuine chip transient response and physical Ultra distortion parity. |
| Complete signal chain | Additional pathological-input, sample-rate, host-block-size and switch/model-change tests protect against NaNs and block-processing inconsistencies, while checking common allocation entry points. | `tests/PedalChainStressTest.cpp`: 648 selector/model/rate cases, three sample-rate block-equivalence sweeps, reset after injected nonfinite input, zero observed scalar/array `operator new` calls in the 4096-sample exercise. | `malloc`, aligned/custom allocators, JUCE host and oversampler paths, and all real device behavior are outside this test's evidence. “Finite” is not equivalent to “no audible clicks” or harmonic parity. |

The figures above are *specific standalone test observations*, not global tolerances. All relevant
tests must also run in the full project build/CI for the target release and should be repeated after
any DSP change. Tests named in this report are about circuit-model algebra, not a hardware A/B.

## Physical capture and comparison workflow

The reproducible data path is described in [the hardware measurement protocol](reference-measurement-protocol.md):

1. Identify one physical pedal generation/revision, serial, supply and chosen output path.
2. Capture a reusable digital reamp stimulus, a **separate measured dry electrical signal at the
   loaded pedal input**, and the corresponding pedal output, with documented fixture gains, common
   sample clock, probe loading, sample rate and switch/control state. Keep original WAVs unnormalized.
   Calibrate the *input monitor* ADC/probe correction **and the output ADC/preamp gain** to volts at
   their respective pedal connectors; also calibrate the plugin renderer's electrical-voltage scale.
3. Record SHA-256 hashes for all WAVs, then run `python3 analysis/validate_hardware_capture.py`
   against a schema-v2 manifest. That checks integrity/coverage and can yield `REVIEW_REQUIRED`,
   **never** a parity pass. `UNVERIFIED` is the default if references are absent or invalid.
4. Render the plugin from the **recorded loaded-input** signal at the same setting and compare
   against the recorded pedal output at a **common physical-voltage scale**. Without both input
   and output ADC calibrations plus plugin render scale, reported dBFS gain differences are merely
   digital-level diagnostics, **not measured analog gain residuals**. Preserve absolute levels, account for fixture-only delay,
   and withhold phase/harmonic quantities when source excitation or synchronization makes them
   indeterminate. Reports remain `DIAGNOSTIC_ONLY` until broader independent scrutiny.

Illustrative commands (no reference WAVs or target-pedal measurements are shipped):

```bash
python3 analysis/validate_hardware_capture.py --manifest analysis/hardware-captures/manifest.json
python3 -m unittest discover -s analysis -p 'test_*.py'
python3 analysis/compare_hardware_captures.py \
  --manifest analysis/hardware-captures/manifest.json \
  --take-index 0 --render-input path/to/verified-render-input.wav \
  --render path/to/plugin-render.wav \
  --output path/to/diagnostic.json
```

## Hard blockers to retail parity

- No manufacturer-authenticated component topology for an identified retail B7K Ultra revision, nor
  a rigorous teardown/trace of a measured target unit. The DIY board's source attribution is not this.
- No committed raw WAVs, dry loaded-input monitors, fixture calibration, identifiable physical pedal,
  sample-clock metadata, repeat captures or independent chain-of-custody supporting the old fit.
- The plugin's fitted mode differs materially from the clone schematic (see
  [accuracy audit](accuracy-audit.md)), and the two-MOSFET/CD4049 and J201 models have known
  device-parameter uncertainty.
- Circuit features beyond the main ¼-inch signal—especially balanced DI/XLR routing, loading,
  common-mode rejection and model-specific IR/cab/headphone/auxiliary behavior—are unvalidated or
  unimplemented, and must not be awarded parity credit.
- The new input-probe model is for a *purely resistive* Thevenin source; an actual pickup's
  inductance/capacitance changes source impedance and frequency response.

**Acceptance rule:** Only state fidelity results with a specified circuit/hardware target,
measurement chain, mode/output path, input level, control configuration, confidence or uncertainty
and independently reproducible observed residuals. No one universal “percent complete” can
honestly combine the unvalidated circuitry, sonic response and peripheral functions.
