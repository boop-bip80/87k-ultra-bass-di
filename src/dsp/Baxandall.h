#pragma once

#include "MnaSolve.h"
#include "RailClamp.h"

// =============================================================================
// EQ block — Baxandall BASS + TREBLE (IC5_C) — circuit.md "Baxandall BASS/TREBLE
//            (IC5_C) — NODES VERIFIED (2026-07-19 pixel-zoom redraw)".
// =============================================================================
// BASS and TREBLE are ONE coupled active Baxandall network: both pots' wipers
// sum into the SAME IC5_C virtual-ground (−) node, so they must be solved as a
// single 7-node network, not two independent filters (circuit.md "Interactive /
// coupled controls"). Ideal op-amp: (+) = VD = 0, (−) held at 0, feedback
// R37(1M) ∥ C30(47p); Vout is the unknown that satisfies the (−)-node KCL (no
// KCL at the driven op-amp output — same active-filter treatment as MidBand).
//
// Node graph (signal ground = VD = 0; verified 2026-07-19):
//   Vin --R33--> A ;  B --R34--> Vout                  (bass end legs)
//   bass pot Rp: A --Rba--> Wb --Rbb--> B  (ab = fraction A→Wb ; ab→0 = BASS BOOST)
//   C25 A↔Wb ;  C26 Wb↔B ;  Wb --R35--> (−)            (caps run lug→WIPER)
//   Vin --C28--> T3 ;  T1 --C29--> Vout                (treble end legs, cap-coupled)
//   treble pot Rp: T3 --Rta--> Wt --Rtb--> T1 (at→0 = TREBLE BOOST)
//   Wt --R36--> (−)
//   feedback: (−) --R37--> Vout ;  (−) --C30--> Vout
// Unknowns [A, B, Wb, T3, T1, Wt, Vout].
//
// ---- MNA + companion caps (matches eq_reference.py :: baxandall_tf) ----------
// Trapezoidal companion caps (gc=2C/T; ieq_new = 2·gc·v_ab − ieq_old) make the
// 7×7 conductance matrix REAL and constant for fixed pot positions; the
// frequency dependence rides in the per-sample RHS history currents, exactly as
// for the 2-node stages and MidBand. Five caps: C25(A↔Wb), C26(B↔Wb) in the bass
// network; C28(Vin↔T3) source-coupled; C29(T1↔Vout); C30(Vout↔(−)) the feedback
// cap. Rows A/B/Wb/T3/T1/Wt use the natural "sum leaving = 0" convention; the
// (−)/Vout row uses the oracle's "currents INTO the node = 0" (a sign flip) — so
// the C30 history current lands as +ieqC30 in that row, mirroring MidBand's C33.
// The matrix depends on both pot splits, so it re-inverts whenever EITHER pot
// moves (dirty flag), never per sample. Maps 1:1 onto baxandall_tf.
//
// ---- Coupling caps C21/C31 (stage boundary) ---------------------------------
// C21(100n) couples IC5_B → this stage's Vin, and C31(2u2) couples this stage's
// Vout → LO-MID. Both are inter-stage HP couplers (like C2/C15/C36 elsewhere);
// the oracle (and this stage) are defined at the Baxandall network boundary and
// EXCLUDE them, so BOTH had to be placed during Phase-6 integration.
//   C21 → ⭐ NOW INSIDE THIS STAGE (`setInputCap()`, an EIGHTH MNA node — the stack's
//         input rail `Vi`). It used to be `PedalChain::C21Highpass`, a first-order HP
//         into ONE fixed resistance (`c21R`). That is structurally wrong in exactly the
//         way the C31 note below describes: the stack's input impedance is strongly
//         knob- and frequency-dependent (R33 + the bass network, in parallel with the
//         C28 treble branch), so the true HP corner MOVES with BASS/TREBLE. Measured
//         against an independent ngspice netlist of the drawn schematic
//         (analysis/spice_reference/compare_clean.py): with the drawn 100n a fixed R
//         is wrong by up to ±15 dB at 20 Hz depending on the BASS knob, no single R is
//         right at more than one knob position, and the same stack with C21 inside the
//         MNA matches spice to <0.1 dB. The legacy fixed-R path remains available
//         (`setInputCap(0)` + `PedalChain::C21Highpass`) as the pre-fix behaviour.
//   C31 → ⭐ LANDED AT SESSION 177 (open-work item 16), and NOT here: it is
//         `MidBand::setInputCap()`, a FIFTH MNA node inside the LO-MID stage.
//         ⚠ It is deliberately NOT a `C21Highpass`-shaped bolt-on, because the
//         load it sees is frequency-dependent (|Zin| falls 42.2k → 2.2k as C32
//         shorts the pot lugs), so a fixed-R first-order HP reproduces ~2% of it.
//         See that method's comment and `analysis/c31_corner_gate.py` (GATE BG).
// ⚠ This block said "deferred … do not forget it in the full chain" for 156
// sessions while C21 had in fact landed and C31 had not — the carry-forward
// outlived its own half-completion. s169 caught the gap by grepping for `C31`.
//
// ---- Rail clamp / polarity --------------------------------------------------
// Op-amp output → RailClamp on Vout (GATE item; disabled by default so the
// linear FR test matches the oracle). INVERTING: DC gain ≈ −0.926 at flat (the
// summing amp inverts; the slight <1 magnitude is the bass shelf's DC droop, not
// a bug — matches the oracle's −0.925926). This is one of the EQ block's four
// inversions (IC5_B, Baxandall, LO-MID, HI-MID → net non-inverting); confirmed by
// the DC-step test.
// =============================================================================
class Baxandall
{
public:
    // Component values (circuit.md "Baxandall BASS/TREBLE" + BASS/TREBLE rows).
    static constexpr double kRp = 100.0e3;  // both pots (100k B-taper)
    static constexpr double kR33 = 10.0e3;  // Vin → A
    static constexpr double kR34 = 10.0e3;  // B → Vout
    static constexpr double kR35 = 10.0e3;  // Wb → (−)
    static constexpr double kR36 = 3.3e3;   // Wt → (−)
    static constexpr double kR37 = 1.0e6;   // feedback (−) → Vout ("1m" on schematic)
    static constexpr double kC25 = 22.0e-9; // A ↔ Wb
    static constexpr double kC26 = 22.0e-9; // Wb ↔ B
    static constexpr double kC28 = 10.0e-9; // Vin ↔ T3
    static constexpr double kC29 = 10.0e-9; // T1 ↔ Vout
    static constexpr double kC30 = 47.0e-12; // feedback cap (−) ↔ Vout

    Baxandall() = default;

    void prepare(double sampleRate)
    {
        fsSeen = sampleRate;
        const double twoOverT = 2.0 * sampleRate;
        gc25 = kC25 * twoOverT;
        gc26 = kC26 * twoOverT;
        gc28 = kC28 * twoOverT;
        gc29 = kC29 * twoOverT;
        gc30 = kC30 * twoOverT;
        gc21 = c21 * twoOverT;
        dirty = true;
        rebuild();
        reset();
    }

    void reset() noexcept { ieq25 = ieq26 = ieq28 = ieq29 = ieq30 = ieq21 = 0.0; }

    // Electrical pot fractions (B-taper linear; knob→fraction + direction applied
    // by the processor). ab→0 = BASS boost, at→0 = TREBLE boost, 0.5 = flat.
    void setBass(double ab) noexcept { setPos(posBass, ab); }
    void setTreble(double at) noexcept { setPos(posTreble, at); }

    // Rail-clamp passthroughs (calibration §6) — applied to Vout (op-amp output).
    void setRailClampEnabled(bool e) noexcept { rail.setEnabled(e); }
    void setRailVoltages(double vNeg, double vPos) noexcept { rail.setRailVoltages(vNeg, vPos); }

    // Optional passive-network KCL correction when the ideal op-amp output
    // reaches its modelled rails. This does not model TL07x open-loop dynamics:
    // it only updates passive nodes/capacitors against the actual driven voltage.
    // Default OFF keeps the historical fitted path sample-for-sample unchanged.
    void setRailAwarePassiveFeedback(bool e) noexcept
    {
        if (e != railAwarePassiveFeedback)
        {
            railAwarePassiveFeedback = e;
            dirty = true; // generate the sensitivity only when the mode is used
        }
    }

    // R36 (Wt -> (-), schematic-verified 3.3k, pixel-zoom 2026-07-19) — TREBLE's own
    // wiper-leg series resistor. Phase 9 A2c (session 24-25): the captured unit's
    // TREBLE boost-to-cut SPAN (matched-pair, dsp.md's isolation method) is ~5 dB
    // narrower than the modelled network's at R36=3.3k; a plain scan of R36 against
    // that span has a clean INTERIOR minimum (not the "make it see less" one-sided
    // degeneracy that killed the session-5/6 clipper fits and the GAP #3b C13 fit) at
    // ~4.7-5.0k, confirmed by the independent 4-point pot-law (not just the extremes,
    // the same guard GAP #4 used). BASS is unaffected: R36 carries only the treble
    // leg's contribution to the shared virtual-ground node, so at treble-flat the
    // change is <0.004 dB at every band — measured, the four bass captures and both
    // ref-clean takes move by <=0.024 dB (they already grade 0.31-0.70 dB).
    // Unlike GAP #4's midWiperR this is not a NEW hypothetical element — R36 already
    // exists in the model and the schematic — so this is a plain VALUE fit, same
    // posture as c21R (10k -> 100k): the captured unit is a real Ultra, the primary
    // schematic is a clone of the ORIGINAL B7K, and per dsp.md "fit the corner" the
    // capture is authoritative. ** The TOPOLOGY was NOT independently re-checked (no
    // schematic-checker pass); the evidence it is right is indirect — a pure VALUE
    // change flattens the span residual across the whole band, where a wrong topology
    // would leave a frequency-dependent shape residual. ** See FitParams.
    void setTrebleWiperR(double rOhm) noexcept
    {
        // R36 is an actual nonzero series resistor. A requested short makes
        // the existing 7/8-node stamp divide by zero; infinity/NaN likewise
        // cannot represent a real fitted part. Keep the last valid value.
        if (! std::isfinite(rOhm) || rOhm < 1.0 || rOhm > 1.0e9)
            return;
        if (mna::differs(rOhm, r36)) { r36 = rOhm; dirty = true; }
    }

    // C21 — the 100n (drawn) IC5_B -> stack-input coupling cap, solved INSIDE the stack as an
    // eighth MNA node so it works against the stack's real, knob-dependent input impedance.
    // `farads <= 0` removes it (the stage then expects the caller to have applied C21 itself,
    // i.e. the legacy PedalChain::C21Highpass path). Takes effect on the next rebuild.
    void setInputCap(double farads) noexcept
    {
        // Accept <=0 as the documented bypass, but reject non-finite or
        // absurdly large capacitances before forming the companion stamp.
        if (! std::isfinite(farads) || farads > 0.01)
            return;
        const double c = farads > 0.0 ? farads : 0.0;
        if (mna::differs(c, c21))
        {
            c21 = c;
            gc21 = c21 * 2.0 * fsSeen;
            ieq21 = 0.0;
            dirty = true;
        }
    }
    bool hasInputCap() const noexcept { return c21 > 0.0; }

    inline double process(double vin) noexcept
    {
        if (dirty)
            rebuild();
        if (c21 > 0.0)
            return process8(vin);

        // RHS: sources (R33, C28) + capacitor history currents. See header.
        double rhs[7];
        rhs[0] = vin / kR33 + ieq25;      // A
        rhs[1] = ieq26;                   // B
        rhs[2] = -ieq25 - ieq26;          // Wb
        rhs[3] = gc28 * vin + ieq28;      // T3 (C28 source-coupled from Vin)
        rhs[4] = ieq29;                   // T1
        rhs[5] = 0.0;                     // Wt (no caps)
        rhs[6] = ieq30;                   // (−)/Vout (C30 feedback, flipped-sign row)

        double x[7];
        mna::matvec<7>(yinv, rhs, x);

        const double drivenOut = rail.process(x[6]);
        if (railAwarePassiveFeedback && mna::differs(drivenOut, x[6]))
        {
            // The ideal solve's ordinary nodes are exactly the passive
            // network solution at output x[6] and Vminus=0. By superposition,
            // changing the driven output by delta shifts each passive node
            // by its precomputed voltage sensitivity; slot 6 is now Vminus.
            const double delta = drivenOut - x[6];
            double corrected[7];
            for (int i = 0; i < 7; ++i)
                corrected[i] = x[i] + railSensitivity7[i] * delta;
            ieq25 = 2.0 * gc25 * (corrected[0] - corrected[2]) - ieq25;
            ieq26 = 2.0 * gc26 * (corrected[1] - corrected[2]) - ieq26;
            ieq28 = 2.0 * gc28 * (corrected[3] - vin) - ieq28;
            ieq29 = 2.0 * gc29 * (corrected[4] - drivenOut) - ieq29;
            ieq30 = 2.0 * gc30 * (drivenOut - railSensitivity7[6] * delta) - ieq30;
            return drivenOut;
        }

        // Capacitor state update: Ieq_new = 2*gc*v_ab − Ieq_old.
        ieq25 = 2.0 * gc25 * (x[0] - x[2]) - ieq25; // A − Wb
        ieq26 = 2.0 * gc26 * (x[1] - x[2]) - ieq26; // B − Wb
        ieq28 = 2.0 * gc28 * (x[3] - vin) - ieq28;  // T3 − Vin
        ieq29 = 2.0 * gc29 * (x[4] - x[6]) - ieq29; // T1 − Vout
        ieq30 = 2.0 * gc30 * x[6] - ieq30;          // Vout − (−)=0

        return drivenOut; // identical to the legacy rail-only output
    }

private:
    // 8-node path: [A, B, Wb, T3, T1, Wt, Vout, Vi]. Vi is the stack input rail (C21 out, R33 and
    // C28 left plates). Same conventions as the 7-node path; C21 runs vin -> Vi, C28 now runs
    // Vi -> T3, and R33 feeds A from Vi instead of from the source.
    inline double process8(double vin) noexcept
    {
        double rhs[8];
        rhs[0] = ieq25;                    // A
        rhs[1] = ieq26;                    // B
        rhs[2] = -ieq25 - ieq26;           // Wb
        rhs[3] = ieq28;                    // T3
        rhs[4] = ieq29;                    // T1
        rhs[5] = 0.0;                      // Wt
        rhs[6] = ieq30;                    // (-)/Vout
        rhs[7] = gc21 * vin + ieq21 - ieq28; // Vi
        double x[8];
        mna::matvec<8>(yinv8, rhs, x);

        const double drivenOut = rail.process(x[6]);
        if (railAwarePassiveFeedback && mna::differs(drivenOut, x[6]))
        {
            const double delta = drivenOut - x[6];
            double corrected[8];
            for (int i = 0; i < 8; ++i)
                corrected[i] = x[i] + railSensitivity8[i] * delta;
            ieq25 = 2.0 * gc25 * (corrected[0] - corrected[2]) - ieq25;
            ieq26 = 2.0 * gc26 * (corrected[1] - corrected[2]) - ieq26;
            ieq28 = 2.0 * gc28 * (corrected[3] - corrected[7]) - ieq28;
            ieq29 = 2.0 * gc29 * (corrected[4] - drivenOut) - ieq29;
            ieq30 = 2.0 * gc30 * (drivenOut - railSensitivity8[6] * delta) - ieq30;
            ieq21 = 2.0 * gc21 * (corrected[7] - vin) - ieq21;
            return drivenOut;
        }

        ieq25 = 2.0 * gc25 * (x[0] - x[2]) - ieq25;
        ieq26 = 2.0 * gc26 * (x[1] - x[2]) - ieq26;
        ieq28 = 2.0 * gc28 * (x[3] - x[7]) - ieq28; // T3 - Vi
        ieq29 = 2.0 * gc29 * (x[4] - x[6]) - ieq29;
        ieq30 = 2.0 * gc30 * x[6] - ieq30;
        ieq21 = 2.0 * gc21 * (x[7] - vin) - ieq21;  // Vi - Vin
        return drivenOut;
    }

    void setPos(double& slot, double v) noexcept
    {
        const double c = v < 1e-6 ? 1e-6 : (v > 1.0 - 1e-6 ? 1.0 - 1e-6 : v);
        if (mna::differs(c, slot)) { slot = c; dirty = true; }
    }

    // Passive-node sensitivity to an externally driven op-amp output.
    // The ideal matrix's slot 6 is output Vout and its row asserts Vminus=0.
    // In this passive matrix slot 6 instead denotes the input Vminus. The
    // actual output O is a known source on R34, C29 and the R37||C30 feedback.
    // ypass^-1 * p is independent of both capacitor history and signal voltage;
    // compute once whenever knobs/caps make the nodal matrix dirty.
    template <int N>
    void rebuildRailSensitivity(const double (&ideal)[N][N],
                                double (&out)[N]) noexcept
    {
        double passive[N][N];
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                passive[i][j] = ideal[i][j];

        passive[1][6] = 0.0;               // B --R34--> O
        passive[4][6] = 0.0;               // T1 --C29--> O
        passive[2][6] = -1.0 / kR35;      // Wb --R35--> Vminus
        passive[5][6] = -1.0 / r36;       // Wt --R36--> Vminus
        passive[6][2] = -1.0 / kR35;
        passive[6][5] = -1.0 / r36;
        passive[6][6] = 1.0 / kR35 + 1.0 / r36
                       + 1.0 / kR37 + gc30;

        double drive[N] = {};
        drive[1] = 1.0 / kR34;
        drive[4] = gc29;
        drive[6] = 1.0 / kR37 + gc30;
        double inverse[N][N];
        if (mna::invert<N>(passive, inverse))
            mna::matvec<N>(inverse, drive, out);
        // Keep the previous good sensitivity on singular/invalid pot values.
    }

    void rebuild() noexcept
    {
        const double Rba = posBass * kRp, Rbb = (1.0 - posBass) * kRp;   // A→Wb, Wb→B
        const double Rta = posTreble * kRp, Rtb = (1.0 - posTreble) * kRp; // T3→Wt, Wt→T1
        const double gRba = 1.0 / Rba, gRbb = 1.0 / Rbb, gRta = 1.0 / Rta, gRtb = 1.0 / Rtb;

        double Y[7][7] = {};
        // 0 A : (1/R33 + gc25 + 1/Rba) A − (gc25 + 1/Rba) Wb
        Y[0][0] = 1.0 / kR33 + gc25 + gRba; Y[0][2] = -(gc25 + gRba);
        // 1 B : (1/R34 + gc26 + 1/Rbb) B − (gc26 + 1/Rbb) Wb − (1/R34) Vout
        Y[1][1] = 1.0 / kR34 + gc26 + gRbb; Y[1][2] = -(gc26 + gRbb); Y[1][6] = -1.0 / kR34;
        // 2 Wb: −(gc25+1/Rba) A − (gc26+1/Rbb) B + (gc25+1/Rba + gc26+1/Rbb + 1/R35) Wb
        Y[2][0] = -(gc25 + gRba); Y[2][1] = -(gc26 + gRbb);
        Y[2][2] = gc25 + gRba + gc26 + gRbb + 1.0 / kR35;
        // 3 T3: (gc28 + 1/Rta) T3 − (1/Rta) Wt
        Y[3][3] = gc28 + gRta; Y[3][5] = -gRta;
        // 4 T1: (gc29 + 1/Rtb) T1 − (1/Rtb) Wt − gc29 Vout
        Y[4][4] = gc29 + gRtb; Y[4][5] = -gRtb; Y[4][6] = -gc29;
        // 5 Wt: −(1/Rta) T3 − (1/Rtb) T1 + (1/Rta + 1/Rtb + 1/R36) Wt
        Y[5][3] = -gRta; Y[5][4] = -gRtb; Y[5][5] = gRta + gRtb + 1.0 / r36;
        // 6 (−)/Vout: (1/R35) Wb + (1/R36) Wt + (1/R37 + gc30) Vout   (into-node sign)
        Y[6][2] = 1.0 / kR35; Y[6][5] = 1.0 / r36; Y[6][6] = 1.0 / kR37 + gc30;

        double tmp[7][7];
        if (mna::invert<7>(Y, tmp))
        {
            for (int i = 0; i < 7; ++i)
                for (int j = 0; j < 7; ++j)
                    yinv[i][j] = tmp[i][j];
        }
        if (railAwarePassiveFeedback && c21 <= 0.0)
            rebuildRailSensitivity<7>(Y, railSensitivity7);
        // else keep the previous good inverse (degenerate pot endpoint).
        if (c21 > 0.0)
        {
            double Y8[8][8] = {};
            for (int i = 0; i < 7; ++i)
                for (int j = 0; j < 7; ++j)
                    Y8[i][j] = Y[i][j];
            // A: R33 now returns to Vi (the source term moved to the Vi row)
            Y8[0][7] = -1.0 / kR33;
            // T3: C28 now returns to Vi, not to the ideal source
            Y8[3][7] = -gc28;
            // Vi: (gc21 + 1/R33 + gc28) Vi - A/R33 - gc28 T3 = gc21*vin + ieq21 - ieq28
            Y8[7][7] = gc21 + 1.0 / kR33 + gc28;
            Y8[7][0] = -1.0 / kR33;
            Y8[7][3] = -gc28;
            double tmp8[8][8];
            if (mna::invert<8>(Y8, tmp8))
            {
                for (int i = 0; i < 8; ++i)
                    for (int j = 0; j < 8; ++j)
                        yinv8[i][j] = tmp8[i][j];
            }
            if (railAwarePassiveFeedback)
                rebuildRailSensitivity<8>(Y8, railSensitivity8);
        }
        dirty = false;
    }

    double gc25 = 0.0, gc26 = 0.0, gc28 = 0.0, gc29 = 0.0, gc30 = 0.0;
    double c21 = 0.0, gc21 = 0.0, ieq21 = 0.0; // C21 input-coupling cap (0 = not in this stage)
    double yinv8[8][8] = {};
    double railSensitivity7[7] = {};
    double railSensitivity8[8] = {};
    bool railAwarePassiveFeedback = false;
    double posBass = 0.5, posTreble = 0.5; // electrical pot fractions (0.5 = flat)
    double r36 = kR36; // runtime-settable; see setTrebleWiperR
    double yinv[7][7] = {};
    double ieq25 = 0.0, ieq26 = 0.0, ieq28 = 0.0, ieq29 = 0.0, ieq30 = 0.0;
    bool dirty = true;
    double fsSeen = 48000.0;
    RailClamp rail;

    Baxandall(const Baxandall&) = delete;
    Baxandall& operator=(const Baxandall&) = delete;
};
