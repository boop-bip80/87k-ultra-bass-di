# Round 6 — sensitivity to unknown circuit/device parameters

**No retail hardware parity established.** This analysis is an independent,
deterministic engineering sensitivity experiment, not a fitted B7K Ultra
netlist, Monte Carlo manufacturing forecast, or hardware measurement.

Run from the repository root:

```bash
python3 analysis/circuit_uncertainty.py --json
python3 -m unittest discover -s analysis -p 'test_circuit_uncertainty.py'
```

The script is Python-stdlib only and emits a deterministic JSON result. The
seven tests check source-divider arithmetic, J201 bias/KCL, harmonic trends,
diode monotonicity, op-amp-rail bounds, numerical finiteness and report stability.
It makes **no percentage-accuracy score**.

## Evidence levels and parameters

| Quantity | Values used in the experiment | Provenance / confidence |
| --- | --- | --- |
| R6, C3 in clone Q1 source circuit | 3.3 kΩ, 220 nF | [Black Mirror VII 1.6v BOM](https://pcbguitarmania.com/wp-content/uploads/2022/07/Black-Mirror-VII-1.6v-Building-Docs.pdf); **not retail topology** |
| J201 `I_DSS` | 0.2, 0.6, 1.0 mA | Endpoints from [Central Semiconductor J201 data sheet](https://central.aeminc.com/datasheets/J201.PDF), middle point chosen for sampling |
| J201 `|V_GS(off)|` | 0.3, 0.9, 1.5 V | Datasheet endpoints; **joint IDSS/Vp distribution unknown**, 3×3 grid is not a tolerance probability |
| Q1 `r_o` | 200 kΩ | Repository model stand-in; **not a measured J201 device** |
| Diode `I_s`, `nV_T` | `{0.1,5,100} nA`, `{26,52,78} mV` | Deliberate model-choice envelope; [Vishay 1N4148 data sheet](https://www.vishay.com/docs/81857/1n4148.pdf) does **not** uniquely determine either parameter |
| Input loading | Black Mirror approximately 500 kΩ; hypothetical Ultra nominal reference 1 MΩ | Clone R1∥R2 vs [Darkglass Ultra nominal specification](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual); no retail input circuit published |
| Op-amp example | Static gain 1,000/100,000; generic feedback factor 0.1; symmetric ±2.7/3.3 V rails | Pure illustrative feedback/rail study, **not** a traced TL07x output or mid-EQ network |

The J201 model takes the textbook long-channel relation
`I_D=β(max(0,Vp−Vs+Vg))²`, with `β=I_DSS/Vp²` and DC self-bias
`Vs/R6=β(Vp−Vs)²`. The coupled R6/C3 source capacitor is integrated with
trapezoidal companions; the instantaneous source-node equation is solved by
bisection. The small-signal `G_m(s)` is
`g_m/[1+(g_m+1/r_o) Z_s(s)]`.

These equations explicitly **exclude** Q1 finite drain compliance,
Q2 active-load transistor behavior, input coupling, pickup inductance,
op-amp dynamics and the rest of the pedal. They cannot be used as an
absolute prediction of physical output gain or harmonic spectra.

## Reproduced results from the declared envelope

| Sensitivity experiment | Observed span | Interpretation |
| --- | ---: | --- |
| Q1 incremental `|G_m|` at 40 Hz | 6.71 dB | Magnitude of modeled drain-current transconductance, **not pedal dB gain** |
| Q1 incremental `|G_m|` at 1 kHz | 14.26 dB | Large possible sensitivity to unselected J201 operating point |
| Q1 incremental `|G_m|` at 8 kHz | 18.59 dB | C3 degenerative shelf makes frequency and device interaction important |
| Q1 incremental drain-current H2/H1 for 0.2 V *gate* sine | −28.40 to −11.47 dBc | Assumed ideal square-law device, not a hardware THD result |
| Same for 0.6 V gate sine | −18.65 to −4.94 dBc | May cross model cutoff; omits physical drain saturation |
| Assumed Shockley diode drop at 100 µA | 0.180–1.078 V | Parameter choices are intentionally broad; do not treat as 1N4148 measured spread |
| Pure 100 kΩ source loaded by 500 kΩ vs 1 MΩ | +0.7558 dB for 1 MΩ | **Only** a resistive divider what-if; real pickups are reactive |
| Illustrative feedback output, input 0.2 V, four gain/rail cases | 1.980–2.000 V | Finite open-loop gain changes output in this *chosen* 10× feedback example |
| Same circuit, input 0.4 V, four gain/rail cases | 2.70–3.30 V | Assumed rail headroom dominates this saturated example |

The harmonic numbers are computed from a coherent 512 Hz gate excitation,
32,768 Hz digital integration rate, transient discard and DFT of *incremental
Q1 drain current* only. At very low harmonic levels, the omitted real noise
floor would dominate; use the high-drive H2 rows for directional insight.

**Sensitivity does not equal accuracy:** a 14 dB model envelope does not mean
that a retail B7K Ultra varies by 14 dB, nor that any one parameter value is
the correct one. The physical distribution, correlations and actual circuit
operating point have not been measured.

## Falsifiable next measurements

1. Measure the **actual Ultra** complex input impedance over 30 Hz–10 kHz
   with several source impedances and pickup/cable equivalents. A single
   manufacturer nominal value cannot identify the input R/C topology.
2. Identify the J201 and its **actual DC bias/rail compliance** in one
   specified hardware revision; where safe, obtain device I–V curves or
   substitute a circuit-traced SPICE model. Then compare staged Gm, phase and
   H2/H3 vs measured, level-aligned pedal output before selecting `β,VTO`.
3. Characterize the active clamp diodes *under conduction*, not by adjusting
   guessed `I_s,nV_T` to match already saturated audible output. Their
   contribution may be essentially inert at regular playing levels.
4. Measure op-amp loaded swing, asymmetry and recovery time. Only then assign
   frequency-dependent gain, slew and hysteresis to the research EQ solvers.
5. Publish acquisition voltages, signal paths, manifest hashes, repeatability
   and held-out-unit comparisons using
   [the retail qualification protocol](retail-parity-qualification.md).

Until these physical references exist, the testable outcome is **bounded
model uncertainty**, not a verified circuit or audio match.
