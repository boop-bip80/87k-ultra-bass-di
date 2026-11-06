// Release-regression gate for opt-in circuit research modes under rapid
// switching and repeated sample-rate preparation.  The two chains must remain
// bit-identical even if one is repeatedly handed the same research options.
// This deliberately does not claim compliance with any retail pedal circuit.
#include "../src/dsp/PedalChain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <new>

static std::atomic<unsigned long long> allocationCount { 0 };

void* operator new(std::size_t size)
{
    allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

[[noreturn]] void fail(const char* what, double rate, int mode, int sample,
                       double a, double b = 0.0)
{
    std::fprintf(stderr, "FAIL %s rate=%.0f mode=%d sample=%d a=%.17g b=%.17g\n",
                 what, rate, mode, sample, a, b);
    std::exit(EXIT_FAILURE);
}

bool bitEqual(double a, double b) noexcept
{
    return std::memcmp(&a, &b, sizeof(double)) == 0;
}

// Fully reproducible input; no compiler/library-dependent tone generator.
double stimulus(std::uint64_t& state, int sample) noexcept
{
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    const std::uint64_t bits = state * UINT64_C(2685821657736338717);
    const double uniform = (double((bits >> 11) & UINT64_C(0x1fffffffffffff)) /
                            9007199254740992.0) * 2.0 - 1.0;
    if (sample % 137 == 0) return (sample & 1) ? -30.0 : 30.0;
    if (sample % 211 == 0) return (sample & 1) ? -1.0e-300 : 1.0e-300;
    return (sample % 17 == 0 ? 5.0 : 0.8) * uniform;
}

PedalChain::Params params(int step)
{
    PedalChain::Params p;
    p.master = double((step * 31 + 6) % 101) / 100.0;
    p.blend = double((step * 47 + 13) % 101) / 100.0;
    p.level = double((step * 29 + 17) % 101) / 100.0;
    p.drive = double((step * 19 + 23) % 101) / 100.0;
    p.lo = double((step * 37 + 9) % 101) / 100.0;
    p.loMid = double((step * 41 + 11) % 101) / 100.0;
    p.hiMid = double((step * 43 + 19) % 101) / 100.0;
    p.hi = double((step * 17 + 43) % 101) / 100.0;
    p.attackIdx = step % 3;
    p.gruntIdx = (step / 2) % 3;
    p.loMidFreq = (step / 3) % 3;
    p.hiMidFreq = (step / 5) % 3;
    p.distEngage = (step % 7) != 0;
    return p;
}

PedalChain::ResearchModes modes(bool all)
{
    PedalChain::ResearchModes r;
    r.clipperDiodeCurrents = all;
    r.jfetFiniteDrainConductance = all;
    r.jfetNonlinearSourceFeedback = all;
    r.baxandallRailFeedback = all;
    r.midBandRailFeedback = all;
    return r;
}

} // namespace

int main()
{
    constexpr int kSamplesPerCase = 10000;
    int samples = 0;
    double highest = 0.0;

    // 8/384k boundaries and 48k canonical; in all 60k full-chain samples.
    for (double rate : {8000.0, 48000.0, 384000.0})
    for (int mode = 0; mode < 2; ++mode)
    {
        PedalChain steady, reasserted;
        steady.prepare(rate, rate);
        reasserted.prepare(rate, rate);
        const auto research = modes(mode != 0);
        steady.setResearchModes(research);
        reasserted.setResearchModes(research);
        steady.applyParams(params(0));
        reasserted.applyParams(params(0));
        steady.reset();
        reasserted.reset();
        std::uint64_t state = UINT64_C(0xfa1b6a4e370c922d);
        allocationCount.store(0, std::memory_order_relaxed);

        for (int n = 0; n < kSamplesPerCase; ++n)
        {
            // Frequent ATTACK, GRUNT, and two mid-band selector changes,
            // intentionally including changes before previous fades finish.
            if ((n % 43) == 0)
            {
                const auto p = params(n / 43);
                steady.applyParams(p);
                reasserted.applyParams(p);
            }
            if (n % 79 == 0)
                reasserted.setResearchModes(research);

            const double x = stimulus(state, n);
            const double a = steady.processSample(x);
            const double b = reasserted.processSample(x);
            if (!std::isfinite(a) || !std::isfinite(b))
                fail("non-finite output", rate, mode, n, a, b);
            if (!bitEqual(a, b))
                fail("research-mode idempotence", rate, mode, n, a, b);
            if (std::abs(a) >= 1.0e6)
                fail("pathological output magnitude", rate, mode, n, a, b);
            highest = std::max(highest, std::abs(a));
            ++samples;
        }
        const auto duringAudio = allocationCount.load(std::memory_order_relaxed);
        if (duringAudio != 0)
            fail("allocation during process/control changes", rate, mode, -1,
                 static_cast<double>(duringAudio));

        // Re-preparing must not silently unset a selected research mode.
        // A fresh rate cannot inherit previous capacitor companion histories.
        const auto before = steady.getResearchModes();
        steady.prepare(rate * 0.5, rate * 0.5);
        reasserted.prepare(rate * 0.5, rate * 0.5);
        const auto after = steady.getResearchModes();
        if (before.jfetNonlinearSourceFeedback != after.jfetNonlinearSourceFeedback ||
            before.clipperDiodeCurrents != after.clipperDiodeCurrents ||
            before.baxandallRailFeedback != after.baxandallRailFeedback ||
            before.midBandRailFeedback != after.midBandRailFeedback ||
            before.jfetFiniteDrainConductance != after.jfetFiniteDrainConductance)
            fail("reprepare changed research flags", rate, mode, -1, 0.0);

        for (int n = 0; n < 256; ++n)
        {
            const double x = stimulus(state, n + kSamplesPerCase);
            const double a = steady.processSample(x);
            const double b = reasserted.processSample(x);
            if (!std::isfinite(a) || !bitEqual(a, b))
                fail("post-reprepare mismatch", rate, mode, n, a, b);
            ++samples;
        }
    }

    std::printf("Round7 mode/switch stress PASS: %d full-chain samples, "
                "8/48/384k rates, rapid four-selector changes, "
                "both modes, zero new allocations, worst |out| %.9g model units; "
                "retail parity NOT implied\n",
                samples, highest);
}
