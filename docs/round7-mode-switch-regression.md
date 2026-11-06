# Round 7: full-chain switch/research-mode stress gate

`Round7ModeSwitchStressTest` supplements the per-stage source/drain and
TrebleAttack oracles with a **live full-chain software regression**, separate
from hardware fidelity. It exercises the existing `PedalChain` as shipped;
neither the test nor a passing result establishes electrical compliance for a
physical Darkglass pedal.

## Coverage

At 8, 48, and 384 kHz, for both default and all-enabled research modes:

- 10,000 deterministic input samples per rate/mode, mixing subnormal inputs,
  pseudorandom nominal/amplified signals, and deliberately excessive ±30 input
  units. This is a **solver robustness** test, not a realistic bass pickup.
- Four selectors (ATTACK, GRUNT, LO-MID frequency, HI-MID frequency) move at
  deliberately shorter intervals than some crossfade durations. All eight
  parameter controls also vary over their full ranges.
- One twin repeatedly re-applies the *same* research flags. Both chains must
  produce bit-identical outputs at every sample and remain finite.
- Both are re-prepared at half the previous rate. Research flags must survive,
  and the next 256 samples must remain bit-identical.
- No `operator new` or `operator new[]` calls are observed while processing
  samples **or** applying knob/selector changes. This does not cover every
  possible C allocation API, aligned allocation, or plugin-host thread.

Run after configuring the project with its `chowdsp_wdf` dependency:

```sh
ctest --test-dir build -R '^Round7ModeSwitchStressTest$' --output-on-failure
```

The native C++17 test also builds with the header-only `chowdsp_wdf` include
directory using `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror`.

## Result and limits

In a local Linux test against the Round 7 evidence checkpoint, the test passed
**61,536 full-chain samples** with zero observed `new`/`new[]` allocations.
The largest absolute processed output was approximately **310 model units**;
no hardware output-voltage conclusion follows because the shipped empirical
model's nominal gain and output scaling are not anchored to a measured retail
unit, and op-amp rail clamps are intentionally default-off.

The following remain **independent gates**, not discharged by this test:

- Correct J201 triode/saturation/turn-off equations and operating point;
- Combined Q1/Q2 transistor and ATTACK network KCL on the same timestep;
- Physically plausible modeled node voltages for a declared clone supply;
- Real ngspice comparisons plus measured hardware captures;
- Host-level JUCE plugin integration and CPU deadline testing.

Address these explicitly before considering an opt-in physical circuit model
ready to replace the empirically calibrated legacy signal path.
