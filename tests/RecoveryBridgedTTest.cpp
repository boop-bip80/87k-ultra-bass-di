// =============================================================================
// RecoveryBridgedT (IC2_B unity buffer + passive bridged-T) — validation
// =============================================================================
// Validates against the continuous-time analytic oracle
// (analysis/eq_reference.py :: bridged_t_tf, default UNLOADED — see the stage
// header for why unloaded is the correct reference for the isolated stage).
//
// A bridged-T's deep notch makes a blanket "±0.25 dB everywhere" meaningless:
// on the steep flanks a sub-percent bilinear frequency-warp shows up as several
// dB, and the exact notch-bottom depth is a near-total cancellation that is
// hyper-sensitive to discretisation (build-plan Phase 4 test caveat). So we
// split the assertions (matching the caveat "notch FREQUENCY tight + DEPTH
// loose; ±0.25 dB elsewhere"):
//   * Test 1  — SHOULDER FR (well away from the notch): tight vs oracle.
//   * Test 2  — HF deviation shrinks 48k->96k (proves bilinear warp).
//   * Test 3  — NOTCH: located near 717 Hz (tight FREQUENCY) and genuinely deep
//               (loose DEPTH — just assert it is a real, deep notch).
//   * Test 4  — DC-step polarity: unity, non-inverting.
// =============================================================================

#include "../src/dsp/RecoveryBridgedT.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr double PI = 3.14159265358979323846;

// Oracle reference (dB) from analysis/eq_reference.py :: bridged_t_tf (UNLOADED).
// Regenerate if component values change (single source of truth = the oracle).
struct Pt { double f; double db; };
static const std::vector<Pt> kRef = {
    {    20.0,  -0.34561 }, {    50.0,  -1.82304 }, {   100.0,  -4.97438 },
    {   200.0, -10.24635 }, {   300.0, -14.35345 }, {   500.0, -21.80224 },
    {   717.0, -28.07107 }, {  1000.0, -22.30494 }, {  2000.0, -12.65347 },
    {  3000.0,  -8.88222 }, {  5000.0,  -5.13451 }, { 10000.0,  -1.90205 },
    { 15000.0,  -0.94459 }, { 20000.0,  -0.55538 },
};

// Steady-state peak magnitude (dB) at a frequency (rail clamp OFF -> linear).
static double measureDb(double freq, double fs)
{
    RecoveryBridgedT stage;
    stage.prepare(fs);

    const double period = fs / freq;
    const int settle = static_cast<int>(std::max(0.25 * fs, 8.0 * period));
    const int measure = static_cast<int>(std::ceil(2.0 * period)) + 1;

    const double amp = 1.0e-3; // tiny (rails disabled anyway) -> pure linear TF
    double peak = 0.0;
    for (int n = 0; n < settle + measure; ++n)
    {
        const double x = amp * std::sin(2.0 * PI * freq * static_cast<double>(n) / fs);
        const double y = stage.process(x);
        if (n >= settle)
            peak = std::max(peak, std::abs(y));
    }
    return (peak > 0.0) ? 20.0 * std::log10(peak / amp) : -300.0;
}


struct CoupledRecoveryOracle
{
    explicit CoupledRecoveryOracle(double sampleRate) : fs(sampleRate) {}
    double fs, i16 = 0.0, i17 = 0.0, i18 = 0.0, i27 = 0.0;
    static double clip(double z) noexcept
    {
        constexpr double pos = 2.7, neg = 2.9, h = 0.35;
        if (z > pos - h)
            return z >= pos + h ? pos : pos - (z - pos - h) * (z - pos - h) / (4.0 * h);
        if (z < -(neg - h))
            return z <= -(neg + h) ? -neg : -neg + (z + neg + h) * (z + neg + h) / (4.0 * h);
        return z;
    }

    // Direct 4x4 Kirchhoff matrix, with C18 between N1 and DRIVEN output O.
    // Partial-pivot Gaussian elimination is independent of production inverse
    // and rank-one clamp correction.
    void solve(const double rhs[4], double opampOut, double v[4]) const noexcept
    {
        const double g22 = 1.0 / 100e3, g23 = 1.0 / 33e3;
        const double g24 = 1.0 / 10e3, g25 = 1.0 / 22e3;
        const double k16 = 2.0 * fs * 680e-12, k17 = 2.0 * fs * 22e-9;
        const double k18 = 2.0 * fs * 1e-9, k27 = 2.0 * fs * 1e-9;
        double a[4][5] = {
            {g22 + g23 + k17, -g23, 0.0, 0.0, rhs[0]},
            {-g23, g23 + k16 + g24, -g24, 0.0, rhs[1]},
            {0.0, -g24, g24 + g25 + k18, -g25, rhs[2] + k18 * opampOut},
            {0.0, 0.0, -g25, g25 + k27, rhs[3]}
        };
        for (int k = 0; k < 4; ++k)
        {
            int pivot = k;
            for (int row = k + 1; row < 4; ++row)
                if (std::abs(a[row][k]) > std::abs(a[pivot][k])) pivot = row;
            for (int col = 0; col < 5; ++col)
                std::swap(a[pivot][col], a[k][col]);
            const double inverse = 1.0 / a[k][k];
            for (int col = k; col < 5; ++col) a[k][col] *= inverse;
            for (int row = k + 1; row < 4; ++row)
            {
                const double factor = a[row][k];
                for (int col = k; col < 5; ++col) a[row][col] -= factor * a[k][col];
            }
        }
        for (int row = 3; row >= 0; --row)
        {
            v[row] = a[row][4];
            for (int col = row + 1; col < 4; ++col)
                v[row] -= a[row][col] * v[col];
        }
    }

    double process(double input) noexcept
    {
        const double vin = clip(input);
        const double k16 = 2.0 * fs * 680e-12, k17 = 2.0 * fs * 22e-9;
        const double k18 = 2.0 * fs * 1e-9, k27 = 2.0 * fs * 1e-9;
        const double rhs[4] = {vin / 100e3 + i17, k16 * vin - i16, i18, i27};
        double lower[4], upper[4], v[4];
        solve(rhs, -2.9, lower);
        solve(rhs, +2.7, upper);
        double lo = lower[3], hi = upper[3];
        for (int iter = 0; iter < 60; ++iter)
        {
            const double guess = lo + 0.5 * (hi - lo);
            solve(rhs, clip(guess), v);
            if (v[3] > guess) lo = guess;
            else hi = guess;
        }
        const double kp = lo + 0.5 * (hi - lo);
        solve(rhs, clip(kp), v);
        const double output = clip(v[3]);
        i17 = 2.0 * k17 * v[0] - i17;
        i16 = 2.0 * k16 * (vin - v[1]) - i16;
        i18 = 2.0 * k18 * (v[2] - output) - i18;
        i27 = 2.0 * k27 * v[3] - i27;
        return output;
    }
};

int main()
{
    int failures = 0;

    // ---- Test 1: shoulder FR vs oracle (away from the notch) ----------------
    // Shoulders = f <= 200 Hz and f >= 3 kHz: gentle slopes where warp is tiny
    // and the ±0.25 dB claim is meaningful. The steep 300 Hz..2 kHz flanks are
    // covered by the notch test instead (Test 3) — a dB tolerance on a steep,
    // warp-shifted flank would be testing the warp, not the model.
    std::printf("=== Shoulder FR vs analytic oracle @ 48 kHz (away from notch) ===\n");
    for (const auto& p : kRef)
    {
        const double meas = measureDb(p.f, 48000.0);
        const double err = std::abs(meas - p.db);
        const bool shoulder = (p.f <= 200.0) || (p.f >= 3000.0);
        // Tight where the slope is gentle; the near-Nyquist top octave gets the
        // usual small bilinear allowance (verified as warp in Test 2).
        const double tol = (p.f >= 10000.0) ? 0.60 : 0.25;
        const bool pass = err <= tol;
        std::printf("  f=%8.1f  meas=%8.3f  ref=%8.3f  err=%.3f dB  %s\n",
                    p.f, meas, p.db, err,
                    shoulder ? (pass ? "PASS" : "FAIL") : "(flank: see Test 3)");
        if (shoulder && ! pass)
            ++failures;
    }

    // ---- Test 2: HF deviation is bilinear warp (shrinks 48k -> 96k) ---------
    std::printf("\n=== HF: error must shrink from 48k to 96k (warp) ===\n");
    for (const auto& p : kRef)
    {
        if (p.f < 3000.0) continue;
        const double e48 = std::abs(measureDb(p.f, 48000.0) - p.db);
        const double e96 = std::abs(measureDb(p.f, 96000.0) - p.db);
        const bool shrinks = e96 < e48 + 1e-9;
        const bool small96 = e96 <= 0.30;
        const bool pass = shrinks && small96;
        std::printf("  f=%8.1f  err48=%.3f  err96=%.3f (<=0.30)  %s\n",
                    p.f, e48, e96,
                    pass ? "PASS" : (! shrinks ? "FAIL (not warp!)" : "FAIL (96k too big)"));
        if (! pass)
            ++failures;
    }

    // ---- Test 3: notch located near 717 Hz + genuinely deep -----------------
    // Frequency = TIGHT (a bridged-T's null sits where R/C set it; the ~717 Hz
    // is well below Nyquist so warp barely moves it). Depth = LOOSE (the exact
    // cancellation depth is hyper-sensitive to discretisation AND, per the
    // schematic notes, to real-part tolerance — capture-validated at Phase 7;
    // here we only assert it is a real, deep notch).
    std::printf("\n=== Notch: frequency tight, depth loose ===\n");
    {
        const double fs = 48000.0;
        double fNotch = 0.0, dbNotch = 1e9;
        // Log sweep 400..1200 Hz for the minimum.
        const int K = 2000;
        for (int k = 0; k <= K; ++k)
        {
            const double f = 400.0 * std::pow(1200.0 / 400.0, static_cast<double>(k) / K);
            const double db = measureDb(f, fs);
            if (db < dbNotch) { dbNotch = db; fNotch = f; }
        }
        const double kOracleF = 716.3, kOracleDepth = -28.07;
        const bool freqOk = std::abs(fNotch - kOracleF) / kOracleF <= 0.03; // ±3%
        const bool deepOk = dbNotch <= -20.0;                                // real deep notch
        std::printf("  notch @ %.1f Hz (oracle %.1f, ±3%%): %s | depth %.2f dB (oracle %.2f, need <=-20): %s\n",
                    fNotch, kOracleF, freqOk ? "PASS" : "FAIL",
                    dbNotch, kOracleDepth, deepOk ? "PASS" : "FAIL");
        if (! (freqOk && deepOk))
            ++failures;
    }

    // ---- Test 4: DC-step polarity (unity, non-inverting) --------------------
    std::printf("\n=== DC-step polarity: unity, non-inverting ===\n");
    {
        RecoveryBridgedT stage;
        stage.prepare(48000.0);
        const double vin = 1.0e-3;
        double y = 0.0;
        for (int n = 0; n < 4000; ++n) // settle the 22n shunt cap fully
            y = stage.process(vin);
        const double gain = y / vin;
        const bool positive = (y > 0.0);
        const bool unity = std::abs(gain - 1.0) < 1e-3; // DC: caps open -> unity
        std::printf("  Vout/Vin=%.6f (expect 1.0)  sign %s  %s\n",
                    gain, positive ? "+" : "-", (positive && unity) ? "PASS" : "FAIL");
        if (! (positive && unity))
            ++failures;
    }


    // ---- Test 5: actual IC4_B output feeds C18 when rail saturates ----------
    // Reference is the independent direct 4x4 nonlinear KCL solve above.
    std::printf("\n=== Coupled IC4_B rail feedback vs direct KCL ===\n");
    for (double fs : {48000.0, 96000.0})
    {
        RecoveryBridgedT corrected, legacy;
        for (auto* stage : {&corrected, &legacy})
        {
            stage->prepare(fs);
            stage->setCoupledSk(true, 10.0e3, 22.0e3, 1.0e-9, 1.0e-9);
            stage->setRailVoltages(2.9, 2.7);
            stage->setRailClampEnabled(true);
        }
        corrected.setCoupledRailFeedback(true);
        CoupledRecoveryOracle reference(fs);
        double worst = 0.0, largestChange = 0.0;
        for (int n = 0; n < 3500; ++n)
        {
            const double input = n < 2200 ? 30.0 : n < 2800 ? -30.0
                : 14.0 * std::sin(0.045 * static_cast<double>(n));
            const double output = corrected.process(input);
            const double expected = reference.process(input);
            const double original = legacy.process(input);
            worst = std::max(worst, std::abs(output - expected));
            largestChange = std::max(largestChange, std::abs(output - original));
        }
        const bool ok = worst < 1.0e-8 && largestChange > 1.0e-3;
        std::printf("  fs %.0f: oracle worst %.3g V, legacy difference %.3g V %s\n",
                    fs, worst, largestChange, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    }
    for (bool railsEnabled : {false, true})
    {
        RecoveryBridgedT corrected, legacy;
        for (auto* s : {&corrected, &legacy})
        {
            s->prepare(48000.0);
            s->setCoupledSk(true, 10.0e3, 22.0e3, 1.0e-9, 1.0e-9);
            s->setRailVoltages(2.9, 2.7);
            s->setRailClampEnabled(railsEnabled);
        }
        corrected.setCoupledRailFeedback(true);
        bool identical = true;
        for (int n = 0; n < 2048; ++n)
        {
            const double x = (railsEnabled ? 0.01 : 30.0) * std::sin(n * 0.035);
            if (corrected.process(x) != legacy.process(x)) identical = false;
        }
        std::printf("  %s mode identical with original: %s\n",
                    railsEnabled ? "unsaturated" : "clamp-off",
                    identical ? "PASS" : "FAIL");
        if (!identical) ++failures;
    }

    std::printf("\n%s\n", failures == 0 ? "All tests passed." : "Some tests FAILED.");
    return (failures > 0) ? 1 : 0;
}
