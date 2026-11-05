// Adversarial electrical KCL regression for the OPT-IN coupled Q1/Q2 model.
// Deliberately uses implausible +/-60 V inputs to test solver robustness;
// passing this test does not imply audio fidelity, rail compliance or retail
// hardware parity. The independent monitor checks the ORIGINAL node equations,
// not a copy of the solver's root-finding algorithm.
#include "../src/dsp/JfetStage.h"
#include "../src/dsp/TrebleAttack.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kBeta = 1.031e-3;
constexpr double kPinch = 0.718;

class ElectricalMonitor {
public:
    explicit ElectricalMonitor(double fs)
        : gc2(2.0*fs*JfetStage::kC2), gc3(2.0*fs*JfetStage::kC3),
          gRin(1.0/(JfetStage::kR4+JfetStage::kR5))
    {
        // Independently re-solve the assumed J201's DC R6 self-bias.
        double lo=0.0, hi=kPinch;
        for (int n=0;n<110;++n)
        {
            const double mid=0.5*(lo+hi);
            if (mid/JfetStage::kR6 > kBeta*(kPinch-mid)*(kPinch-mid))
                hi=mid;
            else lo=mid;
        }
        vov=kPinch-0.5*(lo+hi);
        idq=kBeta*vov*vov;
    }

    double normalizedKcl(double input, double id, double vs, double vd,
                         double open, double rth)
    {
        const double vx=(gc2*input-history2)/(gc2+gRin);
        history2=2.0*gc2*(input-vx)-history2;
        const double vg=vx*JfetStage::kR5/(JfetStage::kR4+JfetStage::kR5);
        const double oldC3=history3;
        history3=2.0*gc3*vs-oldC3;
        const double source=(1.0/JfetStage::kR6+gc3)*vs-oldC3-id;
        const double overdrive=std::max(0.0,vov+vg-vs);
        const double expectedId=kBeta*overdrive*overdrive-idq;
        const double drain=id-expectedId-(vd-vs)/JfetStage::kRo;
        const double port=vd-(open-rth*id);
        return std::max({
            std::abs(source)/(1.0+std::abs(id)+std::abs(oldC3)),
            std::abs(drain)/(1.0+std::abs(id)+kBeta*overdrive*overdrive),
            std::abs(port)/(1.0+std::abs(vd)+std::abs(open)+std::abs(rth*id))
        });
    }

private:
    double gc2,gc3,gRin,vov=0.0,idq=0.0;
    double history2=0.0,history3=0.0;
};

[[noreturn]] void fail(const char* description,double fs,int mode,int sample,
                       double value)
{
    std::fprintf(stderr,"FAIL %s fs%.0f mode%d sample%d value%.16g\n",
                 description,fs,mode,sample,value);
    std::exit(1);
}
} // namespace

int main()
{
    int samples=0,portChecks=0,maxIterations=0;
    double worstKcl=0.0,worstPort=0.0;
    for (const double fs: {8000.0,44100.0,48000.0,96000.0,192000.0,384000.0})
    for (int mode=0;mode<4;++mode)
    {
        JfetStage q1;
        q1.prepare(fs);
        q1.setPhysicalSourceFeedbackModel(true,kBeta,-kPinch);
        TrebleAttack net;
        net.prepare(fs);
        net.setCoupledJfetDrainPort(true);
        net.setQ2GateCapEnabled((mode&1)!=0);
        if (mode&2)
            net.setActiveLoad(1.0/0.7e-3,500.0e3*TrebleAttack::kC4/0.7e-3);
        net.setAttackTap(470e3,506e3,78.5e3,212e3);
        net.setNotchLeg(TrebleAttack::Attack::Boost,20.8e-9,478);
        net.setNotchLeg(TrebleAttack::Attack::Flat,19.7e-9,6140);
        net.setNotchLeg(TrebleAttack::Attack::Cut,22.4e-9,6040);
        ElectricalMonitor monitor(fs);
        for (int n=0;n<4096;++n)
        {
            if (n%257==0)
                net.setAttack(static_cast<TrebleAttack::Attack>((n/257)%3));
            if (n>0 && n%509==0)
                net.setQ2GateCapEnabled(!((n/509)%2));
            if (n>0 && n%701==0)
            {
                if ((n/701)%2)
                    net.setActiveLoad(1.0/0.7e-3,500.0e3*TrebleAttack::kC4/0.7e-3);
                else net.setActiveLoad(0,0);
            }
            const double input=(n%64==0 ? ((n/64)%2 ? -60.0 : 60.0)
                               : 0.7*std::sin(2.0*kPi*0.499*n)
                               + 0.9*std::sin(2.0*kPi*43.0*n/fs));
            const auto port=net.previewCoupledDrainPort();
            if (!std::isfinite(port.openCircuitVolts) ||
                !std::isfinite(port.theveninOhms) || port.theveninOhms<=0.0)
                fail("nonfinite passive port",fs,mode,n,port.theveninOhms);
            if (n%83==0)
            {
                // Affine-port check against three independent copies of the
                // entire passive nodal network with preserved C histories.
                TrebleAttack zero,plus,minus;
                zero.prepare(fs);
                plus.prepare(fs);
                minus.prepare(fs);
                zero=net;
                plus=net;
                minus=net;
                zero.process(0.0);
                plus.process(1e-8);
                minus.process(-1e-8);
                const double portErr=std::max({
                    std::abs(zero.probeLastDrainVoltage()-port.openCircuitVolts),
                    std::abs(plus.probeLastDrainVoltage()-zero.probeLastDrainVoltage()
                             -1e-8*port.theveninOhms),
                    std::abs(minus.probeLastDrainVoltage()-zero.probeLastDrainVoltage()
                             +1e-8*port.theveninOhms)
                });
                worstPort=std::max(worstPort,portErr);
                if (portErr>1e-7)
                    fail("Thevenin affine KCL",fs,mode,n,portErr);
                ++portChecks;
            }
            const double id=-q1.processCoupled(input,port.openCircuitVolts,
                                                port.theveninOhms);
            const double output=net.process(-id);
            const double vs=q1.probePhysicalSourceVoltage();
            const double vd=q1.probeCoupledDrainVoltage();
            if (!std::isfinite(output) || std::abs(vd-net.probeLastDrainVoltage())>1e-7)
                fail("nonlinear/passive drain agreement",fs,mode,n,
                     vd-net.probeLastDrainVoltage());
            const double kcl=monitor.normalizedKcl(input,id,vs,vd,
                                                   port.openCircuitVolts,
                                                   port.theveninOhms);
            worstKcl=std::max(worstKcl,kcl);
            if (!std::isfinite(kcl) || kcl>1e-10)
            {
                std::fprintf(stderr,"Id%.12g Vs%.12g Vd%.12g Vo%.12g Rth%.12g "
                            "solver iterations%d\n",id,vs,vd,
                            port.openCircuitVolts,port.theveninOhms,
                            q1.probeCoupledIterations());
                fail("original nonlinear KCL",fs,mode,n,kcl);
            }
            maxIterations=std::max(maxIterations,q1.probeCoupledIterations());
            ++samples;
        }
    }
    // Invalid ports must select the original physical-source path bit-exactly.
    JfetStage ordinary,coupled;
    ordinary.prepare(48000);
    coupled.prepare(48000);
    ordinary.setPhysicalSourceFeedbackModel(true);
    coupled.setPhysicalSourceFeedbackModel(true);
    for (int n=0;n<4000;++n)
    {
        const double input=std::sin(0.317*n);
        const double a=ordinary.process(input);
        const double b=coupled.processCoupled(input,0.0,-1.0);
        if (std::memcmp(&a,&b,sizeof(a))!=0)
            fail("invalid port bit-exact fallback",48000,0,n,a-b);
    }
    std::printf("Coupled stress KCL: %d samples, %d port checks, "
                "worst normalized KCL %.3g, port %.3g V, "
                "max solver iterations %d, invalid-port fallback bit-exact PASS\n",
                samples,portChecks,worstKcl,worstPort,maxIterations);
}
