// Independent scalar KCL + passive-port test for an opt-in, coupled
// J201 source/drain -> TrebleAttack branch (NOT retail-pedal calibration).
#include "../src/dsp/JfetStage.h"
#include "../src/dsp/TrebleAttack.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ref {
static constexpr double kBeta = 1.031e-3;
static constexpr double kPinch = 0.718;
static constexpr double kRo = JfetStage::kRo;
static constexpr double kR6 = JfetStage::kR6;

struct Step {
    double id, sourceV, drainV, drainResidual;
};

class Q1 {
public:
    explicit Q1(double fs)
        : gc2(2.0 * fs * JfetStage::kC2),
          gc3(2.0 * fs * JfetStage::kC3),
          rGate(1.0 / (JfetStage::kR4 + JfetStage::kR5))
    {
        // Independent self-bias reference: Vs/R6 = beta*(Vp-Vs)^2.
        double lo = 0.0, hi = kPinch;
        for (int i = 0; i < 110; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            const double residual = mid / kR6 - kBeta*(kPinch-mid)*(kPinch-mid);
            if (residual > 0.0) hi = mid;
            else lo = mid;
        }
        vov = kPinch - 0.5*(lo+hi);
        idq = kBeta*vov*vov;
    }

    Step process(double input, double open, double rPort)
    {
        const double vx = (gc2*input-eq2)/(gc2+rGate);
        eq2 = 2.0*gc2*(input-vx)-eq2;
        const double vg = vx * JfetStage::kR5/(JfetStage::kR4+JfetStage::kR5);
        const double baseG = 1.0/kR6+gc3;
        const double prevHistory = eq3;
        const auto residual = [&](double id) {
            const double vs = (id+prevHistory)/baseG;
            const double vd = open-rPort*id;
            const double od = std::max(0.0, vov+vg-vs);
            const double intrinsic = kBeta*od*od-idq;
            return id - intrinsic - (vd-vs)/kRo;
        };
        double lo=-1.0, hi=+1.0;
        if (!(residual(lo) < 0.0 && residual(hi) > 0.0))
        {
            std::fprintf(stderr,"Independent coupled bracket invalid\n");
            std::abort();
        }
        for(int it=0; it<110; ++it)
        {
            const double mid=0.5*(lo+hi);
            if (residual(mid)>0.0) hi=mid;
            else lo=mid;
        }
        const double id=0.5*(lo+hi);
        const double vs=(id+prevHistory)/baseG;
        const double vd=open-rPort*id;
        const double r=residual(id);
        eq3=2.0*gc3*vs-prevHistory;
        return {id,vs,vd,r};
    }

private:
    double gc2, gc3, rGate, vov=0.0, idq=0.0;
    double eq2=0.0, eq3=0.0;
};

void near(double actual,double expected,double tol,const char* label)
{
    if (!std::isfinite(actual) || std::abs(actual-expected)>tol)
    {
        std::fprintf(stderr, "%s: got %.16g, expected %.16g, err %.6g\n",
                     label, actual, expected, std::abs(actual-expected));
        std::exit(1);
    }
}
} // namespace ref

int main()
{
    int cases=0, portCases=0, totalIterations=0, maxIterations=0;
    double worstI=0.0,worstS=0.0,worstDrain=0.0,worstKcl=0.0;
    constexpr double kPi=3.14159265358979323846;
    for (const double fs: {44100.0,48000.0,96000.0,192000.0})
    for (int attack=0;attack<3;++attack)
    for (int q2=0;q2<3;++q2)
    {
        JfetStage jfet;
        jfet.prepare(fs);
        jfet.setPhysicalSourceFeedbackModel(true);

        TrebleAttack treble;
        treble.prepare(fs);
        treble.setCoupledJfetDrainPort(true);
        treble.setAttack(static_cast<TrebleAttack::Attack>(attack));
        if (q2 == 1)
            treble.setQ2GateCapEnabled(true);
        if (q2 == 2)
        {
            treble.setQ2GateCapEnabled(true);
            treble.setActiveLoad(1.0/0.7e-3, (500.0e3*22.0e-9)/0.7e-3);
        }
        ref::Q1 oracle(fs);
        for (int n=0;n<1200;++n)
        {
            // The passive port is linear but STATEFUL: its open voltage can
            // be nonzero from capacitor history even at zero JFET current.
            const auto port=treble.previewCoupledDrainPort();
            if (!(port.theveninOhms>0) || !std::isfinite(port.openCircuitVolts))
                return 1;

            if (n<30 || n%101==0)
            {
                // Verify the preview is an ACTUAL Thevenin affine law of
                // TrebleAttack's independent 8x8 solve, not a scalar guess.
                TrebleAttack z; z.prepare(fs); z=treble;
                TrebleAttack p; p.prepare(fs); p=treble;
                z.process(0.0);
                p.process(1e-6);
                ref::near(z.probeLastDrainVoltage(),port.openCircuitVolts,1e-9,
                          "port open voltage");
                ref::near(p.probeLastDrainVoltage()-z.probeLastDrainVoltage(),
                          port.theveninOhms*1e-6,1e-9,"port Thevenin slope");
                ++portCases;
            }
            const double input=1.7*std::sin(2*kPi*197.0*n/fs)
                              +0.63*std::cos(2*kPi*1043.0*n/fs)
                              +(n%251==0 ? 6.0 : (n%349==0 ? -5.0 : 0.0));
            const auto expected=oracle.process(input,port.openCircuitVolts,
                                               port.theveninOhms);
            const double iIn=jfet.processCoupled(input,port.openCircuitVolts,
                                                  port.theveninOhms);
            totalIterations += jfet.probeCoupledIterations();
            maxIterations = std::max(maxIterations, jfet.probeCoupledIterations());
            const double output=treble.process(iIn);
            (void)output;
            const double id=-iIn;
            const double ie=std::abs(id-expected.id);
            const double se=std::abs(jfet.probePhysicalSourceVoltage()-expected.sourceV);
            const double de=std::abs(treble.probeLastDrainVoltage()-expected.drainV);
            worstI=std::max(worstI,ie);
            worstS=std::max(worstS,se);
            worstDrain=std::max(worstDrain,de);
            worstKcl=std::max(worstKcl,std::abs(expected.drainResidual));
            if (!std::isfinite(output) || ie>1e-10 || se>1e-7 || de>1e-7)
            {
                std::fprintf(stderr,"FAIL fs%.0f attack%d q2%d n%d Id %.5g Vs %.5g Vd %.5g\n",
                             fs,attack,q2,n,ie,se,de);
                return 1;
            }
            ref::near(jfet.probeCoupledDrainVoltage(),
                      treble.probeLastDrainVoltage(),1e-8,"coupled drain consistency");
            ++cases;
        }
    }
    // Both new interfaces are inert when unused. Calling the research
    // entry point on a default JfetStage must delegate bit-exactly.
    JfetStage ordinary,coupled;
    ordinary.prepare(48000);
    coupled.prepare(48000);
    for (int n=0;n<15000;++n)
    {
        const double x=3.0*std::sin(n*0.13);
        const double a=ordinary.process(x);
        const double b=coupled.processCoupled(x,0.0,1000.0);
        if(std::memcmp(&a,&b,sizeof(double))!=0)
            return 1;
    }
    std::printf("Coupled J201 drain/source + Q2/treble: %d samples PASS,"
                " %d Thevenin checks; max Id %.3g A, source %.3g V,"
                " drain %.3g V, KCL %.3g A; Newton mean %.2f, max %d;"
                " default bit-exact PASS\n",
                cases,portCases,worstI,worstS,worstDrain,worstKcl,
                double(totalIterations)/cases,maxIterations);
}
