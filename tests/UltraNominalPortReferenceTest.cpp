// Behavioral nominal-input what-if oracle. This does NOT validate retail
// B7K Ultra circuitry: the only externally specified datum is nominal 1 MΩ.
#include "../src/dsp/InputBuffer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace
{
constexpr double pi = 3.14159265358979323846264338;

class Reference
{
public:
    Reference(double sampleRate, double sourceOhms, double nominalOhms)
    {
        const double tau = InputBuffer::kR2 * InputBuffer::kC1;
        const double t = 2.0 * sampleRate * tau;
        const double gain = nominalOhms / (sourceOhms + nominalOhms);
        a = gain * t / (1.0 + t);
        p = (t - 1.0) / (t + 1.0);
    }
    double process(double x)
    {
        const double y = a * (x - previousInput) + p * previousOutput;
        previousInput = x;
        previousOutput = y;
        return y;
    }
private:
    double a, p, previousInput = 0.0, previousOutput = 0.0;
};

template <typename Fn>
bool throwsInvalid(Fn&& action)
{
    try { action(); }
    catch (const std::invalid_argument&) { return true; }
    return false;
}
bool close(double x, double y)
{
    return std::isfinite(x) && std::isfinite(y)
        && std::abs(x - y) < 2.0e-10 * (1.0 + std::abs(y));
}
}

int main()
{
    int failures = 0, sequences = 0;
    double largestError = 0.0;
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
    for (double rs : {0.0, 1e-9, 1.0, 1000.0, 100000.0, 1e6, 1e9})
    for (double nominal : {0.75e6, 1.0e6, 2.2e6})
    {
        InputBufferNominalPortProbe measuredPort(rs, nominal);
        measuredPort.prepare(fs);
        Reference independent(fs, rs, nominal);
        ++sequences;
        for (int n=0; n<700; ++n)
        {
            const double stimulus = n < 50 ? 1.0
                : n < 120 ? -0.38 : n < 165 ? 0.0
                : 0.86 * std::sin(2*pi*1337.0*n/fs)
                + 0.11 * std::cos(2*pi*107.0*n/fs);
            const double got = measuredPort.process(stimulus);
            const double expected = independent.process(stimulus);
            largestError = std::max(largestError, std::abs(got - expected));
            if (!close(got, expected))
            {
                std::printf("FAIL nominal oracle fs=%.0f Rs=%g Rin=%g n=%d got=%.12g expected=%.12g\n",
                    fs, rs, nominal, n, got, expected);
                ++failures;
                break;
            }
        }
        measuredPort.reset();
        Reference fresh(fs, rs, nominal);
        if (!close(measuredPort.process(0.52), fresh.process(0.52)))
        {
            std::printf("FAIL reset Rs=%g Rin=%g fs=%.0f\n", rs, nominal, fs);
            ++failures;
        }
    }

    // Rs=0 MUST be exactly the live WDF high-pass regardless of the nominal
    // behavioral port setting, ensuring no change to recorded-audio behavior.
    for (double fs : {44100.0, 48000.0, 192000.0})
    {
        InputBuffer live;
        InputBufferNominalPortProbe whatIf(0.0);
        live.prepare(fs);
        whatIf.prepare(fs);
        for (int n = 0; n < 1000; ++n)
        {
            const double x = std::sin(0.07*n) + 0.1 * std::cos(0.5*n);
            if (live.process(x) != whatIf.process(x))
            {
                std::printf("FAIL live-path identity n=%d fs=%.0f\n", n, fs);
                ++failures;
                break;
            }
        }
    }

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    if (!throwsInvalid([&]{ InputBufferNominalPortProbe invalid(-1.0); })
        || !throwsInvalid([&]{ InputBufferNominalPortProbe invalid(nan); })
        || !throwsInvalid([&]{ InputBufferNominalPortProbe invalid(inf); })
        || !throwsInvalid([&]{ InputBufferNominalPortProbe invalid(1000.0, -1.0); })
        || !throwsInvalid([&]{ InputBufferNominalPortProbe invalid(1000.0, 0.0); })
        || !throwsInvalid([&]{ InputBufferNominalPortProbe invalid(1000.0, nan); }))
    {
        std::puts("FAIL: invalid resistances should throw");
        ++failures;
    }

    // Finite astronomical values should not overflow the attenuation division.
    InputBufferNominalPortProbe guarded(1.0e308, 1.0e308);
    guarded.prepare(48000.0);
    if (!close(guarded.sourceAttenuation(), 0.5)
        || !std::isfinite(guarded.process(1.0)))
    {
        std::puts("FAIL large-ohm guard");
        ++failures;
    }

    // The manufacturer spec does not identify the actual circuit. Show only
    // a *reference sensitivity* for a resistive 100k source at 1 kHz.
    const double rs = 100000.0, r1 = 1e6, r2 = 1e6, c1 = 100e-9, f = 1000.0;
    const std::complex<double> s(0.0, 2*pi*f);
    const auto zBranch = r2 + 1.0/(s*c1);
    const auto zClone = r1*zBranch/(r1+zBranch);
    const auto hClone = (zClone/(rs+zClone))*(r2/zBranch);
    const auto hNominal = (1e6/(rs+1e6))*(s*r2*c1/(1.0+s*r2*c1));
    const double differenceDb = 20*std::log10(std::abs(hNominal/hClone));
    if (!(differenceDb > 0.70 && differenceDb < 0.82))
    {
        std::printf("FAIL illustrative 1 kHz loading delta %.6f dB\n", differenceDb);
        ++failures;
    }

    std::printf("Ultra *nominal port what-if*: %d time-domain cases; maximum oracle error %.3g V; 100k-source 1kHz delta %.4f dB; %s\n",
                sequences, largestError, differenceDb,
                failures == 0 ? "PASS (behavioral only)" : "FAIL");
    return failures ? 1 : 0;
}
