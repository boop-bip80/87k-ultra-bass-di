// =============================================================================
// Input-buffer analog source loading: independent complex-impedance oracle.
// =============================================================================
// This is an OFFLINE test of the third-party clone schematic's R1/C1/R2 nodes,
// not a claim of retail B7K Ultra hardware parity. The regular audio plugin
// must not re-apply source loading already incurred at the audio interface.

#include "../src/dsp/InputBuffer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double schematicR1 = 1.0e6;
constexpr double schematicR2 = 1.0e6;
constexpr double schematicC1 = 100.0e-9;

// Derived directly from complex impedances and two cascaded voltage divisions,
// not from the algebraic TF in InputBuffer.h or from the WDF wave equations.
std::complex<double> schematicTransfer(double hz, double sourceOhms)
{
    using Complex = std::complex<double>;
    const Complex jomega(0.0, 2.0 * pi * hz);
    const Complex zCap = 1.0 / (jomega * schematicC1);
    const Complex zBranch = schematicR2 + zCap;
    const Complex zInput = schematicR1 * zBranch / (schematicR1 + zBranch);
    return (zInput / (sourceOhms + zInput)) * (schematicR2 / zBranch);
}

// Synchronous sine/cosine projection over exactly ten periods to recover
// amplitude without sampling-grid dependence on the waveform peak location.
struct Response { double gainDb; double phaseDeg; };
Response measureResponse(double hz, double fs, double sourceOhms)
{
    InputBufferSourceLoadProbe stage(sourceOhms);
    stage.prepare(fs);

    const int settle = static_cast<int>(std::ceil(1.5 * fs));
    const int samples = static_cast<int>(std::round(10.0 * fs / hz));
    double inPhase = 0.0;
    double quadrature = 0.0;
    for (int n = 0; n < settle + samples; ++n)
    {
        const double phase = 2.0 * pi * hz * static_cast<double>(n) / fs;
        const double sinPhase = std::sin(phase);
        const double y = stage.process(sinPhase);
        if (n >= settle)
        {
            inPhase += y * sinPhase;
            quadrature += y * std::cos(phase);
        }
    }
    const double amplitude = (2.0 / static_cast<double>(samples))
                           * std::hypot(inPhase, quadrature);
    return { 20.0 * std::log10(amplitude),
             std::atan2(quadrature, inPhase) * 180.0 / pi };
}
}

int main()
{
    int failures = 0;

    // An invalid offline diagnostic setup must fail explicitly instead of
    // silently modelling a different source resistor.
    for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()})
    {
        try
        {
            InputBufferSourceLoadProbe bad(invalid);
            (void) bad;
            std::printf("FAIL: accepted invalid source resistance\n");
            ++failures;
        }
        catch (const std::invalid_argument&)
        {
        }
    }

    // The probe MUST be exactly identical to the live zero-impedance stage
    // at its default setting. This also protects against unwanted modifications
    // to the plugin's audio path while adding source-loading diagnostics.
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        InputBuffer live;
        InputBufferSourceLoadProbe probe;
        live.prepare(fs);
        probe.prepare(fs);

        for (int n = 0; n < 32768; ++n)
        {
            const double x = 0.34 * std::sin(2.0 * pi * 83.0 * n / fs)
                           + 0.17 * std::sin(2.0 * pi * 410.0 * n / fs)
                           + ((n % 71) == 0 ? 0.25 : 0.0);
            const double expected = live.process(x);
            const double actual = probe.process(x);
            if (expected != actual)
            {
                std::printf("FAIL: ideal-source probe mismatch fs=%.0f, n=%d\n", fs, n);
                ++failures;
                break;
            }
        }
    }

    // The pedal's *input impedance* is frequency-dependent:
    // at DC C1 is open => R1 alone (1 MΩ); in the audio band C1 shorts
    // relative to R2 => R1 || R2 ~= 500 kΩ, not 1 MΩ.
    const double apparentInputHighHz = std::abs(
        (schematicR1 * (schematicR2 + 1.0 / std::complex<double>(0.0, 2.0 * pi * 1000.0 * schematicC1)))
        / (schematicR1 + schematicR2 + 1.0 / std::complex<double>(0.0, 2.0 * pi * 1000.0 * schematicC1)));
    if (std::abs(apparentInputHighHz - 500000.0) > 1500.0)
    {
        std::printf("FAIL: 1 kHz input impedance %.1f ohm (expected ~500k)\n", apparentInputHighHz);
        ++failures;
    }

    // With Rsource>0, verify R1 loading, C1 high-pass, R2 input bias, and
    // source series loss against independent complex nodal analysis. Including
    // 1 MΩ Rsource exposes the otherwise invisible pulldown strongly.
    const std::vector<double> sources { 1000.0, 10000.0, 100000.0, 1000000.0 };
    const std::vector<double> frequencies { 2.0, 20.0, 200.0, 2000.0 };
    const std::vector<double> sampleRates { 48000.0, 96000.0 };

    for (double fs : sampleRates)
        for (double source : sources)
            for (double hz : frequencies)
            {
                const Response measured = measureResponse(hz, fs, source);
                const std::complex<double> expected = schematicTransfer(hz, source);
                const double analyticDb = 20.0 * std::log10(std::abs(expected));
                const double analyticPhaseDeg = std::arg(expected) * 180.0 / pi;
                const double gainErrorDb = std::abs(measured.gainDb - analyticDb);
                const double phaseErrorDeg = std::abs(
                    std::remainder(measured.phaseDeg - analyticPhaseDeg, 360.0));
                if (gainErrorDb > 0.025 || phaseErrorDeg > 0.25)
                {
                    std::printf("FAIL: fs=%.0f Rsource=%.0f hz=%.0f gainErr=%.5f dB phaseErr=%.5f deg\n",
                                fs, source, hz, gainErrorDb, phaseErrorDeg);
                    ++failures;
                }
            }

    const double db100k = 20.0 * std::log10(std::abs(schematicTransfer(1000.0, 100000.0)));
    std::printf("Input source-load: 32 analytic magnitude+phase checks, 3 bit-exact ideal-path checks; "
                "|Zin(1 kHz)|=%.1f kOhm, gain(Rsource=100k, 1kHz)=%.4f dB\n",
                apparentInputHighHz / 1000.0, db100k);
    std::printf("%s\n", failures == 0 ? "All source-loading tests passed."
                                         : "Source-loading tests FAILED.");
    return failures == 0 ? 0 : 1;
}
