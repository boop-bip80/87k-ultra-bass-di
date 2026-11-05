// SchematicParityTest — the plugin, configured with FitParams::schematic(), against ngspice of the
// DRAWN schematic. Independent of every other test in tests/: their oracles (analysis/eq_reference.py)
// were written from the same reading of the circuit as the C++, this data comes from a netlist solved
// by ngspice (analysis/spice_reference/b7k_spice.py -> gen_reference.py -> tests/data/schematic_reference.h).
//
// What it gates, and what it deliberately does not:
//   1. CLEAN path (BLEND = clean): jack -> OUT, seven BASS/TREBLE/LO-MID/HI-MID settings, 31 Hz-4 kHz.
//      Drawn values, so this is a check of the IMPLEMENTATION, not of the shipped calibration.
//   2. OD path small-signal (BLEND = OD): five GRUNT/DRIVE settings, 40 Hz-4 kHz, skipping the
//      ~320 Hz ladder-notch region where a bin-level frequency offset swamps any dB comparison.
//      Needs the J201 Q2 active load, the physical-clipper-free linear region and C21 in the stack.
//   3. CD4049 clipper, LARGE signal: the plugin's physical VTC (simple two-MOSFET model, R19 solved
//      self-consistently) against an ngspice TRANSIENT of the drawn loop — swing and H1-H5.
//   4. The inverter's self-consistent bias point against circuit.md's derived numbers.
// The shipped (calibrated) defaults are NOT gated here: they are fitted to reference captures, not to
// the drawn circuit, and analysis/spice_reference/compare_*.py quantifies how far they sit from it.
#include "../src/dsp/Clipper.h"
#include "../src/dsp/FitParams.h"
#include "../src/dsp/PedalChain.h"
#include "data/schematic_reference.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what, double got, double want, double tol)
{
    if (!ok)
    {
        std::printf("  FAIL: %s  got %.4f want %.4f (tol %.3f)\n", what, got, want, tol);
        ++failures;
    }
}

// steady-state amplitude (V) at the test frequency via single-bin DFT over an integer number of cycles
static double toneGainDb(PedalChain& chain, double fs, double f, double inAmp, double warm, double nCycMin)
{
    chain.reset();
    const int nWarm = static_cast<int>(warm * fs);
    const int nCyc = std::max(static_cast<int>(nCycMin), static_cast<int>(f * 0.25));
    const int nMeas = static_cast<int>(std::round(nCyc * fs / f));
    double re = 0.0, im = 0.0;
    const double w = 2.0 * M_PI * f / fs;
    for (int i = 0; i < nWarm + nMeas; ++i)
    {
        const double x = inAmp * std::sin(w * i);
        const double y = chain.processSample(x);
        if (i >= nWarm)
        {
            const double k = i - nWarm;
            re += y * std::sin(w * i);
            im += y * std::cos(w * i);
            (void) k;
        }
    }
    const double amp = 2.0 * std::hypot(re, im) / nMeas;
    return 20.0 * std::log10(amp / inAmp + 1e-30);
}

static PedalChain::Params paramsFor(double bass, double treble, double lomid, double himid, double blend, double drive,
                                    int gruntApvts)
{
    PedalChain::Params p;
    p.master = 1.0;
    p.level = 1.0;
    p.blend = blend;
    p.drive = drive;
    // PedalChain::Params carries the EQ in DSP space (boost at zero): PluginProcessor::readParams() inverts the knob.
    p.lo = 1.0 - bass;
    p.hi = 1.0 - treble;
    p.loMid = 1.0 - lomid;
    p.hiMid = 1.0 - himid;
    p.attackIdx = 0;          // Flat
    p.gruntIdx = gruntApvts;  // APVTS order {Boost, Cut, Flat}
    p.loMidFreq = 2;
    p.hiMidFreq = 2;
    return p;
}

static double harmDb(const std::vector<double>& y, double fs, double f, int k, int cyc)
{
    const int n = static_cast<int>(std::round(cyc * fs / f));
    const int off = static_cast<int>(y.size()) - n;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double ph = 2.0 * M_PI * f * k * (off + i) / fs;
        re += y[static_cast<size_t>(off + i)] * std::sin(ph);
        im += y[static_cast<size_t>(off + i)] * std::cos(ph);
    }
    return 20.0 * std::log10(2.0 * std::hypot(re, im) / n + 1e-30);
}

int main()
{
    const double fs = 96000.0;
    PedalChain chain;
    chain.prepare(fs, fs);
    chain.setFitParams(FitParams::schematic());

    // ---- 1. clean path ------------------------------------------------------------------------
    double worstClean = 0.0;
    for (const auto& c : schematic_ref::clean)
    {
        chain.applyParams(paramsFor(c.bass, c.treble, c.lomid, c.himid, 0.0, 0.5, 1));
        for (int i = 0; i < schematic_ref::Freqs::nClean; ++i)
        {
            const double g = toneGainDb(chain, fs, schematic_ref::cleanF[i], 1e-3, 0.8, 6);
            const double d = std::abs(g - c.db[i]);
            worstClean = std::max(worstClean, d);
            char what[96];
            std::snprintf(what, sizeof what, "clean %s @ %.0f Hz", c.name, schematic_ref::cleanF[i]);
            check(d <= 0.25, what, g, c.db[i], 0.25);
        }
    }
    std::printf("  [info] clean path worst |plugin - ngspice| = %.3f dB over %zu settings\n", worstClean,
                sizeof(schematic_ref::clean) / sizeof(schematic_ref::clean[0]));

    // ---- 2. overdrive path, small signal ------------------------------------------------------
    double worstOd = 0.0;
    for (const auto& c : schematic_ref::od)
    {
        const int apvts = (c.grunt[0] == 'b') ? 0 : (c.grunt[0] == 'c') ? 1 : 2;
        chain.applyParams(paramsFor(.5, .5, .5, .5, 1.0, c.drive, apvts));
        for (int i = 0; i < schematic_ref::Freqs::nOd; ++i)
        {
            const double g = toneGainDb(chain, fs, schematic_ref::odF[i], 5e-4, 0.8, 6);
            const double d = std::abs(g - c.db[i]);
            worstOd = std::max(worstOd, d);
            char what[96];
            std::snprintf(what, sizeof what, "OD %s drive=%.1f @ %.0f Hz", c.grunt, c.drive, schematic_ref::odF[i]);
            check(d <= 1.5, what, g, c.db[i], 1.5);
        }
    }
    std::printf("  [info] OD small-signal worst |plugin - ngspice| = %.3f dB over %zu settings\n", worstOd,
                sizeof(schematic_ref::od) / sizeof(schematic_ref::od[0]));

    // ---- 3. clipper, large signal, vs ngspice transient ---------------------------------------
    const double cfs = 192000.0;
    for (const auto& c : schematic_ref::clip)
    {
        Clipper clip;
        clip.prepare(cfs);
        clip.setPhysicalVtc(1); // simple two-MOSFET model = what ngspice's LEVEL=1 pair reproduces
        clip.setADAA(Clipper::Adaa::Off);
        clip.setC11(4.7e-9);
        clip.setC12(47e-9);
        clip.setC13(220e-9);
        clip.setR16(6.8e3);
        clip.setGrunt(c.grunt[0] == 'b' ? Clipper::Grunt::Boost : c.grunt[0] == 'f' ? Clipper::Grunt::Flat
                                                                                       : Clipper::Grunt::Cut);
        const int nTot = static_cast<int>(cfs * 0.05), n0 = static_cast<int>(cfs * 0.03);
        std::vector<double> y(static_cast<size_t>(nTot - n0));
        double lo = 1e9, hi = -1e9;
        for (int i = 0; i < nTot; ++i)
        {
            const double v = clip.process(c.amp * std::sin(2.0 * M_PI * c.freq * i / cfs));
            if (i >= n0)
            {
                y[static_cast<size_t>(i - n0)] = v;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
        const double pp = hi - lo;
        char what[112];
        std::snprintf(what, sizeof what, "clipper %s %.0f Hz amp %.2f: p-p swing", c.grunt, c.freq, c.amp);
        check(std::abs(pp / c.pp - 1.0) <= 0.02, what, pp, c.pp, 0.02 * c.pp);
        for (int k = 1; k <= 5; ++k)
        {
            const double g = harmDb(y, cfs, c.freq, k, 4);
            const double ref = c.h[k - 1];
            if (ref < -60.0)
                continue; // sub-0.1 % harmonics at the p-channel's saturation kink are table-resolution limited
            std::snprintf(what, sizeof what, "clipper %s %.0f Hz amp %.2f: H%d", c.grunt, c.freq, c.amp, k);
            check(std::abs(g - ref) <= 1.5, what, g, ref, 1.5);
        }
    }

    // ---- 3b. end-to-end large signal (the whole OD chain incl. the J201 shaper, rails and bridged-T) --------
    // The plugin must use the SIMPLE two-MOSFET model here (clipPhysical = 1): that is the device model
    // ngspice runs. (The schematic() default is the paper's extended model, whose low-level H3 is ~20 dB
    // lower — a difference between two published models of the CHIP, not an error of either implementation.)
    {
        PedalChain big;
        big.prepare(fs, fs);
        FitParams fp = FitParams::schematic();
        fp.clipPhysical = 1;
        big.setFitParams(fp);
        big.applyParams(paramsFor(.5, .5, .5, .5, 1.0, 0.5, 1));
        double worstH1 = 0.0, worstH3 = 0.0;
        int caseIdx = -1;
        for (const auto& c : schematic_ref::chain)
        {
            ++caseIdx;
            big.reset();
            const int nWarm = static_cast<int>(0.45 * fs);
            const int nMeas = static_cast<int>(std::round(40.0 * fs / c.freq)); // integer cycles
            std::vector<double> y(static_cast<size_t>(nMeas));
            for (int i = 0; i < nWarm + nMeas; ++i)
            {
                const double v = big.processSample(c.ampV * std::sin(2.0 * M_PI * c.freq * i / fs));
                if (i >= nWarm)
                    y[static_cast<size_t>(i - nWarm)] = v;
            }
            char what[112];
            for (int k : { 1, 2, 3 })
            {
                const double g = harmDb(y, fs, c.freq, k, 40);
                const double ref = c.h[k - 1];
                // H2 is gated only at the two lowest levels (-30/-24 dBFS), where it tracks to ~0.6 dB. From
                // -18 dBFS up it does NOT: two even-order sources (J201 and clipper) cancel near -18 dBFS, so a dB
                // comparison there only measures where the null lands (ngspice -44 dB, plugin -60 dB), and at
                // -6 dBFS the plugin's H2 is ~6 dB high (docs/accuracy-audit.md §7). H1 and H3 are gated everywhere.
                if (ref < -60.0 || (k == 2 && caseIdx > 1))
                    continue;
                const double tol = (k == 1) ? 2.0 : 2.5;
                std::snprintf(what, sizeof what, "chain 440 Hz amp %.3f V: H%d", c.ampV, k);
                check(std::abs(g - ref) <= tol, what, g, ref, tol);
                if (k == 1) worstH1 = std::max(worstH1, std::abs(g - ref));
                if (k == 3) worstH3 = std::max(worstH3, std::abs(g - ref));
            }
        }
        std::printf("  [info] end-to-end large signal worst |dH1| = %.2f dB, |dH3| = %.2f dB\n", worstH1, worstH3);
    }

    // ---- 4. inverter bias point (circuit.md's derived self-consistent numbers) ----------------
    {
        const auto t1 = cmosvtc::buildTable(cmosvtc::Device::Simple, 8.65, 1.0e3, -4.0, 8.0);
        check(std::abs(t1->vm - 2.657) < 0.003, "simple model trip point (circuit.md 2.657 V)", t1->vm, 2.657, 0.003);
        check(std::abs(t1->vdd0 - 5.636) < 0.005, "simple model dropped rail (circuit.md 5.636 V)", t1->vdd0, 5.636, 0.005);
        check(std::abs(t1->idd0 * 1e3 - 3.01) < 0.02, "simple model supply current (circuit.md 3.01 mA)",
              t1->idd0 * 1e3, 3.01, 0.02);
        const auto t2 = cmosvtc::buildTable(cmosvtc::Device::Extended, 8.65, 1.0e3, -4.0, 8.0);
        check(t2->vm > 2.5 && t2->vm < 2.75, "extended model trip point in 2.5..2.75 V", t2->vm, 2.6, 0.15);
        check(t2->vdd0 > 5.4 && t2->vdd0 < 6.2, "extended model dropped rail in 5.4..6.2 V", t2->vdd0, 5.8, 0.4);
        // VTC must be monotone decreasing everywhere (the solver's uniqueness argument needs it)
        bool mono = true;
        for (double w = -3.9; w < 7.9; w += 0.001)
            if (t2->eval(w + 0.001) > t2->eval(w) + 1e-9) { mono = false; break; }
        check(mono, "extended VTC monotone decreasing", 0, 0, 0);
    }

    std::printf("%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
