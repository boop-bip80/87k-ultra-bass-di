# Reproducible circuit-research A/B routing (experimental)

**Not a retail B7K Ultra emulation mode.** This is a developer-only way to
route already-existing, independently tested stage-local physical-approximation
solvers through the complete offline signal chain. Every research flag defaults
to **OFF**. The commercial-plugin UI/APVTS exposes none of these controls, and
The shipped `FitParams{}` defaults and `FitParams::schematic()` are unchanged.

The JUCE-free `PedalChain::ResearchModes` has these independent opt-ins:

| CLI `--research` token | Chain flag | Meaning / missing physics |
| --- | --- | --- |
| `clipper-diodes` | `clipperDiodeCurrents` | Solves assumed Shockley D1/D2 branch currents in node-W KCL; assumes unmeasured diode Is/nVT and 9 V rail |
| `jfet-gds` | `jfetFiniteDrainConductance` | Includes finite drain conductance in linearized source shelf/output impedance |
| `jfet-source` | `jfetNonlinearSourceFeedback` | Implicit nonlinear J201 source capacitor/R6 state; **not coupled to instantaneous drain voltage or Q2** |
| `bax-rails` | `baxandallRailFeedback` | Uses actual clipped output in Baxandall capacitor feedback |
| `mid-rails` | `midBandRailFeedback` | Uses finite-gain, clipped-output feedback in both MidBand stages |
| `all` | all five | Convenience stress/A/B comparison only: **not a self-consistent complete physical circuit** |
| `none` | none | Reset all research flags; all are OFF by default |

Examples when the `OfflineRender` executable has been built:

```bash
OfflineRender --in measured-loaded-input.wav --out fitted-default.wav --os 8
OfflineRender --in measured-loaded-input.wav --out diode-experiment.wav --os 8 --research clipper-diodes
OfflineRender --in measured-loaded-input.wav --out jfet-experiment.wav --os 8 --research jfet-source --research jfet-gds
OfflineRender --print-fit --research all
```

Use the **identical measured loaded-input WAV** and identical knob/fit settings
in A/B renders. No amplitude matching or release-time filtering is performed
by these flags. A render that differs from baseline is neither a measured
improvement nor evidence of pedal parity: independent hardware reference
recordings, authenticated topology and held-out acceptance gates remain absent.

Calling `PedalChain::setResearchModes(const ResearchModes&)` or
`PedalDSP::setResearchModes(...)` repeatedly with unchanged options is inert
even during processing. **Changing** modes while processing may reset the J201
C3 state; configure between renders or during preparation, not as a live
footswitch. Finite JFET drain conductance also changes source output impedance;
the chain deliberately re-stamps this into `TrebleAttack` when the option flips.

Regression: `tests/CircuitResearchModesTest.cpp` gates 15,000 bitwise-exact
legacy samples, non-vacuous high-drive research signal, repeated identical
setting state-continuity, finite processing, and rate-change mode persistence.
The standalone test is registered in `CMakeLists.txt`.

The experimental coupled Q1-drain/TrebleAttack port is not automatically routed
here: correct crossfading would need paired JFET source/drain history on Attack
selector flips. A standalone port oracle is appropriate before any full-chain
integration.
