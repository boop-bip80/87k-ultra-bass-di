// Real-time safety gate for opt-in Shockley diode currents. This does not
// establish any retail pedal component or thermal calibration.
#include "../src/dsp/Clipper.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
std::atomic<bool> recording {false};
std::atomic<std::size_t> allocations {0};
}

#if defined(_MSC_VER)
#define DIODE_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define DIODE_TEST_NOINLINE __attribute__((noinline))
#else
#define DIODE_TEST_NOINLINE
#endif

void* operator new(std::size_t n)
{
    if (recording.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
DIODE_TEST_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
DIODE_TEST_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
DIODE_TEST_NOINLINE void operator delete[](void* p) noexcept { std::free(p); }
DIODE_TEST_NOINLINE void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    std::size_t observations = 0;
    for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0, 384000.0})
    {
        Clipper stage;
        stage.prepare(fs);
        stage.primePhysicalTables(); // explicitly outside the measured period
        stage.setDiodeCurrentModel(true);
        allocations.store(0, std::memory_order_relaxed);
        recording.store(true, std::memory_order_relaxed);
        volatile double checksum = 0.0;
        bool finite = true;
        for (int n = 0; n < 20000; ++n)
        {
            if (n % 127 == 0)
                stage.setGrunt(static_cast<Clipper::Grunt>((n/127)%3));
            if (n % 251 == 0)
                stage.setADAA(static_cast<Clipper::Adaa>((n/251)%3));
            if (n % 1024 == 0)
                stage.setPhysicalVtc((n/1024)%3); // pre-primed mode switches
            if (n % 769 == 0)
                stage.setDiodeCurrentModel(true, 5e-9, 0.052,
                                           (n/769)%2==0 ? 9.0 : 12.0);
            const double sample = 10.0 * std::sin(n * 0.3789)
                                + 0.9 * std::cos(n * 0.0152);
            const double value = stage.process(sample);
            checksum += value;
            if (!std::isfinite(value) || !std::isfinite(stage.probeNodeW()))
                finite = false;
        }
        recording.store(false, std::memory_order_relaxed);
        const auto nAlloc = allocations.load(std::memory_order_relaxed);
        if (!finite || nAlloc != 0 || !std::isfinite(checksum))
        {
            std::printf("real-time failure fs %.0f: finite=%d alloc=%zu\n",
                        fs, int(finite), nAlloc);
            return 1;
        }
        observations += 20000;
    }
    std::printf("Clipper diode real-time: %zu samples, 0 allocations, all finite PASS\n",
                observations);
}
