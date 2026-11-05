# Round 6 — EQ rail-feedback API and stress audit

**Scope:** the opt-in, default-disabled `Baxandall` and `MidBand`
rail-aware passive-network solvers introduced in Round 5. This is a numerical
robustness audit of the Black Mirror-inspired network, not physical B7K Ultra
hardware qualification.

## Demonstrated defect and correction

The Baxandall 7/8-node stamps contain `1/r36`, where R36 is the physical
nonzero treble-wiper resistor (nominal 3.3 kΩ). The existing
`setTrebleWiperR(0)` accepted zero and the next sample returned **NaN**.
`setTrebleWiperR(infinity)` was also accepted, leaving an invalid fit.
The setter now preserves the last valid R36 for non-finite or out-of-domain
values; its explicit numerical validity range is [1 Ω, 1 GΩ].

Additional public research/fit setters now reject non-finite and unbounded
element values before constructing capacitive conductances or open-loop
amplifier gain:

- Baxandall C21 cap: non-finite values and values above 10 mF rejected;
  finite nonpositive values continue to mean *bypass*, as documented.
- MidBand C31, C32/C34 and switched C33/C35 caps: non-finite or
  out-of-domain values rejected; C31 nonpositive continues to mean *bypass*.
- MidBand wiper resistor: non-finite and values above 1 GΩ rejected;
  zero still means the intentional ideal-cap series limit.
- Research open-loop gain is limited to [1, 1e9], preventing overflow in
  `A * Vminus` before the rail clamp can act.

These wide bounds are numerical validity limits, **not measured component
tolerances**. Normal/fitted operating values and audio defaults are unchanged.

## Repeatable tests

`tests/RailEdgeFuzzTest.cpp` covers:

1. 2,048 sample-for-sample *bit-exact* comparisons between an unmodified
   circuit and one given invalid R/C/gain values. Both C21 and C31 coupling
   branches are tested with rail-aware feedback active.
2. Continuous and monotonic parabolic knee transitions across both negative
   and positive rail endpoints, plus zero-headroom operation without NaN.
3. 16,384 samples at 44.1 and 192 kHz with model, capacitor, pot and
   open-loop-gain transitions every 127 samples, mixed ±100 V impulses, and
   assert-finite/bounded output checks.

`BaxandallTest`, `BaxandallRailFeedbackTest`, `MidBandTest`,
`MidBandRailFeedbackTest` and the new test compile at C++17 with
`-Wall -Wextra -Wpedantic -Werror` and pass. AddressSanitizer and
UndefinedBehaviorSanitizer pass on the new test (leak detection disabled
where unsupported in the sandbox).

One `-O3` microbenchmark on the audit executor, at 48 kHz, 200,000 sine
samples per configuration, measured MidBand about 28 ns/sample in legacy mode
versus 32 ns/sample in research mode at 1 V, and 25 vs 70 ns/sample at 5 V
with saturation. These are **host-dependent timing observations**, not a
plugin-wide real-time performance guarantee or hardware measurement. An
unverified frequency-dependent TL07x op-amp or slew/overload recovery model
has deliberately **not** been added.

## Remaining physical evidence

Actual TL07x open-loop gain vs frequency, slew and post-overdrive recovery
depend on the installed component and circuit load, both unverified in the
retail pedal. The passive-node KCL improvements are useful; these bounds and
stress tests cannot establish B7K Ultra parity on their own.
