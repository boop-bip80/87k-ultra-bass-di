# Coupled Q1 ↔ ATTACK crossfade: research-only protocol

`CoupledAttackResearch` provides a standalone, opt-in reference at
`src/dsp/CoupledAttackResearch.h`. It intentionally has **no PedalChain hook**.
`JfetStage` and `TrebleAttack` still have precisely their original defaults.

## Why two branches cannot share one JFET

`TrebleAttack::previewCoupledDrainPort()` is a **sample-synchronous Thevenin**
representation of the stateful treble/Q2 network. `JfetStage::processCoupled()`
uses that port to solve Q1 drain current and its R6/C3 source bias, *advancing*
the C2 input capacitor and C3 source bypass histories. Two ATTACK branches can
have different drain ports. Feeding those two ports sequentially into a *single*
JFET would step C2 twice and make C3 dependent on whichever branch was processed
last; the second branch's current would not come from its own source history.

The standalone solves **three independent hypothetical circuits on every
sample**, one for each ATTACK throw:

1. Take that branch's `TrebleAttack` drain-port preview (no state advanced).
2. Solve *that branch's* `JfetStage` physical C2/C3 + Q1 source/drain feedback.
3. Stamp the returned Q1 current into *that branch's* `TrebleAttack` network.
4. Blend the three branch output voltages with convex weights that interpolate
   over the requested fade length. All branches remain warm even at weight zero.

The ATTACK selection within each branch is fixed for its entire lifetime, so
charged C8 (and optionally the switched C5 damping leg) never changes topology.
Mid-fade retargeting starts from the exact current weights, without a coefficient
step. This avoids accidentally assigning a single `JfetStage` the state of two
different sources and keeps a dormant branch coherent for later reactivation.

This is a **software transition** among hypothetical circuit histories, not an
assertion that the physical pedal owns three J201s or that a real mechanical
switch would conserve charge in this way. Three always-live Q1/Q2 circuits have
roughly triple upstream CPU cost. No promise of artifact-free audio under all
switch/input waveforms is implied; *coefficient continuity* and per-branch
physical-port consistency are the properties under test.

## Explicit live-chain integration blockers

The shipped `PedalChain` has one JFET source state and TrebleAttack's existing
live/shadow crossfade history; it has parameter fanout, upstream gain staging,
oversampling and calibration behavior that the independent research wrapper
does not yet replicate. Directly replacing its switch crossfade with a shared
`JfetStage::processCoupled()` is electrically invalid. To integrate safely:

- Give *each active ATTACK/treble history* an independent matching physical Q1
  C2/C3 source state and complete passive drain port; ensure each is advanced
  **exactly once per oversampled sample**, even when inaudible.
- Route the identical input-buffer voltage and explicit, identical fit/schematic
  parameters to all Q1 branches, and all non-ATTACK circuit parameters to every
  treble branch. Validate the Q2 active load and ATTACK pole settings are cloned.
- Decide whether to match the existing live/shadow two-branch switch semantics or
  replace them with this three-branch always-warm interpretation. A new branch
  cannot be given a fabricated C2/C3 "snapshot" from a physically incompatible
  drain-port history without an explicit initialization policy.
- Retain a separate explicit opt-in at the `PedalChain` entry, with OFF bit-exact
  to production defaults. Validate crossfade timing, double processing, host
  sample-rate changes, model resets and output phase before any retail enablement.

`tests/CoupledAttackResearchTest.cpp` independently advances three manually
instantiated coupled JFET/treble circuits while requesting ATTACK changes every
seven samples, much faster than a 131-sample fade. It compares each branch's
Q1 C3 state, mixture, port KCL, weight normalization, retarget continuity,
source-state divergence and reset behavior at 44.1/48/96 kHz.
