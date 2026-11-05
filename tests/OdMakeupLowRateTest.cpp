// Regression: the 5.6 kHz engineered HF band must not instantiate an unstable
// RBJ biquad when a host runs the chain at 8/11.025 kHz (band >= Nyquist).
#include "../src/dsp/OdMakeup.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

int main()
{
    bool ok = true;
    for (const double rate : { 8000., 11025., 11200., 12000., 44100., 48000., 96000., 384000. })
    {
        OdMakeup research, bandOff;
        for (OdMakeup* p : { &research, &bandOff })
        {
            p->prepare(rate);
            p->setLaw(6., 130., 3.5, 2800., 6.);
            p->setCleanFraction(0.35);
        }
        research.setHfMix(5600., 0.8, -4., 5., 0.43, -2.);
        bandOff.setHfMix(5600., 0.8, 0., 0., 0.43, 0.);
        research.reset();
        bandOff.reset();

        double peak = 0., delta = 0.;
        bool bitExact = true;
        for (int n = 0; n < 25000; ++n)
        {
            const double x = 18. * std::sin(0.032 * n)
                          + 9. * std::sin(0.091 * n);
            const double y = research.process(x), ref = bandOff.process(x);
            peak = std::max(peak, std::abs(y));
            delta = std::max(delta, std::abs(y - ref));
            bitExact &= y == ref;
            ok &= std::isfinite(y) && std::abs(y) < 1000.;
        }
        // At and beyond Nyquist the corrective band is unrepresentable and
        // explicitly bypassed. Valid-rate filters remain unaffected.
        const bool subNyquist = (5600. < 0.5 * rate);
        ok &= subNyquist ? (delta > 1.0e-7) : bitExact;
        std::printf("OdMakeupLowRateTest: fs=%g Hz, band %s, peak=%g, delta=%g, %s\n",
                    rate, subNyquist ? "enabled" : "bypassed", peak, delta,
                    ok ? "PASS" : "FAIL");
        if (!ok) return 1;
    }
    return 0;
}
