// Independent reference: run three separately allocated coupled Q1/ATTACK
// circuits and verify the research crossfader never cross-contaminates state.
#include "../src/dsp/CoupledAttackResearch.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using Attack = TrebleAttack::Attack;
constexpr std::array<Attack, 3> positions { Attack::Boost, Attack::Flat, Attack::Cut };

void check(bool okay, const char* detail, double fs, int n)
{
    if (!okay)
    {
        std::fprintf(stderr, "CoupledAttackResearch FAIL: %s fs=%.0f sample=%d\n",
                     detail, fs, n);
        std::exit(1);
    }
}

struct OracleBranch
{
    JfetStage jfet;
    TrebleAttack treble;
    void prepare(double fs, Attack attack)
    {
        jfet.prepare(fs);
        jfet.setPhysicalSourceFeedbackModel(true);
        treble.prepare(fs);
        treble.setCoupledJfetDrainPort(true);
        treble.setAttack(attack);
    }
    double process(double input)
    {
        const auto port = treble.previewCoupledDrainPort();
        return treble.process(jfet.processCoupled(
            input, port.openCircuitVolts, port.theveninOhms));
    }
};

void run(double fs, unsigned fadeSamples)
{
    CoupledAttackResearch dut;
    dut.prepare(fs, Attack::Flat, fadeSamples);
    std::array<OracleBranch, 3> oracle {};
    for (std::size_t i = 0; i < oracle.size(); ++i)
        oracle[i].prepare(fs, positions[i]);

    constexpr double pi = 3.14159265358979323846;
    bool distinctSourceHistory = false;
    int fadesRetargeted = 0;
    for (int n = 0; n < 4500; ++n)
    {
        if (n > 0 && n % 7 == 0)
        {
            // Re-target much faster than fadeLength, including all 3 throws.
            const auto before = dut.blendWeights();
            const auto next = positions[static_cast<std::size_t>(n / 7) % 3];
            dut.selectAttack(next);
            const auto after = dut.blendWeights();
            for (std::size_t i = 0; i < 3; ++i)
                check(before[i] == after[i], "retarget changed weights instantly", fs, n);
            ++fadesRetargeted;
        }
        const double input = 1.7 * std::sin(2*pi*879.0*n/fs)
                           + 0.8 * std::cos(2*pi*5143.0*n/fs)
                           + (n % 41 == 0 ? 3.2 : 0.0);
        const auto weights = dut.blendWeights();
        std::array<double, 3> oracleOutputs {};
        double expectedMix = 0.0;
        double weightSum = 0.0;
        for (std::size_t i = 0; i < oracle.size(); ++i)
        {
            const double expectedBranch = oracle[i].process(input);
            oracleOutputs[i] = expectedBranch;
            expectedMix += weights[i] * expectedBranch;
            weightSum += weights[i];
            check(std::isfinite(expectedBranch), "nonfinite branch oracle", fs, n);
        }
        const double got = dut.process(input);
        check(std::isfinite(got), "nonfinite blend", fs, n);
        check(std::abs(got-expectedMix) < 1e-12,
              "blend disagrees with isolated branch oracle", fs, n);
        check(std::abs(weightSum-1.0) < 2e-15, "weights not normalized", fs, n);
        for (std::size_t i = 0; i < oracle.size(); ++i)
        {
            check(weights[i] >= 0.0 && weights[i] <= 1.0,
                  "weight outside convex hull", fs, n);
            check(std::abs(dut.branchOutput(positions[i])
                           - oracleOutputs[i]) < 1e-12,
                  "branch output disagrees with independent oracle", fs, n);
            check(std::abs(dut.branchPortError(positions[i])) < 1e-8,
                  "Q1/Q2 drain port inconsistent", fs, n);
            check(std::abs(dut.branchSourceVoltage(positions[i])
                           - oracle[i].jfet.probePhysicalSourceVoltage()) < 1e-12,
                  "C3 source state contaminated by other ATTACK branch", fs, n);
        }
        const double a = dut.branchSourceVoltage(Attack::Boost);
        const double c = dut.branchSourceVoltage(Attack::Cut);
        distinctSourceHistory |= std::abs(a-c) > 1e-13;
    }
    check(distinctSourceHistory, "independent sources never diverged", fs, 4500);
    check(fadesRetargeted > 100, "missing rapid retargets", fs, 4500);

    // A fully uninterrupted fade reaches a genuine one-hot endpoint rather
    // than an asymptote; no accumulated fractional weights linger indefinitely.
    dut.selectAttack(Attack::Flat);
    for (unsigned n = 0; n < fadeSamples; ++n)
        check(std::isfinite(dut.process(0.37 * std::sin(n * 0.2))),
              "nonfinite fade endpoint", fs, static_cast<int>(n));
    const auto settled = dut.blendWeights();
    check(settled[0] == 0.0 && settled[1] == 1.0 && settled[2] == 0.0,
          "uninterrupted crossfade did not settle exactly", fs, 4500);

    // Reset must be equivalent to a newly constructed, identically selected
    // wrapper; neither inactive JFET C2/C3 nor ATTACK capacitor history survives.
    dut.selectAttack(Attack::Cut);
    dut.reset();
    CoupledAttackResearch fresh;
    fresh.prepare(fs, Attack::Cut, fadeSamples);
    for (int n = 0; n < 200; ++n)
    {
        const double x = std::sin(n * 0.35);
        check(dut.process(x) == fresh.process(x), "reset history differs", fs, n);
    }
}

} // namespace

int main()
{
    for (const double fs : {44100.0, 48000.0, 96000.0})
        run(fs, 131);
    CoupledAttackResearch immediate;
    immediate.prepare(48000.0, Attack::Flat, 0);
    immediate.selectAttack(Attack::Cut);
    const auto w = immediate.blendWeights();
    check(w[0] == 0.0 && w[1] == 0.0 && w[2] == 1.0,
          "explicit zero-duration switch not immediate", 48000.0, 0);
    std::printf("Coupled ATTACK 3-way isolated Q1 C2/C3 + treble port PASS: "
                "3 sample rates, rapid retarget, exact settle, drain KCL, reset\n");
}
