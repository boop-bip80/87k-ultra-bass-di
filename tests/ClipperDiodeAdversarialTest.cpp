// Independent nodal, companion-state and ADAA oracle for opt-in D1/D2 currents.
// Build with -I path/to/proposed/implementation so the test never patches DUT.
#include "../src/dsp/Clipper.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
constexpr double a0 = 25.0;
constexpr double satHi = 3.85, satLo = 3.15;
constexpr double vm = 2.657;
struct DiodeParams { double is, nvt, rail; };
double sigma(double z) { return z / std::sqrt(1.0 + z*z); }
double vtc(double w) { return w >= 0. ? -satLo*sigma(a0*w/satLo) : satHi*sigma(-a0*w/satHi); }
double prim(double w)
{
    const double s = w >= 0. ? satLo : satHi, z = a0*w/s;
    return -(s*s/a0)*(std::sqrt(1.+z*z)-1.);
}
double vtcSub(double w, double last, int mode)
{
    if(mode==0) return vtc(w);
    const double dw = w-last;
    const double bar = std::abs(dw) < 1e-7 ? vtc(.5*(w+last)) : (prim(w)-prim(last))/dw;
    return mode==2 ? bar-.5*a0*dw : bar;
}
double diodeCurrents(double w, const DiodeParams& p)
{
    const double d2 = p.is*std::expm1(std::min(50.,(-w-vm)/p.nvt));
    const double d1 = p.is*std::expm1(std::min(50.,(w+vm-p.rail)/p.nvt));
    return d2-d1;
}
struct Oracle
{
    double fs, gcG, dNode, gin, gc14, gfb, eqG=0., eq14=0., last=0.;
    int mode;
    DiodeParams params;
    explicit Oracle(double f,int m,DiodeParams p) : fs(f),gcG(2*f*4.7e-9),
        dNode(gcG+1./6800.),gin(gcG/(6800.*dNode)),gc14(2*f*220e-12),
        gfb(1./330000.+gc14),mode(m),params(p){}
    double F(double w,double x) const
    {
        return gin*(x-w)-eqG/(6800.*dNode)+gfb*(vtcSub(w,last,mode)-w)
               -eq14+diodeCurrents(w,params);
    }
    double root(double x) const
    {
        double lo=-10000.,hi=10000.;
        if (!(F(lo,x)>0 && F(hi,x)<0)) return std::numeric_limits<double>::quiet_NaN();
        for(int i=0;i<150;++i)
        {
            const double m=lo+.5*(hi-lo);
            if(F(m,x)>=0) lo=m;
            else hi=m;
        }
        return lo+.5*(hi-lo);
    }
    void update(double x,double w,double y)
    {
        const double m=(gcG*x-eqG+w/6800.)/dNode;
        eqG=2*gcG*(x-m)-eqG;
        eq14=2*gc14*(y-w)-eq14;
        last=w;
    }
};
}

int main()
{
    int cases=0, failures=0;
    double maxErrorW=0.,maxErrorY=0.,maxKcl=0.;
    std::uint32_t seed=0x2847a1u;
    // The nominal illustrative diode plus all eight corners of the validated
    // numerical research envelope (Is, nVT, rail), not actual retail tolerances.
    const DiodeParams parameters[] = {
        {5e-9, .052, 9.0},
        {1e-15, .005, 3.157}, {1e-15, .005, 9.0},
        {1e-15, .25, 3.157}, {1e-15, .25, 9.0},
        {1e-3, .005, 3.157}, {1e-3, .005, 9.0},
        {1e-3, .25, 3.157}, {1e-3, .25, 9.0},
    };
    for (const DiodeParams p : parameters)
    for (const double fs : {44100.,48000.,96000.,192000.,384000.})
        for (int m=0;m<3;++m)
            for (const int gruntMode : {0,1,2})
            {
                Clipper dut;
                dut.prepare(fs);
                dut.setADAA(static_cast<Clipper::Adaa>(m));
                dut.setGrunt(static_cast<Clipper::Grunt>(gruntMode));
                dut.setDiodeCurrentModel(true,p.is,p.nvt,p.rail);
                Oracle ref(fs,m,p);
                // Recalculate only the GRUNT-dependent source conductance.
                const double cg=Clipper::gruntCap(static_cast<Clipper::Grunt>(gruntMode));
                ref.gcG=2*fs*cg;
                ref.dNode=ref.gcG+1./6800.;
                ref.gin=ref.gcG/(6800.*ref.dNode);
                for (int n=0;n<1500;++n)
                {
                    const double x =
#ifdef DIODE_STRESS_SINE
                        1000.0*std::sin(n*.637) + 17.0*std::cos(n*.115);
#else
                        n==0 ? -100. : n==1 ? 100. : n==2 ? 0. :
                        n==3 ? 1e-12 : n==4 ? -1e-12 : n==5 ? -1000. :
                        n==6 ? 1000. : n==7 ? 12. : n==8 ? -8. :
                        (seed=1664525u*seed+1013904223u,((seed>>8)/16777215.-.5)*200.);
#endif
                    const double wRef=ref.root(x);
                    const double yRef=vtcSub(wRef,ref.last,m);
                    const double y=dut.process(x);
                    const double w=dut.probeNodeW();
                    const double residual=ref.F(w,x);
                    const double ew=std::abs(w-wRef), ey=std::abs(y-yRef), er=std::abs(residual);
                    maxErrorW=std::max(maxErrorW,ew);
                    maxErrorY=std::max(maxErrorY,ey);
                    maxKcl=std::max(maxKcl,er);
                    if(!std::isfinite(w)||!std::isfinite(y)||!std::isfinite(wRef)
                        ||ew>1e-6 ||ey>1e-5 ||er>1e-9)
                    {
                        if(failures++<12)
                            std::printf("FAIL fs=%.0f ADAA=%d grunt=%d n=%d x=%.9g w=%.9g want=%.9g y=%.9g wantY=%.9g F=%.9g\n",
                                fs,m,gruntMode,n,x,w,wRef,y,yRef,residual);
                    }
                    ++cases;
#ifdef DIODE_FREE_RUNNING
                    ref.update(x,wRef,yRef);
#else
                    ref.update(x,w,y);
#endif
                }
            }
    std::printf("%d cases, %d failures, maxW=%.3g maxY=%.3g maxKCL=%.3gA\n",
       cases,failures,maxErrorW,maxErrorY,maxKcl);
    return failures!=0;
}
