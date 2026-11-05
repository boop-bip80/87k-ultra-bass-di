// Deterministic coupled bridged-T / IC4_B AC oracle (no ngspice, JUCE, or hardware).
//
// THIS IS A CHECK OF THE DRAWN PCB GUITAR MANIA BLACK MIRROR VII CLONE ONLY.
// The drawing is not an authenticated Darkglass B7K Ultra manufacturer
// schematic. Matching this oracle does not establish real-pedal parity.
//
// Independent formula, derived by eliminating N1 and Kp from the *continuous*
// circuit before bilinear discretisation. It does not reuse RecoveryBridgedT's
// trapezoidal 4x4 nodal inverse or its cap history implementation:
//
//   Dsk(s) = 1 + s*C27*(R24+R25) + s*s*R24*R25*C18*C27
//   Vsk/Vno = 1 / Dsk
//   Yin(s)  = s*C27*(1+s*R25*C18) / Dsk
//   T(s)    = 1/R22 + 1/R23 + s*C17
//   Vno/Vin = (s*C16 + 1/(R22*R23*T)) /
//             (1/R23+s*C16+Yin-1/(R23*R23*T))
//   H(s)    = (Vno/Vin) / Dsk.
//
// Evaluate at s = j*2*fs*tan(pi*f/fs) to account for the production trapezoidal
// capacitor discretisation, then compare to sinusoidal lock-in of the real
// RecoveryBridgedT implementation. This verifies its *coupled* branch, not
// just the already-tested unloaded bridged-T branch. Additionally vary every
// component to reject accidental matching of a single nominal fixture.

#include "../src/dsp/RecoveryBridgedT.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>

namespace {
constexpr double kPi = 3.14159265358979323846264338327950288;

struct Parts
{
    double r22, r23, c16, c17, r24, r25, c18, c27;
};

constexpr Parts kDrawn{100e3, 33e3, 680e-12, 22e-9, 10e3, 22e3, 1e-9, 1e-9};
// Deliberately different component set: exercises the parameterised solver,
// not merely the hardcoded schematic nominal constants.
constexpr Parts kPerturbed{91e3, 39e3, 820e-12, 18e-9, 15e3, 18e3, 1.5e-9, 820e-12};

std::complex<double> analyticTransfer(const Parts& p, double f, double fs)
{
    using C = std::complex<double>;
    const C s(0.0, 2.0 * fs * std::tan(kPi * f / fs));
    const C dsk = 1.0 + s * p.c27 * (p.r24 + p.r25)
                  + s * s * p.r24 * p.r25 * p.c18 * p.c27;
    const C yin = s * p.c27 * (1.0 + s * p.r25 * p.c18) / dsk;
    const C t = 1.0 / p.r22 + 1.0 / p.r23 + s * p.c17;
    const C numerator = s * p.c16 + 1.0 / (p.r22 * p.r23 * t);
    const C denominator = 1.0 / p.r23 + s * p.c16 + yin
                          - 1.0 / (p.r23 * p.r23 * t);
    return numerator / denominator / dsk;
}

std::complex<double> measuredTransfer(const Parts& p, double f, double fs)
{
    RecoveryBridgedT stage;
    stage.prepare(fs);
    stage.setComponents(p.r22, p.r23, p.c16, p.c17);
    stage.setCoupledSk(true, p.r24, p.r25, p.c18, p.c27);
    stage.setRailClampEnabled(false);

    const int settle = static_cast<int>(fs);  // > 1 s; RC states fully settled
    const int count = static_cast<int>(fs);   // integer-Hz tones, coherent FFT bin
    const double dw = 2.0 * kPi * f / fs;
    std::complex<double> xsum{}, ysum{};
    constexpr double amplitude = 0.1;
    for (int n = 0; n < settle + count; ++n)
    {
        const double phase = dw * static_cast<double>(n);
        const double x = amplitude * std::sin(phase);
        const double y = stage.process(x);
        if (n >= settle)
        {
            const std::complex<double> demod(std::cos(phase), -std::sin(phase));
            xsum += x * demod;
            ysum += y * demod;
        }
    }
    return ysum / xsum;
}

double db(const std::complex<double>& h) { return 20.0 * std::log10(std::abs(h)); }
double phaseErrorDeg(const std::complex<double>& actual, const std::complex<double>& expected)
{
    return std::abs(std::arg(actual / expected)) * 180.0 / kPi;
}

int run(const char* title, const Parts& p, double fs)
{
    const std::array<double, 9> freqs{{40.0, 100.0, 500.0, 717.0, 1000.0,
                                       2000.0, 4000.0, 8000.0, 12000.0}};
    int failures = 0;
    double largestDbError = 0.0, largestPhaseError = 0.0;
    std::printf("\n%s @ %.0f Hz: clone bridged-T + loaded Sallen-Key\n", title, fs);
    for (const double freq : freqs)
    {
        const auto want = analyticTransfer(p, freq, fs);
        const auto got = measuredTransfer(p, freq, fs);
        const double dbError = std::abs(db(got) - db(want));
        const double phaseError = phaseErrorDeg(got, want);
        largestDbError = std::max(largestDbError, dbError);
        largestPhaseError = std::max(largestPhaseError, phaseError);
        const bool ok = std::isfinite(dbError) && std::isfinite(phaseError)
                        && dbError < 0.015 && phaseError < 0.10;
        failures += !ok;
        std::printf("  %6.0f Hz  expected=%+9.4f dB  DSP=%+9.4f dB  delta=%6.4f dB"
                    "  phase=%6.4f deg  %s\n",
                    freq, db(want), db(got), dbError, phaseError, ok ? "PASS" : "FAIL");
    }
    std::printf("  MAX: %.6f dB / %.6f deg (limits 0.015 dB / 0.10 deg)\n",
                largestDbError, largestPhaseError);
    return failures;
}

int runIndividualBranchChecks()
{
    // Every physical branch of the clone's loaded bridged-T is independently
    // perturbed. Require a *visible* predicted response change as well as DSP
    // agreement, so this is not merely a 16-case test of an unchanged output.
    struct Variation { const char* name; double Parts::* field; double value; double testFreq; };
    const std::array<Variation, 8> changes{{
        {"R22", &Parts::r22, kPerturbed.r22, 717.0},
        {"R23", &Parts::r23, kPerturbed.r23, 717.0},
        {"C16", &Parts::c16, kPerturbed.c16, 717.0},
        {"C17", &Parts::c17, kPerturbed.c17, 717.0},
        {"R24", &Parts::r24, kPerturbed.r24, 8000.0},
        {"R25", &Parts::r25, kPerturbed.r25, 8000.0},
        {"C18", &Parts::c18, kPerturbed.c18, 8000.0},
        {"C27", &Parts::c27, kPerturbed.c27, 8000.0},
    }};
    constexpr double fs = 96000.0;
    int failures = 0;
    std::printf("\nOne-at-a-time branch sensitivity @ 96 kHz\n");
    for (const auto& variation : changes)
    {
        Parts varied = kDrawn;
        varied.*(variation.field) = variation.value;
        const auto want = analyticTransfer(varied, variation.testFreq, fs);
        const auto got = measuredTransfer(varied, variation.testFreq, fs);
        const auto baseline = analyticTransfer(kDrawn, variation.testFreq, fs);
        const double predictedChange = std::abs(db(want) - db(baseline));
        const double errorDb = std::abs(db(got) - db(want));
        const double errorPhase = phaseErrorDeg(got, want);
        const bool ok = predictedChange > 0.25 && errorDb < 0.015 && errorPhase < 0.10;
        failures += !ok;
        std::printf("  %-3s changed @ %.0f Hz: sensitivity %.4f dB,"
                    " DSP error %.6f dB / %.6f deg  %s\n",
                    variation.name, variation.testFreq, predictedChange,
                    errorDb, errorPhase, ok ? "PASS" : "FAIL");
    }
    return failures;
}
} // namespace

int main()
{
    int failures = 0;
    failures += run("drawn parts", kDrawn, 48000.0);
    failures += run("drawn parts", kDrawn, 96000.0);
    failures += run("perturbed parts", kPerturbed, 96000.0);
    failures += runIndividualBranchChecks();
    std::printf("\n%s: %d discrepancies (clone-schematic topology only; hardware unverified)\n",
                failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
