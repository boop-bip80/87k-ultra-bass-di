#include "../src/dsp/Clipper.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
int main()
{
  int errors=0,total=0; double worst=0.;
  for(double fs:{48000.,192000.})
    for(int device:{1,2})
      for(Clipper::Grunt g:{Clipper::Grunt::Cut,Clipper::Grunt::Flat,Clipper::Grunt::Boost})
      {
        Clipper d; d.prepare(fs);
        d.setPhysicalVtc(device);
        d.setGrunt(g);
        d.setDiodeCurrentModel(true);
        const double gc=2*fs*Clipper::gruntCap(g), dy=gc+1./6800.,
          gin=gc/(6800.*dy), gf=2*fs*220e-12+1./330000.,vm=d.physicalTripPoint();
        double eqg=0.,eqf=0.,last=0.;
        std::uint32_t rng=4411;
        for(int i=0;i<1000;++i)
        {
            rng=1664525*rng+1013904223;
            const double x=i%7==0 ? +1000 : i%7==1 ? -1000 : ((rng>>8)/16777215.-.5)*200.;
            const double y=d.process(x),w=d.probeNodeW();
            const double kcl=gin*(x-w)-eqg/(6800.*dy)+gf*(y-w)-eqf
                +5e-9*std::expm1(std::min(50.,(-w-vm)/.052))
                -5e-9*std::expm1(std::min(50.,(w+vm-9.)/.052));
            const double yExpect=d.probeVtc(w);
            worst=std::max(worst,std::abs(kcl));
            if(!std::isfinite(w)||!std::isfinite(y)||std::abs(kcl)>1e-9
               ||std::abs(y-yExpect)>1e-9)
            {
              if(errors++<5)std::printf("FAIL fs %.0f device %d g%d n%d residual %.4g ydiff %.4g\n",fs,device,int(g),i,kcl,y-yExpect);
            }
            double m=(gc*x-eqg+w/6800.)/dy;
            eqg=2*gc*(x-m)-eqg;
            eqf=2*(2*fs*220e-12)*(y-w)-eqf;
            last=w;
            (void)last;
            ++total;
        }
      }
  std::printf("physical VTC diode %d samples %d failures maxKCL %.3g A\n",total,errors,worst);
  return errors?1:0;
}
