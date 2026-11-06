// Independent adversarial checks for the experimental LEVEL-1 NJF primitive.
// Not a retail-pedal accuracy test. Keeps an independent long-double oracle,
// terminal-exchange checks, a no-diode corner, and finite-number safeguards.
#include "../src/dsp/JfetDevicePhysics.h"
#include "../src/dsp/J201Q2ActiveLoad.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>

using namespace ultrabass::dsp::jfet_physics;

namespace {
[[noreturn]] void fail(const char* what, double a, double b = 0.0) {
    std::fprintf(stderr, "ADVERSARIAL FAIL: %s (actual %.17g, reference %.17g)\n", what, a, b);
    std::exit(1);
}

// Independent oriented NJF current formula in extended precision, B=1.
long double oracleForward(const Level1NjfetParameters& p, long double vgs,
                          long double vds) {
    const long double vgst = vgs - p.thresholdVolts;
    long double channel = 0.0L;
    if (vgst > 0.0L) {
        const long double shape = vds <= vgst
            ? vds * (2.0L * vgst - vds) : vgst * vgst;
        channel = p.betaAmpsPerVoltSquared *
                  (1.0L + p.channelModulationPerVolt * vds) * shape;
    }
    return channel + static_cast<long double>(p.gminSiemens) * vds;
}

long double oracleCurrent(const Level1NjfetParameters& p, long double vg,
                          long double vd, long double vs) {
    if (vd >= vs) return oracleForward(p, vg - vs, vd - vs);
    return -oracleForward(p, vg - vd, vs - vd);
}

void testDevicePolarityAndContinuity() {
    std::mt19937_64 rng{0x87654321};
    std::uniform_real_distribution<double> voltage(-5.0, 5.0);
    Level1NjfetParameters p{};
    p.betaAmpsPerVoltSquared = 0.001031;
    p.thresholdVolts = -0.718;
    p.gminSiemens = 0.0; // no artificial leakage across Vds=0
    p.channelModulationPerVolt = 0.013;
    double worstAbs = 0;
    for (int i = 0; i < 100000; ++i) {
        const double vg = voltage(rng), vd = voltage(rng), vs = voltage(rng);
        const double actual = channelDrainCurrentAmps(p, vg, vd, vs);
        const double expected = static_cast<double>(oracleCurrent(p, vg, vd, vs));
        const double error = std::abs(actual - expected);
        worstAbs = std::max(error, worstAbs);
        if (!std::isfinite(actual) || error > 3e-15 * (1 + std::abs(expected)))
            fail("signed LEVEL-1 oracle mismatch", actual, expected);
        const double reversed = channelDrainCurrentAmps(p, vg, vs, vd);
        if (actual != -reversed) fail("terminal reversal", actual, -reversed);
        if ((vd - vs) * actual < 0.0) fail("negative channel dissipation", actual);
    }

    // Check both one-sided derivatives at zero Vds, explicitly with gmin=0.
    for (const double vgs : {-1.0, -0.718, -0.5, 0.0, 0.3, 0.7}) {
        const double h = 1e-6;
        const double left = -channelDrainCurrentAmps(p, vgs, 0, h) / h;
        const double right = channelDrainCurrentAmps(p, vgs, h, 0) / h;
        if (std::abs(left-right) > 3e-8) fail("Vds=0 derivative jump", left, right);
        if (channelDrainCurrentAmps(p, vgs, 0, 0) != 0.0)
            fail("nonzero Vds=0 branch", channelDrainCurrentAmps(p, vgs, 0, 0));
    }
    std::printf("JFET 100k oriented/trio/reverse/gmin=0 oracle checks: PASS (worst %.3g A)\n", worstAbs);
}

void testOptionalGateJunction() {
    Level1NjfetParameters p{};
    p.gateSaturationCurrentAmps = 0.0;
    for (double forward : {0.0, 0.2, 1.0, 20.0, 50.0, 100.0}) {
        const double actual = gateJunctionCurrentAmps(p, forward, 0.0);
        if (actual != 0.0) fail("disabled gate diode must be exactly zero", actual, 0.0);
    }
    p.gateSaturationCurrentAmps = 1.0e-14;
    const double largeButFiniteGate = 720.0 * p.thermalVoltageVolts;
    const double largeButFiniteCurrent = gateJunctionCurrentAmps(p, largeButFiniteGate, 0.0);
    const double independentExpected = static_cast<double>(
        1.0e-14L * std::expm1(720.0L));
    if (!std::isfinite(largeButFiniteCurrent) ||
        std::abs(largeButFiniteCurrent / independentExpected - 1.0) > 5e-13)
        fail("gate diode premature expm1 overflow", largeButFiniteCurrent, independentExpected);
    for (double forward : {0.0, 0.2, 1.0, 10.0, 20.0}) {
        const double actual = gateJunctionCurrentAmps(p, forward, 0.0);
        // When a physically unbounded exponential exceeds double range, the
        // documented finite-argument API must fail closed (NaN), not +Inf.
        if (std::isinf(actual)) fail("overflowing gate diode returned +Inf", actual);
        if (forward == 20.0) {
            if (!std::isnan(actual)) fail("unrepresentable gate current not rejected", actual);
        } else if (!std::isfinite(actual)) {
            fail("representable gate current unexpectedly rejected", actual);
        }
    }
    // Independent extended-precision diode oracle, including the region
    // 700 < (Vg-Vt)/Vthermal < 750 in which expm1() alone would overflow
    // although Is*expm1() can still fit in double.
    int diodeChecks = 0;
    for (const double saturation : {1e-250, 1e-14, 1e-3, 1e100}) {
        p.gateSaturationCurrentAmps = saturation;
        for (int n = -100; n <= 800; n += 17) {
            const double gate = n * p.thermalVoltageVolts;
            const long double normalized = static_cast<long double>(gate) /
                                           p.thermalVoltageVolts;
            const long double exact = static_cast<long double>(saturation) *
                                      std::expm1(normalized);
            const double actual = gateJunctionCurrentAmps(p, gate, 0.0);
            if (std::abs(exact) > std::numeric_limits<double>::max()) {
                if (!std::isnan(actual)) fail("large current must fail closed", actual);
            } else {
                if (!std::isfinite(actual)) fail("finite diode oracle yielded nonfinite", actual);
                const long double error = std::abs(static_cast<long double>(actual) - exact) /
                    std::max(1e-280L, std::abs(exact));
                if (error > 2e-12L) fail("extended-precision diode oracle mismatch", actual,
                                        static_cast<double>(exact));
            }
            ++diodeChecks;
        }
    }
    std::printf("Gate diode long-double oracle checks: %d PASS\n", diodeChecks);
    std::puts("Gate diode disabled/overflow checks: PASS");
}

void surveyQ2GateForwardCorners() {
    using namespace bm::dsp::j201q2;
    int forward = 0, total = 0;
    double worstForward = 0.0;
    Circuit c;
    for (double idss1 : {0.0002, 0.0005, 0.001})
    for (double vto1 : {-0.3, -0.718, -1.5})
    for (double idss2 : {0.0002, 0.0005, 0.001})
    for (double vto2 : {-0.3, -0.718, -1.5}) {
        c.q1 = {idss1, vto1};
        c.q2 = {idss2, vto2};
        BiasPoint b{};
        if (!solveDc(c, b)) fail("Q2 DC failed declared corner", idss1);
        const double vgs2 = b.q2GateVolt - b.outputVolt;
        if (b.q2GateForwardBiased != (vgs2 > 0.0) ||
            std::abs(b.q2GateSourceVolt - vgs2) > 1e-15)
            fail("Q2 gate-forward diagnostic inaccurate", b.q2GateSourceVolt, vgs2);
        if (vgs2 > 0.0) {
            ++forward;
            worstForward = std::max(worstForward, vgs2);
            std::printf("  FORWARD q1(Idss=%.4g,Vto=%.3g) q2(Idss=%.4g,Vto=%.3g) Q2 Vgs=%.9g V, output=%.9g V\n",
                        idss1, vto1, idss2, vto2, vgs2, b.outputVolt);
        }
        ++total;
    }
    std::printf("Q2 gate-forward unsupported corners: %d/%d; worst Vgs %.9g V\n",
                forward, total, worstForward);
}
} // namespace

int main() {
    testDevicePolarityAndContinuity();
    surveyQ2GateForwardCorners();
    testOptionalGateJunction();
    std::puts("JFET adversarial audit: PASS");
}
