// Adversarial research-stage parameter, knee, and mode-transition checks.
#include "../src/dsp/Baxandall.h"
#include "../src/dsp/MidBand.h"
#include "../src/dsp/RailClamp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

static void require(bool ok, const char* label)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
static bool exact(double x, double y) { return std::memcmp(&x,&y,sizeof(x)) == 0; }
static void invalidBax(bool coupled)
{
    Baxandall a,b;
    a.prepare(48000); b.prepare(48000);
    a.setRailClampEnabled(true); b.setRailClampEnabled(true);
    a.setRailAwarePassiveFeedback(true); b.setRailAwarePassiveFeedback(true);
    if (coupled) { a.setInputCap(100e-9); b.setInputCap(100e-9); }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for(double r : {0.0,-100.0,nan,inf,1.0e300}) a.setTrebleWiperR(r);
    for(double c : {nan,inf,0.5}) a.setInputCap(c);
    for(int i=0;i<512;++i)
    {
        const double x = 5*std::sin(i*.191), y=a.process(x),z=b.process(x);
        require(std::isfinite(y)&&exact(y,z),"Bax invalid setters must preserve previous circuit");
    }
}
static void invalidMid(bool coupled)
{
    MidBand a,b;
    a.configure(MidBand::kLoMid,MidBand::kLoMid47n);
    b.configure(MidBand::kLoMid,MidBand::kLoMid47n);
    a.prepare(48000); b.prepare(48000);
    a.setRailClampEnabled(true); b.setRailClampEnabled(true);
    a.setRailAwarePassiveFeedback(true); b.setRailAwarePassiveFeedback(true);
    if (coupled) { a.setInputCap(2.2e-6); b.setInputCap(2.2e-6); }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for(double c : {nan,inf,-0.1,0.5})
    {
        a.setSeriesCap(c); a.setAcrossCap(c);
        if(c>=0.0 || !std::isfinite(c)) a.setInputCap(c);
    }
    a.setWiperR(nan); a.setWiperR(inf);
    a.setOpenLoopGain(inf); a.setOpenLoopGain(std::numeric_limits<double>::max());
    for(int i=0;i<512;++i)
    {
        const double x = 5*std::sin(i*.139),y=a.process(x),z=b.process(x);
        require(std::isfinite(y)&&exact(y,z),"Mid invalid setters must preserve previous circuit");
    }
}
static void knee()
{
    RailClamp r;
    r.setEnabled(true); r.setRailVoltages(2.9,3.4); r.setKnee(.35);
    for(double x : {-3.25,-2.55,3.05,3.75})
        require(std::abs(r.process(x-1e-7)-r.process(x+1e-7))<3e-7,
                "both rail knees must be continuous");
    double prev=r.process(-100);
    for(int i=1;i<=10000;++i)
    {
        const double next=r.process(-100.0+200.0*i/10000.0);
        require(std::isfinite(next)&&next>=prev-1e-12, "rail clamp monotone");
        require(next>=-2.9-1e-12&&next<=3.4+1e-12,"rail clamp bounds");
        prev=next;
    }
    r.setRailVoltages(0,0);
    for(double x : {-1e6,-1.,-1e-8,0.,1e-8,1.,1e6})
        require(r.process(x)==0.,"zero headroom must output zero");
}
static void transitions()
{
    int nChecks=0;
    for(double fs : {44100.,192000.})
    {
        Baxandall bax; bax.prepare(fs);
        bax.setRailClampEnabled(true);
        MidBand mid; mid.configure(MidBand::kLoMid,MidBand::kLoMid47n);
        mid.prepare(fs); mid.setRailClampEnabled(true);
        for(int n=0;n<8192;++n)
        {
            if(n%127==0)
            {
                const bool mode=((n/127)&1)!=0;
                bax.setRailAwarePassiveFeedback(mode);
                mid.setRailAwarePassiveFeedback(mode);
                bax.setInputCap(mode?100e-9:0.);
                mid.setInputCap(mode?2.2e-6:0.);
                bax.setBass(mode?1e-6:1.-1e-6);
                bax.setTreble(mode?1.-1e-6:1e-6);
                mid.setPosition(mode?1e-6:1.-1e-6);
                mid.setOpenLoopGain(mode?1e3:1e5);
            }
            const double x=n%100==0?100.:n%100==1?-100.:8.*std::sin(.08*n);
            const double y=bax.process(x),z=mid.process(x);
            require(std::isfinite(y)&&std::isfinite(z),"rapid mode transitions finite");
            require(y>=-3.3-1e-10&&y<=3.3+1e-10,"Bax output rail bound");
            require(z>=-3.3-1e-10&&z<=3.3+1e-10,"Mid output rail bound");
            ++nChecks;
        }
    }
    std::printf("%d rapid rail/cap/mode samples PASS\n",nChecks);
}
int main()
{
    invalidBax(false);invalidBax(true);invalidMid(false);invalidMid(true);
    knee();transitions();
    std::puts("RailEdgeFuzzTest PASS");
}
