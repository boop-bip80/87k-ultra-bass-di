#pragma once

#include "JfetStage.h"
#include "TrebleAttack.h"

#include <array>
#include <cstddef>

// Research-only, all-branches-warm ATTACK switch for the *coupled* Q1 model.
//
// The software crossfade runs three hypothetical switch configurations at once.
// Each requires BOTH its own TrebleAttack capacitor histories and its own Q1
// C2/C3 history: source current and source capacitor charge are functions of
// that branch's *own* drain Thevenin port. Sharing a JfetStage across branches
// would advance C2 multiple times/sample and corrupt its C3 self-bias state.
//
// This is deliberately not wired into PedalChain and does not change either
// stage's default behavior. See docs/coupled-attack-research.md.
class CoupledAttackResearch
{
public:
    using Attack = TrebleAttack::Attack;
    static constexpr std::size_t kBranches = 3;
    using Weights = std::array<double, kBranches>;

    void prepare(double sampleRate, Attack initial = Attack::Flat,
                 unsigned fadeSamples = 128) noexcept
    {
        for (std::size_t i = 0; i < branches.size(); ++i)
        {
            auto& b = branches[i];
            b.jfet.prepare(sampleRate);
            b.jfet.setPhysicalSourceFeedbackModel(true);
            b.treble.prepare(sampleRate);
            b.treble.setCoupledJfetDrainPort(true);
            // The switch is fixed in each branch for its full lifetime: no
            // reinterpretation/reset of a charged C8 or switched C5 leg.
            b.treble.setAttack(static_cast<Attack>(i));
        }
        fadeLength = fadeSamples;
        // An enum can be cast from an out-of-range host/control integer.
        // Never let that value index a three-entry weights/branch array.
        target = validAttack(initial) ? initial : Attack::Flat;
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : branches)
        {
            b.jfet.reset();
            b.treble.reset();
            b.output = 0.0;
            b.portError = 0.0;
        }
        weights = oneHot(target);
        fadeStart = weights;
        fadePosition = fadeLength;
    }

    // A new switch request during a fade starts from the CURRENT blend weights,
    // not an old branch or an instantaneous coefficient jump. The next sample
    // still uses those weights; subsequent samples linearly approach the target.
    void selectAttack(Attack next) noexcept
    {
        // Ignore malformed enum casts without interrupting an ongoing fade.
        if (!validAttack(next) || next == target)
            return;
        target = next;
        fadeStart = weights;
        fadePosition = 0;
        if (fadeLength == 0)
        {
            weights = oneHot(target);
            fadePosition = fadeLength;
        }
    }

    // Input is the SAME input-buffer voltage to all three independent Q1s.
    // Output is one convex software blend, not a physical three-pole circuit.
    double process(double input) noexcept
    {
        double mixed = 0.0;
        for (std::size_t i = 0; i < branches.size(); ++i)
        {
            auto& b = branches[i];
            const auto port = b.treble.previewCoupledDrainPort();
            const double current = b.jfet.processCoupled(
                input, port.openCircuitVolts, port.theveninOhms);
            b.output = b.treble.process(current);
            b.portError = b.jfet.probeCoupledDrainVoltage()
                        - b.treble.probeLastDrainVoltage();
            mixed += weights[i] * b.output;
        }

        if (fadePosition < fadeLength)
        {
            ++fadePosition;
            const double t = static_cast<double>(fadePosition)
                           / static_cast<double>(fadeLength);
            const auto destination = oneHot(target);
            for (std::size_t i = 0; i < weights.size(); ++i)
                weights[i] = (1.0 - t) * fadeStart[i] + t * destination[i];
        }
        return mixed;
    }

    Attack selectedAttack() const noexcept { return target; }
    Weights blendWeights() const noexcept { return weights; }
    double branchOutput(Attack pos) const noexcept
    {
        return branches[index(pos)].output;
    }
    double branchPortError(Attack pos) const noexcept
    {
        return branches[index(pos)].portError;
    }
    double branchSourceVoltage(Attack pos) const noexcept
    {
        return branches[index(pos)].jfet.probePhysicalSourceVoltage();
    }

private:
    struct Branch
    {
        JfetStage jfet;
        TrebleAttack treble;
        double output = 0.0;
        double portError = 0.0;
    };

    static constexpr bool validAttack(Attack pos) noexcept
    {
        return pos == Attack::Boost || pos == Attack::Flat || pos == Attack::Cut;
    }

    static constexpr std::size_t index(Attack pos) noexcept
    {
        // Probes of an invalid position are read-only and refer to Flat.
        // The control-facing paths above always store a valid target.
        return pos == Attack::Boost ? 0U : pos == Attack::Cut ? 2U : 1U;
    }

    static constexpr Weights oneHot(Attack pos) noexcept
    {
        Weights w {};
        w[index(pos)] = 1.0;
        return w;
    }

    std::array<Branch, kBranches> branches {};
    Weights weights = oneHot(Attack::Flat);
    Weights fadeStart = weights;
    Attack target = Attack::Flat;
    unsigned fadeLength = 128;
    unsigned fadePosition = 128;
};
