#include "src/dsp/J201Q2ActiveLoad.h"

#include <cassert>
#include <initializer_list>
#include <cmath>
#include <cstdio>

using namespace bm::dsp::j201q2;

static bool close(double a, double b, double tolerance = 1e-12) {
    return std::abs(a-b) <= tolerance;
}

static void checkBias(const Circuit& c) {
    BiasPoint b{};
    assert(solveDc(c, b));
    assert(b.q1SourceVolt >= 0.0 && b.q1SourceVolt <= b.outputVolt);
    assert(b.outputVolt > 0.0 && b.outputVolt < c.supplyVolt);
    assert(b.q2GateVolt > 0.0 && b.q2GateVolt < c.supplyVolt);
    assert(b.q1DrainCurrentAmp >= 0.0 && b.q2DrainCurrentAmp >= 0.0);
    assert(b.passiveCurrentAmp > 0.0);
    assert(std::abs(b.sourceKclResidualAmp) < 1e-11);
    assert(std::abs(b.outputKclResidualAmp) < 1e-11);
    BiasPoint repeat{};
    assert(solveDc(c, repeat));
    assert(close(b.q1SourceVolt, repeat.q1SourceVolt));
    assert(close(b.outputVolt, repeat.outputVolt));
}

int main() {
    J201Parameters p{};
    const double beta = p.idssAmp / (p.pinchOffVolt*p.pinchOffVolt);
    assert(njfetDrainCurrent(p, -1.0, 9.0, 0.0) == 0.0);  // cutoff
    assert(njfetDrainCurrent(p, 0.0, 0.0, 0.0) == 0.0);
    assert(close(njfetDrainCurrent(p, 0.0, 0.1, 0.0), beta*0.1*(2*0.718-0.1)));
    assert(close(njfetDrainCurrent(p, 0.0, 9.0, 0.0), p.idssAmp));
    assert(close(njfetDrainCurrent(p, 0.0, 0.718, 0.0),
                 njfetDrainCurrent(p, 0.0, 0.718+1e-9, 0.0), 1e-12));

    Circuit c{};
    checkBias(c);
    BiasPoint nominal{};
    assert(solveDc(c, nominal));
    assert(close(nominal.q2GateVolt, 4.5));
    std::printf("Nominal: Q1 source %.9f V, output %.9f V, Q2 gate %.3f V, Q2 Id %.9f A\n",
                nominal.q1SourceVolt, nominal.outputVolt, nominal.q2GateVolt,
                nominal.q2DrainCurrentAmp);

    // Datasheet envelope exercised on BOTH interacting devices, not just Q2.
    for (double idss1 : {0.0002, 0.0005, 0.001}) {
        for (double vto1 : {-0.3, -0.718, -1.5}) {
            for (double idss2 : {0.0002, 0.0005, 0.001}) {
                for (double vto2 : {-0.3, -0.718, -1.5}) {
                    c.q1 = {idss1, vto1};
                    c.q2 = {idss2, vto2};
                    checkBias(c);
                }
            }
        }
    }
    c = Circuit{};
    c.r9Ohm = 2000000.0;
    checkBias(c);
    BiasPoint shifted{};
    assert(solveDc(c, shifted));
    assert(close(shifted.q2GateVolt, 3.0));

    c.r6Ohm = -1.0;
    BiasPoint unchanged{123.0, 456.0};
    assert(!solveDc(c, unchanged));
    assert(unchanged.q1SourceVolt == 123.0 && unchanged.outputVolt == 456.0);
    c = Circuit{};
    c.q2.pinchOffVolt = 0.0;
    assert(!solveDc(c, unchanged));
    std::puts("J201Q2ActiveLoad tests: PASS (81 device-pair corners plus nominal)");
}
