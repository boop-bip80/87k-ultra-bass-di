#pragma once

#include <cmath>
#include <limits>

// Experimental, independent DC JFET primitive. Nothing in the legacy signal
// path includes or invokes this header: adoption must be explicitly opt-in.
namespace ultrabass::dsp::jfet_physics {

struct Level1NjfetParameters {
    // Illustrative values only; these are not measurements of a particular J201.
    double betaAmpsPerVoltSquared = 0.001;
    double thresholdVolts = -1.5;
    double channelModulationPerVolt = 0.0;
    double gminSiemens = 1e-12;
    double gateSaturationCurrentAmps = 1e-14;
    double gateIdealityFactor = 1.0;
    double thermalVoltageVolts = 0.02585;
};

inline bool validParameters(const Level1NjfetParameters& p) noexcept {
    return std::isfinite(p.betaAmpsPerVoltSquared) && p.betaAmpsPerVoltSquared > 0.0 &&
           std::isfinite(p.thresholdVolts) && p.thresholdVolts < 0.0 &&
           std::isfinite(p.channelModulationPerVolt) && p.channelModulationPerVolt >= 0.0 &&
           std::isfinite(p.gminSiemens) && p.gminSiemens >= 0.0 &&
           std::isfinite(p.gateSaturationCurrentAmps) && p.gateSaturationCurrentAmps >= 0.0 &&
           std::isfinite(p.gateIdealityFactor) && p.gateIdealityFactor > 0.0 &&
           std::isfinite(p.thermalVoltageVolts) && p.thermalVoltageVolts > 0.0;
}

// ngspice LEVEL-1 NJF (B=1), for Vds >= 0: forward channel plus Vds*GMIN.
// The private helper's polarity precondition is guaranteed by its caller.
inline double forwardChannelAmps(const Level1NjfetParameters& p,
                                 double vgs, double vds) noexcept {
    const double vgst = vgs - p.thresholdVolts;
    if (vgst <= 0.0)
        return vds * p.gminSiemens;

    const double betaPrime = p.betaAmpsPerVoltSquared *
                             (1.0 + p.channelModulationPerVolt * vds);
    const double squareLaw = vds <= vgst
        ? vds * (2.0 * vgst - vds)
        : vgst * vgst;
    return betaPrime * squareLaw + vds * p.gminSiemens;
}

// Drain-to-source conventional current. For reverse Vds, swap intrinsic D/S
// before evaluating the NJF branch, then reverse current. This avoids feeding
// negative Vds into the forward triode polynomial.
inline double channelDrainCurrentAmps(const Level1NjfetParameters& p,
                                      double gateVolts, double drainVolts,
                                      double sourceVolts) noexcept {
    if (!validParameters(p) || !std::isfinite(gateVolts) ||
        !std::isfinite(drainVolts) || !std::isfinite(sourceVolts))
        return std::numeric_limits<double>::quiet_NaN();

    if (drainVolts >= sourceVolts)
        return forwardChannelAmps(p, gateVolts - sourceVolts,
                                 drainVolts - sourceVolts);
    return -forwardChannelAmps(p, gateVolts - drainVolts,
                              sourceVolts - drainVolts);
}

// Optional *DC* gate-to-terminal junction current. Positive means gate ->
// terminal. No dynamic Cgs/Cgd, breakdown, or SPICE limiter is modeled.
// Fail closed with NaN if finite, but out-of-model, forward bias would exceed
// the representable current range. In particular, a disabled junction (Is=0)
// must remain exactly zero rather than letting 0 * expm1(+Inf) become NaN.
inline double gateJunctionCurrentAmps(const Level1NjfetParameters& p,
                                     double gateVolts, double terminalVolts) noexcept {
    if (!validParameters(p) || !std::isfinite(gateVolts) ||
        !std::isfinite(terminalVolts))
        return std::numeric_limits<double>::quiet_NaN();

    if (p.gateSaturationCurrentAmps == 0.0)
        return 0.0;

    const double normalized = (gateVolts - terminalVolts) /
                              (p.gateIdealityFactor * p.thermalVoltageVolts);
    if (!std::isfinite(normalized))
        return std::numeric_limits<double>::quiet_NaN();

    // expm1(x) itself overflows near x=710 even when Is*exp(x) still fits.
    // Work in the log domain for large positive x to avoid that artifact.
    double current;
    if (normalized < 700.0) {
        current = p.gateSaturationCurrentAmps * std::expm1(normalized);
    } else {
        const double logCurrent = std::log(p.gateSaturationCurrentAmps) + normalized;
        if (!std::isfinite(logCurrent) ||
            logCurrent > std::log(std::numeric_limits<double>::max()))
            return std::numeric_limits<double>::quiet_NaN();
        // At x >= 700, the omitted -Is term is negligible in double.
        current = std::exp(logCurrent);
    }
    return std::isfinite(current) ? current
                                  : std::numeric_limits<double>::quiet_NaN();
}

struct ResistorLoadSolution {
    bool valid = false;
    double drainVolts = std::numeric_limits<double>::quiet_NaN();
    double sourceVolts = std::numeric_limits<double>::quiet_NaN();
    double channelAmps = std::numeric_limits<double>::quiet_NaN();
    double kclResidualAmps = std::numeric_limits<double>::quiet_NaN();
    int iterations = 0;
};

// A specifically bounded *one-JFET* DC fixture:
// supply -- Rd -- D, S -- Rs -- ground; ideal gate voltage <= 0 V.
// This deliberately does NOT replace the full audio stage/Q2 load. Bracketing
// I in [0, Vdd/(Rd+Rs)] enforces rail-safe D and S by circuit KCL, not by
// truncating a computed waveform or presenting a fabricated voltage clamp.
inline ResistorLoadSolution solveNineVoltResistorLoad(
    const Level1NjfetParameters& p, double gateVolts, double supplyVolts,
    double drainOhms, double sourceOhms) noexcept {
    ResistorLoadSolution result;
    if (!validParameters(p) || !std::isfinite(gateVolts) || gateVolts > 0.0 ||
        !std::isfinite(supplyVolts) || supplyVolts <= 0.0 || supplyVolts > 9.0 ||
        !std::isfinite(drainOhms) || drainOhms <= 0.0 ||
        !std::isfinite(sourceOhms) || sourceOhms <= 0.0)
        return result;

    double lo = 0.0;
    double hi = supplyVolts / (drainOhms + sourceOhms);
    if (!std::isfinite(hi) || hi <= 0.0)
        return result;

    // F(0)=Id(Vdd,0)>=0 and F(Imax)=Id(D<=S)-Imax<=0 for reverse-biased gate.
    // A bisection root is independent of Newton initialization/damping.
    for (int n = 0; n < 100; ++n) {
        const double mid = lo + 0.5 * (hi - lo);
        const double d = supplyVolts - mid * drainOhms;
        const double s = mid * sourceOhms;
        const double device = channelDrainCurrentAmps(p, gateVolts, d, s);
        if (!std::isfinite(device))
            return result;
        if (device > mid) lo = mid;
        else hi = mid;
        result.iterations = n + 1;
    }
    const double i = lo + 0.5 * (hi - lo);
    result.drainVolts = supplyVolts - i * drainOhms;
    result.sourceVolts = i * sourceOhms;
    result.channelAmps = i;
    result.kclResidualAmps = channelDrainCurrentAmps(
        p, gateVolts, result.drainVolts, result.sourceVolts) - i;
    result.valid = std::isfinite(result.kclResidualAmps) &&
                   result.drainVolts >= 0.0 && result.drainVolts <= supplyVolts &&
                   result.sourceVolts >= 0.0 && result.sourceVolts <= supplyVolts &&
                   result.drainVolts >= result.sourceVolts;
    return result;
}

} // namespace ultrabass::dsp::jfet_physics
