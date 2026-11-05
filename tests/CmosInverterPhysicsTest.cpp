// CmosInverterPhysicsTest: numerical invariants of the *assumed* CD4049UB model.
//
// This is deliberately NOT a retail B7K Ultra fidelity test. The MOSFET
// equations are taken from a published generic CD4049UB model; they have not
// been identified or measured on the target pedal. What we can and must test
// is that our nonlinear solution respects its own KCL, supply-drop equation,
// DC self-bias point, and monotone table interpolation.
#include "../src/dsp/CmosInverterVtc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

constexpr double kVcc = 8.65;
constexpr double kR19 = 1.0e3;
int failures = 0;

void check(bool ok, const char* name, cmosvtc::Device device, double got, double limit)
{
    if (!ok)
    {
        std::printf("  FAIL: %s device=%d got=%.9g limit=%.9g\n", name,
                    static_cast<int>(device), got, limit);
        ++failures;
    }
}

void testDevice(cmosvtc::Device device)
{
    double worstKcl = 0.0;
    double worstSupply = 0.0;
    double lastVout = std::numeric_limits<double>::infinity();
    double worstRise = 0.0;

    // A wide sweep is numerical coverage, not a declaration that the
    // experimental transistor equations are valid over the whole range.
    for (int i = 0; i <= 346; ++i)
    {
        const double vin = kVcc * static_cast<double>(i) / 346.0;
        const auto s = cmosvtc::solve(device, vin, kVcc, kR19);
        const double inCurrent = cmosvtc::nId(device, vin, s.vout);
        const double outCurrent = cmosvtc::pId(device, s.vdd - vin, s.vdd - s.vout);
        worstKcl = std::max(worstKcl, std::abs(inCurrent - outCurrent));
        worstSupply = std::max(worstSupply, std::abs(s.vdd - (kVcc - kR19 * s.idd)));
        worstRise = std::max(worstRise, s.vout - lastVout);

        check(std::isfinite(s.vout) && std::isfinite(s.vdd) && std::isfinite(s.idd)
                  && s.vout >= -1e-10 && s.vout <= s.vdd + 1e-10
                  && s.vdd >= -1e-10 && s.vdd <= kVcc + 1e-10 && s.idd >= -1e-12,
              "finite and inside supply rails", device, s.vout, kVcc);
        lastVout = s.vout;
    }
    // Absolute-ampere error tolerance: sub-microamp KCL on a mA device.
    check(worstKcl < 1e-8, "inverter output-node KCL", device, worstKcl, 1e-8);
    check(worstSupply < 1e-8, "R19 supply drop", device, worstSupply, 1e-8);
    check(worstRise < 1e-6, "inverter transfer is non-increasing", device, worstRise, 1e-6);

    const double vm = cmosvtc::tripPoint(device, kVcc, kR19);
    const auto atTrip = cmosvtc::solve(device, vm, kVcc, kR19);
    const double biasError = std::abs(atTrip.vout - vm);
    check(vm > 0.0 && vm < kVcc && biasError < 1e-8,
          "self-biased shunt-feedback operating point", device, biasError, 1e-8);

    // The piecewise-C1 interpolation is used in real time by the clipper
    // Newton solver, which assumes a monotone VTC and hence a unique root.
    // Check this property especially at the two grid seams (-0.3/+0.3 V).
    const auto table = cmosvtc::buildTable(device, kVcc, kR19, -3.0, 6.0);
    const double tableAtZero = std::abs(table->eval(0.0));
    check(tableAtZero < 1e-6, "table is zero at physical trip point", device,
          tableAtZero, 1e-6);
    double worstTableRise = 0.0;
    double maxInterpolationError = 0.0;
    double last = table->eval(table->wLo);
    for (int i = 1; i <= 3000; ++i)
    {
        const double w = table->wLo + (table->wHi - table->wLo) * i / 3000.0;
        const double value = table->eval(w);
        worstTableRise = std::max(worstTableRise, value - last);
        // Compare a *sample* of interpolation points to the direct model,
        // not every point: the real-time table is intentionally approximate.
        if (i % 75 == 0)
        {
            const double absVin = std::clamp(table->vm + w, 0.0, kVcc);
            const double direct = cmosvtc::solve(device, absVin, kVcc, kR19).vout - table->vm;
            maxInterpolationError = std::max(maxInterpolationError, std::abs(value - direct));
        }
        last = value;
    }
    // Dense checks through the transition region: a coarse-grid comparison
    // would look perfect in the saturated tails while missing the actual knee.
    for (int i = 0; i <= 600; ++i)
    {
        // Offset endpoints intentionally: an exact multiple of the 0.25 mV
        // table step would only hit knots and vacuously "verify" interpolation.
        const double w = -0.14991 + 0.29982 * i / 600.0;
        const double absVin = std::clamp(table->vm + w, 0.0, kVcc);
        const double direct = cmosvtc::solve(device, absVin, kVcc, kR19).vout - table->vm;
        maxInterpolationError = std::max(maxInterpolationError, std::abs(table->eval(w) - direct));
    }
    check(worstTableRise <= 1e-8, "Hermite table is monotone", device, worstTableRise, 1e-8);
    check(maxInterpolationError < 2e-3, "table vs direct nonlinear solver incl knee", device,
          maxInterpolationError, 2e-3);

    std::printf("  device=%d Vm=%.5f V Vdd=%.5f V Idd=%.4f mA |KCL|<=%.2e A "
                "|R19|<=%.2e V max table error %.5g V\n",
                static_cast<int>(device), vm, atTrip.vdd, atTrip.idd * 1000.0,
                worstKcl, worstSupply, maxInterpolationError);
}

} // namespace

int main()
{
    testDevice(cmosvtc::Device::Simple);
    testDevice(cmosvtc::Device::Extended);
    if (failures)
        std::printf("CmosInverterPhysicsTest: FAIL (%d invariants)\n", failures);
    else
        std::printf("CmosInverterPhysicsTest: PASS (generic-device numerical invariants; no hardware parity claim)\n");
    return failures ? 1 : 0;
}
