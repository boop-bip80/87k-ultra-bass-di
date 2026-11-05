// Independent branch-stamped circuit check for rail-aware Baxandall feedback.
// No matrix, sensitivity vector, or capacitor history from the DSP implementation
// is used: two fresh Gaussian nodal solves reproduce the schematic per sample.
#include "../src/dsp/Baxandall.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kVin = -1;
constexpr int kOut = -2;
constexpr int kMinus = -3;
constexpr int kA = 0, kB = 1, kWb = 2, kT3 = 3, kT1 = 4, kWt = 5;
constexpr int kShared = 6, kVi = 7;

// Both solvers use distinct matrices and ordinary Gaussian elimination with
// partial pivoting, not the DSP implementation's precomputed Gauss-Jordan inverse.
template <int N>
bool solve(double (&a)[N][N], double (&b)[N], double (&x)[N])
{
    for (int col = 0; col < N; ++col)
    {
        int best = col;
        for (int row = col + 1; row < N; ++row)
            if (std::abs(a[row][col]) > std::abs(a[best][col])) best = row;
        if (!(std::abs(a[best][col]) > 1.0e-30)) return false;
        if (best != col)
        {
            for (int j = col; j < N; ++j) std::swap(a[best][j], a[col][j]);
            std::swap(b[best], b[col]);
        }
        for (int row = col + 1; row < N; ++row)
        {
            const double m = a[row][col] / a[col][col];
            a[row][col] = 0.0;
            for (int j = col + 1; j < N; ++j) a[row][j] -= m * a[col][j];
            b[row] -= m * b[col];
        }
    }
    for (int i = N - 1; i >= 0; --i)
    {
        double numerator = b[i];
        for (int j = i + 1; j < N; ++j) numerator -= a[i][j] * x[j];
        x[i] = numerator / a[i][i];
    }
    return true;
}

struct Reference {
    double fs = 48000.0;
    double bass = 0.5, treble = 0.5, r36 = Baxandall::kR36;
    double c21 = 0.0;
    double history[6] = {}; // C25, C26, C28, C29, C30, C21

    double conductance(double cap) const { return 2.0 * fs * cap; }
    void setInputCap(double cap) { c21 = cap; history[5] = 0.0; }

    static double clamp(double x)
    {
        // RailClamp independently transcribed as a quadratic-plus-hard rail:
        // physical absolute limits are not assumed known or measured.
        const double vPos = 2.7, vNeg = 2.9, knee = 0.35;
        if (x >= vPos + knee) return vPos;
        if (x > vPos - knee)
        {
            const double d = x - vPos - knee;
            return vPos - d * d / (4.0 * knee);
        }
        if (x <= -vNeg - knee) return -vNeg;
        if (x < -vNeg + knee)
        {
            const double d = x + vNeg + knee;
            return -vNeg + d * d / (4.0 * knee);
        }
        return x;
    }

    template <int N>
    bool circuit(double vin, double outVolts, bool ideal, double (&nodes)[N]) const
    {
        double y[N][N] = {};
        double rhs[N] = {};

        // In an ideal-op-amp matrix, output is node 6 and Vminus is known 0.
        // In a physically driven passive matrix, Vminus is node 6 and the
        // externally clamped output is known outVolts. Stamping is identical.
        const auto unknown = [ideal](int id) -> int {
            if (id >= 0) return id;
            if (id == kOut) return ideal ? kShared : -1;
            if (id == kMinus) return ideal ? -1 : kShared;
            return -1; // input source is known
        };
        const auto voltage = [vin, outVolts](int id) -> double {
            if (id == kVin) return vin;
            if (id == kOut) return outVolts;
            return 0.0;
        };
        const auto stamp = [&](int a, int b, double g, double hist) {
            const int ia = unknown(a), ib = unknown(b);
            const auto addRow = [&](int row, int col, double sign) {
                if (row < 0 || (ideal && row == kShared)) return;
                y[row][row] += g;
                if (col >= 0) y[row][col] -= g;
                else rhs[row] += g * voltage(sign > 0.0 ? b : a);
                rhs[row] += sign * hist;
            };
            addRow(ia, ib, 1.0);
            addRow(ib, ia, -1.0);
        };

        const int input = N == 8 ? kVi : kVin;
        stamp(input, kA, 1.0 / Baxandall::kR33, 0.0);
        stamp(kB, kOut, 1.0 / Baxandall::kR34, 0.0);
        stamp(kA, kWb, 1.0 / (bass * Baxandall::kRp), 0.0);
        stamp(kWb, kB, 1.0 / ((1.0 - bass) * Baxandall::kRp), 0.0);
        stamp(kA, kWb, conductance(Baxandall::kC25), history[0]);
        stamp(kB, kWb, conductance(Baxandall::kC26), history[1]);
        stamp(kWb, kMinus, 1.0 / Baxandall::kR35, 0.0);
        stamp(kT3, input, conductance(Baxandall::kC28), history[2]);
        stamp(kT3, kWt, 1.0 / (treble * Baxandall::kRp), 0.0);
        stamp(kWt, kT1, 1.0 / ((1.0 - treble) * Baxandall::kRp), 0.0);
        stamp(kT1, kOut, conductance(Baxandall::kC29), history[3]);
        stamp(kWt, kMinus, 1.0 / r36, 0.0);
        stamp(kOut, kMinus, 1.0 / Baxandall::kR37, 0.0);
        stamp(kOut, kMinus, conductance(Baxandall::kC30), history[4]);
        if (N == 8) stamp(kVi, kVin, conductance(c21), history[5]);

        if (ideal)
        {
            // Ideal zero-voltage inverting input has no KCL unknown; replace
            // output KCL with the op amp's Vminus=0 summing-junction equation.
            y[6][2] = 1.0 / Baxandall::kR35;
            y[6][5] = 1.0 / r36;
            y[6][6] = 1.0 / Baxandall::kR37 + conductance(Baxandall::kC30);
            rhs[6] = history[4];
        }
        return solve<N>(y, rhs, nodes);
    }

    template <int N>
    double next(double vin)
    {
        double ideal[N] = {}, passive[N] = {};
        if (!circuit<N>(vin, 0.0, true, ideal)) std::abort();
        const double actual = clamp(ideal[6]);
        if (!circuit<N>(vin, actual, false, passive)) std::abort();

        // Reconstruct history from the *actual* capacitor terminal voltages.
        const double input = N == 8 ? passive[7] : vin;
        const double vm = passive[6];
        const double v[6] = {
            passive[0] - passive[2], passive[1] - passive[2],
            passive[3] - input, passive[4] - actual, actual - vm,
            input - vin,
        };
        const double caps[6] = {
            Baxandall::kC25, Baxandall::kC26, Baxandall::kC28,
            Baxandall::kC29, Baxandall::kC30, c21,
        };
        for (int i = 0; i < 5 + (N == 8 ? 1 : 0); ++i)
            history[i] = 2.0 * conductance(caps[i]) * v[i] - history[i];
        return actual;
    }
    double next(double vin) { return c21 > 0.0 ? next<8>(vin) : next<7>(vin); }
};

double signal(int n, double fs)
{
    if (n < 5) return 0.0;
    if (n < 130) return 5.0;
    if (n < 250) return 0.0;
    if (n < 370) return -4.8;
    if (n < 500) return 0.0;
    if (n < 650) return 1.8;
    if (n < 800) return -1.8;
    return 2.0 * std::sin(2.0 * kPi * 471.0 * n / fs)
         + 0.8 * std::sin(2.0 * kPi * 93.0 * n / fs);
}

bool test(double fs, double bass, double treble, double inputCap, double r36)
{
    Baxandall actual, disabled;
    actual.prepare(fs);
    disabled.prepare(fs);
    for (Baxandall* stage : { &actual, &disabled })
    {
        stage->setBass(bass);
        stage->setTreble(treble);
        stage->setInputCap(inputCap);
        stage->setTrebleWiperR(r36);
        stage->setRailVoltages(2.9, 2.7);
        stage->setRailClampEnabled(true);
    }
    actual.setRailAwarePassiveFeedback(true);
    disabled.setRailAwarePassiveFeedback(false);

    Reference ref;
    ref.fs = fs;
    ref.bass = bass;
    ref.treble = treble;
    ref.r36 = r36;
    ref.setInputCap(inputCap);

    double maxErr = 0.0, maxImpact = 0.0, maxOutput = 0.0;
    bool finite = true;
    for (int n = 0; n < 1300; ++n)
    {
        if (n == 700 || n == 950)
        {
            // Rebuild both sensitivity matrices at runtime while capacitor
            // history remains live; catch stale or mis-ordered inverse data.
            const double newBass = n == 700 ? 0.05 : 0.85;
            const double newTreble = n == 700 ? 0.95 : 0.20;
            const double newR36 = n == 700 ? 4700.0 : 3300.0;
            for (Baxandall* s : { &actual, &disabled })
            {
                s->setBass(newBass);
                s->setTreble(newTreble);
                s->setTrebleWiperR(newR36);
            }
            ref.bass = newBass;
            ref.treble = newTreble;
            ref.r36 = newR36;
        }
        const double vin = signal(n, fs);
        const double truth = ref.next(vin);
        const double proposed = actual.process(vin);
        const double legacy = disabled.process(vin);
        maxErr = std::max(maxErr, std::abs(proposed - truth));
        maxImpact = std::max(maxImpact, std::abs(proposed - legacy));
        maxOutput = std::max(maxOutput, std::abs(proposed));
        finite = finite && std::isfinite(proposed) && std::isfinite(truth);
    }
    const bool pass = finite && maxErr < 1.0e-6 && maxImpact > 1.0e-5
                      && maxOutput > 1.5;
    std::printf("  fs=%6.0f B=%.2f T=%.2f C21=%s R36=%4.0f "
                "oracle %.3e V, legacy delta %.3e V %s\n",
                fs, bass, treble, inputCap > 0.0 ? "100n" : "off ",
                r36, maxErr, maxImpact, pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace

int main()
{
    int fails = 0;
    struct Knobs { double b, t, r36; };
    const Knobs settings[] = {
        { 0.5, 0.5, 3300.0 },
        { 0.15, 0.85, 3300.0 },
        { 0.85, 0.15, 4700.0 },
    };
    for (double fs : { 48000.0, 96000.0, 192000.0 })
        for (double cap : { 0.0, 100.0e-9 })
            for (const auto& knob : settings)
                if (!test(fs, knob.b, knob.t, cap, knob.r36)) ++fails;

    // An opt-in disabled flag must leave the original fitted response exact,
    // even with rail clipping present (including when C21 is on).
    for (double cap : { 0.0, 100.0e-9 })
    {
        Baxandall a, b;
        a.prepare(48000.0);
        b.prepare(48000.0);
        for (Baxandall* s : { &a, &b })
        {
            s->setInputCap(cap);
            s->setBass(0.1);
            s->setTreble(0.9);
            s->setRailVoltages(2.9, 2.7);
            s->setRailClampEnabled(true);
        }
        b.setRailAwarePassiveFeedback(false);
        bool ok = true;
        for (int n = 0; n < 1400; ++n)
        {
            const double x = signal(n, 48000.0);
            const double ya = a.process(x), yb = b.process(x);
            ok = (std::memcmp(&ya, &yb, sizeof(ya)) == 0) && ok;
        }
        std::printf("  opt-out bit-for-bit C21 %s: %s\n",
                    cap > 0.0 ? "on" : "off", ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    }

    // Never engage the passive correction when the op-amp is genuinely linear:
    // the ideal summing-junction constraint must remain unchanged.
    for (double cap : { 0.0, 100.0e-9 })
    {
        Baxandall a, b;
        a.prepare(48000.0);
        b.prepare(48000.0);
        for (Baxandall* s : { &a, &b })
        {
            s->setInputCap(cap);
            s->setBass(0.32);
            s->setTreble(0.68);
            s->setRailClampEnabled(false);
        }
        b.setRailAwarePassiveFeedback(true);
        bool ok = true;
        for (int n = 0; n < 1400; ++n)
        {
            const double x = signal(n, 48000.0);
            const double ya = a.process(x), yb = b.process(x);
            ok = (std::memcmp(&ya, &yb, sizeof(ya)) == 0) && ok;
        }
        std::printf("  no rail clamp, opt-in C21 %s: %s\n",
                    cap > 0.0 ? "on" : "off", ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    }

    std::printf("Baxandall rail feedback: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
