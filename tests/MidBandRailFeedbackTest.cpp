// Independent primitive-branch nodal oracle for the opt-in finite-gain
// feedback path.  This deliberately does not use MidBand's inverse matrices,
// MnaSolve, cached output sensitivity, or scalar closed-form solution.
#include "../src/dsp/MidBand.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char* label)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}

struct PrimitiveOracle
{
    MidBand::Values v = MidBand::kLoMid;
    double sampleRate = 48000.0, c33 = MidBand::kLoMid10n;
    double c31 = 0.0, rw = 0.0, a = 0.5, gain = 100000.0;
    double vneg = 3.3, vpos = 3.3, knee = 0.35;
    bool rails = true;
    double h31 = 0.0, h32 = 0.0, h33 = 0.0;
    std::array<double,5> lastNodes {};
    double lastOutput = 0.0, maxResidual = 0.0;

    double clamp(double x) const
    {
        if (!rails) return x;
        if (x > vpos - knee)
        {
            if (x >= vpos + knee) return vpos;
            const double d = x - vpos - knee;
            return vpos - d*d/(4*knee);
        }
        if (x < -vneg + knee)
        {
            if (x <= -vneg - knee) return -vneg;
            const double d = x + vneg + knee;
            return -vneg + d*d/(4*knee);
        }
        return x;
    }

    // Deterministic Gaussian elimination, performed afresh at every root
    // evaluation; it shares no factorisation or matrix arrangement with DSP.
    double solvePassive(double source, double output, std::array<double,5>& out) const
    {
        const bool has31 = c31 > 0;
        const int n = has31 ? 5 : 4;
        const int in = has31 ? 0 : 5;
        const int p3 = has31 ? 1 : 0;
        const int p1 = has31 ? 2 : 1;
        const int w  = has31 ? 3 : 2;
        const int m  = has31 ? 4 : 3;
        const int src = 6, drivenOut = 7;
        double y[5][6] = {};

        // Natural KCL companion: branch current a->b = g(Va-Vb)-history.
        auto stamp = [&](int na, int nb, double g, double history)
        {
            const auto known = [&](int node) -> double {
                return node == drivenOut ? output : source;
            };
            if (na < n)
            {
                y[na][na] += g;
                if (nb < n) y[na][nb] -= g;
                else y[na][n] += g*known(nb);
                y[na][n] += history;
            }
            if (nb < n)
            {
                y[nb][nb] += g;
                if (na < n) y[nb][na] -= g;
                else y[nb][n] += g*known(na);
                y[nb][n] -= history;
            }
        };

        const double g31 = 2*sampleRate*c31;
        const double g32 = 2*sampleRate*v.c32;
        const double g33 = 2*sampleRate*c33;
        const double damp = 1+g33*rw;
        stamp(in, p3, 1/v.r38, 0);
        stamp(p1, drivenOut, 1/v.r39, 0);
        stamp(m, drivenOut, 1/v.r40, 0);
        stamp(in, m, 1/v.r41, 0);
        stamp(p3, w, 1/(100000*std::clamp(a, 1e-6, 1-1e-6)), 0);
        stamp(w, p1, 1/(100000*(1-std::clamp(a, 1e-6, 1-1e-6))), 0);
        stamp(p3, p1, g32, h32);
        stamp(w, m, g33/damp, h33/damp);
        if (has31) stamp(src, in, g31, h31);

        for (int k=0; k<n; ++k)
        {
            int pivot=k;
            for(int j=k+1; j<n; ++j)
                if(std::abs(y[j][k]) > std::abs(y[pivot][k])) pivot=j;
            if(pivot != k)
                for(int j=k; j<=n; ++j) std::swap(y[k][j],y[pivot][j]);
            require(std::abs(y[k][k]) > 1e-15, "oracle singularity");
            for(int j=k+1; j<n; ++j)
            {
                const double q=y[j][k]/y[k][k];
                for(int z=k; z<=n; ++z) y[j][z] -= q*y[k][z];
            }
        }
        out.fill(0);
        for(int k=n-1; k>=0; --k)
        {
            double t=y[k][n];
            for(int j=k+1; j<n; ++j) t -= y[k][j]*out[j];
            out[k]=t/y[k][k];
        }
        return out[m];
    }

    double process(double source)
    {
        double lo=rails ? -vneg : -1.0e4;
        double hi=rails ? +vpos : +1.0e4;
        std::array<double,5> x {};
        for(int it=0;it<60;++it)
        {
            const double m=0.5*(lo+hi);
            const double vn=solvePassive(source,m,x);
            const double driven=clamp(-gain*vn);
            if(m>driven) hi=m;
            else lo=m;
        }
        lastOutput = 0.5*(lo+hi);
        solvePassive(source,lastOutput,lastNodes);
        const bool has31 = c31 > 0;
        const int p3=has31?1:0,p1=has31?2:1,w=has31?3:2,m=has31?4:3;
        const double vIn=has31?lastNodes[0]:source;
        const double g31=2*sampleRate*c31,g32=2*sampleRate*v.c32;
        const double g33=2*sampleRate*c33,damp=1+g33*rw;
        const double h33b=h33/damp;
        const double i33=(g33/damp)*(lastNodes[w]-lastNodes[m])-h33b;
        const double vCap33=(lastNodes[w]-lastNodes[m])-rw*i33;
        if(has31) h31=2*g31*(source-vIn)-h31;
        h32=2*g32*(lastNodes[p3]-lastNodes[p1])-h32;
        h33=2*g33*vCap33-h33;
        return lastOutput;
    }
};

static void testGrid()
{
    int cases=0;
    double maxError=0.0;
    for(bool hiMid : {false,true})
    for(bool c31on : {false,true})
    for(double fs : {48000.0,192000.0})
    for(double a : {0.0,0.025,0.5,0.975,1.0})
    for(double rw : {0.0,22000.0})
    for(int capIndex=0;capIndex<2;++capIndex)
    for(double gain : {1000.0,100000.0})
    for(bool rails : {false,true})
    {
        MidBand dut;
        PrimitiveOracle oracle;
        oracle.v=hiMid?MidBand::kHiMid:MidBand::kLoMid;
        const double c33=hiMid?(capIndex?MidBand::kHiMid820p:MidBand::kHiMid15n):
                             (capIndex?MidBand::kLoMid2n2:MidBand::kLoMid47n);
        oracle.sampleRate=fs; oracle.a=a; oracle.rw=rw;
        oracle.c33=c33; oracle.c31=c31on?2.2e-6:0;
        oracle.gain=gain;
        oracle.rails=rails;
        oracle.vneg=2.9; oracle.vpos=3.4; // asymmetric supply headroom
        dut.configure(oracle.v,c33);
        dut.setPosition(a);
        dut.setWiperR(rw);
        dut.setInputCap(oracle.c31);
        dut.setRailVoltages(oracle.vneg,oracle.vpos);
        dut.setRailClampEnabled(rails);
        dut.setOpenLoopGain(gain);
        dut.setRailAwarePassiveFeedback(true);
        dut.prepare(fs);
        for(int t=0;t<70;++t)
        {
            // Signed transients and nonlinear release, at extreme gain/drive.
            double v = (t<8 ? 5.0 : (t<20 ? 0.0 : (t<32 ? -5.0 :
                     (t<45 ? 0.0 : 0.8*std::sin(0.17*t)))));
            const double actual=dut.process(v), expected=oracle.process(v);
            const double err=std::abs(actual-expected);
            maxError=std::max(maxError,err);
            if(err>1e-8)
            {
                std::fprintf(stderr,"mismatch hi=%d c31=%d rails=%d fs=%g a=%g rw=%g C33=%g "
                         "gain=%g t=%d actual=%.16g oracle=%.16g err=%g\n",
                         hiMid,c31on,rails,fs,a,rw,c33,gain,t,actual,expected,err);
                std::exit(1);
            }
        }
        ++cases;
    }
    std::printf("Independent nodal oracle: %d cases * 70 samples, max |delta| %.3g V PASS\n",
                cases,maxError);
}

static void testLegacyUnchanged()
{
    for(bool c31on : {false,true})
    {
        MidBand a,b;
        for(auto* dut : {&a,&b})
        {
            dut->setInputCap(c31on?2.2e-6:0);
            dut->configure(MidBand::kLoMid,MidBand::kLoMid47n);
            dut->setPosition(0.7);
            dut->setRailClampEnabled(true);
            dut->prepare(48000);
        }
        b.setRailAwarePassiveFeedback(false);
        for(int i=0;i<500;++i)
        {
            const double vin=std::sin(i*0.4)*5.0;
            require(a.process(vin)==b.process(vin),"legacy opt-out not bit-exact");
        }
    }
    std::puts("Legacy opt-out (C31 on/off): bit-exact PASS");
}

static void testReleaseDifference()
{
    MidBand original, corrected;
    for(auto* dut : {&original,&corrected})
    {
        dut->configure(MidBand::kLoMid,MidBand::kLoMid47n);
        dut->setPosition(0.02);
        dut->setRailClampEnabled(true);
        dut->prepare(48000);
    }
    corrected.setRailAwarePassiveFeedback(true);
    const double oldPulse=original.process(5),newPulse=corrected.process(5);
    const double oldRelease=original.process(0),newRelease=corrected.process(0);
    std::printf("+5 V impulse: ideal+post-clamp = %.8f V, rail-aware = %.8f V; "
                "release old = %.8f V, new = %.8f V; delta = %.8f V\n",
                oldPulse,newPulse,oldRelease,newRelease,newRelease-oldRelease);
    require(std::abs(newRelease-oldRelease)>1e-4,"rail-aware release was vacuous");
}

int main()
{
    testLegacyUnchanged();
    testGrid();
    testReleaseDifference();
    std::puts("All finite-gain passive-feedback tests passed.");
}
