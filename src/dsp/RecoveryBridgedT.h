#pragma once

#include "MnaSolve.h"
#include "RailClamp.h"

// =============================================================================
// Stage 5 — Recovery buffer + passive bridged-T (IC2_B) — circuit.md
//           "Recovery + bandlimiting (IC2_B ...)" + the IC2_B CORRECTION note.
// =============================================================================
// IC2_B is a UNITY-GAIN VOLTAGE FOLLOWER (pin6- tied to pin7-out, verified on
// BOTH schematics) — NOT the +12 dB active shelf an earlier reading assumed.
// There is NO recovery make-up gain here; do not budget one into gain-staging.
//
// The R22/R23/C16/C17 parts form a PASSIVE bridged-T network hanging off the
// buffer output (circuit.md node graph):
//     buf-out --C16(680pF)--> Nout                       (bridge cap)
//     buf-out --R22(100k)--> Nmid --R23(33k)--> Nout     (the "T")
//     Nmid --C17(22n)--> GND                             (shunt leg)
// Stage output = V(Nout) (→ R24 → IC4_B Sallen-Key). Ideal-component response
// is a deep midrange notch (~717 Hz / −28 dB UNLOADED, tolerance-sensitive) —
// surprising for this pedal, so the DEPTH must be capture-validated at Phase 7;
// the TOPOLOGY (unity buffer + bridged-T) is firmly verified.
//
// ---- Why MNA rather than a WDF tree (same reasoning as TrebleAttack) --------
// A bridged-T is a bridge (C16 spans buf-out↔Nout in parallel with the R22-R23
// path) — not a series/parallel WDF tree; it would need a hand-derived R-type
// scattering matrix. For a LINEAR passive block with an ideal source in
// (the unity buffer output) and a high-Z output, 2-node nodal analysis with
// trapezoidal-companion caps is exact, uses the SAME bilinear cap discretisation
// as chowdsp's CapacitorT (identical warp), and maps 1:1 onto the analytic
// oracle (analysis/eq_reference.py :: bridged_t_tf, default UNLOADED). The
// matrix is fixed (no switch), so its 2×2 inverse is precomputed once in
// prepare(). Cap-stamp / history conventions match TrebleAttack exactly.
//
// ---- ⚠ THE "UNLOADED" ASSUMPTION BELOW IS WRONG ABOVE ~1 kHz (measured, 2026-11) ---------------
// ngspice of the drawn schematic (analysis/spice_reference/compare_od.py, stage taps): with the bridged-T
// feeding R24 into the IC4_B Sallen-Key, the real node `Nout` sits 1.1 dB below the unloaded stage at
// 1 kHz, 2.3 dB at 2 kHz, 4.4 dB at 4 kHz and ~6 dB at 8 kHz — because C18/C27 turn the SK input into a
// frequency-dependent load that the bridged-T's own output impedance (R22/R23/C16) divides against.
// "Well approximated as open" held only around the 717 Hz notch. `setCoupledSk()` below solves the
// bridged-T + R24 + IC4_B as ONE network (4 nodes) and returns the SK output; with it on, the plugin
// matches spice at every stage tap. It is OFF by default (the shipped calibration was fitted on the
// unloaded model) and ON in FitParams::schematic().
//
// ---- Loading (build-plan Phase 4 test caveat) -------------------------------
// The oracle default is UNLOADED, and so is this stage: Nout feeds R24(10k) in
// SERIES into the IC4_B SK non-inverting input, which is high-Z and bootstrapped
// in the SK passband — a light load well approximated as open at/around the
// 717 Hz notch (717 Hz ≪ the 10.7 kHz SK corner). The bridged-T→SK PAIR is
// validated together (and against captures) at Phase 7; here the isolated stage
// validates 1:1 against the unloaded oracle, decoupled from the SK load.
// (The oracle's Rload arg models Nout→GND through R, which is NOT the real
// topology — R24 does not go to ground — so it is only a crude sensitivity
// bound, never the reference for this stage.)
//
// ---- Rail clamp (calibration §6, build-plan Phase 4 GATE item) --------------
// The unity buffer is an op-amp output → carries a RailClamp (apply to EVERY
// op-amp stage). The clamp sits on the BUFFER output (the op-amp node), i.e. the
// source feeding the passive bridged-T. Disabled by default so the linear FR
// test validates against the oracle unchanged; the processor enables it.
//
// ---- Polarity ---------------------------------------------------------------
// Unity, non-inverting at DC (caps open → Nout follows buf-out through R22/R23
// with no drop into the open output → gain 1). Confirmed by the DC-step test.
// =============================================================================
class RecoveryBridgedT
{
public:
    // Component values (circuit.md "Recovery + bandlimiting" table).
    static constexpr double kR22 = 100.0e3; // "T" upper leg (buf-out -> Nmid)
    static constexpr double kR23 = 33.0e3;  // "T" lower leg (Nmid -> Nout)
    static constexpr double kC16 = 680.0e-12; // bridge cap (buf-out -> Nout)
    static constexpr double kC17 = 22.0e-9;   // shunt leg (Nmid -> GND)

    RecoveryBridgedT() = default;

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        rebuild();
        reset();
    }

    // Phase-7 capture fit (FitParams.h). ALL FOUR values are fit parameters, not
    // just scaled: risk-register #1 is that the ideal-value ~-28 dB notch at
    // ~717 Hz is far deeper than this pedal plausibly sounds, and notch DEPTH is
    // extremely tolerance-sensitive while the FREQUENCY is not — so the reshape
    // needs independent control of all four to land depth and frequency at once.
    void setComponents(double R22, double R23, double C16, double C17) noexcept
    {
        r22 = R22;
        r23 = R23;
        c16 = C16;
        c17 = C17;
        rebuild();
    }

    void reset() noexcept { ieqC16 = ieqC17 = ieqC18 = ieqC27 = 0.0; }

    // Solve the bridged-T TOGETHER with R24 and the IC4_B Sallen-Key (unity buffer): nodes
    // [Nmid, Nout, N1, Kp]. process() then returns the IC4_B OUTPUT (= V(Kp), through its own rail clamp),
    // and PedalChain must NOT run a separate IC4_B stage. skR1/skR2 = R24/R25, skC1 = C18 (N1 -> out),
    // skC2 = C27 (Kp -> GND). enable=false restores the legacy unloaded 2-node stage bit-for-bit.
    void setCoupledSk(bool enable, double skR1 = 10.0e3, double skR2 = 22.0e3, double skC1 = 1.0e-9,
                      double skC2 = 1.0e-9) noexcept
    {
        coupled = enable;
        r24 = skR1;
        r25 = skR2;
        c18 = skC1;
        c27 = skC2;
        rebuild();
        reset();
    }
    bool coupledSk() const noexcept { return coupled; }

    // Rail-clamp passthroughs (calibration §6) — applied to the BUFFER output.
    void setRailClampEnabled(bool e) noexcept { rail.setEnabled(e); railSk.setEnabled(e); }
    void setRailVoltages(double vNeg, double vPos) noexcept
    {
        rail.setRailVoltages(vNeg, vPos);
        railSk.setRailVoltages(vNeg, vPos);
    }

    // Process one sample (real volts in from the clipper-recovery path, out at Nout).
    inline double process(double x) noexcept
    {
        const double vin = rail.process(x); // unity buffer (op-amp output, clamped)

        if (coupled)
        {
            const double rhsC[4] = { vin / r22 + ieqC17, gc16 * vin - ieqC16, ieqC18, ieqC27 };
            double v[4];
            mna::matvec<4>(yinv4, rhsC, v);
            ieqC17 = 2.0 * gc17 * v[0] - ieqC17;              // Nmid - GND
            ieqC16 = 2.0 * gc16 * (vin - v[1]) - ieqC16;      // Vin - Nout
            ieqC18 = 2.0 * gc18 * (v[2] - v[3]) - ieqC18;     // N1 - Kp (= IC4_B output)
            ieqC27 = 2.0 * gc27 * v[3] - ieqC27;              // Kp - GND
            return railSk.process(v[3]);
        }

        // RHS: source contributions + capacitor history (Ieq).
        //   C17 (a=Nmid, b=GND): RHS[Nmid] += ieqC17
        //   C16 (a=Vin src, b=Nout): source -> RHS[Nout] += gc16*Vin ; RHS[Nout] -= ieqC16
        const double rhs0 = vin / r22 + ieqC17;
        const double rhs1 = gc16 * vin - ieqC16;

        const double nmid = yi00 * rhs0 + yi01 * rhs1;
        const double nout = yi10 * rhs0 + yi11 * rhs1;

        // Capacitor state update: Ieq_new = 2*gc*v_ab - Ieq_old.
        ieqC17 = 2.0 * gc17 * nmid - ieqC17;         // v_ab = Nmid - GND
        ieqC16 = 2.0 * gc16 * (vin - nout) - ieqC16; // v_ab = Vin - Nout

        return nout;
    }

private:
    // Rebuild the companion conductances + the precomputed nodal inverse from the
    // current component values and sample rate.
    void rebuild() noexcept
    {
        const double twoOverT = 2.0 * fs;
        gc16 = c16 * twoOverT; // trapezoidal companion conductances
        gc17 = c17 * twoOverT;

        // Nodal matrix Y (unknowns [Nmid, Nout]); Vin (=buffer out) is the source.
        //   Nmid: (1/R22 + 1/R23 + gc17) Nmid + (-1/R23) Nout = Vin/R22 + ieqC17
        //   Nout: (-1/R23) Nmid + (1/R23 + gc16) Nout       = gc16*Vin - ieqC16
        const double a = 1.0 / r22 + 1.0 / r23 + gc17;
        const double b = -1.0 / r23;
        const double c = -1.0 / r23;
        const double d = 1.0 / r23 + gc16;
        const double det = a * d - b * c;
        // Precomputed inverse of [[a,b],[c,d]].
        yi00 = d / det;
        yi01 = -b / det;
        yi10 = -c / det;
        yi11 = a / det;

        if (coupled)
        {
            gc18 = c18 * twoOverT;
            gc27 = c27 * twoOverT;
            const double g22 = 1.0 / r22, g23 = 1.0 / r23, g24 = 1.0 / r24, g25 = 1.0 / r25;
            double Y[4][4] = {};
            // Nmid
            Y[0][0] = g22 + g23 + gc17; Y[0][1] = -g23;
            // Nout
            Y[1][0] = -g23; Y[1][1] = g23 + gc16 + g24; Y[1][2] = -g24;
            // N1 (R24 from Nout; R25 and C18 toward the (+) node / buffer output Kp)
            Y[2][1] = -g24; Y[2][2] = g24 + g25 + gc18; Y[2][3] = -(g25 + gc18);
            // Kp = IC4_B (+) node: R25 from N1, C27 to ground, no other load (the buffer supplies C18's current)
            Y[3][2] = -g25; Y[3][3] = g25 + gc27;
            double tmp[4][4];
            if (mna::invert<4>(Y, tmp))
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        yinv4[i][j] = tmp[i][j];
        }
    }

    double fs = 48000.0;
    // Phase-7 capture-fit component values (FitParams.h), nominal-initialised.
    double r22 = kR22, r23 = kR23, c16 = kC16, c17 = kC17;
    double gc16 = 0.0, gc17 = 0.0;          // companion conductances (set in prepare)
    double yi00 = 0.0, yi01 = 0.0, yi10 = 0.0, yi11 = 0.0; // precomputed Y^-1
    double ieqC16 = 0.0, ieqC17 = 0.0;      // capacitor history currents
    // ---- coupled bridged-T + R24 + IC4_B mode (setCoupledSk) ----
    bool coupled = false;
    double r24 = 10.0e3, r25 = 22.0e3, c18 = 1.0e-9, c27 = 1.0e-9;
    double gc18 = 0.0, gc27 = 0.0, ieqC18 = 0.0, ieqC27 = 0.0;
    double yinv4[4][4] = {};
    RailClamp rail;
    RailClamp railSk;                        // IC4_B output clamp (coupled mode only)

    RecoveryBridgedT(const RecoveryBridgedT&) = delete;
    RecoveryBridgedT& operator=(const RecoveryBridgedT&) = delete;
};
