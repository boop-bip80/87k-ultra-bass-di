// Independent nonlinear KCL oracle for research-only Shockley D1/D2 mode.
// These tests validate circuit-equation consistency, NOT retail hardware parity.
#include "../src/dsp/Clipper.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace reference {
enum class Mode { Off, Full, Residue };
struct Step { double node, output, residual; };

// Independent scalar/branch formulation; no Clipper VTC or solver calls.
class Circuit {
public:
    Circuit(double rate, Clipper::Grunt grunt, Mode mode)
        : fs(rate), adaa(mode)
    {
        const double cap = grunt == Clipper::Grunt::Cut ? 4.7e-9
                         : grunt == Clipper::Grunt::Flat ? 51.7e-9 : 224.7e-9;
        gcIn = 2.0 * rate * cap;
        gcFb = 2.0 * rate * 220e-12;
        midY = gcIn + 1.0 / 6800.0;
        inputY = gcIn / (6800.0 * midY);
        feedbackY = gcFb + 1.0 / 330000.0;
    }

    static double vtc(double w)
    {
        const double sat = w >= 0.0 ? 3.15 : 3.85;
        const double q = 25.0 * w / sat;
        return -sat * q / std::hypot(1.0, q);
    }
    static double integral(double w)
    {
        const double sat = w >= 0.0 ? 3.15 : 3.85;
        const double q = 25.0 * w / sat;
        return -(sat * sat / 25.0) * (std::sqrt(1.0 + q * q) - 1.0);
    }
    static double diode(double z)
    {
        // An independent smoothly-continued exponential above 60 n*Vt;
        // actual physical roots in this test remain far below that cutoff.
        if (z <= 60.0) return 5.0e-9 * std::expm1(z);
        return 5.0e-9 * (std::exp(60.0) * (1.0 + z - 60.0) - 1.0);
    }
    double outputAt(double w) const
    {
        if (adaa == Mode::Off) return vtc(w);
        const double delta = w - prevNode;
        const double mean = std::abs(delta) < 1e-7
                            ? vtc(0.5 * (w + prevNode))
                            : (integral(w) - integral(prevNode)) / delta;
        return adaa == Mode::Full ? mean : mean - 12.5 * delta;
    }
    double kcl(double w, double x) const
    {
        const double bias = ieqIn / (6800.0 * midY);
        const double nodeAbsolute = w + 2.657;
        const double fromGround = diode(-nodeAbsolute / 0.052);
        const double toRail = diode((nodeAbsolute - 9.0) / 0.052);
        return inputY * (x - w) - bias
             + feedbackY * (outputAt(w) - w) - ieqFb
             + fromGround - toRail;
    }
    Step process(double input)
    {
        double lo = -10000.0, hi = 10000.0;
        if (!(kcl(lo, input) > 0.0 && kcl(hi, input) < 0.0))
            std::abort();
        for (int i = 0; i < 120; ++i)
        {
            const double mid = lo + 0.5 * (hi - lo);
            if (kcl(mid, input) > 0.0) lo = mid;
            else hi = mid;
        }
        const double w = lo + 0.5 * (hi - lo);
        const double output = outputAt(w);
        const double residual = kcl(w, input);
        const double mid = (gcIn * input - ieqIn + w / 6800.0) / midY;
        ieqIn = 2.0 * gcIn * (input - mid) - ieqIn;
        ieqFb = 2.0 * gcFb * (output - w) - ieqFb;
        prevNode = w;
        return {w, output, residual};
    }
private:
    double fs, gcIn, gcFb, midY, inputY, feedbackY;
    Mode adaa;
    double ieqIn = 0.0, ieqFb = 0.0, prevNode = 0.0;
};
} // namespace reference

static void checkNear(double actual, double expected, double tolerance,
                      const char* label)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    {
        std::fprintf(stderr, "%s: got %.15g, expected %.15g (tol %g)\n",
                     label, actual, expected, tolerance);
        std::exit(1);
    }
}

int main()
{
    const struct Fixture { double fs, input, node, output; } fixtures[] = {
        {44100, -8, -3.2382946219328, +3.8456538612950},
        {44100,+12, +6.9193699949651, -3.1494778677130},
        {48000, -8, -3.2381634163121, +3.8456535096857},
        {48000,+12, +6.9185366409511, -3.1494777419526},
        {96000, -8, -3.2261706183983, +3.8456211900032},
        {96000,+12, +6.8917342139894, -3.1494736728788},
        {192000,-8, -3.1363365334737, +3.8453672121060},
        {192000,+12,+6.0680747057189, -3.1493211412810},
    };
    for (const auto& f : fixtures)
    {
        Clipper dut;
        dut.prepare(f.fs);
        dut.setDiodeCurrentModel(true);
        reference::Circuit oracle(f.fs, Clipper::Grunt::Cut, reference::Mode::Off);
        const auto expected = oracle.process(f.input);
        const double output = dut.process(f.input);
        checkNear(dut.probeNodeW(), f.node, 1e-6, "first-sample W fixture");
        checkNear(output, f.output, 2e-6, "first-sample Y fixture");
        checkNear(dut.probeNodeW(), expected.node, 1e-6, "first-sample W oracle");
        checkNear(output, expected.output, 2e-6, "first-sample Y oracle");
    }

    // Multi-sample implicit capacitor state must be updated with the solved
    // *conducting* W. Compare the real DSP to a scalar bisection reference.
    double worstNode = 0.0, worstOutput = 0.0, worstResidual = 0.0;
    int cases = 0;
    const reference::Mode modes[] = {
        reference::Mode::Off, reference::Mode::Full, reference::Mode::Residue,
    };
    const Clipper::Adaa adaa[] = {
        Clipper::Adaa::Off, Clipper::Adaa::Full, Clipper::Adaa::Residue,
    };
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    for (const auto grunt : {Clipper::Grunt::Cut, Clipper::Grunt::Flat,
                             Clipper::Grunt::Boost})
    for (int m = 0; m < 3; ++m)
    {
        Clipper dut;
        dut.prepare(rate);
        dut.setGrunt(grunt);
        dut.setADAA(adaa[m]);
        dut.setDiodeCurrentModel(true);
        reference::Circuit oracle(rate, grunt, modes[m]);
        for (int i = 0; i < 500; ++i)
        {
            const double x = 11.5 * std::sin(i * 0.637)
                           + 7.5 * std::cos(i * 0.115);
            const auto expected = oracle.process(x);
            const double actual = dut.process(x);
            const double nodeErr = std::abs(dut.probeNodeW() - expected.node);
            const double outErr = std::abs(actual - expected.output);
            worstNode = std::max(worstNode, nodeErr);
            worstOutput = std::max(worstOutput, outErr);
            worstResidual = std::max(worstResidual, std::abs(expected.residual));
            if (!std::isfinite(actual) || nodeErr > 1e-7 || outErr > 1e-6)
            {
                std::fprintf(stderr, "diode KCL mismatch fs %.0f grunt %d mode %d"
                                     " sample %d: W err %.9g Y err %.9g\n",
                             rate, static_cast<int>(grunt), m, i, nodeErr, outErr);
                return 1;
            }
            ++cases;
        }
    }
    // With the opt-in flag unused, the shipped path is *bitwise* unchanged.
    Clipper defaultPath, disabledPath;
    defaultPath.prepare(48000.0);
    disabledPath.prepare(48000.0);
    disabledPath.setDiodeCurrentModel(false);
    for (int i = 0; i < 10000; ++i)
    {
        const double x = 10.0 * std::sin(i * 0.13);
        const double a = defaultPath.process(x);
        const double b = disabledPath.process(x);
        if (std::memcmp(&a, &b, sizeof(a)) != 0)
        {
            std::fprintf(stderr, "Legacy opt-out bit-exact regression n=%d\n", i);
            return 1;
        }
    }
    // Reject numerically nonsensical / incompatible device parameters. In
    // particular a rail below the inverter's assumed self-bias point causes
    // extreme forward current at W=0 and invalidates the physical setup.
    Clipper parameterGuard;
    parameterGuard.prepare(48000.0);
    parameterGuard.setDiodeCurrentModel(true, 1e100, 0.005, 9.0);
    if (parameterGuard.diodeCurrentModelEnabled()) return 1;
    parameterGuard.setDiodeCurrentModel(true, 5e-9, 0.052, 1.0);
    if (parameterGuard.diodeCurrentModelEnabled()) return 1;
    parameterGuard.setDiodeCurrentModel(true);
    if (! parameterGuard.diodeCurrentModelEnabled()) return 1;
    parameterGuard.setDiodeCurrentModel(false, std::numeric_limits<double>::quiet_NaN());
    if (parameterGuard.diodeCurrentModelEnabled()) return 1;
    parameterGuard.setDiodeCurrentModel(true, 1e-15, 0.005, 3.157);
    if (! parameterGuard.diodeCurrentModelEnabled()) return 1;
    parameterGuard.setPhysicalVtc(1, 18.0, 1000.0);
    if (parameterGuard.diodeCurrentModelEnabled()) return 1;
    if (! std::isfinite(parameterGuard.process(-100.0))) return 1;
    std::printf("Clipper Shockley KCL: %d independent cases PASS; max W %.3g V,"
                " max Y %.3g V, KCL %.3g A; legacy bit-exact PASS\n",
                cases, worstNode, worstOutput, worstResidual);
}
