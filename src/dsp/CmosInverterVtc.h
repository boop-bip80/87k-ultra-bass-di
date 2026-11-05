#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// =============================================================================
// CmosInverterVtc — static voltage-transfer curve of the CD4049UBE clipper section built from the
// two-MOSFET device models of Köper & Holters, "Taming the Red Llama" (DAFx-2020, docs/refs/),
// INCLUDING the R19 supply dropper solved self-consistently.
// =============================================================================
// WHY THIS EXISTS. `Clipper`'s shipped VTC is a fitted asymmetric sigmoid whose output swing is
// ~1.04 V peak-to-peak (satLo + satHi) with a ~35 mV-wide knee. The drawn circuit does not look like
// that: ngspice of the drawn loop (analysis/spice_reference/compare_clipper.py) swings ~8.6 Vpp,
// because R19 only drops the rail while the inverter conducts CROWBAR current — with the output
// high (NMOS off) there is no IR drop and the output reaches the full ~8.65 V rail. The supply
// "sag" is therefore a first-order, signal-dependent effect, not a fixed ~5.64 V rail.
//
// MODEL. For input voltage vin (absolute, 0..vcc) the output vout and the dropped supply vdd satisfy
//        Idn(vin, vout)            = Idp(vdd - vin, vdd - vout)        (KCL at the output)
//        vdd                       = vcc - R19 * Idp                    (supply dropper)
// solved by nested bisection: both residuals are monotone in their unknown, so each root is
// bracketed and unique. The output load current (a few uA through R18) is neglected.
//   Device::Simple   — eq. (8) of the paper (Table 2 alpha/vT) with the p-channel channel-length
//                      modulation lambda = 0.06 V^-1; same equations as ngspice LEVEL=1 with
//                      KP*W/L = alpha, which is how analysis/spice_reference verifies it.
//   Device::Extended — eqs. (9)/(17): alpha and vT linear (n-ch) / cubic+linear (p-ch) functions of
//                      vGS (Table 3). The paper's own best model; it is only measured over
//                      |vGS| = 2.5..6 V, so the region near the trip point is at its edge.
// Both reproduce the self-consistent bias point derived in circuit.md (Vm ~ 2.6 V, VDD ~ 5.6-5.9 V,
// ~2.8-3.0 mA) — the extended model is NOT used to pin the small-signal gain, which the paper does not
// constrain (it comes out ~165 here; the community figure for the real part is 20-30).
//
// OUTPUT convention: the table is y(w) = vout(Vm + w) - Vm, with w = vin - Vm, so y(0) = 0 and the
// existing Clipper plumbing (trip-point-referenced, AC) is unchanged.
// =============================================================================
namespace cmosvtc {

enum class Device : int
{
    Simple = 1,
    Extended = 2,
};

// n-channel drain current (A), magnitudes: vgs, vds >= 0.
inline double nId(Device d, double vgs, double vds) noexcept
{
    if (vds < 0.0)
        vds = 0.0;
    double a, vt;
    if (d == Device::Simple)
    {
        a = 5.1021e-3;
        vt = 1.5702;
    }
    else
    {
        a = 2.0662e-2 - 1.7182e-3 * vgs;
        vt = 1.2083 + 0.31391 * vgs;
    }
    if (vgs <= vt)
        return 0.0;
    const double vov = vgs - vt;
    if (vds <= vov)
        return a * (vov - 0.5 * vds) * vds;
    return 0.5 * a * vov * vov;
}

// p-channel current out of the drain (A), source-referenced magnitudes: vsg = -vGS, vsd = -vDS >= 0.
inline double pId(Device d, double vsg, double vsd) noexcept
{
    if (vsd < 0.0)
        vsd = 0.0;
    constexpr double lambda = 0.06;
    double a, vov;
    if (d == Device::Simple)
    {
        a = 8.2246e-4;
        vov = vsg - 0.48476;
    }
    else
    {
        const double vgs = -vsg; // negative
        a = -1.0801e-5 * vgs * vgs * vgs - 1.6849e-4 * vgs * vgs - 8.6202e-4 * vgs - 3.5774e-4;
        const double vt = 0.27051 * vgs - 0.2561; // negative
        vov = vsg + vt;
    }
    if (vov <= 0.0 || a <= 0.0)
        return 0.0;
    if (vsd <= vov)
        return a * (vov - 0.5 * vsd) * vsd * (1.0 + lambda * vsd);
    return 0.5 * a * vov * vov * (1.0 + lambda * vsd);
}

struct Solution
{
    double vout, vdd, idd;
};

// Self-consistent inverter output for an absolute input voltage.
inline Solution solve(Device d, double vin, double vcc, double r19) noexcept
{
    auto voutFor = [&](double vdd) {
        double lo = 0.0, hi = vdd;
        // f(vo) = Idn - Idp is increasing in vo: f(0) <= 0, f(vdd) >= 0
        for (int i = 0; i < 46; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            const double f = nId(d, vin, mid) - pId(d, vdd - vin, vdd - mid);
            if (f < 0.0)
                lo = mid;
            else
                hi = mid;
        }
        return 0.5 * (lo + hi);
    };
    double lo = 0.2, hi = vcc;
    double vo = 0.0, idd = 0.0;
    for (int i = 0; i < 46; ++i)
    {
        const double vdd = 0.5 * (lo + hi);
        vo = voutFor(vdd);
        idd = pId(d, vdd - vin, vdd - vo);
        // g(vdd) = vdd - (vcc - r19*idd) is increasing in vdd
        if (vdd - (vcc - r19 * idd) < 0.0)
            lo = vdd;
        else
            hi = vdd;
    }
    const double vdd = 0.5 * (lo + hi);
    vo = voutFor(vdd);
    return { vo, vdd, pId(d, vdd - vin, vdd - vo) };
}

// Self-bias (trip) point: the input at which vout == vin (shunt feedback holds the node there).
inline double tripPoint(Device d, double vcc, double r19) noexcept
{
    double lo = 0.5, hi = vcc - 0.5;
    for (int i = 0; i < 50; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        if (solve(d, mid, vcc, r19).vout > mid)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

// Immutable lookup table of y(w) = vout(Vm + w) - Vm over w in [wLo, wHi], C1 cubic Hermite
// interpolation with monotone (Fritsch-Butland) node slopes. The grid is PIECEWISE UNIFORM: fine (0.25 mV)
// across the transition region |w| < 0.3 V and coarse (5 mV) in the saturated tails. The fine zone is
// load-bearing: at the self-bias point the VTC has slope ~-150 and the p-channel leaves saturation
// within ~3 mV of the trip point, so a uniform 3 mV grid smears that kink and under-reads the
// small-signal gain by ~2 dB. Shared (shared_ptr) between the live Clipper and its GRUNT-crossfade
// shadow so the audio-thread copy allocates nothing.
struct Table
{
    double vm = 0.0, vdd0 = 0.0, idd0 = 0.0; // trip point, dropped rail and supply current AT the trip point
    double wLo = 0.0, wHi = 0.0;
    double yMax = 0.0; // max |y| over the table (bounds the solver bracket)
    // three uniform segments: [wLo, b0) step hA ; [b0, b1) step hB ; [b1, wHi] step hC
    double b0 = -0.3, b1 = 0.3, hA = 5e-3, hB = 2.5e-4, hC = 5e-3;
    size_t nA = 0, nB = 0, nC = 0;
    std::vector<double> x, y, m; // knots, values and slopes

    size_t index(double w) const noexcept
    {
        size_t i;
        if (w < b0)
            i = static_cast<size_t>((w - wLo) / hA);
        else if (w < b1)
            i = nA + static_cast<size_t>((w - b0) / hB);
        else
            i = nA + nB + static_cast<size_t>((w - b1) / hC);
        if (i + 1 >= x.size())
            i = x.size() - 2;
        // fix-up against rounding at the segment seams
        while (i > 0 && w < x[i])
            --i;
        while (i + 2 < x.size() && w >= x[i + 1])
            ++i;
        return i;
    }

    double eval(double w) const noexcept
    {
        if (w <= wLo)
            return y.front();
        if (w >= wHi)
            return y.back();
        const size_t i = index(w);
        const double h = x[i + 1] - x[i];
        const double t = (w - x[i]) / h;
        const double t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * y[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * y[i + 1]
               + (t3 - t2) * h * m[i + 1];
    }

    double deriv(double w) const noexcept
    {
        if (w <= wLo || w >= wHi)
            return 0.0;
        const size_t i = index(w);
        const double h = x[i + 1] - x[i];
        const double t = (w - x[i]) / h;
        const double t2 = t * t;
        return ((6 * t2 - 6 * t) * y[i] + (3 * t2 - 4 * t + 1) * h * m[i] + (-6 * t2 + 6 * t) * y[i + 1]
                + (3 * t2 - 2 * t) * h * m[i + 1])
               / h;
    }
};

inline std::shared_ptr<const Table> buildTable(Device d, double vcc, double r19, double wLo, double wHi)
{
    auto t = std::make_shared<Table>();
    t->vm = tripPoint(d, vcc, r19);
    const auto at = solve(d, t->vm, vcc, r19);
    t->vdd0 = at.vdd;
    t->idd0 = at.idd;
    t->wLo = wLo;
    t->wHi = wHi;
    t->nA = static_cast<size_t>(std::llround((t->b0 - wLo) / t->hA));
    t->hA = (t->b0 - wLo) / static_cast<double>(t->nA);
    t->nB = static_cast<size_t>(std::llround((t->b1 - t->b0) / t->hB));
    t->hB = (t->b1 - t->b0) / static_cast<double>(t->nB);
    t->nC = static_cast<size_t>(std::llround((wHi - t->b1) / t->hC));
    t->hC = (wHi - t->b1) / static_cast<double>(t->nC);
    for (size_t i = 0; i < t->nA; ++i)
        t->x.push_back(wLo + static_cast<double>(i) * t->hA);
    for (size_t i = 0; i < t->nB; ++i)
        t->x.push_back(t->b0 + static_cast<double>(i) * t->hB);
    for (size_t i = 0; i <= t->nC; ++i)
        t->x.push_back(t->b1 + static_cast<double>(i) * t->hC);
    const size_t n = t->x.size();
    t->y.resize(n);
    t->m.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double vin = std::min(std::max(t->vm + t->x[i], 0.0), vcc);
        t->y[i] = solve(d, vin, vcc, r19).vout - t->vm;
        t->yMax = std::max(t->yMax, std::abs(t->y[i]));
    }
    // Fritsch-Butland monotone slopes: the data is monotone decreasing, and plain central differences let the
    // Hermite cubic overshoot (slightly POSITIVE slope) at the p-channel's saturation kink. A positive slope
    // would break the solver's uniqueness argument (dF/dw = -gIn + gFb*(vtc' - 1) must stay negative).
    std::vector<double> sec(n - 1);
    for (size_t i = 0; i + 1 < n; ++i)
        sec[i] = (t->y[i + 1] - t->y[i]) / (t->x[i + 1] - t->x[i]);
    t->m[0] = sec[0];
    t->m[n - 1] = sec[n - 2];
    for (size_t i = 1; i + 1 < n; ++i)
    {
        const double s0 = sec[i - 1], s1 = sec[i];
        if (s0 * s1 <= 0.0)
        {
            t->m[i] = 0.0;
            continue;
        }
        const double h0 = t->x[i] - t->x[i - 1], h1 = t->x[i + 1] - t->x[i];
        t->m[i] = 3.0 * (h0 + h1) / ((2.0 * h1 + h0) / s0 + (h1 + 2.0 * h0) / s1);
    }
    return t;
}

} // namespace cmosvtc
