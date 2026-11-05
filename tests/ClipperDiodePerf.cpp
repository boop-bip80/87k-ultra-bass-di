// Non-gating microbenchmark. Compare before/after with same compiler & machine.
#include "../src/dsp/Clipper.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

static constexpr int frames = 250000;
static double data[4096];
static volatile double sink = 0.0;

int main()
{
    for (int i = 0; i < 4096; ++i)
    {
        data[i] = 6.5 * std::sin(i * .293)
                + 2.2 * std::sin(i * .0091)
                + ((i % 211 == 0) ? 14.0 : 0.0)
                - ((i % 307 == 0) ? 12.0 : 0.0);
    }
    for (auto mode : {Clipper::Adaa::Off, Clipper::Adaa::Full, Clipper::Adaa::Residue})
    {
        for (double fs : {44100.0, 48000.0, 96000.0, 192000.0, 384000.0})
        {
            for (bool diode : {false, true})
            {
                std::array<double, 3> times{};
                for (int rep = 0; rep < 3; ++rep)
                {
                    Clipper c;
                    c.prepare(fs);
                    c.setADAA(mode);
                    c.setDiodeCurrentModel(diode);
                    double checksum = 0.0;
                    auto start = std::chrono::steady_clock::now();
                    for (int i = 0; i < frames; ++i)
                        checksum += c.process(data[i % 4096]);
                    auto end = std::chrono::steady_clock::now();
                    sink = sink + checksum;
                    times[rep] = std::chrono::duration<double, std::nano>(end-start).count()/frames;
                }
                std::sort(times.begin(), times.end());
                std::printf("%d %.0f %d %.3f ns/sample\n", int(mode), fs, int(diode), times[1]);
            }
        }
    }
}
