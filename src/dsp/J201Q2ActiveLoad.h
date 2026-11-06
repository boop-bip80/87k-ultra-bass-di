// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>
#include <limits>

// Research-only, opt-in DC operating-point oracle for the Black Mirror VII
// clone's J201 source-follower load. No runtime incremental-voltage clamping.
namespace bm::dsp::j201q2 {

struct J201Parameters {
    double idssAmp = 0.0005;
    double pinchOffVolt = -0.718;
};

struct Circuit {
    double supplyVolt = 9.0;
    double r6Ohm = 3300.0;       // Q1 source to ground
    double r7Ohm = 200000.0;     // Passive DC path from output to ground
    double r8Ohm = 470000.0;
    double r11Ohm = 470000.0;
    double r9Ohm = 1000000.0;    // Q2 gate divider upper resistor
    double r10Ohm = 1000000.0;   // Q2 gate divider lower resistor
    J201Parameters q1{};
    J201Parameters q2{};
};

struct BiasPoint {
    double q1SourceVolt = 0.0;
    double outputVolt = 0.0;     // Q1 drain / Q2 source
    double q2GateVolt = 0.0;
    double q1DrainCurrentAmp = 0.0;
    double q2DrainCurrentAmp = 0.0;
    double passiveCurrentAmp = 0.0;
    double sourceKclResidualAmp = 0.0;
    double outputKclResidualAmp = 0.0;
    // The channel-only solve neglects gate diode current. An otherwise
    // mathematically valid KCL root with this flag true is outside its
    // reverse-biased-gate assumption, not a validated DC operating point.
    double q2GateSourceVolt = 0.0; // Q2 gate minus source/output voltage
    bool q2GateForwardBiased = false;
};

// Forward-conduction NJFET Level-1 Shichman-Hodges / ngspice B=1, lambda=0.
// Terminal voltages are ABSOLUTE, with drain current positive drain -> source.
// Reverse Vds, gate conduction and channel-length modulation are unsupported.
inline double njfetDrainCurrent(const J201Parameters& p, double gateVolt,
                               double drainVolt, double sourceVolt) noexcept {
    const double vds = drainVolt - sourceVolt;
    const double vgst = gateVolt - sourceVolt - p.pinchOffVolt;
    if (!(vds > 0.0) || !(vgst > 0.0)) return 0.0;
    const double beta = p.idssAmp / (p.pinchOffVolt * p.pinchOffVolt);
    return vds < vgst ? beta * vds * (2.0 * vgst - vds)
                      : beta * vgst * vgst;
}

namespace detail {
inline bool validPositive(double v) noexcept { return std::isfinite(v) && v > 0.0; }
inline bool validJfet(const J201Parameters& p) noexcept {
    return validPositive(p.idssAmp) && std::isfinite(p.pinchOffVolt) &&
           p.pinchOffVolt < 0.0 && std::isfinite(p.idssAmp / (p.pinchOffVolt * p.pinchOffVolt));
}
}  // namespace detail

// Solves the two coupled DC KCL equations with nested monotonic bisections:
//   Id1(0, Vo, Vs1) = Vs1/R6
//   Id2(Vg2, V9, Vo) = Id1(0, Vo, Vs1) + Vo/(R7+R8+R11)
// Gate currents and all capacitor currents are zero at DC. Q2 drain=V9,
// gate=V9*R10/(R9+R10), source=Vo; Q1 gate=0, drain=Vo, source=Vs1.
// Returns false (leaving result unchanged) for an invalid or unsolvable case.
inline bool solveDc(const Circuit& c, BiasPoint& result) noexcept {
    if (!detail::validPositive(c.supplyVolt) || !detail::validPositive(c.r6Ohm) ||
        !detail::validPositive(c.r7Ohm) || !detail::validPositive(c.r8Ohm) ||
        !detail::validPositive(c.r11Ohm) || !detail::validPositive(c.r9Ohm) ||
        !detail::validPositive(c.r10Ohm) || !detail::validJfet(c.q1) ||
        !detail::validJfet(c.q2)) return false;

    const double loadOhm = c.r7Ohm + c.r8Ohm + c.r11Ohm;
    const double dividerOhm = c.r9Ohm + c.r10Ohm;
    if (!std::isfinite(loadOhm) || !std::isfinite(dividerOhm)) return false;
    const double gate2 = c.supplyVolt * (c.r10Ohm / dividerOhm);
    if (!std::isfinite(gate2)) return false;

    const auto sourceForOutput = [&](double out) noexcept {
        double lower = 0.0, upper = out;
        for (int i = 0; i < 80; ++i) {
            const double mid = lower + 0.5 * (upper - lower);
            const double residual = njfetDrainCurrent(c.q1, 0.0, out, mid) - mid / c.r6Ohm;
            if (residual > 0.0) lower = mid;
            else upper = mid;
        }
        return lower + 0.5 * (upper - lower);
    };
    const auto outputResidual = [&](double out) noexcept {
        const double source = sourceForOutput(out);
        const double q1 = njfetDrainCurrent(c.q1, 0.0, out, source);
        const double q2 = njfetDrainCurrent(c.q2, gate2, c.supplyVolt, out);
        return q2 - q1 - out / loadOhm;
    };
    if (!(outputResidual(0.0) >= 0.0) || !(outputResidual(c.supplyVolt) <= 0.0)) return false;

    double lower = 0.0, upper = c.supplyVolt;
    for (int i = 0; i < 80; ++i) {
        const double mid = lower + 0.5 * (upper - lower);
        if (outputResidual(mid) > 0.0) lower = mid;
        else upper = mid;
    }
    BiasPoint p{};
    p.outputVolt = lower + 0.5 * (upper - lower);
    p.q1SourceVolt = sourceForOutput(p.outputVolt);
    p.q2GateVolt = gate2;
    p.q2GateSourceVolt = gate2 - p.outputVolt;
    p.q2GateForwardBiased = p.q2GateSourceVolt > 0.0;
    p.q1DrainCurrentAmp = njfetDrainCurrent(c.q1, 0.0, p.outputVolt, p.q1SourceVolt);
    p.q2DrainCurrentAmp = njfetDrainCurrent(c.q2, gate2, c.supplyVolt, p.outputVolt);
    p.passiveCurrentAmp = p.outputVolt / loadOhm;
    p.sourceKclResidualAmp = p.q1DrainCurrentAmp - p.q1SourceVolt / c.r6Ohm;
    p.outputKclResidualAmp = p.q2DrainCurrentAmp - p.q1DrainCurrentAmp - p.passiveCurrentAmp;
    if (!std::isfinite(p.sourceKclResidualAmp) || !std::isfinite(p.outputKclResidualAmp)) return false;
    result = p;
    return true;
}

}  // namespace bm::dsp::j201q2
