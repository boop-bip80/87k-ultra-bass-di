#include "../src/dsp/JfetDevicePhysics.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace ultrabass::dsp::jfet_physics;

static void near(double actual, double expected, double tolerance = 1e-13) {
    assert(std::isfinite(actual));
    assert(std::abs(actual - expected) <= tolerance);
}

int main() {
    Level1NjfetParameters p;
    p.betaAmpsPerVoltSquared = 0.002;
    p.thresholdVolts = -1.0;
    p.channelModulationPerVolt = 0.1;
    p.gminSiemens = 1e-9;

    // Expected values computed directly from NJF LEVEL-1 branch formulas.
    near(channelDrainCurrentAmps(p, 0, 0.5, 0),
         0.002 * 1.05 * (0.5 * (2.0 - 0.5)) + 0.5e-9);
    near(channelDrainCurrentAmps(p, 0, 2, 0),
         0.002 * 1.2 + 2e-9);
    near(channelDrainCurrentAmps(p, -2, 2, 0), 2e-9);

    // Source/drain orientation reverses physical current; no reversed triode
    // polynomial is ever evaluated. Check the branch joins and Vds=0.
    near(channelDrainCurrentAmps(p, 0, 0, 2),
         -channelDrainCurrentAmps(p, 0, 2, 0));
    near(channelDrainCurrentAmps(p, 0, 0, 0), 0);
    near(channelDrainCurrentAmps(p, 0, 1, 0),
         channelDrainCurrentAmps(p, 0, 1 + 1e-9, 0), 1e-11);
    near(channelDrainCurrentAmps(p, 0, 1, 0),
         channelDrainCurrentAmps(p, 0, 1 - 1e-9, 0), 1e-11);
    near(channelDrainCurrentAmps(p, -0.3, 1e-10, 0),
         -channelDrainCurrentAmps(p, -0.3, 0, 1e-10), 1e-15);

    // Junction current has a bounded reverse asymptote and positive forward
    // current. Gate voltage at a terminal gives exactly zero DC current.
    near(gateJunctionCurrentAmps(p, 0, 0), 0);
    assert(gateJunctionCurrentAmps(p, 0.2, 0) > 0);
    assert(gateJunctionCurrentAmps(p, -0.2, 0) < 0);
    assert(gateJunctionCurrentAmps(p, -1, 0) >= -p.gateSaturationCurrentAmps);

    // Independent resistor KCL: I_Rd=(Vdd-D)/Rd; I_Rs=S/Rs;
    // I_channel=f(Vg,D,S). All must agree and remain on the 0..Vdd rails.
    for (double vdd : {3.0, 6.0, 9.0}) {
        double previousCurrent = -1.0;
        for (double vg : {-3.0, -2.0, -1.0, -0.25, 0.0}) {
            auto result = solveNineVoltResistorLoad(p, vg, vdd, 10000.0, 3300.0);
            assert(result.valid);
            assert(result.iterations == 100);
            assert(result.drainVolts >= 0 && result.drainVolts <= vdd);
            assert(result.sourceVolts >= 0 && result.sourceVolts <= vdd);
            near((vdd - result.drainVolts) / 10000.0, result.channelAmps, 1e-16);
            near(result.sourceVolts / 3300.0, result.channelAmps, 1e-16);
            near(channelDrainCurrentAmps(p, vg, result.drainVolts,
                                         result.sourceVolts), result.channelAmps, 1e-13);
            assert(std::abs(result.kclResidualAmps) < 1e-13);
            assert(result.channelAmps >= previousCurrent);
            previousCurrent = result.channelAmps;
        }
    }

    assert(!solveNineVoltResistorLoad(p, 0, 9.01, 10000, 3300).valid);
    assert(!solveNineVoltResistorLoad(p, 0.1, 9, 10000, 3300).valid);
    assert(!solveNineVoltResistorLoad(p, 0, 9, 0, 3300).valid);
    auto invalid = p;
    invalid.betaAmpsPerVoltSquared = std::numeric_limits<double>::quiet_NaN();
    assert(!solveNineVoltResistorLoad(invalid, 0, 9, 10000, 3300).valid);
    assert(std::isnan(channelDrainCurrentAmps(invalid, 0, 1, 0)));

    std::cout << "JFET LEVEL-1 primitive, polarity, diode, 9V KCL: PASS\n";
}
