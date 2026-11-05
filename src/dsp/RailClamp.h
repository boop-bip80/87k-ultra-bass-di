#pragma once

#include <algorithm>
#include <cmath> // std::abs (floating-point overload) in setRailVoltages

// =============================================================================
// RailClamp — op-amp output-rail saturation (shared across every op-amp stage)
// =============================================================================
// calibration-and-gain-staging.md §6: a non-rail-to-rail op-amp (TL07x in the
// published clone schematic) does not swing all the way to the supply rails.
// This utility APPROXIMATES saturation as linear gain, a short parabolic knee,
// then a hard clamp. The curve and fixed knee width are engineering choices,
// NOT a measured TL07x output-stage transfer or authenticated Darkglass part.
//
// The DSP chain runs bipolar with signal ground = VD (~4.5 V) mapped to 0, so
// the clamp limits are given RELATIVE to VD: the output saturates into
// [-vNeg, +vPos]. The class's standalone default is symmetric ±3.3 V; the
// current FitParams pass asymmetric estimates of −2.9/+2.7 V. Neither is a
// hardware measurement; refine against a documented target unit and load.
//
// At high DRIVE, IC2_A's modeled ~78x gain can bring it to the fitted rails;
// that is a potentially audible effect rather than merely a numerical safety
// bound (build-plan Phase 4 rail paragraph). The outer DSP chain may oversample the clamp,
// but this class currently evaluates its static curve directly per sample; it
// does NOT implement antiderivative antialiasing (ADAA) for the rail knee.
//
// Disabled by default so a freshly-built LINEAR stage validates against its
// analytic oracle unchanged; the processor/stage enables it for the audio path.
// =============================================================================
class RailClamp
{
public:
    RailClamp() = default;

    // Clamp MAGNITUDES relative to VD (signal ground = 0) — the output saturates
    // into [-vNeg, +vPos], so both are positive. |v| is taken defensively: a
    // signed vNeg makes the negative branch below fire for every sample under
    // +(|vNeg| - knee) and return a constant +|vNeg|, i.e. the clamp emits DC
    // instead of audio. That is exactly what FitParams shipped (railNeg = -3.3)
    // until 2026-07-22, undetected because railEnabled defaulted to false — and
    // `railNeg` is a --fit key, so a signed value can still arrive from a sweep.
    void setRailVoltages(double vNeg, double vPos) noexcept
    {
        // Non-finite fit values have no physical rail interpretation. Collapse
        // that side to zero available headroom rather than silently defeating
        // the clamp and propagating an infinite output. The ordinary signed
        // magnitude convention (including -3.3) remains unchanged.
        railNeg = std::isfinite(vNeg) ? std::abs(vNeg) : 0.0;
        railPos = std::isfinite(vPos) ? std::abs(vPos) : 0.0;
        updateEffectiveKnee();
    }

    void setKnee(double kneeVolts) noexcept
    {
        requestedKnee = std::isfinite(kneeVolts) ? std::max(0.0, kneeVolts) : 0.0;
        updateEffectiveKnee();
    }
    void setEnabled(bool e) noexcept { enabled = e; }
    bool isEnabled() const noexcept { return enabled; }

    // Saturate one sample (real volts, VD-referenced).
    inline double process(double x) const noexcept
    {
        if (! enabled)
            return x;

        const double h = knee;
        // ---- Positive rail: linear -> parabolic knee -> hard clamp ----
        if (x > railPos - h)
        {
            if (x >= railPos + h)
                return railPos;
            const double d = x - railPos - h; // in [-2h, 0)
            return railPos - (d * d) / (4.0 * h);
        }
        // ---- Negative rail (mirror image) ----
        if (x < -(railNeg - h))
        {
            if (x <= -(railNeg + h))
                return -railNeg;
            const double d = x + railNeg + h; // in (0, 2h]
            return -railNeg + (d * d) / (4.0 * h);
        }
        return x; // dead-linear between the knees
    }

private:
    void updateEffectiveKnee() noexcept
    {
        // A knee that reaches through signal ground when headroom is narrow
        // produces f(0) != 0, an unphysical DC source (e.g. -0.2/+2.0 V with
        // a 0.35 V half-knee emitted +16.07 mV at zero input). Cap the EFFECTIVE
        // width to the nearer rail; retain the requested value so a later
        // widening of the rails recovers the original knee.
        knee = std::min({ requestedKnee, railNeg, railPos });
    }

    double railNeg = 3.3, railPos = 3.3; // relative to VD; estimates (calibration §6)
    double requestedKnee = 0.35;         // unconstrained requested half-width (V)
    double knee = 0.35;                  // physically admissible effective half-width
    bool enabled = false;                // linear until a stage/processor turns it on
};
