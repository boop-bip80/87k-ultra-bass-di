# Schematic provenance and limits of the B7K Ultra model

This document separates independently published pedal specifications from a third-party clone circuit, simulation checks, and project-specific engineering choices. It is an evidence ledger, **not** a declaration of 1:1 circuit or acoustic parity with any retail Darkglass unit.

## Source hierarchy

| Source | What it supports | What it does **not** establish |
| --- | --- | --- |
| [Darkglass Microtubes B7K Ultra manual](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual) | Manufacturer-stated user-facing controls, switch options, nominal EQ centers, connections, and electrical specifications for the documented revision. | Internal component values, op-amp/JFET/CMOS types, exact filter transfer functions, switch wiring, saturation topology, or manufacturing tolerances. The manual is not a factory schematic. |
| [PCB Guitar Mania Black Mirror VII 1.1v building documentation](https://pcbguitarmania.com/wp-content/uploads/2021/07/Black-Mirror-VII-1v1-Building-Docs-18-10-2022.pdf) | Parts list and published schematic for an independently produced **B7K-inspired clone PCB**. Its component values can be transcribed and its schematic simulated. | That the drawing represents a factory Darkglass B7K, B7K Ultra, or any specific retail hardware revision. It is especially not an authenticated B7K Ultra circuit diagram. |
| [PCB Guitar Mania Black Mirror VIII product page](https://pcbguitarmania.com/product/black-mirror-viii/) | Existence of a **separate**, explicitly *Ultra-inspired* DIY project, described by its maker. | Equivalence between the VII and VIII schematics, or between either DIY board and Darkglass hardware. The advertised seven controls are not the official Ultra's eight rotary controls. |
| Project `schematics/Primary BOM and schematic.pdf` | Local copy of the Black Mirror VII-style source drawing used for this project's component model. | Any independent authentication as a genuine Darkglass schematic. The filename alone cannot establish origin. |
| Project `docs/accuracy-audit.md` | The model's internal schematic-to-ngspice comparisons, fitted-versus-drawn value deviations, and stated uncertainty. | Retail hardware validation: independent ngspice agreement for the same input drawing is not hardware ground truth. |

The two public PCB Guitar Mania projects must not be conflated. In particular, the VII PDF provides no basis for treating its circuit values as a manufacturer-authenticated Ultra netlist. No publicly authenticated factory B7K Ultra schematic has been established by this audit.

## Independently checkable control and nominal-specification baseline

The manufacturer's B7K Ultra manual describes **eight rotary controls**: Master, Level, Blend, Drive, Bass, Lo Mid, Hi Mid and Treble. Nominal EQ specification: Bass ±12 dB at 100 Hz; Lo Mid ±12 dB with selectable **250 / 500 / 1000 Hz** centers; Hi Mid ±12 dB with selectable **750 / 1500 / 3000 Hz** centers; Treble ±12 dB at 5 kHz. Grunt and Attack each offer three switch positions. The cited manual also specifies a 9 V center-negative supply, 120 mA consumption, 1 MΩ input impedance and 1 kΩ output impedance. These are **manufacturer-published nominal values**, not measured responses and not proof of any one circuit implementation. Confirm the exact retail version/revision before applying these criteria to a target unit.

The Black Mirror VII 1.1v parts list instead lists **seven** pots—Blend B100k, Drive C100k, Hi B100k, Hi-Mids B100k, Level A100k, Lo B100k, Lo-Mid B100k—with no Master. Its Ultra-Hi SPDT modification is **two-position**, not an authenticated three-position Ultra Attack network. Thus a model that adds Master or three-way Attack to the VII topology is implementing Ultra-like *behavior*, not recovering the manufacturer's unseen circuit.

## Black Mirror VII component facts and document caveats

From the published VII 1.1v PDF: IC1, IC2 and IC4 are **TL072ACP**; IC5 and IC6 are **TL074ACN**; IC3 is a **CD4049N**, specified as **unbuffered** in the building instructions; Q1 and Q2 are **J201** JFETs; D1 and D2 are **1N4148**, and D3 is **1N5817**. The schematic's 1N4148 pair belongs to supply/rail clamping rather than an authenticated audio diode clipping stage; a diode count alone is insufficient evidence for the distortion topology. Values here identify *this clone board*, not Darkglass production parts.

The PDF's suggested Lo-Mid modification table associates C33 values of 100 nF, 47 nF, 22 nF, 10 nF and 5 nF with approximate peaks of **168, 236, 340, 500 and 708 Hz**, respectively, with substantial stated peak gains. Its Hi-Mid modification table associates C35 values 3300 pF, 2200 pF, 1000 pF, 680 pF and 500 pF with approximate peaks **1.55, 1.9, 2.8, 3.3 and 4 kHz**. These are clone-design modification estimates and should **not** be mapped uncritically to the manufacturer's selectable Ultra centers. The PDF is internally inconsistent about the *stock* C33 value (prose suggests 10 nF while BOM/drawing indicate 22 nF); follow the actual source drawing for a simulation and record the discrepancy. Do not silently resolve it as a real-pedal fact.

## Current implementation: explicit boundaries

`docs/accuracy-audit.md` identifies materially non-schematic fitted parameters. Examples include `trebleR7` **1.65 MΩ fitted versus 200 kΩ drawn**, `trebleC7` **756 pF fitted versus 100 nF drawn**, `clipC15` **5.2 nF fitted versus 2.2 µF drawn**, and `jfetGm` **0.10 mS fitted versus approximately 0.695 mS typical J201**. It also identifies empirical `OdToneRestore`, `OdMakeup` and `OdDriveTilt` correction stages without a counterpart in the VII drawing. The reported roughly **1.04 Vpp** model saturation ceiling versus about **8.6 Vpp** in a modeled physical rail is a particularly material headroom discrepancy. Those parameters may improve a chosen fitting target; they must not be described as original component extraction or first-principles Ultra circuit reproduction.

The project reports independent SPICE comparison against a simulation of **the same clone drawing** at its `FitParams::schematic()` values (sub-dB clean residuals and approximately 1 dB overdrive residuals on tested synthetic conditions). This is a valuable software correctness check **relative to the drawing**. It says nothing by itself about the transfer function, component topology, noise, or headroom of genuine B7K Ultra hardware. The fitted model and engineered Ultra control additions require separately labeled tests. The stated lack of a fully modeled XLR differential/balanced DI path is another unresolved hardware feature.

## Other public leads, not primary proof

- A [freestompboxes.org B7K Ultra discussion](https://www.freestompboxes.org/viewtopic.php?t=30748&start=40) contains community reports that a nominal 250 Hz mode was observed around 370 Hz. The unit/revision, measurement conditions and calibration are not verified here. Treat this as a hypothesis for measurement, not a replacement official value.
- [AdmiralBumblebee's hardware/plugin comparison](https://www.admiralbumblebee.com/music/2018/11/10/darkglass-ultra-plugins-vs-hardware) is a promising first-hand test lead. Source content was not directly accessible during this audit (HTTP 403); **no quantitative claims from search snippets are adopted here**.

## Required evidence before asserting retail-hardware parity

1. Identify **the exact target unit** (B7K Ultra generation, V1/V2 or other revision, serial/revision as available), its controls, and the measurement path.
2. Obtain independent, repeatable hardware captures with documented interface impedance, supply voltage, sample rate, instrument/load, knob angles and switch positions. Sweep the complete control matrix, rather than matching a few presets.
3. Measure magnitude **and phase**, frequency centers/Q, input-to-output level, blend polarity, small-signal response, drive/level-dependent compression and harmonics, attack/release/memory where present, headroom/clipping onset, noise, latency, bypass behavior and power/supply sensitivity.
4. Characterize separately the normal output, headphone/auxiliary paths and **balanced DI/XLR** if available, including level, impedance and common-mode rejection; use proper electrical test fixtures.
5. Distinguish parameter-fitting results from topology claims. Hardware sonic agreement across stated test conditions is possible without proof of original internal circuitry; **physical circuit parity** requires authoritative internal evidence or rigorous independently documented PCB tracing and component identification.

**Claim status:** *Clone-informed, component-structured model with engineered Ultra functional additions and incomplete retail validation*. No factory B7K Ultra netlist, comprehensive hardware measurement set, or verified 1:1 circuit parity is established by the sources above.
