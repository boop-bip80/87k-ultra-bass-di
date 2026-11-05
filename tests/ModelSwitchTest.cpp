// ModelSwitchTest — the plugin's runtime MODEL switch (calibrated <-> drawn schematic).
// processBlock() calls PedalDSP::setFitParams(FitParams::schematic() / FitParams{}) on the AUDIO thread, so:
//   1. after the physical-CD4049 tables are primed (prepare time), the switch must perform ZERO heap
//      allocations — measured here with a counting global operator new, not assumed;
//   2. it must be fully REVERSIBLE: shipped -> schematic -> shipped (+ reset) must reproduce a freshly
//      constructed shipped chain sample for sample (no configuration leaks across the switch);
//   3. both configurations must stay finite across a hard stimulus, and the two must actually differ
//      (otherwise the switch is a no-op and the UI control is a lie).
#include "../src/dsp/FitParams.h"
#include "../src/dsp/PedalChain.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static std::atomic<long> g_allocs { 0 };
static std::atomic<bool> g_count { false };

void* operator new(std::size_t n)
{
    if (g_count.load(std::memory_order_relaxed))
        g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok)
    {
        std::printf("  FAIL: %s\n", what);
        ++failures;
    }
}

// renders into a caller-owned buffer so the allocation counter sees only the code under test
static void render(PedalChain& c, std::vector<double>& y)
{
    const int n = (int) y.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / 48000.0;
        const double x = 0.25 * std::sin(2.0 * M_PI * 110.0 * t) + 0.08 * std::sin(2.0 * M_PI * 1830.0 * t);
        y[(size_t) i] = c.processSample(x);
    }
}

int main()
{
    const double fs = 48000.0;
    PedalChain::Params p;
    p.blend = 0.8;
    p.drive = 0.6;
    p.level = 0.7;
    p.master = 1.0;
    p.gruntIdx = 2;

    PedalChain a;
    a.prepare(fs, fs);
    a.primePhysicalTables(); // prepare-time, allocating
    a.setFitParams(FitParams {});
    a.applyParams(p);
    a.reset();

    // ---- 1. allocation-free switching -------------------------------------------------------------
    std::vector<double> ySch(4800), yShip(4800); // allocated BEFORE counting starts
    g_allocs = 0;
    g_count = true;
    a.setFitParams(FitParams::schematic());
    a.reset();
    render(a, ySch);
    a.setFitParams(FitParams {});
    a.reset();
    render(a, yShip);
    g_count = false;
    std::printf("  [info] heap allocations during switch + 2x4800 samples: %ld\n", g_allocs.load());
    check(g_allocs.load() == 0, "runtime model switch performs zero heap allocations");

    // ---- 3. finite and genuinely different --------------------------------------------------------
    double rmsS = 0.0, rmsH = 0.0, rmsD = 0.0;
    bool finite = true;
    for (size_t i = 0; i < ySch.size(); ++i)
    {
        finite = finite && std::isfinite(ySch[i]) && std::isfinite(yShip[i]);
        rmsS += ySch[i] * ySch[i];
        rmsH += yShip[i] * yShip[i];
        rmsD += (ySch[i] - yShip[i]) * (ySch[i] - yShip[i]);
    }
    check(finite, "both configurations produce finite output");
    check(std::sqrt(rmsD / rmsH) > 0.05, "schematic and calibrated configurations actually differ");
    std::printf("  [info] output rms schematic %.3f V, calibrated %.3f V, relative difference %.2f\n",
                std::sqrt(rmsS / (double) ySch.size()), std::sqrt(rmsH / (double) yShip.size()),
                std::sqrt(rmsD / rmsH));

    // ---- 2. reversibility: shipped -> schematic -> shipped == fresh shipped ------------------------
    PedalChain b;
    b.prepare(fs, fs);
    b.setFitParams(FitParams {});
    b.applyParams(p);
    b.reset();
    a.reset();
    std::vector<double> ya(9600), yb(9600);
    render(a, ya);
    render(b, yb);
    double worst = 0.0;
    for (size_t i = 0; i < ya.size(); ++i)
        worst = std::max(worst, std::abs(ya[i] - yb[i]));
    std::printf("  [info] worst |switched-back - fresh| = %.3e V\n", worst);
    check(worst < 1e-9, "shipped -> schematic -> shipped reproduces a fresh shipped chain");

    std::printf("%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
