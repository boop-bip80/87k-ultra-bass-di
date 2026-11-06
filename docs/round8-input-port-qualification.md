# Round 8 — input-port impedance: clone prediction versus retail specification

**Evidence status, 2026-11-06:** A frequency-dependent impedance is derivable
from the **transcribed Black Mirror VII input network**. This is a calculated
small-signal property of a particular documented DIY topology, **not a
measurement of a retail B7K Ultra**. The manufacturer's nominal 1 MΩ figure
does not authenticate any internal retail circuit or fix a frequency-by-frequency
impedance tolerance. No real pedal input-impedance sweep is supplied here.

## Scope and revision custody

| Source | What it establishes | What it does **not** establish |
|---|---|---|
| [Darkglass B7K Ultra manual](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual), Technical Specifications | Retail **B7K Ultra** nominal input **1 MΩ**, output **1 kΩ**, external 9 V DC supply | Specific testing frequency/method, complex input impedance, topology or production-board revision |
| [Darkglass B7K *non-Ultra* manual](https://www.darkglass.com/en-eu/pages/microtubes-b7k-manual), Technical Specifications | Separate **non-Ultra** pedal nominal input **500 kΩ** | An Ultra specification or proof that the clone implements a retail non-Ultra board |
| Bundled `schematics/Primary BOM and schematic.pdf`, document **1.1v, 9 May 2023**, BOM p. 1–2, schematic p. 4 | R1=1 MΩ from input jack to ground, C1=100 nF in series to buffer input, R2=1 MΩ from post-C1 node to VD, R3=10 kΩ into the TL072 input | Installed values on any real board, retail Darkglass topology |
| [Black Mirror VII public **1.6v**, 20 Aug 2022 build document](https://pcbguitarmania.com/wp-content/uploads/2022/07/Black-Mirror-VII-1.6v-Building-Docs.pdf), BOM pp. 3–4 | Maker's R1=1 MΩ, R2=1 MΩ, C1=100 nF parts for this named DIY revision | Identical net connectivity across revisions merely because designators/values agree; retail authenticity |

The local 1.1v primary is **not** the same file as the maker's older
18 Oct 2022 "1.1v" publication. The 2023 local document specifies a TL074ACN
for IC6, while the older public 1.1v BOM specifies TL072ACP for IC6. The
public 1.6v BOM also differs elsewhere from the bundled 2023 drawing (for
example C33 and the Ultra-Hi switch), so component tables must keep their
revision identifiers attached. See `docs/round7-retail-evidence.md`.

## Independently calculable property of the transcribed input

The isolated circuit assumed for this calculation is:

1. Input jack to R1 to signal ground (**shunt**).
2. Input jack through series C1 to a post-cap node.
3. Post-cap node through R2 to VD, treated as **AC ground**.
4. Post-cap node through R3 into an **ideal infinite-impedance** op-amp input.

The last assumption is an approximation. It deliberately omits TL072 finite
input impedance/capacitance, PCB parasitics, powered versus unpowered behavior,
switch and bypass routing, virtual-reference finite impedance, and real-part
tolerances. None is quantified by a BOM.

For angular frequency `ω = 2πf`, `s = jω` and the listed nominal components:

```text
R1 = R2 = 1,000,000 ohms; C1 = 100e-9 farads
Yin(s) = 1/R1 + 1/(R2 + 1/(s C1))
Zin(s) = 1/Yin(s)
       = R1 (1 + s R2 C1) / (1 + s (R1 + R2) C1)
```

This is a **complex** impedance: report `Re(Zin)`, `Im(Zin)`, `abs(Zin)`
and `arg(Zin)`, not just a single resistive number. Its DC limit is **1 MΩ**;
its infinite-frequency resistive limit is **R1 || R2 = 500 kΩ**. The familiar
`1/(2π R2 C1) = 1.59155 Hz` is the *isolated buffer coupling-filter*
corner, not a license to ignore R1 in the input-port impedance.

| Frequency | Re(Zin) | Im(Zin) | `abs(Zin)` | phase |
|---|---:|---:|---:|---:|
| DC | 1,000,000 Ω | 0 Ω | 1,000,000 Ω | 0° |
| 1 Hz | 693,863.3 Ω | −243,615.8 Ω | 735,387.6 Ω | −19.3462° |
| 20 Hz | 500,790.3 Ω | −19,862.9 Ω | 501,184.1 Ω | −2.2713° |
| 100 Hz | 500,031.7 Ω | −3,978.6 Ω | 500,047.5 Ω | −0.4559° |
| 1 kHz | 500,000.3 Ω | −397.9 Ω | 500,000.5 Ω | −0.0456° |
| 20 kHz | 500,000.0 Ω | −19.9 Ω | 500,000.0 Ω | −0.0023° |

*Table is mathematical prediction for nominal parts, **not simulation
measurements or a physical sweep**. Values are rounded for readability.* At
1 kHz the clone-network magnitude is approximately **half** of the published
Ultra nominal value. That difference is expected **from the chosen topology**;
it is not by itself evidence of a software bug or proof of a particular
retail resistor value. The Darkglass manual does not say that retail `Zin`
is a frequency-independent 1 MΩ, nor identify its nominal test conditions.

`src/dsp/InputBuffer.h` intentionally implements the isolated ideal-source
C1/R2 high-pass in `InputBuffer`. An ideal voltage source is unaffected by a
shunt R1, so that class cannot measure analog input loading. Its **offline**
`InputBufferSourceLoadProbe` models a known Thevenin resistance driving the
clone shunt/series network; `InputBufferNominalPortProbe` separately explores
the counterfactual of a constant resistive 1 MΩ input port. That what-if port
is not a Darkglass netlist and should **not** replace the clone network.
Neither offline probe belongs in the live recording path, where the audio
interface has already applied its own analog input loading.

## Executed software measurements (not physical pedal measurements)

Reproduce the documented clone's *continuous-time* complex impedance and
finite-Thevenin-source transfer with:

```sh
python3 analysis/input_impedance.py 0 1 20 1000 20000 --source-ohm 100000
python3 -m unittest analysis.test_input_impedance -v
python3 -m unittest discover -s analysis -p 'test_*.py' -v
```

The analytical implementation is checked against a separate **70-digit
decimal inverse-admittance** calculation at 10 frequencies and three distinct
R/C sets, and a separately assembled **two-node Kirchhoff matrix** at seven
frequencies and four source resistances. The following errors were measured
locally using nominal code precision and the independent oracles:

| Numerical comparison | Cases | Maximum absolute error | Maximum relative error |
|---|---:|---:|---:|
| Complex `Zin`, versus high-precision nodal admittance | 30 | **2.33 × 10⁻¹⁰ Ω** | **1.72 × 10⁻¹⁶** |
| Complex source-loaded voltage transfer, versus two-node Kirchhoff solve | 28 | **7.07 × 10⁻¹⁴ V/V** | **2.12 × 10⁻¹³** |

An additional independent 1 V / 100 kΩ source-node KCL check at 20 Hz and
1 kHz recovered the same complex port impedance, with maximum algebraic
current-balance residuals **1.63 × 10⁻²¹ A** and **2.72 × 10⁻²⁰ A**,
respectively. These are *idealized matrix residuals*, not instrument-noise
measurements or evidence of a physical TL072's finite input impedance.

The complete Python discovery run passed **62/62 tests**, including the
eight new impedance tests. The preceding numerical comparison counts are
individual frequency/parameter cases *within* those eight tests, not extra
top-level unit tests.

Existing compiled, JUCE-free C++ regressions also passed in this checkout:
`SourceLoadingTest` (32 complex magnitude/phase cases),
`SourceLoadingBoundaryTest` (36 finite-source histories with worst
**6.11 × 10⁻¹⁵ V** absolute deviation), `InputBufferTest` (isolated
high-pass and polarity), and `UltraNominalPortReferenceTest` (84 *what-if*
time-domain cases, worst **3.05 × 10⁻¹⁵ V** deviation). The illustrative
100 kΩ source at 1 kHz produces **0.7558 dB** more voltage in the constant
1 MΩ what-if model than in the clone model. This is a sensitivity difference
between assumptions, **not** an error measured on a retail device.

The new `InputPortACRegressionTest` is registered in CMake and additionally
reconstructs **complex `Zin` from measured WDF transfer phasors** at three
sample rates, six frequencies and two source resistances. Its separate
nodal-admittance reference uses the **bilinear-warped analog frequency** for
the discrete WDF capacitor. In **36/36** local standalone cases, the largest
impedance discrepancy was **0.000412 Ω** (**5.60 × 10⁻¹⁰ relative**), with
maximum magnitude error **4.81 × 10⁻⁹ dB** and phase error
**4.56 × 10⁻⁹ degrees**. These are actual DSP-versus-oracle numerical
residuals, *not* a pedal-hardware measurement. Reproduce without JUCE:

```sh
g++ -std=c++17 -O2 -Ilibs/chowdsp_wdf/include \
  tests/InputPortACRegressionTest.cpp -o /tmp/b7k-input-port-ac
/tmp/b7k-input-port-ac
```

Full JUCE/CMake configuration in this environment was blocked by missing
Linux development packages (`ft2build.h`/FreeType, ALSA, EGL, etc.); it is
therefore **not** reported as an executed full CTest pass. The direct C++
tests above compiled with the pinned `chowdsp_wdf` headers and ran locally.

## Reproducible physical measurement before declaring parity

1. **Identify the test article.** Label it retail B7K Ultra with serial,
   manufacturing/revision evidence, or Black Mirror VII with visible PCB
   revision, document date, installed R1/R2/C1 markings and modification
   history. Include a powered state and bypass/drive/DI control setting.
2. **Calibrate the open-circuit source.** For each sine frequency from
   20 Hz to 20 kHz, record complex source phasor `Vs` open-circuit and the
   known calibrated series resistance `Rs` (for example 100 kΩ). A phase-
   coherent two-channel measurement and a high-impedance buffered voltage
   probe are preferred; record the probe's input admittance and loading.
3. **Measure complex input voltage `Vin` at the pedal jack** at low levels
   where the front end is linear. With a **purely resistive** known source,
   compute `Zin(f) = Rs * Vin(f) / (Vs(f) - Vin(f))` using **complex phasors**,
   not magnitudes; reject or separately quantify ill-conditioned points
   where `Vs - Vin` is near acquisition noise. For a calibrated complex
   `Zs(f)`, replace `Rs` by `Zs(f)`. Remove measured probe admittance before
   inferring the pedal input impedance.
4. **Crosscheck source resistance.** Use at least two calibrated `Rs` values,
   record acquisition uncertainty, repeat at multiple safe low drive levels,
   and report magnitude, real/imaginary components, phase and confidence
   interval; a single `1 kHz` number cannot identify an R/C topology.
5. **Distinguish circuit identity.** For a DIY board, unpowered trace R1,
   C1, R2, R3 node-to-node and observe the assembled parts. For retail,
   do not infer an internal circuit from matching an external spec. Repeating
   a port sweep on two retail units improves representativeness but does not
   prove the internal topology.

## Software validation and acceptance boundary

- **Clone math gate:** independent complex AC nodal equations and code
  agree across the audio band, including DC/high-frequency limits and
  source-loading transfer. Preserve intermediate complex phasors and report
  absolute and relative errors for each method; do not substitute the
  frequency-independent 1 MΩ counterfactual for the clone circuit.
- **Discrete-time gate:** sample-rate and frequency sweeps of the offline
  source-load probe should track the explicitly chosen integration convention,
  or quantify the bilinear-versus-continuous-time difference. Real-time
  `InputBuffer` output with an ideal source remains unaffected by R1.
- **Physical clone gate:** qualify **only the named assembled PCB revision**
  after measured complex input impedance and traced topology agree within
  justified instrument/part tolerances. Reproducing its BOM or matching
  simulator outputs does not meet this gate.
- **Retail gate:** classify as **unverified** until a provenance-verified
  retail specimen is measured; the nominal 1 MΩ published value alone is not
  a retail complex-impedance curve or a percent-accuracy score.

Do not collapse these levels into a single “99% accurate” claim. A model can
pass numerical clone-network tests to machine precision while being entirely
unqualified as a retail Darkglass B7K Ultra hardware reproduction.
