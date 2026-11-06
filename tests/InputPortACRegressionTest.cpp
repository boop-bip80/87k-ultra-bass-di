// Complex jack input-impedance regression for the Black Mirror VII clone.
// This measures an OFFLINE WDF source-load probe through two independent
// transfer phasors; it does not claim retail B7K Ultra hardware parity.
//
// The ordinary InputBuffer sees an ideal ADC-recorded voltage and deliberately
// does not apply a hypothetical pickup impedance. The probe accepts a known
// Thevenin source R. If H0 is the ideal-source output transfer and Hs is the
// finite-source output transfer, the circuit's input port is observable by
// Z = Rs * (Hs/H0) / (1 - Hs/H0), including both real and imaginary parts.

#include "../src/dsp/InputBuffer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double drawnR1 = 1.0e6;
constexpr double drawnR2 = 1.0e6;
constexpr double drawnC1 = 100.0e-9;

struct Phasors
{
    std::complex<double> ideal;
    std::complex<double> sourceLoaded;
};

Phasors measurePhasors(int sampleRate, int frequencyHz, double sourceOhms)
{
    InputBuffer ideal;
    InputBufferSourceLoadProbe loaded(sourceOhms);
    ideal.prepare(sampleRate);
    loaded.prepare(sampleRate);

    // Frequency in whole Hz and one-second integration produce an exactly
    // coherent DFT. Three seconds of settling remove coupling-cap history.
    const int settle = 3 * sampleRate;
    const int count = sampleRate;
    double idealSin = 0.0, idealCos = 0.0;
    double loadedSin = 0.0, loadedCos = 0.0;
    for (int n = 0; n < settle + count; ++n)
    {
        const double phase = 2.0 * pi * frequencyHz * n / sampleRate;
        const double s = std::sin(phase);
        const double c = std::cos(phase);
        const double y0 = ideal.process(s);
        const double ys = loaded.process(s);
        if (n >= settle)
        {
            idealSin += y0 * s;
            idealCos += y0 * c;
            loadedSin += ys * s;
            loadedCos += ys * c;
        }
    }
    const double factor = 2.0 / count;
    return { factor * std::complex<double>(idealSin, idealCos),
             factor * std::complex<double>(loadedSin, loadedCos) };
}

std::complex<double> independentNodalImpedance(double angularFrequency)
{
    // Independent parallel-branch nodal-admittance oracle: R1 to ground,
    // with C1 in series with R2 to AC-ground reference VD. R3 feeds an ideal
    // op-amp input so carries no current. The values are fixed from the
    // bundled Black Mirror VII 1.1v (9 May 2023) drawing, not read from DSP.
    const std::complex<double> s(0.0, angularFrequency);
    const auto zCap = 1.0 / (s * drawnC1);
    const auto yJack = 1.0 / drawnR1 + 1.0 / (drawnR2 + zCap);
    return 1.0 / yJack;
}
}

int main()
{
    int cases = 0, failures = 0;
    double worstAbsoluteOhms = 0.0, worstRelative = 0.0;
    double worstMagnitudeDb = 0.0, worstPhaseDegrees = 0.0;

    for (int fs : {44100, 48000, 96000})
        for (double source : {100000.0, 1000000.0})
            for (int frequency : {1, 20, 100, 1000, 10000, 20000})
            {
                ++cases;
                const auto transfer = measurePhasors(fs, frequency, source);
                const auto voltageRatio = transfer.sourceLoaded / transfer.ideal;
                const auto measuredZ = source * voltageRatio / (1.0 - voltageRatio);

                // Trapezoidal WDF capacitors use the bilinear transform. The
                // analog frequency corresponding to a discrete f is warped;
                // comparing against unwarped 20 kHz would be a false error.
                const double analogOmega = 2.0 * fs * std::tan(pi * frequency / fs);
                const auto expectedZ = independentNodalImpedance(analogOmega);
                const double absOhms = std::abs(measuredZ - expectedZ);
                const double relative = absOhms / std::abs(expectedZ);
                const double magnitudeDb = std::abs(20.0 * std::log10(std::abs(measuredZ / expectedZ)));
                const double phaseDegrees = std::abs(std::arg(measuredZ / expectedZ) * 180.0 / pi);
                worstAbsoluteOhms = std::max(worstAbsoluteOhms, absOhms);
                worstRelative = std::max(worstRelative, relative);
                worstMagnitudeDb = std::max(worstMagnitudeDb, magnitudeDb);
                worstPhaseDegrees = std::max(worstPhaseDegrees, phaseDegrees);

                if (!std::isfinite(relative) || !std::isfinite(magnitudeDb)
                    || !std::isfinite(phaseDegrees)
                    || relative > 1.0e-5 || magnitudeDb > 0.0001 || phaseDegrees > 0.001)
                {
                    std::printf("FAIL fs=%d f=%d Rs=%.0f: measured=(%.6f%+.6fj)"
                                " expected=(%.6f%+.6fj) rel=%.9g dB=%.9g phase=%.9g deg\n",
                                fs, frequency, source, measuredZ.real(), measuredZ.imag(),
                                expectedZ.real(), expectedZ.imag(), relative, magnitudeDb, phaseDegrees);
                    ++failures;
                }
            }

    std::printf("Black Mirror VII complex input-port phasor regression: %d cases,"
                " max |error|=%.6g ohm, relative=%.6g, gain=%.6g dB, phase=%.6g deg; %s\n",
                cases, worstAbsoluteOhms, worstRelative, worstMagnitudeDb,
                worstPhaseDegrees, failures == 0 ? "PASS (clone only)" : "FAIL");
    return failures == 0 ? 0 : 1;
}
