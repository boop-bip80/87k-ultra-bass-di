// Offline source-loading boundaries: independent bilinear-domain reference.
// The live plugin input buffer must remain independent of this diagnostic.
#include "../src/dsp/InputBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kPi = 3.1415926535897932384626433832795;

struct Reference
{
    explicit Reference(double fs, double sourceOhms)
    {
        constexpr double r1 = 1.0e6;
        constexpr double r2 = 1.0e6;
        constexpr double c1 = 100.0e-9;
        const double b = sourceOhms * (r1 + r2) + r1 * r2;
        const double dc = sourceOhms + r1;
        const double k = r1 * r2 / b;
        const double tustin = 2.0 * fs * c1 * (b / dc);
        a = k * tustin / (1.0 + tustin);
        p = (tustin - 1.0) / (tustin + 1.0);
    }

    double process(double x)
    {
        const double y = a * (x - lastInput) + p * lastOutput;
        lastInput = x;
        lastOutput = y;
        return y;
    }

    double a {}, p {}, lastInput {}, lastOutput {};
};

bool closeEnough(double actual, double expected, double tol = 1.5e-10)
{
    return std::isfinite(actual) && std::isfinite(expected)
        && std::abs(actual - expected) <= tol * (1.0 + std::abs(expected));
}
}

int main()
{
    int failures = 0;
    int cases = 0;
    double worstError = 0.0;

    // Source-limit cases exercise both WDF polarity and the coupling-cap
    // state recurrence. A magnitude-only frequency response cannot catch
    // 180-degree inversion, incorrect DC steps, or a stale reset state.
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double rs : { 1.0e-9, 1.0, 100.0, 1000.0,
                          10000.0, 100000.0, 1000000.0, 1.0e7, 1.0e9 })
        {
            InputBufferSourceLoadProbe probe(rs);
            probe.prepare(fs);
            Reference oracle(fs, rs);
            ++cases;

            for (int n = 0; n < 700; ++n)
            {
                // Includes both polarities, DC plateaus, and broad-band
                // excitation. The 9.7 kHz sine also checks high-rate warping.
                const double input = n < 100 ? 1.0
                    : n < 200 ? -0.35
                    : n < 300 ? 0.0
                    : std::sin(2.0 * kPi * 9700.0 * n / fs)
                       + 0.18 * std::sin(2.0 * kPi * 43.0 * n / fs);
                const double actual = probe.process(input);
                const double expected = oracle.process(input);
                worstError = std::max(worstError, std::abs(actual - expected));
                if (!closeEnough(actual, expected))
                {
                    std::printf("FAIL: phase/DC rs=%g fs=%g n=%d actual=%+.12g expected=%+.12g\n",
                                rs, fs, n, actual, expected);
                    ++failures;
                    break;
                }
            }

            // Reset removes all capacitor memory, even after sustained
            // positive and negative drive.
            probe.reset();
            Reference fresh(fs, rs);
            const double actual = probe.process(-0.67);
            const double expected = fresh.process(-0.67);
            if (!closeEnough(actual, expected))
            {
                std::printf("FAIL: reset rs=%g fs=%g actual=%+.12g expected=%+.12g\n",
                            rs, fs, actual, expected);
                ++failures;
            }
        }

    // Exactly-zero source resistance is intentionally delegated to the
    // unchanged live InputBuffer, not a nearly-zero resistor.
    for (double fs : { 44100.0, 48000.0, 192000.0 })
    {
        InputBuffer live;
        InputBufferSourceLoadProbe ideal(0.0);
        live.prepare(fs);
        ideal.prepare(fs);
        for (int n = 0; n < 1024; ++n)
        {
            const double x = n % 31 == 0 ? 0.95 : -0.2;
            if (live.process(x) != ideal.process(x))
            {
                ++failures;
                std::printf("FAIL: zero-resistance ideal path fs=%g n=%d\n", fs, n);
                break;
            }
        }
    }
    std::printf("Source-load boundary oracle: %d finite-source cases, 3 live-path identity cases, max error %.3g V; %s\n",
                cases, worstError, failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
