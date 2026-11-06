// Standalone C++17 research-physics CPU and real-time allocation probe.
// Build without JUCE: c++ -std=c++17 -O3 -I<chowdsp_wdf/single_include>
//                        tests/ResearchPhysicsPerf.cpp -o ResearchPhysicsPerf
//
// Only finite output and zero operator-new allocations are acceptance gates.
// ns/sample measurements vary with CPU/compiler and make NO hardware-parity claim.
// The opt-in JFET/treble coupled port is measured separately because the
// full PedalChain does not currently route through that experimental API.
#include "../src/dsp/JfetStage.h"
#include "../src/dsp/PedalChain.h"
#include "../src/dsp/TrebleAttack.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
std::atomic<unsigned long long> newCalls { 0 };
std::atomic<bool> counting { false };
constexpr int frames = 32768;
constexpr int stimulusSize = 4096;
constexpr std::array<double, 3> rates { 8000.0, 48000.0, 384000.0 };
std::array<double, stimulusSize> stimulus {};
volatile double sink = 0.0;
int failures = 0;

void require(bool pass, const char* description, double rate)
{
    if (!pass)
    {
        std::fprintf(stderr, "FAIL %.0f Hz: %s\n", rate, description);
        ++failures;
    }
}

bool sameBits(double a, double b) noexcept
{
    return std::memcmp(&a, &b, sizeof(double)) == 0;
}

void beginCount() noexcept
{
    newCalls.store(0, std::memory_order_relaxed);
    counting.store(true, std::memory_order_relaxed);
}

unsigned long long endCount() noexcept
{
    counting.store(false, std::memory_order_relaxed);
    return newCalls.load(std::memory_order_relaxed);
}

struct Run
{
    double nsPerSample = 0.0;
    double checksum = 0.0;
    unsigned long long allocations = 0;
    bool finite = true;
};

enum class PairMode { Legacy, Physical, Coupled, Switching };

Run runPair(double fs, PairMode mode)
{
    JfetStage jfet;
    TrebleAttack treble;
    jfet.prepare(fs);
    treble.prepare(fs);
    if (mode != PairMode::Legacy)
    {
        jfet.setPhysicalSourceFeedbackModel(true);
        treble.setCoupledJfetDrainPort(mode != PairMode::Physical);
        treble.setActiveLoad(1600.0, 17.6);
        treble.setQ2GateCapEnabled(true);
    }

    Run result;
    beginCount();
    const auto started = std::chrono::steady_clock::now();
    for (int n = 0; n < frames; ++n)
    {
        if (mode == PairMode::Switching && n % 512 == 0)
        {
            const bool on = (n / 512) % 2 == 0;
            jfet.setPhysicalSourceFeedbackModel(on);
            treble.setCoupledJfetDrainPort(on);
            treble.setQ2GateCapEnabled(on);
            treble.setAttack(static_cast<TrebleAttack::Attack>((n / 512) % 3));
        }
        const double x = stimulus[static_cast<size_t>(n % stimulusSize)];
        const auto port = treble.previewCoupledDrainPort();
        const double current = mode == PairMode::Legacy
            ? jfet.process(x)
            : jfet.processCoupled(x, port.openCircuitVolts, port.theveninOhms);
        const double output = treble.process(current);
        result.finite &= std::isfinite(current) && std::isfinite(output)
                      && std::isfinite(port.openCircuitVolts)
                      && std::isfinite(port.theveninOhms);
        result.checksum += output * 1.0e-5 + current * 1.0e-2;
    }
    const auto ended = std::chrono::steady_clock::now();
    result.allocations = endCount();
    result.nsPerSample = std::chrono::duration<double, std::nano>(ended - started).count()
                       / static_cast<double>(frames);
    return result;
}

PedalChain::ResearchModes researchOn()
{
    PedalChain::ResearchModes m;
    m.clipperDiodeCurrents = true;
    m.jfetFiniteDrainConductance = true;
    m.jfetNonlinearSourceFeedback = true;
    m.baxandallRailFeedback = true;
    m.midBandRailFeedback = true;
    return m;
}

enum class ChainMode { Legacy, Research, Switching };

Run runChain(double fs, ChainMode mode)
{
    PedalChain chain;
    chain.prepare(fs, fs);  // no JUCE / oversampling wrapper in this probe
    chain.primePhysicalTables(); // permitted preparation-time table allocations
    if (mode == ChainMode::Research)
        chain.setResearchModes(researchOn());
    PedalChain::Params p;
    p.blend = 0.8;
    p.drive = 0.7;
    p.level = 0.7;
    chain.applyParams(p);
    chain.reset();

    Run result;
    const auto allOn = researchOn();
    const PedalChain::ResearchModes allOff;
    beginCount();
    const auto started = std::chrono::steady_clock::now();
    for (int n = 0; n < frames; ++n)
    {
        if (mode == ChainMode::Switching && n % 512 == 0)
        {
            const bool on = (n / 512) % 2 == 0;
            chain.setResearchModes(on ? allOn : allOff);
            p.attackIdx = (n / 512) % 3;
            p.gruntIdx = (n / 1024) % 3;
            p.loMidFreq = (n / 512) % 3;
            p.hiMidFreq = (n / 1024) % 3;
            p.lo = on ? 0.2 : 0.8;
            p.hi = on ? 0.8 : 0.2;
            chain.applyParams(p);
        }
        const double output = chain.processSample(
            stimulus[static_cast<size_t>(n % stimulusSize)]);
        result.finite &= std::isfinite(output);
        result.checksum += output * 1.0e-5;
    }
    const auto ended = std::chrono::steady_clock::now();
    result.allocations = endCount();
    result.nsPerSample = std::chrono::duration<double, std::nano>(ended - started).count()
                       / static_cast<double>(frames);
    return result;
}

// The disabled coupled API must be bit-for-bit indistinguishable from process().
void checkLegacyFallback(double fs)
{
    JfetStage ordinary, coupledApi;
    TrebleAttack ordinaryNet, coupledNet;
    ordinary.prepare(fs);
    coupledApi.prepare(fs);
    ordinaryNet.prepare(fs);
    coupledNet.prepare(fs);
    bool exact = true;
    beginCount();
    for (int n = 0; n < stimulusSize; ++n)
    {
        if (n % 257 == 0)
        {
            const auto a = static_cast<TrebleAttack::Attack>((n / 257) % 3);
            ordinaryNet.setAttack(a);
            coupledNet.setAttack(a);
        }
        const double x = stimulus[static_cast<size_t>(n)];
        const auto port = coupledNet.previewCoupledDrainPort(); // disabled: {0,0}
        const double i0 = ordinary.process(x);
        const double i1 = coupledApi.processCoupled(x, port.openCircuitVolts,
                                                       port.theveninOhms);
        exact &= sameBits(i0, i1) && port.openCircuitVolts == 0.0
              && port.theveninOhms == 0.0;
        exact &= sameBits(ordinaryNet.process(i0), coupledNet.process(i1));
    }
    const auto allocations = endCount();
    require(exact, "disabled port changes legacy sample bits", fs);
    require(allocations == 0, "disabled port/fallback allocated", fs);
}

template <typename Runner, typename Mode>
void benchmark(double fs, const char* label, Mode mode, Runner&& run)
{
    std::array<double, 3> times {};
    double checksum = 0.0;
    for (int rep = 0; rep < 3; ++rep)
    {
        const Run result = run(fs, mode);
        times[static_cast<size_t>(rep)] = result.nsPerSample;
        checksum += result.checksum;
        require(result.finite, "nonfinite sample/port", fs);
        require(result.allocations == 0, "operator new on processing/control path", fs);
    }
    std::sort(times.begin(), times.end());
    sink = sink + checksum;
    std::printf("  %7.0f Hz | %-18s | %9.1f ns/sample | checksum %.7g\n",
                fs, label, times[1], checksum);
}
} // namespace

void* operator new(std::size_t bytes)
{
    if (counting.load(std::memory_order_relaxed))
        newCalls.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes)
{
    if (counting.load(std::memory_order_relaxed))
        newCalls.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
// GCC's inliner can incorrectly diagnose a mismatched allocation here even
// though these are the paired global new/delete replacements used only to
// instrument allocations in this test executable. Keep the delete wrappers
// out-of-line rather than weakening project-wide warning checks.
#if defined(__GNUC__) || defined(__clang__)
#define RESEARCH_NOINLINE __attribute__((noinline))
#else
#define RESEARCH_NOINLINE
#endif
RESEARCH_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
RESEARCH_NOINLINE void operator delete[](void* p) noexcept { std::free(p); }
RESEARCH_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
RESEARCH_NOINLINE void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#undef RESEARCH_NOINLINE

int main()
{
    for (int i = 0; i < stimulusSize; ++i)
        stimulus[static_cast<size_t>(i)] = 1.2 * std::sin(i * 0.021)
            + 0.55 * std::sin(i * 0.237)
            + ((i % 511 == 0) ? 2.0 : 0.0);

    std::puts("ResearchPhysicsPerf (median of 3 x 32768 samples; not a CPU gate)");
    for (double fs : rates)
    {
        checkLegacyFallback(fs);
        benchmark(fs, "pair legacy", PairMode::Legacy, runPair);
        benchmark(fs, "pair physical", PairMode::Physical, runPair);
        benchmark(fs, "pair coupled", PairMode::Coupled, runPair);
        benchmark(fs, "pair toggled", PairMode::Switching, runPair);
        benchmark(fs, "chain legacy", ChainMode::Legacy, runChain);
        benchmark(fs, "chain research", ChainMode::Research, runChain);
        benchmark(fs, "chain toggled", ChainMode::Switching, runChain);
    }
    std::printf("ResearchPhysicsPerf: %s (%d failures; allocation and finiteness gates only)\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
