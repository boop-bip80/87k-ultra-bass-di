// Real-time allocation regression for the experimental circuit solvers.
// Verifies only that process/control changes do not allocate on the audio
// thread. It does NOT certify CPU deadlines or retail hardware fidelity.
#include "../src/dsp/Clipper.h"
#include "../src/dsp/JfetStage.h"
#include "../src/dsp/MidBand.h"
#include "../src/dsp/Baxandall.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

static std::atomic<unsigned long long> allocations { 0 };

void* operator new(std::size_t size)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size))
        return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size))
        return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main()
{
    Clipper clipper;
    clipper.prepare(96000.0);
    clipper.setDiodeCurrentModel(true);
    clipper.primePhysicalTables();
    clipper.setPhysicalVtc(1);

    JfetStage jfet;
    jfet.prepare(96000.0);
    jfet.setExactFiniteDrainConductance(true);
    jfet.setPhysicalSourceFeedbackModel(true);

    MidBand mid;
    mid.configure(MidBand::kLoMid, MidBand::kLoMid10n);
    mid.setInputCap(2.2e-6);
    mid.prepare(96000.0);
    mid.setRailClampEnabled(true);
    mid.setRailAwarePassiveFeedback(true);

    Baxandall bax;
    bax.prepare(96000.0);
    bax.setInputCap(100e-9);
    bax.setRailClampEnabled(true);
    bax.setRailAwarePassiveFeedback(true);

    // Trigger table construction and all nodal matrix rebuilds before the
    // allocation counter is reset, as permitted during preparation.
    (void)clipper.process(0.0);
    (void)jfet.process(0.0);
    (void)mid.process(0.0);
    (void)bax.process(0.0);

    allocations.store(0, std::memory_order_relaxed);
    double checksum = 0.0;
    for (int n = 0; n < 16384; ++n)
    {
        if ((n % 1024) == 0)
        {
            const bool even = (n / 1024) % 2 == 0;
            clipper.setGrunt(even ? Clipper::Grunt::Cut : Clipper::Grunt::Boost);
            clipper.setPhysicalVtc(even ? 1 : 2);
            mid.setPosition(even ? 0.1 : 0.9);
            mid.setSeriesCap(even ? MidBand::kLoMid47n : MidBand::kLoMid2n2);
            bax.setBass(even ? 0.15 : 0.85);
            bax.setTreble(even ? 0.85 : 0.15);
        }
        const double input = 4.0 * std::sin(0.037 * n);
        const double a = clipper.process(input);
        const double b = jfet.process(input);
        const double c = mid.process(input);
        const double d = bax.process(input);
        if (!std::isfinite(a) || !std::isfinite(b)
            || !std::isfinite(c) || !std::isfinite(d))
        {
            std::fprintf(stderr, "Nonfinite circuit result at sample %d\n", n);
            return 1;
        }
        checksum += 1e-4 * (a + b + c + d);
    }
    const auto count = allocations.load(std::memory_order_relaxed);
    if (count != 0)
    {
        std::fprintf(stderr, "Experimental audio path allocated %llu times\n",
                     count);
        return 1;
    }
    std::printf("ExperimentalRealtimeTest: PASS 16384 samples x 4 stages, "
                "16 rapid control flips, 0 allocations, checksum %.9g\n", checksum);
}
