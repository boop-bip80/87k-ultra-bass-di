# Obsidian B7000

![CI](https://github.com/boop-bip80/87k-ultra-bass-di/actions/workflows/ci.yml/badge.svg?branch=main)
[![License](https://img.shields.io/badge/License-AGPLv3-blue.svg)](https://opensource.org/license/agpl-v3)

A bass overdrive/DI-style preamp plugin (AU/VST3, JUCE 8) modelled on a third-party **Black Mirror VII**
DIY schematic, which its builder advertises as *inspired by* a Darkglass B7K Ultra, and reportedly calibrated to recordings of a B7K Ultra. The DSP uses
analytic stages rather than neural captures, impulse responses, or sample playback. It includes
**engineered, non-schematic correction filters**, and the shipped build's constants are **fitted to
reference captures, not read off the schematic**; several are far from the drawn values. Please read
**[Accuracy — what is and isn't true](#accuracy--what-is-and-isnt-true)**
before treating this as a circuit-exact emulation: it is not, and the repository previously said more
than it could support.

> **Hardware parity status: NOT ESTABLISHED.** This repository does not include a verified schematic of
> an actual Darkglass B7K Ultra unit, independently reproducible hardware captures, or a release-wide
> A/B report against a specified pedal revision. The supplied schematic describes PCB Guitar Mania's
> **Black Mirror VII clone**, not a manufacturer-issued Ultra circuit drawing. Passing DSP tests or
> matching an ngspice model of that clone is evidence of *model consistency*, **not** of 1:1 accuracy
> against the retail pedal. See [the accuracy audit](docs/accuracy-audit.md) and the reference sources
> below. The [schematic provenance ledger](docs/schematic-provenance.md) separates original-manufacturer
> specifications from DIY-clone component values. The missing evidence must be collected before a
> parity claim can be made. The proposed [retail parity qualification gates](docs/retail-parity-qualification.md)
> spell out what a reproducible bench-and-audio validation would have to demonstrate; **none is a certificate yet**.

**Round 3 electrical audit:** the *drawn-schematic* model now has opt-in
Q2 bootstrap-capacitor current and rail-aware Sallen-Key feedback (including
the IC4_B stage embedded in the bridged-T); an optional receiving-load setting
covers the quarter-inch output. The fitted production defaults are deliberately
unchanged. The capture comparator now withholds grades on repeated chirps and
suspected ADC clipping. See [Round 3 circuit evidence](docs/round3-circuit-evidence.md).
The earlier SPICE residual figures below are **pre-Round-3 baseline figures**
until the full schematic/CI gates are rerun; they have not been re-certified.

**Round 4 independent follow-up:** the source-load test now includes phase oracles;
CI now installs ngspice for an independent coupled-stage AC check; the diagnostic
comparator explicitly rejects XLR takes and changing whole-take THD stimuli.
Four modified standalone circuit-test executables passed C++17 runtime checks,
but complete CTest/CI and real hardware parity remain pending. See
[Round 4 verification and open physics gaps](docs/round4-evidence.md).

**Round 5 circuit-equation hardening:** an offline source-loading polarity error
has been fixed. Experimental, **default-disabled** circuit solvers now cover
Shockley D1/D2 currents inside the CMOS clipper's node equation, nonlinear
J201 R6/C3 source feedback and finite drain conductance, and rail-aware
passive-capacitor feedback in both mid-band and Baxandall EQ networks. Independent
stateful circuit oracles and legacy bit-exact tests are registered in CTest.
These options are engineering research APIs, **not** validated retail-pedal
settings or currently exposed plugin parameters. See [Round 5 circuit evidence](docs/round5-circuit-evidence.md)
and the [JFET derivation](docs/round5-jfet-source-feedback.md).

![Obsidian B7000 plugin UI](image.png)

**[Releases from this repository](https://github.com/boop-bip80/87k-ultra-bass-di/releases)**

### What can actually be verified

| Question | Evidence in this repository | Verdict |
|---|---|---|
| Does the *drawn-value* DSP follow the Black Mirror VII schematic? | Independent ngspice comparisons plus `SchematicParityTest`; additional coupled-stage magnitude/phase and component-perturbation tests | **Partially verified**, within the published frequency/harmonic limits; a shared circuit description still is not hardware proof |
| Does the *shipped fitted* DSP reproduce that schematic's component values? | `FitParams` and the numerical comparisons in the accuracy audit | **No**: 34 fitted constants and three extra correction stages |
| Does either DSP mode reproduce a physical B7K Ultra over its whole control range? | Earlier results are described, but the raw hardware recordings and acquisition metadata are unavailable | **Not independently verifiable** |
| Does this plugin reproduce the pedal's full DI hardware and cabinet simulation? | Audio path and plugin implementation | **No**: the balanced XLR DI path is not modelled; version-dependent cab-sim/IR functions are outside this analog model |

The public [Darkglass B7K Ultra manual](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual)
is an authoritative **control/specification** reference, not a component schematic. The
[Black Mirror VII build documentation](https://pcbguitarmania.com/wp-content/uploads/2021/07/Black-Mirror-VII-1v1-Building-Docs-18-10-2022.pdf)
is a useful **third-party clone** schematic and BOM. These are different evidentiary sources;
neither proves identical internal component values in a retail B7K Ultra. Note the distinction:
Darkglass lists the **ordinary B7K** at nominal **500 kΩ** input impedance, but the **B7K Ultra**
at nominal **1 MΩ**. The clone's calculated audio-band ~500 kΩ is not evidence of Ultra parity.
Details of the validation
limits and test protocol are in [`docs/reference-measurement-protocol.md`](docs/reference-measurement-protocol.md)
and `analysis/`. The hardware-reference manifest validator deliberately distinguishes *missing
evidence* (`UNVERIFIED`, exit 2) from *metadata ready for waveform review* (`REVIEW_REQUIRED`, exit 0);
neither means physical parity:

```bash
python3 analysis/validate_hardware_capture.py
python3 -m unittest discover -s analysis -p 'test_*.py'
```

## What it is

Eight pots (**Master**, Blend, Level, Drive, Bass, Lo‑Mids, Hi‑Mids, Treble), two 3‑way toggles
(**Attack**: Boost/Flat/Cut, **Grunt**: three bass-content levels into the clipper), two 3‑position
frequency selectors (Lo‑Mid 250/500/1 kHz, Hi‑Mid 750 Hz/1.5 kHz/3 kHz), and two independent
footswitch controls — an emulated bypass and a second **Distortion** switch that mutes just the overdrive
path while the EQ/Master stage keep running, so the pedal can sit always-on as a clean DI/EQ with
drive stomped in on top.

- **CMOS-inverter overdrive**, not diode clipping — the distortion stage is a CD4049UBE unbuffered hex
  inverter in shunt feedback. The shipped clipper curve is a *fitted* asymmetric sigmoid (it swings
  ~1 Vpp where the drawn circuit swings ~8.6 Vpp); a physical two-MOSFET + R19-sag version exists as an
  option (see below)
- **A J201 JFET gain stage** ahead of the clipper (common-source + active-load pair), modelled as a
  Wiener–Hammerstein approximation with fitted shaper constants
- **Full 4-band active EQ** (Baxandall Bass/Treble + two switchable-frequency mid peaking bands)
  built as coupled MNA networks, not independent biquads. At drawn component values this block matches an
  independent SPICE simulation to ≲0.4 dB rms; the *shipped* mid-band caps and wiper resistors are fitted
  and differ from the drawn board (see the audit)
- **Switchable oversampling** (1×/2×/4×/8×, separate Live/Render settings) with 1st-order ADAA on
  the JFET waveshaper and — at 1×/2× only — on the fitted clipper curve
- **Gain staging in volts** — internal voltages are modelled in real volts against a calibrated input
  reference (`kInputRef`), not normalized ±1.0. The reference is itself a fit to the captures, and the
  op-amp rail windows (±2.9/2.7 V about VD) are estimates, so "clips where the real circuit would" is an
  aim, not a verified property

## Under the hood

The topology comes from the traced schematic (`.claude/rules/circuit.md`), cross-checked against a second
schematic revision and the manufacturer's control list, and each stage is tested against a closed-form
oracle in `analysis/eq_reference.py`. Those oracles were written from the same reading of the circuit as
the C++, so they do **not** by themselves show the plugin matches the circuit; the independent check is the
ngspice reference described under *Accuracy*.

- **Linear stages** (input buffer, treble/Attack network, Drive gain, recovery bridged-T, two Sallen-Key
  low-passes, Level/Blend crossfade, the EQ block, Master output) are solved as MNA networks with
  trapezoidal-companion capacitors, precomputed per switch position. (Only the small input buffer is a
  literal chowdsp_wdf tree.) C21 and C31 are solved *inside* the tone-stack/LO-MID networks, where the
  load they work against actually lives.
- **The J201 JFET stage** is a Wiener–Hammerstein cascade: input high-pass, source-degeneration shelf,
  inverting gain, per-polarity soft waveshaper, output as a Norton current into the treble network's real
  impedance. The bootstrapped Q2 active-load network has an explicit small-signal clone-schematic model
  (opt-in, below); the shipped build
  uses a fixed stand-in.
- **The CD4049UBE clipper** is a shunt-feedback stage solved per sample (bracketed Newton) with the
  4049's *finite* open-loop gain, around either the shipped fitted sigmoid VTC or — opt-in — the
  Köper–Holters two-MOSFET device model with R19 as a self-consistent supply dropper
  (`src/dsp/CmosInverterVtc.h`). The default models D1/D2 as hard rail clamps;
  an experimental, normally disabled mode instead solves their Shockley currents
  within node-W KCL. Its diode parameters have not been measured on retail hardware.
- **Rail saturation** is applied on every op-amp output stage (enabled in the shipped build, ±2.9/2.7 V
  about VD — an estimate).
- **Non-schematic corrections.** Three OD-path stages — `OdToneRestore`, `OdMakeup`, `OdDriveTilt` — are
  *engineered* corrections fitted to captures, not circuit elements, and a large block of ladder/clipper
  constants is fitted (34 constants differ from the drawn values). They are listed in
  [`docs/accuracy-audit.md`](docs/accuracy-audit.md) §6.

The CD4049UBE clipper and J201 JFET stage are the two principal **nonlinear device models**;
many of the linear stages are MNA solvers rather than literal chowdsp WDF trees. The sourcing behind
the nonlinear models is in
`docs/nonlinear-component-modeling.md`.

Several features aren't on the Black Mirror VII third-party schematic at all — they are
engineered to reproduce documented Ultra behaviour: the **Master** volume stage, the **3-way Attack**
centre position, the **switchable mid frequencies**, and the second **Distortion** footswitch. See
`circuit.md`'s `[ENG]` tags. Note the shipped mid caps are no longer the originally-computed table but a
fitted scaled pair (`FitParams.h`).

### Additional numerical checks, not physical-parity evidence

The previous engineering pass added independent mathematical and robust-input tests around the
existing model. It corrects small-signal cancellation in the J201 waveshaper antiderivatives, tests
the coupled bridged-T/Sallen-Key branch values with magnitude **and phase**, checks the two-MOSFET
CMOS approximation against independent current-law cases, and exercises model/switch transitions under
pathological inputs. A diagnostic source-loading probe represents the DIY drawing's input pulldown and
coupling network with a finite *resistive* source; the normal audio path is unchanged because a recorded
signal may already have undergone analog source loading. The DIY input is about **1 MΩ at DC** and
**500 kΩ at audio frequencies**; this is not an independently measured impedance curve of a retail
B7K Ultra. See [the engineering evidence ledger](docs/round2-evidence-report.md) for tests, assumptions
and remaining blockers.

## Accuracy — what is and isn't true

**Short version:** the repository contains two configurations of the same model, and they answer
different questions.

| | **shipped (default)** | **schematic** (plugin **MODEL → DRAWN** button, bottom strip; `--schematic` offline) |
|---|---|---|
| constants | fitted to reference captures of an Ultra | the drawn component values of the B7K-clone schematic |
| OD correction stages | `OdToneRestore`, `OdMakeup`, `OdDriveTilt` on | bypassed |
| CD4049 clipper | fitted sigmoid, ~1.04 Vpp swing | generic two-MOSFET model + R19 sag, ~8.6 Vpp *in the model* |
| vs independent ngspice of the drawn circuit | **1.5–10 dB rms (EQ), 9–12 dB rms (OD small-signal)** | **0.01–0.41 dB rms (clean path), ≈1 dB rms (OD small-signal), clipper large-signal within 0.3 % swing / ~0.5 dB per harmonic** |
| vs the real pedal | reportedly calibrated to captures that are **not in this repo**; cannot be re-verified from a clone | **not verified** — the drawn circuit is Black Mirror VII, an independently designed Ultra-*inspired* board, and earlier notes describe four places where captures disagreed with its schematic |

Neither configuration has been compared against a physical B7K Ultra by anyone who can show their data
here, so **no claim of unit-level parity is made**. The independent SPICE reference
(`analysis/spice_reference/`) is what makes the *schematic* column measurable; it also found three
structural modelling errors (C21 loaded by a fixed resistor, the J201's bootstrapped active load modelled
as a fixed 1 MΩ, the bridged-T treated as unloaded), now fixed — the last two are opt-in because the
shipped calibration was fitted on top of the old structure. Full tables, method, limits, and the list of
every constant that departs from the drawn board: **[`docs/accuracy-audit.md`](docs/accuracy-audit.md)**.

### Historical null-depth figures (unreproducible here)

Earlier project notes report sub-sample-aligned null depths against a pre-registered 12-capture set.
They are gain-matched (shape/phase only, not absolute level); the reference WAVs, acquisition metadata,
and original run logs **are not committed**, so these historical numbers are **not independently
verified** and must not be presented as a current hardware-accuracy score:

| Mode / setting | Best null, 100 Hz–8 kHz band (gain-matched) | Reference capture |
|---|---|---|
| Clean (OD path fully out of circuit) | −26.5 dB | `ref-clean.wav` |
| Mid-drive (Drive at noon) | −15.3 dB | `ref-od.wav` |
| Max drive | −9.4 dB | `drive-1700_base-od.wav` |

For scale: −9.4 dB is a ~34 % residual by amplitude. `CLAUDE.md` also records that the OD path "reads
quiet" against the reference, that 6 of 14 gated accuracy rows are over their target, and that the XLR DI
output is not modelled. (After this audit the shipped C21 is solved inside the tone stack with a 1 µF
default; its reference response is unchanged at flat/bass± to ~0.3 dB rms, but the null figures above were
measured before that change.)

## Performance

Historically reported by `PerfBenchmark` (`ctest -R PerfBenchmark`), best-of-5 on the real `PedalDSP` chain at
48 kHz with **BLEND at 100 % overdrive and DRIVE at 0.85** — i.e. the clipper's worst case, not a
flattering average. Rebuild and re-run rather than trusting these figures on your own machine: they
are specific to the box that produced them (Apple Silicon laptop, Release build).

| Oversampling | CPU (≈ % of one core) | × realtime | Added latency | Best for |
|---|---|---|---|---|
| **1×** | 1.1 % | 88× | 0 samples | Tracking / low-latency live use |
| **2×** | 2.4 % | 41× | 49 samples | Everyday playing |
| **4×** | 3.7 % | 27× | 60 samples | Higher-fidelity highs |
| **8×** | 6.6 % | 15× | 64 samples | Maximum fidelity |
| **Render** *(auto, offline bounce)* | 6.6 % | n/a — not realtime-constrained | 64 samples | Maximum fidelity; engages automatically on DAW export (defaults to the 8× factor) |

Oversampling is the CPU-vs-fidelity dial, and the CD4049 clipper's per-sample Newton solve is the
dominant cost. Anchoring the clipper's knee exponent to `k = 2` puts `sig()`/`sigDeriv()` on their
closed-form path instead of two `std::pow` each, worth roughly half the chain's total CPU at every
factor; 1st-order ADAA (on at 1×/2× only) costs some of that back at those two factors and nothing
at 4×/8×.

## Building

**Requirements:** CMake 3.15+, a C++17 compiler. AU + VST3 on macOS 10.13+; VST3 on Windows/Linux
(AU is an Apple-only format).

```bash
git clone https://github.com/boop-bip80/87k-ultra-bass-di.git
cd 87k-ultra-bass-di

# Pull in dependencies (JUCE 8, chowdsp_wdf, xsimd)
git submodule update --init --recursive

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ObsidianB7000_AU      # Audio Unit (macOS only)
cmake --build build --target ObsidianB7000_VST3    # VST3 (macOS/Windows/Linux)
```

`COPY_PLUGIN_AFTER_BUILD` is on, so a build installs straight to your system's plugin folder.
Logic caches AU components — bump the `VERSION` in `CMakeLists.txt` to force a rescan after
changes, or remove/re-add the plugin.

The build also produces the calibration/diagnostic tooling used during development:

| Target | What it does |
|--------|--------------|
| `OfflineRender` | Renders a WAV through the project's `PedalChain`/`PedalDSP` at given knob-space settings, designed to correspond to `processBlock`, for A/B against a capture. `--fit name=value` overrides any calibration constant without a rebuild. |
| `EditorSnapshot` | Headless render of the plugin editor to PNGs at min/default/max UI scale — no display needed |
| `OSValidationTest` | Oversampling aliasing + delay-compensation gate (registered with CTest) |
| `ModelSwitchTest` | The plugin's runtime MODEL switch: no observed scalar `operator new` calls in the tested path, reversible bit-for-bit, finite, and the two configurations genuinely differ |
| `SchematicParityTest` | The plugin in schematic configuration vs committed ngspice data (clean path, OD small-signal, CD4049 large-signal, inverter bias point); needs no ngspice at run time |
| `SchematicOracleExtensionTest`, `CmosInverterPhysicsTest` | Isolated clone-topology magnitude/phase and component sensitivity; generic MOSFET-law and interpolation consistency. No claim that the equations describe an authenticated retail circuit |
| `SourceLoadingTest`, `RailHeadroomExtendedTest`, `PedalChainStressTest` | Source impedance and default-path invariance; rail-knee zero bias under narrow rails; selector/switch/block-size stability and allocation regression |
| `InputBufferTest`, `TrebleAttackTest`, `DriveStageTest`, `RecoveryBridgedTTest`, `SallenKeyLPFTest`, `LevelBlendTest`, `MidBandTest`, `BaxandallTest`, `EqPreGainTest`, `MasterOutTest`, `JfetStageTest`, `ClipperTest`, `PedalChainTest` | Per-stage DSP correctness tests (see `tests/`), each checked against a closed-form oracle in `analysis/eq_reference.py` |

```bash
cmake --build build           # build everything, including the probes
ctest --test-dir build --output-on-failure -j 4
```

The **drawn-schematic model** is selectable inside the plugin: the **MODEL / DRAWN** button in the bottom
strip (off by default = the capture-calibrated build). Switching showed no scalar `operator new`
calls in the focused test but causes a brief transient (`ctest -R ModelSwitchTest`). It sounds
different — tighter, louder OD path, and a modeled two-MOSFET clipper — and
is the configuration gated against ngspice by `ctest -R SchematicParityTest`. `OfflineRender --schematic`
renders it offline, and `cmake -DOBSIDIAN_SCHEMATIC_MODEL=ON` builds a plugin whose *default* is DRAWN.

The Python side of the calibration harness needs NumPy/SciPy (and `ngspice` for
`analysis/spice_reference/`):

```bash
python3.11 -m pip install -r analysis/requirements.txt   # if present, else numpy scipy
python3.11 analysis/captures.py                          # self-validates the capture filename matrix
```

The new hardware-capture metadata validator and paired-waveform comparator are separate,
diagnostic-only tools. The validator uses only the Python standard library; raw physical-pedal WAVs
are not present in this repository. See [`analysis/README.md`](analysis/README.md) for commands and
measurement prerequisites. In particular, independent calibrations of the dry-input probe,
pedal-output ADC/preamp gain **and plugin-render voltage scale** are necessary before an A/B gain
number represents electrical pedal gain rather than arbitrary dBFS levels. An optional targeted
ngspice check for the isolated bridged-T stage is
`python3 analysis/spice_reference/verify_coupled_stage.py` and is **not** part of the green CTest
gate if ngspice is unavailable.

## Where to find things

```
src/
  PluginProcessor.{h,cpp}   Top-level AudioProcessor, APVTS parameter layout, processBlock
  PluginEditor.{h,cpp}      Top-level editor
  dsp/                      One header per circuit stage (InputBuffer, TrebleAttack, DriveStage,
                             JfetStage, Clipper, RecoveryBridgedT, SallenKeyLPF, LevelBlend,
                             EqPreGain, Baxandall, MidBand, MasterOut, RailClamp) + PedalChain,
                             which wires the full signal path, and PedalDSP, the oversampling wrapper
  ui/                       PedalFace (data-driven from ui/component_positions.csv), the shared
                             LookAndFeel, VU meter, LED, three-position switch
  utils/TaperUtils.h        Pot taper math (audio/log vs. linear vs. fitted power-law)

tests/        Per-stage DSP validation programs (see table above)
analysis/     spice_reference/ — independent ngspice netlist of the drawn schematic + parity scripts.
              Real-pedal captures (gitignored — not in the repo), plus the reusable A/B harness:
              gen_test_signal.py (the capture signal), analyze.py (FR / Farina swept-THD /
              sub-sample null harness), eq_reference.py (closed-form oracle for every linear
              stage), captures.py (capture filename grammar + OfflineRender CLI-argument
              mapping), comprehensive_report.py / matrix_grade.py / shape_gate.py (the A/B report
              + grading), fit_nonlinear.py + phase_harmonics.py (nonlinear parameter fitting),
              offline_render.cpp (source for the OfflineRender tool) — see analysis/README.md
docs/
  accuracy-audit.md                ★ what the model does and does not match, with numbers
  round2-evidence-report.md        ★ independent software checks and remaining physical-parity blockers
  schematic-provenance.md          authoritative manufacturer specs vs third-party DIY drawing
  reference-measurement-protocol.md  physical capture, hashing, control coverage and review rules
  nonlinear-component-modeling.md   datasheets/papers/recommended approach for the J201 + CD4049
  calibration-and-gain-staging.md  ★ input/output level calibration, taper fitting, rail modelling
  validation-and-capture.md        ★ how the plugin is measured against real-pedal captures
  ui-peripheral-spec.md            full visual spec for the reusable UI chrome
.claude/rules/   Circuit topology (circuit.md — the source of truth for every component value and
                 node graph), DSP rules (dsp.md), plugin architecture (architecture.md), UI layout
                 (ui.md), and build setup (build.md)
```

## Thanks

Built on the shoulders of:

- [JUCE](https://juce.com) — the plugin framework
- [chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf) — Wave Digital Filter library that
  made circuit modelling tractable
- [xsimd](https://github.com/xtensor-stack/xsimd) — SIMD acceleration

## License

This project is licensed under the [GNU Affero General Public License v3.0](LICENSE)
(AGPLv3), the same license under which the open-source edition of JUCE is distributed.
chowdsp_wdf and xsimd are compatible and included under their own license terms in `libs/`.

## Credits

Original Obsidian B7000 source: Leigh Pierce ([@tehguitarist](https://github.com/tehguitarist),
[source project](https://github.com/tehguitarist/Obsidian-B7000)). This repository is maintained
under [boop-bip80](https://github.com/boop-bip80); its modifications and test results should be
judged against the evidence published **here**, not the source project's badges or release downloads.
