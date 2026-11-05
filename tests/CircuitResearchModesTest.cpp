// PedalChain's stage-level KCL physics are intentionally DEFAULT-OFF.
// This suite verifies that an explicit offline research route can audition
// those equations without silently changing the fitted plugin and that
// configuration/resets do not corrupt the live signal path.
#include "../src/dsp/PedalChain.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s: %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
static bool sameBits(double a, double b) noexcept
{
    return std::memcmp(&a, &b, sizeof(double)) == 0;
}

int main()
{
    // Every new research option must be OFF by default.
    PedalChain::ResearchModes off;
    check(!off.clipperDiodeCurrents && !off.jfetFiniteDrainConductance
       && !off.jfetNonlinearSourceFeedback && !off.baxandallRailFeedback
       && !off.midBandRailFeedback, "all research switches are default OFF");

    PedalChain defaultChain, explicitOff;
    defaultChain.prepare(48000.0, 48000.0);
    explicitOff.prepare(48000.0, 48000.0);
    explicitOff.setResearchModes(off);
    PedalChain::Params p;
    p.drive = 0.65;
    p.blend = 0.7;
    p.lo = 0.8;
    p.loMid = 0.1;
    p.hiMid = 0.9;
    p.hi = 0.3;
    defaultChain.applyParams(p);
    explicitOff.applyParams(p);
    defaultChain.reset();
    explicitOff.reset();
    bool legacyExact = true;
    for (int n = 0; n < 15000; ++n)
    {
        const double x = 1.8 * std::sin(n * 0.019)
                       + 0.55 * std::sin(n * 0.173);
        const double y0 = defaultChain.processSample(x);
        const double y1 = explicitOff.processSample(x);
        legacyExact &= sameBits(y0, y1);
    }
    check(legacyExact, "default-off is bit-exact for 15000 full-chain samples");

    PedalChain::ResearchModes all;
    all.clipperDiodeCurrents = true;
    all.jfetFiniteDrainConductance = true;
    all.jfetNonlinearSourceFeedback = true;
    all.baxandallRailFeedback = true;
    all.midBandRailFeedback = true;

    PedalChain experimental, idempotenceTwin;
    experimental.prepare(48000.0, 48000.0);
    idempotenceTwin.prepare(48000.0, 48000.0);
    experimental.setResearchModes(all);
    idempotenceTwin.setResearchModes(all);
    experimental.applyParams(p);
    idempotenceTwin.applyParams(p);
    experimental.reset();
    idempotenceTwin.reset();
    const auto state = experimental.getResearchModes();
    check(state.clipperDiodeCurrents && state.jfetFiniteDrainConductance
       && state.jfetNonlinearSourceFeedback && state.baxandallRailFeedback
       && state.midBandRailFeedback, "research configuration is queryable");

    double worstPeak = 0.0, worstDifference = 0.0;
    bool allFinite = true, idempotent = true;
    PedalChain control;
    control.prepare(48000.0, 48000.0);
    control.applyParams(p);
    control.reset();
    for (int n = 0; n < 16000; ++n)
    {
        // Research flags are configuration parameters, not per-sample controls;
        // re-applying the *same* settings must never reset the physical C3.
        if (n % 211 == 0) experimental.setResearchModes(all);
        const double x = 4.5 * std::sin(n * 0.026)
                       + 1.9 * std::sin(n * 0.08);
        const double y1 = experimental.processSample(x);
        const double y2 = idempotenceTwin.processSample(x);
        const double yc = control.processSample(x);
        allFinite &= std::isfinite(y1) && std::isfinite(y2);
        idempotent &= sameBits(y1, y2);
        worstPeak = std::max(worstPeak, std::abs(y1));
        worstDifference = std::max(worstDifference, std::abs(y1 - yc));
    }
    check(allFinite && worstPeak < 1.0e5,
          "all-physics offline path remains finite under high drive");
    check(idempotent, "re-sending identical research options preserves state bitwise");
    check(worstDifference > 1.0e-6, "research options change the actual full-chain signal");

    // Prepare/rate changes must preserve the explicit research choices. New
    // physical capacitor coefficients are recomputed at the new sample rate.
    experimental.prepare(96000.0, 96000.0);
    const auto afterPrepare = experimental.getResearchModes();
    check(afterPrepare.clipperDiodeCurrents && afterPrepare.jfetFiniteDrainConductance
       && afterPrepare.jfetNonlinearSourceFeedback
       && afterPrepare.baxandallRailFeedback && afterPrepare.midBandRailFeedback,
       "mode survives a host/oversampling rate change");
    bool afterRateFinite = true;
    for (int n = 0; n < 3000; ++n)
        afterRateFinite &= std::isfinite(experimental.processSample(
            3.5 * std::sin(n * 0.097)));
    check(afterRateFinite, "rate-changed research chain remains finite");
    experimental.prepareOd(192000.0);
    const auto afterOdChange = experimental.getResearchModes();
    check(afterOdChange.clipperDiodeCurrents && afterOdChange.jfetNonlinearSourceFeedback,
          "OD oversampling re-prepare preserves research flags");

    // The mixed states are not claimed to represent measured pedal hardware.
    std::printf("Research chain peak %.6g V; largest legacy-vs-research delta %.6g V\n",
                worstPeak, worstDifference);
    std::printf("CircuitResearchModesTest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
