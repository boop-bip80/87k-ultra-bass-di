#include "../src/dsp/JfetStage.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr double pi = 3.14159265358979323846;

void check(bool pass, const char* what)
{
    if (!pass) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

// Independent oracle: analytical quadratic DC self-bias; wide-interval
// 110-iteration scalar bisection for the dynamic source-node KCL. This does
// NOT call any JfetStage helper or share its bracketing algorithm.
struct SourceOracle
{
    const double fs;
    const double beta;
    const double vPinch;
    const double gc2;
    const double gc3;
    const double gRin;
    const double idq;
    const double vov;
    double ieq2 = 0.0;
    double ieq3 = 0.0;
    double vs = 0.0;

    SourceOracle(double sampleRate, double b, double pinch)
        : fs(sampleRate), beta(b), vPinch(pinch),
          gc2(2.0 * sampleRate * JfetStage::kC2),
          gc3(2.0 * sampleRate * JfetStage::kC3),
          gRin(1.0 / (JfetStage::kR4 + JfetStage::kR5)),
          idq((vPinch - 2.0 * vPinch /
               (1.0 + std::sqrt(1.0 + 4.0 * JfetStage::kR6 * b * vPinch))) /
              JfetStage::kR6),
          vov(vPinch - JfetStage::kR6 * idq)
    {}

    double current(double vg, double source) const
    {
        const double x = std::max(0.0, vov + vg - source);
        return beta * x * x - idq;
    }

    double step(double input)
    {
        const double vx = (gc2 * input - ieq2) / (gc2 + gRin);
        ieq2 = 2.0 * gc2 * (input - vx) - ieq2;
        const double vg = (JfetStage::kR5 / (JfetStage::kR4 + JfetStage::kR5)) * vx;
        const double old = ieq3;
        const auto kcl = [&](double source) {
            return (gc3 + 1.0 / JfetStage::kR6) * source - old - current(vg, source);
        };
        double lo = -1000.0, hi = +1000.0;
        check(kcl(lo) < 0.0 && kcl(hi) > 0.0, "wide physical source bracket");
        for (int k = 0; k < 110; ++k)
        {
            const double mid = 0.5 * (lo + hi);
            if (kcl(mid) > 0.0) hi = mid;
            else lo = mid;
        }
        vs = 0.5 * (lo + hi);
        const double residual = kcl(vs);
        check(std::abs(residual) < 1.0e-12, "physical source current KCL");
        ieq3 = 2.0 * gc3 * vs - old;
        return -current(vg, vs);
    }
};

void testDefaultPathBitwise()
{
    JfetStage legacy, explicitlyOff;
    legacy.prepare(48000.0);
    explicitlyOff.prepare(48000.0);
    explicitlyOff.setExactFiniteDrainConductance(false);
    explicitlyOff.setPhysicalSourceFeedbackModel(false);
    for (int n = 0; n < 15000; ++n)
    {
        const double x = 1.8 * std::sin(2 * pi * n / 101.0)
            + (n % 2 ? 2.3 : -2.7);
        const double a = legacy.process(x);
        const double b = explicitlyOff.process(x);
        check(std::memcmp(&a, &b, sizeof(double)) == 0,
              "legacy default and explicitly disabled bitwise equal");
    }
}

void testFiniteGdsSourceZ()
{
    JfetStage stage;
    stage.prepare(48000.0);
    stage.setExactFiniteDrainConductance(true);
    const auto z = stage.getSourceZ();
    constexpr double gm = JfetStage::kGm;
    constexpr double ro = JfetStage::kRo;
    constexpr double rq = JfetStage::kRq2;
    check(std::abs(z.rp - (ro * gm + 1) * JfetStage::kR6) < 1e-8,
          "finite-gds output rp exact relation");
    for (double f : { 2.0, 20.0, 200.0, 2000.0, 8000.0 })
    {
        const std::complex<double> s(0.0, 2 * pi * f);
        const auto zs = 1.0 / (1.0 / JfetStage::kR6 + s * JfetStage::kC3);
        const auto exactZ = 1.0 / (1.0 / (ro + (1.0 + gm * ro) * zs) + 1.0 / rq);
        const auto approxRp = 1.0 / (1.0 / z.rp + s * z.cp);
        const auto shippedZ = 1.0 / (1.0 / (z.ro + approxRp) + 1.0 / z.rq2);
        check(std::abs(exactZ - shippedZ) < 1e-8 * std::abs(exactZ),
              "finite-gds complex output impedance oracle");
        const auto gExact = gm / (1.0 + (gm + 1.0 / ro) * zs);
        check(std::isfinite(std::abs(gExact)), "finite-gds complex gm finite");
    }
}

void testFiniteGdsCurrentFrequency()
{
    // Independent digital-BLT complex transfer of C2, R4/R5, Q1/R6/C3.
    // Probe Q1's Norton CURRENT directly; the Q2 drain network is downstream.
    constexpr double fs = 48000.0;
    constexpr double amp = 1.0e-4;
    constexpr double div = JfetStage::kR5 / (JfetStage::kR4 + JfetStage::kR5);
    constexpr double gm = JfetStage::kGm, ro = JfetStage::kRo;
    for (double frequency : { 20.0, 200.0, 2000.0, 8000.0 })
    {
        JfetStage stage;
        stage.prepare(fs);
        stage.setExactFiniteDrainConductance(true);

        std::complex<double> projection(0.0, 0.0);
        for (int n = 0; n < 96000; ++n)
        {
            const double theta = 2 * pi * frequency * n / fs;
            const double y = stage.process(amp * std::cos(theta));
            if (n >= 48000)
                projection += y * std::complex<double>(std::cos(theta), -std::sin(theta));
        }
        const auto measured = (2.0 / (48000.0 * amp)) * projection;
        const std::complex<double> s(0.0, 2 * fs * std::tan(pi * frequency / fs));
        const auto zs = 1.0 / (1.0 / JfetStage::kR6 + s * JfetStage::kC3);
        const auto hp = (s * JfetStage::kC2) /
            (s * JfetStage::kC2 + 1.0 / (JfetStage::kR4 + JfetStage::kR5));
        const auto expected = -div * hp * gm / (1.0 + (gm + 1.0 / ro) * zs);
        check(std::abs(measured - expected) / std::abs(expected) < 3.0e-4,
              "finite-gds sample-domain Norton gm magnitude+phase oracle");
    }
}

void testPhysicalSourceOracle()
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    for (double beta : { 0.48e-3, 1.031e-3, 1.80e-3 })
    {
        JfetStage stage;
        stage.prepare(fs);
        stage.setPhysicalSourceFeedbackModel(true, beta, -0.718);
        SourceOracle ref(fs, beta, 0.718);
        check(std::abs(stage.probePhysicalBiasCurrent() - ref.idq) < 1e-15,
              "DC JFET quadratic-bias oracle");
        check(std::abs(stage.probePhysicalOverdriveVoltage() - ref.vov) < 1e-12,
              "DC JFET overdrive oracle");
        for (int n = 0; n < 2000; ++n)
        {
            double input = 0.0;
            if (n < 20) input = 0.0;
            else if (n < 240) input = 5.0;
            else if (n < 440) input = -4.0;
            else if (n < 650) input = 0.4 * std::sin(2 * pi * n / 79.0);
            else input = 5.0 * std::sin(2 * pi * n / 29.0)
                       + ((n % 37) ? 0.0 : 8.0);

            const double actual = stage.process(input);
            const double expected = ref.step(input);
            check(std::isfinite(actual), "physical JFET output finite");
            check(std::abs(actual - expected) < 2e-13,
                  "physical source current vs independent primitive oracle");
            check(std::abs(stage.probePhysicalSourceVoltage() - ref.vs) < 2e-11,
                  "physical source voltage vs independent root oracle");
        }
    }
}
} // namespace

int main()
{
    testDefaultPathBitwise();
    testFiniteGdsSourceZ();
    testFiniteGdsCurrentFrequency();
    testPhysicalSourceOracle();
    std::puts("JfetPhysicalFeedbackTest: PASS (legacy exact, finite-gds Z, nonlinear source KCL)");
}
