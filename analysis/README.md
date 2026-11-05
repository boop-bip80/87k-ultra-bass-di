# Analysis Harness

This directory contains a legacy A/B fitting harness and the newer physical-capture provenance
and comparison tooling. The legacy scripts were designed for real-pedal reference captures, but
**those raw WAVs and their acquisition metadata are not committed in this repository**.
Consequently, historic frequency-response, THD and null-depth scores cannot be independently
reproduced from a fresh clone and are **not evidence of hardware parity**. The schematic-based
SPICE comparison is to an independent *clone board*, not a traced retail Darkglass circuit.

For the evidence hierarchy and corrected claims, see
[`docs/schematic-provenance.md`](../docs/schematic-provenance.md),
[`docs/accuracy-audit.md`](../docs/accuracy-audit.md), and
[`docs/round2-evidence-report.md`](../docs/round2-evidence-report.md).

## New reproducible hardware-capture gate

Use [`docs/reference-measurement-protocol.md`](../docs/reference-measurement-protocol.md) to collect
three different signals for each physical test: the digital reamp stimulus (which may be reused),
the separately *measured loaded input voltage* at the pedal connector, and the actual pedal
output. The schema-v2 manifest records the source, input-monitor and output WAV locations,
individual SHA-256 hashes, identified hardware/revision and calibrated routing/controls.

```bash
# Without independent real-hardware captures this correctly returns UNVERIFIED (exit 2).
python3 analysis/validate_hardware_capture.py \
    --manifest analysis/hardware-captures/manifest.json

# Test the validator and the diagnostic comparator on synthetic known answers.
python3 -m unittest discover -s analysis -p 'test_*.py'

# After collecting an authentic take and rendering the plugin from that take's
# measured loaded-input signal (not from an ideal-source stimulus WAV):
python3 analysis/compare_hardware_captures.py \
    --manifest analysis/hardware-captures/manifest.json \
    --take-index 0 --render-input path/to/verified-render-input.wav \
    --render path/to/plugin-render.wav \
    --output path/to/diagnostic.json

# Only when independently measured, supply BOTH input volts/FS scales:
#   --physical-pedal-input-volts-per-fs <measured_at_connector>
#   --effective-renderer-input-ref-volts-per-fs <actual_render_setting>
# Both are needed to compare nonlinear headroom at an equal electrical drive.
# Output volts/FS and fixture delay must separately be calibrated for gain/phase.
```

The validator can yield `UNVERIFIED` or `REVIEW_REQUIRED`; it never certifies a sound match.
The comparator rejects nonzero DC-only WAVs as excitation and checks stationary
Welch assumptions at both four and sixteen subwindows (repeated sweeps must not
pose as stationary noise). Possible ADC clipping withholds pedal THD and
calibrated frequency/gain grades. Missing or mismatched absolute input-voltage
scales withhold manifest-backed nonlinear attribution, while retaining raw
digital-domain diagnostics.

The stationarity check covers the same (up to 1,048,576-frame) window as the
Welch-H1 frequency estimator, so a stationary introduction cannot hide a later
sweep. The JSON field `declared_input_scale_and_manifest_checks_passed` is only a
metadata/input-scale precondition, never permission to attribute circuit errors.
For manifest comparisons, physical and modeled per-track THD diagnostics remain
available, but the numerical **THD difference is withheld** pending verified
pedal identity, electrical input calibration and actual renderer output lineage.

The comparator reports diagnostic differences with `parity_certified=false`, including
gain/level and only those phase/harmonic measurements supported by actual excitation and
fixture calibration. A small error on **one** take cannot certify circuitry parity across
all controls, operating levels, physical units and XLR/quarter-inch outputs.

**Absolute-gain prerequisite:** A dry input monitor and a pedal output must be calibrated
separately from their ADC counts to **volts at their respective connectors**. A physical
input/output gain comparison also requires a documented mapping from the plugin renderer's
input/output digital scale to the same electrical voltage scale. `dry_monitor.calibration_db_to_pedal_input`
handles only the measured dry-input probe correction; it says **nothing** about the output
ADC/DI preamp gain. If output calibration or render scale is absent, numbers expressed in
dBFS are **diagnostic digital-level differences**, not verified pedal voltage gain. Keep any
phase result withheld unless the dry monitor and output share a clock and known fixture
delays; likewise do not report general THD from an arbitrary broadband/noisy clip.

The legacy quick start below remains available for the earlier fitting scripts but
should not be confused with the provenance-checked workflow.

## Legacy fit-harness quick start

```bash
# 1. Generate the reference test signal
python3 analysis/gen_test_signal.py
# → writes analysis/test_signal_48k.wav

# 2. Populate analysis/captures/ with real-pedal recordings (see captures.py)
# 3. Build the OfflineRender binary
cmake --build build --target OfflineRender

# 4. Run the comprehensive A/B (once captures exist in analysis/captures/)
python3 analysis/comprehensive_report.py --os 8
# → writes analysis/reports/comprehensive_data.json

# 5. Grade the report
python3 analysis/matrix_grade.py analysis/reports/comprehensive_data.json
```

## Independent clone-circuit reference (`spice_reference/`)

`spice_reference/` holds an ngspice netlist of the drawn schematic (`b7k_spice.py`) and scripts that
compare the plugin against it — `compare_clean.py`, `compare_od.py`, `compare_clipper.py`,
`compare_chain.py` — plus `gen_reference.py`, which regenerates `tests/data/schematic_reference.h`
(the data `ctest -R SchematicParityTest` gates on). It needs `ngspice` on PATH; it shares no code with the
plugin and needs no captures. Results and caveats: `docs/accuracy-audit.md`.

An additional `spice_reference/verify_coupled_stage.py` command checks that the isolated
bridged-T/loaded-Sallen-Key netlist has the eight intended clone component values and, if
`ngspice` is installed, compares its magnitude/phase against the nodal/DSP model. It is an
explicitly **optional verification**, not a falsely green test when `ngspice` is missing:

```bash
python3 analysis/spice_reference/verify_coupled_stage.py --print-netlist
python3 analysis/spice_reference/verify_coupled_stage.py  # needs ngspice
```

## File Reference

| File | What It Does |
|------|-------------|
| `analyze.py` | **Analysis library.** Load/align WAVs, frequency response (CSD/Welch), discrete-tone THD (harmonic binning), **Farina continuous THD(f)** with order-limiting (eliminates the spurious-edge-spike artefact), sub-sample fractional alignment, gain-matched null depth, linear-removed (coherence-based) null floor, capture-filename parser. Everything else imports this. |
| `gen_test_signal.py` | **Reference signal generator.** Exponential sine sweeps (Farina ESS) at clean + 3 driven levels, discrete harmonic tones, 1 kHz level steps for compression knee, SMPTE IMD (60 Hz + 7 kHz, 4:1), guitar-band IMD (220 Hz + 660 Hz), plucked decay notes. **Append-only** — inserting segments in the middle invalidates all existing captures. |
| `captures.py` | **Capture interface.** Provides `find_captures()`, `load_capture()`, `render_args()`, and `RENDER_BIN`. Everything else imports from here. |
| `parallel.py` | Shared parallel-map helper — every heavy tool loops over independent items (captures, candidate values, bands), which is embarrassingly parallel; this module gives that to a new tool for free. |
| `eq_reference.py` | Closed-form analytic transfer functions for each linear WDF stage (treble/ATTACK ladder, Sallen-Key filters, Baxandall, mid bands, bridged-T, drive stage). The oracle every per-stage `ctest` target checks against. |
| `comprehensive_report.py` | Reads every capture in `analysis/captures/`, renders the plugin at matching settings, and writes a JSON report (FR, THD, H2–H7 harmonics) to `analysis/reports/comprehensive_data.json`. Parallel by default (`--jobs N`). |
| `matrix_grade.py` | Aggregate grade for a `comprehensive_report.py` JSON: per-row band-RMS of `|plugin_db - pedal_db|` over the graded band, plus the FR tilt. |
| `shape_gate.py` | Decomposes every FR/THD residual into LEVEL + TILT + CURVATURE + LOCAL components instead of a single band-RMS — catches narrow features (nulls, peaks) that an aggregate statistic averages away. |
| `fit_nonlinear.py` | Fits the clipper + gain-stage nonlinear parameters to captures, scoring on harmonic-to-harmonic ratios (not harmonic-to-fundamental, which is contaminated by any clean/wet mix in the signal path). |
| `phase_harmonics.py` | Extracts complex (magnitude + phase) harmonics from a steady tone and reports the shift-invariant relative phase `psi_n = phi_n - n*phi_1`. Used by `fit_nonlinear.py`. |
| `offline_render.cpp` | CLI renderer (built as the `OfflineRender` CMake target) that mirrors `processBlock()` gain staging outside the plugin host — every fit and every report above needs it. |

## Key Concepts

### The Test Signal

`test_signal_48k.wav` (48 kHz, 32-bit float) contains these segments in order:

| Segment | Content | Purpose |
|---------|---------|---------|
| `cal_1k` | 1 kHz tone @ -18 dBFS | Level calibration + sample-rate detection anchor |
| `sweep_clean` | Log sweep 20 Hz → 20 kHz @ -30 dBFS | Primary clean FR + alignment anchor |
| `sweep_clean_-36` | Same sweep @ -36 dBFS | Second clean-end FR point (rolled-off input) |
| `sweep_drv_-18` | Log sweep @ -18 dBFS | Driven FR + continuous THD(f) via Farina deconvolution |
| `sweep_drv_-12` | Log sweep @ -12 dBFS | Deeper drive; bracket-tests the -18 sweep |
| `sweep_drv_-6` | Log sweep @ -6 dBFS | Hot pickup level; heaviest clipping |
| `lvl_-36` … `lvl_-3` | 1 kHz tone steps, 3 dB apart | Compression knee vs. input level |
| `tone_82.41` … `tone_8000` | Discrete tones @ -14 dBFS | Harmonic spot-checks (anchor the swept THD) |
| `imd_smpte` | 60 Hz + 7 kHz (4:1) | SMPTE intermodulation distortion |
| `imd_guitar` | 220 Hz + 660 Hz (musical 5th) | Guitar-band intermod |
| `decay_220`, `decay_1k` | Plucked exp-decay notes | Touch / dynamic response |

**Never insert segments in the middle** — it shifts every later segment's offset
and invalidates all existing captures. Append new segments at the end only.

### The Farina THD Curve

`analyze.harmonic_thd_curve()` deconvolves a driven exponential sweep against
the clean reference sweep to extract time-separated harmonic impulse responses.
This yields a **continuous THD(f) curve** from a single capture.

**Order limiting** (on by default): the reference sweep has no energy above
`SWEEP_F1` (20 kHz), so order N is only measurable while `N·f ≤ SWEEP_F1`.
Without limiting, each order produces a large spurious spike at exactly
`SWEEP_F1/N` (e.g., H7 spikes at 2857 Hz). With limiting:
- Nothing below ~2714 Hz changes (all 7 orders in-band)
- Coverage extends to ~9.5 kHz (H2 only)
- Above 12 kHz, THD doesn't exist at 48 kHz (H2 past Nyquist)

### Suggested calibration workflow (not yet independently revalidated)

1. **Validate STRUCTURE before AMOUNT** — get FR shape and per-harmonic structure
   (which orders, where placed) right first. THD magnitude is downstream.
2. **FR shape** — gain-match, compare linear FR. Fix EQ/tapers.
3. **Per-harmonic structure** — compare H2–H7 re fundamental. A correct THD can
   hide wrong individual magnitudes (same RSS, different timbre).
4. **Input reference calibration** (volts/FS from clip onset).
5. **Clip character** — asymmetry, knee softness, junction capacitance.
6. **Output level** — per-revision makeup gain.
7. **Re-run** full A/B; decompose residuals with `linear_removed_null()` before
   changing more constants.

## Capture File Naming

`captures.py`'s filename parser recognises knob settings as either clock notation
(`level-1200`, `blend-1330` — 0700=min, 1200=noon, 1700=max) or a 0–10 dial scale
(`grunt-3`), plus named tokens for the switches (`grunt-flat`, `attack-boost`, …).
`analysis/captures/` itself is not tracked in this repository (it holds the raw
reference recordings); populate it locally before running `comprehensive_report.py`.

## Known Gotchas

- **Rate-mislabeled captures**: Some NAM modelers export 44.1 kHz audio inside
  a 48 kHz WAV header. Reading naively plays 8.8% fast and decorrelates the
  entire upper band. The `captures.load_capture()` skeleton includes detection
  via the 1 kHz cal tone — keep this logic.
- **Write 32-bit float renders**: Fixed-point output (16/24-bit int) hard-clips
  at ±1.0 FS. Driven sweeps routinely exceed 0 dBFS after makeup gain — this
  injects a spurious, input-level-independent THD floor that silently corrupts
  every measurement.
- **Judge wet path on full-wet captures**: At partial blend, the pedal's dry+wet
  paths can phase-cancel in the top octave (20 dB at 14 kHz on a BL=0.50
  capture). The plugin typically won't reproduce that cancellation, so a
  partial-blend FR read shows a false "plugin too bright" error.
- **Always write analysis scripts as files**, never as inline commands. Renders
  take seconds each; Farina harmonic analysis takes seconds per segment.
- **Separate source and loaded input**: A line-level stimulus, passive pickup's
  open-circuit Thevenin source, and measured voltage after input loading are
  not interchangeable. The clone's R1/C1/R2 network approaches 500 kΩ input
  impedance in the audio band, even though the manufacturer specifies a 1 MΩ
  nominal input for a retail B7K Ultra; the latter is not a measured impedance
  curve. Do not double-apply pickup/loading losses to an ADC-recorded signal.

## Dependencies

- Python ≥ 3.9
- `numpy`
- `scipy` (for `scipy.io.wavfile`, `scipy.signal`)
- The `OfflineRender` binary (`cmake --build build --target OfflineRender`)
