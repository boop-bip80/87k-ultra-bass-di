// Research-only rail plausibility probe for the existing coupled Q1 runtime.
// Assumed 9 V supply and 4.5 V quiescent drain bias are NOT measurements.
#include "../src/dsp/JfetStage.h"
#include "../src/dsp/TrebleAttack.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kBeta = 1.031e-3;
constexpr double kPinch = 0.718;
constexpr double kSupply = 9.0;
constexpr double kAssumedDrainBias = 4.5;

// Independent trapezoidal-capacitor/source/drain/KCL residual check.
struct KclMonitor {
    explicit KclMonitor(double fs)
        : gc2(2.0 * fs * JfetStage::kC2),
          gc3(2.0 * fs * JfetStage::kC3),
          gInput(1.0 / (JfetStage::kR4 + JfetStage::kR5)) {
        double lo = 0.0, hi = kPinch;
        for (int i = 0; i < 110; ++i) {
            const double mid = 0.5 * (lo + hi);
            if (mid / JfetStage::kR6 > kBeta * (kPinch - mid) * (kPinch - mid))
                hi = mid;
            else
                lo = mid;
        }
        vov = kPinch - 0.5 * (lo + hi);
        quiescentCurrent = kBeta * vov * vov;
    }

    double residual(double input, double drainCurrent, double source,
                    double drain, double portOpen, double portOhms) {
        const double inputNode = (gc2 * input - h2) / (gc2 + gInput);
        h2 = 2.0 * gc2 * (input - inputNode) - h2;
        const double gate = inputNode * JfetStage::kR5 /
                            (JfetStage::kR4 + JfetStage::kR5);
        const double previousH3 = h3;
        h3 = 2.0 * gc3 * source - previousH3;
        const double sourceKcl = (1.0 / JfetStage::kR6 + gc3) * source -
                                 previousH3 - drainCurrent;
        const double overdrive = std::max(0.0, vov + gate - source);
        const double expectedCurrent = kBeta * overdrive * overdrive - quiescentCurrent;
        const double drainKcl = drainCurrent - expectedCurrent -
                                (drain - source) / JfetStage::kRo;
        const double portKcl = drain - (portOpen - portOhms * drainCurrent);
        return std::max({std::abs(sourceKcl) /
                             (1.0 + std::abs(drainCurrent) + std::abs(previousH3)),
                         std::abs(drainKcl) /
                             (1.0 + std::abs(drainCurrent) + kBeta * overdrive * overdrive),
                         std::abs(portKcl) /
                             (1.0 + std::abs(drain) + std::abs(portOpen) +
                              std::abs(portOhms * drainCurrent))});
    }

    double gc2, gc3, gInput, vov = 0.0, quiescentCurrent = 0.0;
    double h2 = 0.0, h3 = 0.0;
};
} // namespace

int main() {
    unsigned long long totalSamples = 0, totalOutside = 0;
    double worstResidual = 0.0;
    std::puts("Hypothetical 9.0 V rail, ASSUMED 4.5 V Q1 drain DC bias (not measured)");
    std::puts("fs_hz attack amplitude samples rail_outside fraction min_v max_v max_KCL");

    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (int mode = 0; mode < 3; ++mode) {
            for (double amplitude : {0.05, 0.5, 2.0, 8.0}) {
                JfetStage q1;
                q1.prepare(fs);
                q1.setPhysicalSourceFeedbackModel(true, kBeta, -kPinch);
                TrebleAttack network;
                network.prepare(fs);
                network.setCoupledJfetDrainPort(true);
                network.setQ2GateCapEnabled(true);
                network.setAttackTap(470e3, 506e3, 78.5e3, 212e3);
                network.setNotchLeg(TrebleAttack::Attack::Boost, 20.8e-9, 478.0);
                network.setNotchLeg(TrebleAttack::Attack::Flat, 19.7e-9, 6140.0);
                network.setNotchLeg(TrebleAttack::Attack::Cut, 22.4e-9, 6040.0);
                network.setAttack(static_cast<TrebleAttack::Attack>(mode));
                KclMonitor kcl(fs);
                unsigned outside = 0;
                double minimum = std::numeric_limits<double>::infinity();
                double maximum = -std::numeric_limits<double>::infinity();
                double maxKcl = 0.0;
                constexpr unsigned kSamples = 4096;
                for (unsigned n = 0; n < kSamples; ++n) {
                    const double t = static_cast<double>(n) / fs;
                    const double input = amplitude *
                        (0.65 * std::sin(2.0 * kPi * 197.0 * t) +
                         0.30 * std::sin(2.0 * kPi * 1319.0 * t) +
                         0.05 * ((n % 257u) == 0u ? 1.0 : 0.0));
                    const auto port = network.previewCoupledDrainPort();
                    const double current = -q1.processCoupled(
                        input, port.openCircuitVolts, port.theveninOhms);
                    const double signal = network.process(-current);
                    const double source = q1.probePhysicalSourceVoltage();
                    const double incrementalDrain = q1.probeCoupledDrainVoltage();
                    const double mappedDrain = kAssumedDrainBias + incrementalDrain;
                    const double residual = kcl.residual(input, current, source,
                        incrementalDrain, port.openCircuitVolts, port.theveninOhms);
                    if (!(std::isfinite(input) && std::isfinite(current) &&
                          std::isfinite(signal) && std::isfinite(source) &&
                          std::isfinite(incrementalDrain) && std::isfinite(mappedDrain) &&
                          std::isfinite(residual) &&
                          std::abs(incrementalDrain - network.probeLastDrainVoltage()) < 1e-7 &&
                          residual < 1e-9)) {
                        std::fprintf(stderr,
                            "FAIL numerical/coupled KCL invariant at fs=%g mode=%d amp=%g n=%u residual=%.9g\n",
                            fs, mode, amplitude, n, residual);
                        return EXIT_FAILURE;
                    }
                    if (mappedDrain < 0.0 || mappedDrain > kSupply)
                        ++outside;
                    minimum = std::min(minimum, mappedDrain);
                    maximum = std::max(maximum, mappedDrain);
                    maxKcl = std::max(maxKcl, residual);
                }
                totalSamples += kSamples;
                totalOutside += outside;
                worstResidual = std::max(worstResidual, maxKcl);
                std::printf("%.0f %d %.2f %u %u %.6f %.4f %.4f %.3e\n",
                    fs, mode, amplitude, kSamples, outside,
                    static_cast<double>(outside) / kSamples, minimum, maximum, maxKcl);
            }
        }
    }
    std::printf("TOTAL: outside=%llu/%llu fraction=%.6f worst_KCL=%.3e\n",
        totalOutside, totalSamples,
        static_cast<double>(totalOutside) / static_cast<double>(totalSamples),
        worstResidual);
    std::puts("Rail excursion is an ungated research flag, NOT a retail pedal measurement or rail proof.");
    return EXIT_SUCCESS;
}
