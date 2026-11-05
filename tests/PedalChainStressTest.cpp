// PedalChainStressTest — integration robustness, NOT a physical-pedal parity test.
//
// Independent of the historical capture-fit numbers: these checks guard the
// processing contract across host buffer boundaries, selector permutations,
// sample rates and pathological inputs. In particular, the split API and the
// fused convenience API must be mathematically the SAME 1x signal path.
//
// This console test intentionally cannot exercise the plugin's host-level
// bypass, FIR oversampler or latency-compensating clean tap: those live in the
// JUCE-aware PedalDSP/PluginProcessor layer, not in PedalChain.
#include "../src/dsp/PedalChain.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

#if defined(_MSC_VER)
#define B7K_STRESS_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define B7K_STRESS_NOINLINE __attribute__((noinline))
#else
#define B7K_STRESS_NOINLINE
#endif

namespace
{
constexpr double kPi = 3.141592653589793238462643383279502884;
std::atomic<bool> countAllocations {false};
std::atomic<std::size_t> allocations {0};
int failures = 0;
int assertions = 0;

void check(bool ok, const char* message)
{
    ++assertions;
    if (!ok)
    {
        ++failures;
        std::printf("  FAIL: %s\n", message);
    }
}

double stimulus(std::size_t index, double fs)
{
    // A deterministic bass fundamental, high partial and sparse transients.
    // Keep it well below the physical rails; out-of-range inputs are covered
    // separately and must not affect ordinary-signal reference results.
    const double t = static_cast<double>(index) / fs;
    const double note = 0.28 * std::sin(2.0 * kPi * 73.42 * t)
                      + 0.09 * std::sin(2.0 * kPi * 881.0 * t)
                      + 0.04 * std::sin(2.0 * kPi * 3117.0 * t);
    const double transient = ((index % 1009) == 3) ? 0.6 : 0.0;
    return note + transient;
}

PedalChain::Params parameters(int profile)
{
    PedalChain::Params p;
    if (profile == 0)
    {
        p.drive = 0.16;
        p.blend = 0.13;
        p.master = 0.78;
        p.level = 0.37;
    }
    else if (profile == 1)
    {
        p.drive = 0.91;
        p.blend = 0.96;
        p.master = 1.0;
        p.level = 0.85;
        p.lo = 0.0;
        p.hi = 1.0;
        p.loMid = 0.05;
        p.hiMid = 0.95;
    }
    else
    {
        p.drive = 0.54;
        p.blend = 0.57;
        p.master = 0.9;
        p.level = 0.62;
        p.lo = 0.93;
        p.hi = 0.04;
        p.loMid = 1.0;
        p.hiMid = 0.0;
        p.distEngage = false;
    }
    p.attackIdx = profile % 3;
    p.gruntIdx = (profile + 1) % 3;
    p.loMidFreq = (profile + 2) % 3;
    p.hiMidFreq = profile % 3;
    return p;
}

void initialize(PedalChain& c, double fs, const PedalChain::Params& p,
                bool schematic = false)
{
    c.prepare(fs, fs);
    if (schematic)
    {
        c.primePhysicalTables();
        c.setFitParams(FitParams::schematic());
    }
    else
    {
        c.setFitParams(FitParams {});
    }
    c.applyParams(p);
    c.reset();
}

void testSplitAndChunkParity()
{
    constexpr std::size_t kSamples = 4096;
    constexpr int kChunks[] = {1, 13, 64, 257};
    double largestDifference = 0.0;
    // Pure chain-domain 1x equivalence, including odd, non-power-of-two host
    // buffers and repeated (unchanged) control values from the host.
    for (const double fs : {44100.0, 48000.0, 96000.0})
        for (const bool schematic : {false, true})
            for (int profile = 0; profile < 3; ++profile)
            {
                const auto p = parameters(profile);
                PedalChain fused;
                initialize(fused, fs, p, schematic);
                std::vector<double> reference(kSamples);
                for (std::size_t i = 0; i < kSamples; ++i)
                    reference[i] = fused.processSample(stimulus(i, fs));

                for (const int chunk : kChunks)
                {
                    PedalChain split;
                    initialize(split, fs, p, schematic);
                    for (std::size_t i = 0; i < kSamples; ++i)
                    {
                        if (i % static_cast<std::size_t>(chunk) == 0)
                            split.applyParams(p); // unchanged APVTS block state

                        const double buf = split.runInputBuffer(stimulus(i, fs));
                        const double od = split.runOdSample(buf);
                        const double actual = split.processPostBlend(buf, od);
                        largestDifference = std::max(largestDifference,
                                                     std::abs(reference[i] - actual));
                        if (!std::isfinite(actual) || actual != reference[i])
                        {
                            std::printf("  [info] split mismatch fs %.0f schematic %d profile %d"
                                        " chunk %d sample %zu reference %.17g actual %.17g\n",
                                        fs, static_cast<int>(schematic), profile, chunk,
                                        i, reference[i], actual);
                            check(false, "split/fused 1x paths must remain bit-identical across host chunks");
                            return;
                        }
                    }
                }
            }
    check(true, "split/fused 1x paths bit-identical across host chunks");
    std::printf("  [info] maximum split/fused difference: %.3g V\n", largestDifference);
}

void testAllSwitchCombinations()
{
    constexpr std::size_t kSamples = 768;
    double largestPeak = 0.0;
    int cases = 0;
    for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const int model : {0, 1}) // default fit and nominal clone schematic
            for (int attack = 0; attack < 3; ++attack)
                for (int grunt = 0; grunt < 3; ++grunt)
                    for (int loMidFreq = 0; loMidFreq < 3; ++loMidFreq)
                        for (int hiMidFreq = 0; hiMidFreq < 3; ++hiMidFreq)
                        {
                            PedalChain::Params p = parameters(cases % 3);
                            p.attackIdx = attack;
                            p.gruntIdx = grunt;
                            p.loMidFreq = loMidFreq;
                            p.hiMidFreq = hiMidFreq;
                            PedalChain c;
                            initialize(c, fs, p, model == 1);
                            ++cases;
                            for (std::size_t i = 0; i < kSamples; ++i)
                            {
                                const double x = ((i % 157) == 0) ? 12.0 : stimulus(i, fs);
                                const double y = c.processSample(x);
                                if (!std::isfinite(y) || std::abs(y) > 1.0e5)
                                {
                                    std::printf("  [info] divergence fs %.0f model %d"
                                                " selectors %d/%d/%d/%d sample %zu output %.17g\n",
                                                fs, model, attack, grunt, loMidFreq,
                                                hiMidFreq, i, y);
                                    check(false, "switch/model/rate permutations must remain bounded");
                                    return;
                                }
                                largestPeak = std::max(largestPeak, std::abs(y));
                            }
                        }
    check(cases == 648, "full selector Cartesian product coverage");
    std::printf("  [info] %d switch/model/rate cases, peak %.4g V\n", cases, largestPeak);
}

void testLiveRapidSwitches()
{
    for (const double fs : {48000.0, 96000.0})
    {
        PedalChain c;
        auto p = parameters(1);
        initialize(c, fs, p);
        double peak = 0.0;
        // New selector positions every 1, 7, 113 or 1021 samples. This is more
        // aggressive than a real footswitch and intentionally interrupts fades.
        constexpr int kIntervals[] = {1, 7, 113, 1021};
        for (const int interval : kIntervals)
        {
            c.reset();
            for (int i = 0; i < 4096; ++i)
            {
                if (i % interval == 0)
                {
                    p.attackIdx = (p.attackIdx + 1) % 3;
                    p.gruntIdx = (p.gruntIdx + 2) % 3;
                    p.loMidFreq = (p.loMidFreq + 1) % 3;
                    p.hiMidFreq = (p.hiMidFreq + 2) % 3;
                    p.distEngage = !p.distEngage;
                    c.applyParams(p);
                }
                const double y = c.processSample(stimulus(static_cast<std::size_t>(i), fs));
                if (!std::isfinite(y) || std::abs(y) > 1.0e5)
                {
                    check(false, "rapid selector/footswitch flips stay bounded");
                    return;
                }
                peak = std::max(peak, std::abs(y));
            }
        }
        std::printf("  [info] rapid-flip peak fs %.0f: %.4g V\n", fs, peak);
    }
    check(true, "rapid selector/footswitch flips stay bounded");
}

void testResetAfterExceptionalInput()
{
    // Host audio buffers should be finite. If an upstream driver sends NaN or
    // Inf, the chain is not required to sanitize that buffer, but reset() must
    // eliminate bad recursive state before a subsequent, valid render.
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()})
    {
        const auto p = parameters(1);
        PedalChain damaged, fresh;
        initialize(damaged, 48000.0, p);
        initialize(fresh, 48000.0, p);
        (void) damaged.processSample(bad);
        damaged.reset();
        for (int i = 0; i < 1024; ++i)
        {
            const double x = stimulus(static_cast<std::size_t>(i), 48000.0);
            const double y = damaged.processSample(x);
            const double ref = fresh.processSample(x);
            if (!std::isfinite(y) || y != ref)
            {
                std::printf("  [info] bad input %.3g -> reset mismatch at %d,"
                            " actual %.17g expected %.17g\n", bad, i, y, ref);
                check(false, "reset after nonfinite input restores deterministic processing");
                return;
            }
        }
    }
    check(true, "reset after nonfinite input restores deterministic processing");
}

void testRealTimeAllocationBoundary()
{
    PedalChain c;
    c.prepare(48000.0, 96000.0);
    c.primePhysicalTables(); // deliberately outside the measured audio-thread region
    c.setFitParams(FitParams {});
    auto p = parameters(1);
    c.applyParams(p);
    c.reset();

    allocations.store(0, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    volatile double sum = 0.0;
    for (int i = 0; i < 4096; ++i)
    {
        if (i % 64 == 0)
        {
            p.attackIdx = (p.attackIdx + 1) % 3;
            p.gruntIdx = (p.gruntIdx + 1) % 3;
            p.loMidFreq = (p.loMidFreq + 1) % 3;
            p.hiMidFreq = (p.hiMidFreq + 1) % 3;
            p.distEngage = !p.distEngage;
            c.applyParams(p);
        }
        if (i % 1024 == 0)
            c.setFitParams((i / 1024) % 2 == 0 ? FitParams::schematic() : FitParams {});
        sum += c.processSample(stimulus(static_cast<std::size_t>(i), 48000.0));
    }
    countAllocations.store(false, std::memory_order_relaxed);
    (void) sum;
    const auto observed = allocations.load(std::memory_order_relaxed);
    std::printf("  [info] scalar/array operator-new calls during 4096 samples/64 switch flips/4 model flips: %zu\n",
                observed);
    check(observed == 0, "audio-thread selector/model switches and samples call no scalar/array operator new");
}
} // namespace

void* operator new(std::size_t size)
{
    if (countAllocations.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size ? size : 1))
        return ptr;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
B7K_STRESS_NOINLINE void operator delete(void* ptr) noexcept { std::free(ptr); }
B7K_STRESS_NOINLINE void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
B7K_STRESS_NOINLINE void operator delete[](void* ptr) noexcept { std::free(ptr); }
B7K_STRESS_NOINLINE void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

int main()
{
    testSplitAndChunkParity();
    testAllSwitchCombinations();
    testLiveRapidSwitches();
    testResetAfterExceptionalInput();
    testRealTimeAllocationBoundary();
    std::printf("PedalChainStressTest: %s (%d/%d failed)\n",
                failures == 0 ? "PASS" : "FAIL", failures, assertions);
    return failures == 0 ? 0 : 1;
}
