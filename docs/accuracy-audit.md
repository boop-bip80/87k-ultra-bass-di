# Accuracy audit — how close is the plugin to the circuit?

> Written 2026-11-05 after an independent re-measurement. **Read this before quoting any accuracy
> claim about this project.** The earlier README described the plugin as "solved … from real component
> values rather than approximated by ear". That was not a fair summary of the shipped build, and this
> document says what is and is not true, with numbers a reader can reproduce.

## 1. Short version

1. The plugin ships a **capture-calibrated** model, not a model of the drawn schematic. 34 constants
   differ from the drawn component values (`trebleC7` by 132×, `clipC15` by 423×), and three OD-path stages
   (`OdToneRestore`, `OdMakeup`, `OdDriveTilt`) are explicitly *non-schematic* corrections added to pull
   the render toward reference captures. Against an independent SPICE simulation of the drawn circuit the
   shipped build is **9–12 dB rms off in the overdrive path** and 1.5–10 dB rms off in the EQ.
2. The captures themselves are **not in this repository** (`analysis/captures/` is gitignored), so the
   shipped calibration cannot be re-verified from a clone. Its quality claims rest on numbers recorded in
   source comments by earlier sessions.
3. The schematic being modelled is the **original-B7K clone board** ("Black Mirror VII"), not a Darkglass
   B7K *Ultra* schematic. MASTER, the 3-way ATTACK centre position, the switchable mid caps and the second
   (DIST) footswitch are engineered approximations (`[ENG]` in `circuit.md`).
4. An independent ngspice netlist now exists (`analysis/spice_reference/`). Using it found **three
   structural modelling errors**, all fixed (opt-in or in-place, see §5), and added a
   `FitParams::schematic()` configuration — every fitted constant put back to its drawn value, the
   empirical stages bypassed, a physical CD4049 clipper — that tracks ngspice closely:
   clean path 0.01–0.41 dB rms, overdrive small-signal ≈1 dB rms, CD4049 stage large-signal within
   0.3 % swing and ≈0.5 dB per harmonic, and the whole OD chain at large signal within 0.56 dB (H1) / 1.0 dB (H3) of
   ngspice (H2 at heavy drive does not match, §7). That configuration is gated by `ctest` (`SchematicParityTest`).
5. **What this does not establish:** that either configuration matches a real Darkglass B7K Ultra. The
   drawn circuit and the captured unit are different things (`circuit.md` already records four places
   where the captures disagree with the drawn values). The shipped calibration is the better match to the
   captures *as of its last fit*; the schematic configuration is the better match to the drawn circuit.
   No claim of unit-level parity is made or supportable from this repository.

## 2. What "the circuit" means here

| Source | What it is | Status |
|---|---|---|
| `schematics/Primary BOM and schematic.pdf` p.4 | Black Mirror VII (B7K clone) rev 1.1v | read at 300 DPI on 2026-11-05; input network, JFET stage, treble/ATTACK ladder, GRUNT/clipper, bridged-T, both Sallen-Keys, LEVEL/BLEND, tone stack, mids, output all agree with `circuit.md`'s node graphs |
| `[ENG]` parts | MASTER pot, ATTACK centre throw, switchable mid caps, DIST footswitch | engineered; no schematic exists in the repo |
| Reference captures | recordings of an Ultra | **not in the repo**; used for every `FitParams` default |

## 3. Method — an independent reference

`analysis/spice_reference/b7k_spice.py` builds an ngspice netlist from the drawn values (no code shared
with the plugin), with single-pole op-amps, the SPICE-typical J201 (`VTO −0.718 V, BETA 1.031 mA/V²`) and
the two-MOSFET CD4049 of Köper & Holters (DAFx-2020) with R19 as a self-consistent supply dropper.

* The inverter model reproduces `circuit.md`'s independently-derived bias point
  (**Vm 2.657 V, VDD 5.636 V, 3.01 mA**), which checks both that derivation and the netlist.
* Limits: the J201 has a ~5:1 unit spread, so the JFET block is a *typical-part* reference; the paper's
  MOSFET fits are only measured over |vGS| = 2.5–6 V; op-amps are ideal here (the plugin assumes ideal ones
  too); the CD4049's own input-protection diodes and output-load current are not modelled by either side.
* Scripts: `compare_clean.py`, `compare_od.py`, `compare_clipper.py`, `compare_chain.py`; the committed
  gate data is `tests/data/schematic_reference.h` (`gen_reference.py` regenerates it).

## 4. Results

Numbers are dB, plugin minus ngspice, 30 Hz–16 kHz unless stated. "schematic" = `OfflineRender --schematic`
(`FitParams::schematic()`); "shipped" = the calibrated defaults.

### 4.1 Clean path (BLEND = clean, jack → OUT) — `compare_clean.py`

| setting | schematic rms / max | shipped rms / max |
|---|---|---|
| flat | 0.01 / 0.06 | 1.72 / 4.92 |
| BASS boost | 0.01 / 0.06 | 5.24 / 15.39 |
| BASS cut | 0.01 / 0.05 | 1.48 / 3.16 |
| TREBLE boost | 0.17 / 0.49 | 1.99 / 4.90 |
| LO-MID boost / cut (1 kHz) | 0.06 / 0.18 · 0.06 / 0.13 | 7.80 / 19.15 · 10.16 / 23.31 |
| HI-MID boost / cut (3 kHz) | 0.41 / 1.38 · 0.40 / 1.27 | 1.83 / 4.92 · 1.82 / 4.92 |
| all boost / all cut | 0.29 / 1.05 · 0.32 / 1.06 | 8.16 / 18.46 · 10.09 / 23.15 |

The residual in the schematic column is the 48 kHz base-rate bilinear warp of the 3 kHz peaking stage
near Nyquist. The shipped column is dominated by the *deliberate* fitted mid-band caps/ratio/wiper-R and
R36 (see §6) — it is the distance from the drawn board, not a bug.

### 4.2 Overdrive path, small signal (BLEND = OD, LEVEL max) — `compare_od.py`

| | schematic rms / max | shipped rms / max |
|---|---|---|
| GRUNT cut, DRIVE 0 / 0.5 / 1 | 0.93 / 4.5 · 0.95 / 4.6 · 0.95 / 4.6 | 8.8 / 14.7 · 9.3 / 14.5 · 9.9 / 21.3 |
| GRUNT flat, DRIVE 0 / 0.5 / 1 | 1.03 / 4.6 · 1.04 / 4.7 · 0.99 / 4.7 | 10.6 / 19.8 · 11.0 / 24.3 · 11.4 / 26.7 |
| GRUNT boost, DRIVE 0 / 0.5 / 1 | 1.09 / 4.6 · 1.10 / 4.7 · 1.07 / 5.1 | 11.2 / 18.6 · 11.4 / 21.2 · 11.6 / 20.2 |

The schematic maxima sit on the ~320 Hz ladder-notch flank and at 12 kHz (deep in the SK stop-band);
`SchematicParityTest` gates ≤ 1.5 dB at 40–160 Hz and 640 Hz–4 kHz (measured worst 0.82 dB).
Shipped is 9–12 dB *quieter* than the drawn circuit through the mid/high band at small signal.

### 4.3 CD4049 clipper, large signal — `compare_clipper.py`

Ideal sine at the DRIVE-stage output, ngspice transient of the drawn loop vs the plugin. Peak-to-peak
swing at the clipper output (V):

| input amplitude | 0.1 V | 0.3 V | 1 V | 3 V |
|---|---|---|---|---|
| ngspice (drawn loop, 1 kHz, GRUNT cut) | 1.64 | 3.97 | 6.81 | 8.58 |
| plugin physical clipper (`clipPhysical`) | 1.64 | 3.98 | 6.82 | 8.61 |
| **shipped fitted-sigmoid clipper** | **0.89** | **1.03** | **1.04** | **1.04** |

The physical mode matches ngspice to ≤ 0.3 % swing, ≤ 0.1 dB H1 and ≈ 0.5 dB for H2–H5 over a 50 dB drive
range and all three GRUNT positions. **The shipped clipper swings ~1.04 Vpp where the drawn circuit swings
~8.6 Vpp** (R19 only drops the rail while the inverter conducts crowbar current; with the output high there
is no drop, so the output reaches the ~8.65 V rail), and has a ~35 mV-wide knee, so its harmonic ladder is
much harder: at 1 V in, H3 is −9.9 dB re fundamental against ngspice's −19.5 dB.

### 4.4 End-to-end large signal — `compare_chain.py` (BLEND = OD, drive 0.5, GRUNT cut, 440 Hz)

Whole OD chain (J201 shaper, rails, bridged-T, both SKs, LEVEL/BLEND, EQ, MASTER) against ngspice with
op-amps clamped to the same rail window. **The plugin must use the same MOSFET model as ngspice for this to
be a like-for-like test** (`--phys 1`, the simple model); with that, over −30…−6 dBFS input (`SchematicParityTest`,
base-rate 96 kHz): **|ΔH1| ≤ 0.56 dB, |ΔH3| ≤ 1.0 dB**, and H2 tracks to ~0.6 dB at −30/−24 dBFS. H2 does *not*
track from −18 dBFS up: two even-order sources (J201 and clipper) cancel near −18 dBFS so the dB difference there
is just where the null lands (ngspice −44 dB, plugin −60 dB), and at −6 dBFS the plugin's H2 is ~6 dB high.
Shipped calibration: fundamental **−5.5…−8.9 dB** vs ngspice.

⚠ **Correction of an earlier draft of this audit.** It attributed a ~20 dB H3-onset mismatch to the J201
waveshaper. That was wrong: it compared the plugin's *extended* MOSFET model against ngspice's *simple* one.
The two published models of the same chip give very different low-level harmonics (H3 at −30 dBFS: simple
−58 dB, extended −77 dB), which is a statement about uncertainty in the *chip model*, not about either
implementation. `schematic()` uses the extended model (the paper's own best fit, with the caveat that its
regressions are measured only for |vGS| ≥ 2.5 V and the trip point sits at that edge); `clipPhysical = 1`
selects the simple one.

## 5. Structural errors found, and what was done

| # | Error | Evidence | Fix |
|---|---|---|---|
| 1 | **C21** (100 n, IC5_B → tone stack) was a first-order high-pass into **one fixed resistance** (`c21R`). The stack's input impedance depends on the BASS/TREBLE knobs and on frequency, so the true corner moves with the knobs. | at the drawn 100 n a fixed R is wrong by up to ±15 dB at 20 Hz depending on the BASS knob; no single R is right at two knob positions | C21 is now an 8th MNA node inside `Baxandall` (`setInputCap`). Drawn 100 n: 0.06 dB vs ngspice. **Shipped default changed**: `c21Cap = 1 µF` reproduces the old hardware-anchored `c21R = 130k` response at flat / bass± (0.28 / 0.07 / 0.29 dB rms, 20 Hz–2 kHz) while making the knob dependence physical. `c21R` remains as the legacy path (`c21Cap = 0`). |
| 2 | **J201 active load (Q2)** modelled as a fixed `Rq2 = 1 MΩ` "fully bootstrapped across the audio band". Q2's gate sits on R9‖R10 (≈500 k) with C4 (22 n) bootstrapping it from the output, so Q2 is a series R = 1/gm2 + L = τ/gm2 (τ = 11 ms) to AC ground: ~1.4 kΩ at LF, ~19 kΩ at 200 Hz, ~100 kΩ at 1 kHz. | the treble-network output (IC2_A input) was **+28 dB too loud at 40 Hz** (+37 dB at 20 Hz); with the fix it matches ngspice to < 1 dB from 40 Hz to 4 kHz | `TrebleAttack::setActiveLoad` (an 8th node, isolated when off). Opt-in (`jfetQ2Gm > 0`), on in `schematic()`. ⚠ The shipped 17-value ladder, `trebleC7` (132× below drawn), `jfetGm` (7× below) etc. were fitted **on top of the wrong structure**, and plausibly compensate for exactly this. They have not been re-fitted (no captures here). |
| 3 | **Bridged-T treated as unloaded**, then fed into an ideal-source Sallen-Key. R24 and the SK input network load it heavily above ~1 kHz. | real node 1.1 dB below the unloaded stage at 1 kHz, 2.3 dB at 2 kHz, **4.4 dB at 4 kHz**, ~6 dB at 8 kHz | `RecoveryBridgedT::setCoupledSk` solves [Nmid, Nout, N1, Kp] as one network. Opt-in (`btCoupledSk`), on in `schematic()`. Every OD stage tap then matches ngspice within ~0.5 dB. |

Not an error but now exposed: the CD4049 VTC (§4.3) is selectable (`clipPhysical` 0 fitted / 1 simple /
2 extended two-MOSFET model with R19 sag, `CmosInverterVtc.h`).

## 6. Where the shipped calibration departs from the drawn circuit

Generated with `OfflineRender --print-fit` vs `--schematic --print-fit`. Ratio = shipped / drawn.

| group | constants (shipped vs drawn) |
|---|---|
| treble / ATTACK ladder (the "17-value set") | `trebleR7` 1.65 MΩ vs 200 k (×8.2); `trebleC7` 756 pF vs 100 n (×0.0076); `trebleC6` 1.39 n vs 22 n (×0.063); `trebleC9` 12.8 n vs 22 n; `trebleC5` 7.96 n vs 22 n; `trebleLadderR12` 27.1 k vs 6.8 k (×4); `trebleLadderR14` 48.5 k vs 22 k; `trebleC8` **0 vs 220 pF** (the drawn ATTACK cap is removed); `attackTapRa/Rb/Rc/R11` 393 k / 420 k / 77 k / 164 k vs 470 k / 0 / 0 / 470 k (R11 ×0.35); `trebleLadderDampR` 15.4 k vs 0 (a series loss that does not exist on the board); `attackDampBoost/Cut` 7.06 k / 118 Ω; `attackC5TrimBoost/Cut` +0.10 n / +0.32 n |
| clipper | `clipC11` 3.69 n vs 4.7 n; `clipC15` **5.2 n vs 2.2 µ (×0.0024)**; VTC swing 1.04 Vpp vs ~8.6 Vpp (§4.3) |
| J201 stage | `jfetGm` 0.10 mS vs 0.695 mS; `jfetRo` 200 k vs 4.35 M; `jfetRq2` 1 M vs 4.18 M and no Q2 bootstrap; waveshaper shape constants fitted |
| EQ | mid caps: LO 6.8 n / 3.9 n / 2.2 n (+ C32 = 10× C33) vs 22 n / 22 n (C33 / C32); HI 2.7 n / 1.5 n / 680 p (+ C34 = 10× C35) vs 680 p / 6.8 n; a **6.8 k series R in each mid wiper leg that is not on the schematic**; `trebleWiperR` 4.7 k vs 3.3 k; `c21Cap` 1 µ vs 100 n |
| level/blend | `blendEndStop` 0.0242 (an [ENG] wiper end stop) vs 0 |
| non-schematic stages | `OdToneRestore` (drive-tracking RBJ notch/peak around 323 Hz), `OdMakeup` (+6 dB makeup with low/high shelves and an HF peak), `OdDriveTilt` (envelope-driven 5.4 kHz shelf) — none of which is a circuit element; bypassed by `odEmpirical = 0` |
| pot tapers | `driveTaperExp`, 4-segment LEVEL and 3-segment MASTER piecewise-linear tapers are fits (kept in `schematic()`; a taper is not a schematic quantity) |

`FitParams.h` itself records that the ladder block "is an OD-path ABSOLUTE-LEVEL fix that happens to live
in the ATTACK ladder", that "the notch width/depth and the absolute OD level are in genuine CONFLICT in
this topology", and that the model's OD path "saturates too early". §5 item 2 is a candidate root cause
for that conflict; it has not been tested against captures.

## 7. What is still not modelled or not verified

* **J201 large-signal behaviour.** The stage is a Wiener–Hammerstein model (input HP → degeneration
  shelf → memoryless square-law shaper with an asymmetric ceiling → Norton current) whose shaper constants
  are fitted, not derived. Against ngspice end-to-end it tracks H1/H3 well (§4.4) but **even-order content at
  heavy drive does not match** (H2 up to ~6 dB high at −6 dBFS, and the cancellation null lands elsewhere).
  The true degeneration is implicit nonlinear feedback; that is the candidate cause, untested.
* **Which CD4049 model is right.** The simple and extended two-MOSFET models disagree by ~20 dB in low-level
  H3 (§4.4). Without a measurement of the actual chip the project cannot say which is closer.
* **CD4049 beyond the static model**: the chip's input-protection diodes to its *dropped* VDD pin, output
  load current, and input/output capacitances are ignored; the extended model's regressions are measured
  only for |vGS| ≥ 2.5 V and the trip point is at its edge; the real small-signal gain is not pinned by the
  paper (the extended model gives ≈165 at the trip point, the community figure for the real part is 20–30).
  The physical clipper is therefore verified against **ngspice with the same device equations**, not
  against a real chip.
* **Op-amp finite GBW / slew / rails**: a 3 MHz TL07x GBW costs 0.3 / 0.9 / 1.6 dB at 4 / 8 / 16 kHz at
  DRIVE max (< 0.1 dB at DRIVE min) in ngspice; the plugin assumes ideal op-amps in both configurations.
  Rails are the fitted ±2.9 / 2.7 V windows in both configurations.
* **ATTACK centre ("Flat")** and everything `[ENG]`: engineered, unverified.
* **XLR balanced DI**: not modelled.
* **The captured Ultra.** Nothing here tests the plugin against hardware.

## 8. Reproducing

```bash
sudo apt-get install ngspice            # or your package manager
pip install numpy scipy
cmake --build build --target OfflineRender
python3 analysis/spice_reference/compare_clean.py
python3 analysis/spice_reference/compare_od.py
python3 analysis/spice_reference/compare_clipper.py --probe <compiled clipper probe> --freq 1000 --grunt cut --phys 1
python3 analysis/spice_reference/compare_chain.py --freq 440 --drive 0.5 --grunt cut
ctest --test-dir build -R SchematicParityTest --output-on-failure      # needs no ngspice
```

`OfflineRender --schematic` renders the drawn-value configuration. In the plugin it is the **MODEL / DRAWN**
button (APVTS `schematic_model`, default off) in the bottom strip; `cmake -DOBSIDIAN_SCHEMATIC_MODEL=ON` makes
DRAWN the default. It is a different sound — tighter and louder in the OD path — not a "better" default: the
calibrated build is the one fitted to the reference captures. Both CD4049 tables are built in
`prepareToPlay` (about 0.3 s the first time per process) so the live switch only copies pointers.
