// =============================================================================
// Rail-headroom robustness / extreme-fit regression
// =============================================================================
// The RailClamp is a *phenomenological* smooth-knee model of a generic TL07x
// output, not an authenticated B7K Ultra transistor circuit. These tests
// certify algebraic / numerical properties of that assumption, not measured
// rail voltages or retail-pedal parity. Real op-amp saturation can depend on
// load, supply, frequency, temperature and prior history.
//
// In particular, a fitted rail magnitude SMALLER than the knee half-width
// must not introduce a DC source: f(0) == 0. The old implementation returned
// f(0) ~= +16.07 mV for (-0.2, +2.0) V with a 0.35 V knee. That is a model
// invariant violation, independent of the unknown real-pedal topology.
// =============================================================================

#include "../src/dsp/RailClamp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{

bool closeTo(double actual, double expected, double tolerance = 1.0e-12)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

RailClamp makeClamp(double negRail, double posRail, double knee)
{
    RailClamp result;
    result.setRailVoltages(negRail, posRail);
    result.setKnee(knee);
    result.setEnabled(true);
    return result;
}

// Independently specified smooth knee with a physically meaningful domain:
// the knee half-width cannot extend past signal ground on either rail.
double reference(double x, double negativeLimit, double positiveLimit, double knee)
{
    const double neg = std::abs(negativeLimit);
    const double pos = std::abs(positiveLimit);
    const double h = std::max(0.0, std::min({ knee, neg, pos }));
    if (h == 0.0)
        return std::max(-neg, std::min(x, pos));
    if (x > pos - h)
    {
        if (x >= pos + h)
            return pos;
        const double d = x - pos - h;
        return pos - d * d / (4.0 * h);
    }
    if (x < -(neg - h))
    {
        if (x <= -(neg + h))
            return -neg;
        const double d = x + neg + h;
        return -neg + d * d / (4.0 * h);
    }
    return x;
}

bool testZeroInputNoBias()
{
    const std::array<double, 9> limits { 0.0, 0.02, 0.1, 0.2, 0.3, 0.35, 0.5, 2.9, 5.0 };
    const std::array<double, 6> knees { 0.0, 0.01, 0.15, 0.35, 0.7, 10.0 };

    for (double neg : limits)
        for (double pos : limits)
            for (double h : knees)
            {
                const RailClamp c = makeClamp(neg, pos, h);
                if (c.process(0.0) != 0.0)
                {
                    std::printf("FAIL zero-bias: [-%g,+%g] h=%g f(0)=%+.12g\n",
                                neg, pos, h, c.process(0.0));
                    return false;
                }
            }
    return true;
}

bool testSaturationAndContinuity()
{
    const std::array<double, 7> limits { 0.0, 0.02, 0.2, 0.35, 1.0, 2.9, 5.0 };
    const std::array<double, 4> knees { 0.0, 0.1, 0.35, 10.0 };
    for (double neg : limits)
        for (double pos : limits)
            for (double h : knees)
            {
                const RailClamp c = makeClamp(neg, pos, h);
                double prev = -std::numeric_limits<double>::infinity();
                for (int i = -2000; i <= 2000; ++i)
                {
                    const double x = static_cast<double>(i) * 0.01;
                    const double y = c.process(x);
                    const double ref = reference(x, neg, pos, h);
                    if (! std::isfinite(y) || y < -neg - 1.0e-12 || y > pos + 1.0e-12
                        || y < prev - 1.0e-12 || ! closeTo(y, ref))
                    {
                        std::printf("FAIL clamp sweep: [-%g,+%g] h=%g x=%g y=%g ref=%g prev=%g\n",
                                    neg, pos, h, x, y, ref, prev);
                        return false;
                    }
                    prev = y;
                }
                if (c.process(-1.0e300) != -neg || c.process(1.0e300) != pos)
                {
                    std::printf("FAIL rail endpoints: [-%g,+%g] h=%g\n", neg, pos, h);
                    return false;
                }
            }
    return true;
}

bool testSetterOrderingAndNominalOracle()
{
    RailClamp a;
    a.setKnee(0.35);
    a.setRailVoltages(0.2, 2.0);
    a.setEnabled(true);

    RailClamp b;
    b.setRailVoltages(0.2, 2.0);
    b.setKnee(0.35);
    b.setEnabled(true);

    for (int i = -1000; i <= 1000; ++i)
    {
        const double x = i * 0.01;
        if (a.process(x) != b.process(x))
        {
            std::printf("FAIL setter order x=%g a=%g b=%g\n", x, a.process(x), b.process(x));
            return false;
        }
    }

    // Restoring larger rails after a narrow-rail fit should recover the
    // originally configured knee, rather than permanently shrinking it.
    a.setRailVoltages(2.9, 2.7);
    const RailClamp nominal = makeClamp(2.9, 2.7, 0.35);
    for (int i = -1000; i <= 1000; ++i)
    {
        const double x = i * 0.01;
        if (a.process(x) != nominal.process(x)
            || ! closeTo(a.process(x), reference(x, 2.9, 2.7, 0.35)))
        {
            std::printf("FAIL nominal or restored rail x=%g actual=%g expected=%g\n",
                        x, a.process(x), nominal.process(x));
            return false;
        }
    }
    return true;
}

bool testInvalidConfigurationFailsClosed()
{
    RailClamp a = makeClamp(2.9, 2.7, -0.5);
    RailClamp b = makeClamp(2.9, 2.7, std::numeric_limits<double>::quiet_NaN());
    RailClamp c = makeClamp(std::numeric_limits<double>::infinity(), 2.7, 0.35);
    for (double x : { -100.0, -3.0, -1.0, 0.0, 1.0, 3.0, 100.0 })
    {
        if (! closeTo(a.process(x), std::max(-2.9, std::min(x, 2.7)))
            || ! closeTo(b.process(x), std::max(-2.9, std::min(x, 2.7)))
            || ! std::isfinite(c.process(x)))
        {
            std::printf("FAIL malformed rail/knee config x=%g a=%g b=%g c=%g\n",
                        x, a.process(x), b.process(x), c.process(x));
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    int failures = 0;
    for (auto test : { testZeroInputNoBias, testSaturationAndContinuity,
                       testSetterOrderingAndNominalOracle, testInvalidConfigurationFailsClosed })
        if (! test())
            ++failures;

    std::printf("RailHeadroomExtendedTest: %s (%d failures)\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
