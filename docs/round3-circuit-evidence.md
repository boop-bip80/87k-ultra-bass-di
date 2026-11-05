# Round 3 — clone-circuit corrections and measurement integrity

_Date: 2026-11-05. Status: electrical model improvements under test; **retail B7K Ultra parity is NOT established**._

## What the circuit source actually proves

This repository's original component drawing is [PCB Guitar Mania's Black Mirror VII 1.1v](https://pcbguitarmania.com/wp-content/uploads/2021/07/Black-Mirror-VII-1v1-Building-Docs-18-10-2022.pdf), an independent DIY design advertised as Ultra-inspired. It is **not** a Darkglass production design package. A separate [1.2v document](https://pcbguitarmania.com/wp-content/uploads/2021/07/Black-Mirror-VII-1.2v-Building-Docs.pdf) has a BOM disagreement: IC6 is TL072ACP in 1.1v, TL074ACN in 1.2v. Neither identifies the actual IC6 installed in a particular retail pedal.

The [Central Semiconductor J201 data sheet](https://central.aeminc.com/datasheets/J201.PDF) specifies IDSS 0.2–1.0 mA and |VGS(off)| 0.3–1.5 V. The single typical-part J201 parameters in `FitParams::schematic()` are assumptions; they cannot authenticate a physical device or its even-order distortion.

## New, opt-in electrical corrections

| Stage | Clone-derived circuit obligation | Implementation | What it does **not** prove |
|---|---|---|---|
| Q2 bootstrap, `TrebleAttack` | R9=R10=1 MΩ to AC ground; C4=22 nF between output and Q2 gate | Separately gated series C4–500 kΩ Norton branch at node G; passive admittance `s*C4/(1+s*500k*C4)` added to previously modeled transconductance branch | J201 device spread, actual B7K transistor network |
| IC4_A and standalone IC4_B, `SallenKeyLPF` | C1 feeds back from the **driven op-amp output**, not its unclamped +input | Unique monotone scalar solve `A*Y-gc1*rail(Y)=RHS`; update C1 against `X-rail(Y)` | Finite op-amp gain/GBW, slew rate, actual rail curve |
| Internally coupled IC4_B, `RecoveryBridgedT` | Same C18 output-plate obligation; standalone IC4_B is bypassed when bridged-T is coupled | Rank-one update of the independent four-node MNA solve, including C18 history at actual driven output | Unmodeled loading and saturation elsewhere in EQ |
| ¼-inch output, `MasterOut` | R47=1 kΩ in series, R46=100 kΩ pulldown; a receiver draws current | Opt-in receiver resistance; exact R46 parallel (R47+load) affecting the C37 high-pass, plus jack divider | Real XLR balanced DI circuitry, cable capacitance or op-amp current limiting |

**Shipped fitted mode is preserved by default.** `FitParams::skRailFeedbackCoupled=0`, `jfetQ2GateCap=0`, and `receiverLoadOhms=+infinity` select the prior behavior; `FitParams::schematic()` explicitly enables the first two. For an offline diagnostic use `--schematic`, or independently toggle `--fit skRailFeedbackCoupled=1`, `--fit jfetQ2GateCap=1`, and `--fit receiverLoadOhms=10000`. The last example is a *hypothetical* 10 kΩ receiving input, **not** a measured B7K output fixture.

The small-signal Q2 target is
`Y_Q2(s)=gds2+(gm2+s*C4)/(1+s*Rg*C4)`. Independent complex nodal analysis predicts, for the isolated nominal Flat ladder at 48 kHz, **65.647873 dB** transimpedance and **+19.53184°** at 1 kHz (against 65.764666 dB before passive C4); this is a clone-circuit target only. The Sallen-Key rail-coupled equation is monotone because `A>gc1` and `0<=rail'(Y)<=1`; the embedded IC4_B scalar correction has contraction ratio 0.176 at 48 kHz. Independent calculations detect an approximately 35.8 mV coupled-IC4_B rail-recovery difference from the old implementation. These are analytic/numerical **oracle targets**, not measurements of a real pedal.

## Fail-closed capture analysis

1. Repeated identical sweeps can pass a four-quarter spectral-centroid test. The comparator now checks four **and sixteen** subdivisions; synthetic 4×(50–12 kHz chirp) is withheld from Welch-H1 spectral grading while seeded stationary broadband noise qualifies.
2. A physical output recording with many PCM samples at full scale can contain *converter* harmonics. Suspected ADC clipping now withholds pedal THD and calibrated frequency, gain and waveform grades, retaining raw PCM statistics; it is a conservative suspicion, **not proof** that the converter clipped.
3. A constant nonzero PCM WAV is not meaningful acoustic excitation. The manifest validator now requires DC-removed AC variation in source and loaded-input-monitor WAVs and uses exact per-channel integer sample moments.
4. The loaded-input monitor calibration is relative. For manifest-backed nonlinear attribution the comparator additionally needs measured **physical input volts/FS** and the exact effective **renderer input-reference volts/FS**. It reports and withholds interpretations when their difference exceeds 0.1 dB or either is absent; declarations alone still cannot prove the render was made with those settings.

This capture system never sets `parity_certified=true`; no raw authenticated B7K Ultra reference WAVs/fixture are committed. Do not fit transistors or announce hardware parity from these synthetic checks.

## Verification status at this round

- **Checked without mutating the read-only checkout:** all four changed standalone C++ test/header combinations passed `g++ -std=c++20 -fsyntax-only` with warnings enabled; changed Python modules passed `compile(..., "exec")`. In-memory Python smoke tests confirmed repeated-chirp withholding, seeded stationary-noise acceptance, and THD withholding on a deliberately clipped synthetic output.
- **Not yet executed:** full C++ binaries/CTest (including `SchematicParityTest` and the new embedded IC4_B KCL regression), complete Python unittest suite, cross-platform JUCE builds, and GitHub Actions. A manual workflow trigger was added to CI because prior Git-database ref updates did not produce a workflow run. **Do not label CI green until a run exists.**
- **Still open:** audited retail schematic/revision, real raw capture and absolute electrical calibration, level-dependent J201 implicit source feedback, CMOS D1/D2 nonlinear Shockley current rather than approximate hard clamps, op-amp saturation feedback in Baxandall/MidBand, finite-bandwidth op-amp and DI/XLR path, and comparison across real units.

No method here can infer 1:1 retail topology from an independently designed clone. The next evidence milestone is a named real-unit measurement set with controls, clock, level, load, reference volts/FS and repeatability published alongside the raw WAVs.
