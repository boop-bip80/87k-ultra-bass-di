// Optional native perf probe; never a CTest correctness gate.
#include "../src/dsp/JfetStage.h"
#include "../src/dsp/TrebleAttack.h"
#include <chrono>
#include <cmath>
#include <cstdio>

int main()
{
    constexpr int count=300000;
    const double fs=192000.0;
    volatile double sink=0.0;
    const auto run=[&](bool coupled) {
        JfetStage j; j.prepare(fs);
        j.setPhysicalSourceFeedbackModel(true);
        TrebleAttack t; t.prepare(fs);
        t.setActiveLoad(1.0/0.7e-3, (500.0e3*22.0e-9)/0.7e-3);
        t.setQ2GateCapEnabled(true);
        if(coupled) t.setCoupledJfetDrainPort(true);
        const auto start=std::chrono::steady_clock::now();
        double acc=0.0;
        for(int n=0;n<count;++n)
        {
            const double x=1.7*std::sin(n*0.021)+0.2*std::sin(n*0.001);
            double i;
            if(coupled)
            {
                const auto p=t.previewCoupledDrainPort();
                i=j.processCoupled(x,p.openCircuitVolts,p.theveninOhms);
            }
            else i=j.process(x);
            acc+=t.process(i);
        }
        sink+=acc;
        const auto stop=std::chrono::steady_clock::now();
        return std::chrono::duration<double,std::nano>(stop-start).count()/count;
    };
    run(false); run(true);
    const double baseline=run(false), corrected=run(true);
    std::printf("J201+Treble 192kHz microbenchmark %d samples: uncoupled %.1f ns,"
                " coupled %.1f ns, ratio %.2fx, sink %.6f\n",
                count,baseline,corrected,corrected/baseline,sink);
}
