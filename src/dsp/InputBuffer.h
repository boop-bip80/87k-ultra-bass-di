#pragma once

#include <chowdsp_wdf/chowdsp_wdf.h>
#include <cmath>
#include <stdexcept>

// =============================================================================
// Stage 1 — Input buffer + bias network (IC1_A, R1/C1/R2/R3)
// =============================================================================
// circuit.md "Input & buffer (IC1_A)":
//   R1 = 1M   input pulldown to GND (sets ~1 MΩ input Z, pop suppression)
//   C1 = 100n input coupling
//   R2 = 1M   bias input node to VD
//   R3 = 10k  series into IC1_A (+)
//   IC1_A = TL072ACP unity-gain buffer; output splits into the OD path (C2)
//           and the clean tap (long wire to BLEND pin1).
//
// ---- Isolated linear transfer function (ideal source, unloaded output) ------
// The only frequency-shaping elements that affect the isolated stage response
// are C1 and R2:
//   * R1 (input pulldown) sits ACROSS the source. With an ideal voltage-source
//     drive it carries current but cannot change any downstream node voltage,
//     so it does not appear in the isolated TF. (It sets input impedance, which
//     only matters once a real, finite source impedance is modelled.)
//     Specifically, input Z is R1 || (R2 + 1/sC1): approximately 1 MΩ at DC
//     and 500 kΩ well above the coupling corner, NOT a constant 1 MΩ.
//   * R3 feeds the op-amp (+) input, which draws no current, so there is no
//     drop across R3 and it does not appear in the isolated TF either (it forms
//     only a negligible LP with the op-amp input capacitance).
//   * IC1_A is an ideal unity buffer (output Z ~ 0), so downstream loads (C2,
//     the clean tap) do not load this node — unity gain regardless.
//
// What remains is a single first-order high-pass formed by C1 into R2:
//        H(s) = s R2 C1 / (1 + s R2 C1)
//        fc   = 1 / (2 pi R2 C1) = 1 / (2 pi * 1e6 * 100e-9) ~= 1.5915 Hz
//   passband gain = unity (0 dB), non-inverting.
//
// WDF realisation: ideal voltage source -> series(C1, R2); the stage output is
// the voltage across R2 (the high-pass output = IC1_A output).
// =============================================================================
class InputBuffer
{
public:
    // Component values (circuit.md). Public so tests reference the single source
    // of truth rather than re-declaring their own (avoids silent oracle drift).
    static constexpr double kR2 = 1.0e6;    // bias resistor to VD -> HP resistor
    static constexpr double kC1 = 100.0e-9; // input coupling cap -> HP cap

    InputBuffer() = default;

    void prepare(double sampleRate)
    {
        c1.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        c1.reset();
    }

    // Process one sample (real volts in, real volts out). Non-inverting.
    inline double process(double x) noexcept
    {
        vs.setVoltage(x);

        vs.incident(p1.reflected());
        p1.incident(vs.reflected());

        // Output node = IC1_A (+) input = voltage across R2 (the HP output).
        return chowdsp::wdft::voltage<double>(r2);
    }

private:
    chowdsp::wdft::ResistorT<double> r2 { kR2 };
    chowdsp::wdft::CapacitorT<double> c1 { kC1 };
    chowdsp::wdft::WDFSeriesT<double, decltype(c1), decltype(r2)> s1 { c1, r2 };
    chowdsp::wdft::PolarityInverterT<double, decltype(s1)> p1 { s1 };
    chowdsp::wdft::IdealVoltageSourceT<double, decltype(p1)> vs { p1 };

    // JUCE-free (this header is compiled into pure chowdsp_wdf console tests too).
    InputBuffer(const InputBuffer&) = delete;
    InputBuffer& operator=(const InputBuffer&) = delete;
};

// =============================================================================
// OFFLINE SOURCE-LOADING DIAGNOSTIC — not used in the plugin processing path.
// =============================================================================
// A plugin receives a voltage already converted by an audio interface. Applying
// the pedal's analog input load to that recording as if it were a raw pickup
// Thevenin voltage would double-count (or misrepresent) loading. Use this only
// when an independently known source *open-circuit* voltage and source
// resistance are available, e.g. to compare the drawn input with a SPICE model.
//
// Circuit: Thevenin Vs -> Rsource -> [ R1 || (C1 in series with R2) ] -> GND.
// Output = voltage across R2; R3 and ideal op-amp input draw no current.
// For a purely resistive source, its continuous-time transfer function is:
//
//                R1 R2 s C1
// H(s) = ------------------------------------
//        Rsource + R1 + s C1 [Rsource(R1+R2)+R1 R2]
//
// The default Rsource=0 delegates to InputBuffer EXACTLY: this avoids a zero-
// ohm WDF resistor and preserves identical ideal-source behavior. Real passive
// pickups have *complex* source impedance (winding L and parasitic C), so a
// resistive source is a diagnostic only, not a validated pickup emulator.
class InputBufferSourceLoadProbe
{
public:
    static constexpr double kR1 = 1.0e6;
    static constexpr double kR2 = InputBuffer::kR2;
    static constexpr double kC1 = InputBuffer::kC1;

    // Must be non-negative and finite. Immutable after construction: neither
    // source resistance nor WDF structure changes while processing samples.
    explicit InputBufferSourceLoadProbe(double sourceResistanceOhms = 0.0)
        : sourceOhms(validateResistance(sourceResistanceOhms)),
          rsource(sourceOhms > 0.0 ? sourceOhms : 1.0)
    {
    }

    void prepare(double sampleRate)
    {
        idealSource.prepare(sampleRate);
        c1.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        idealSource.reset();
        c1.reset();
    }

    inline double process(double x) noexcept
    {
        if (sourceOhms == 0.0)
            return idealSource.process(x);

        vs.setVoltage(x);
        vs.incident(inv.reflected());
        inv.incident(vs.reflected());
        return -chowdsp::wdft::voltage<double>(r2);
    }

private:
    static double validateResistance(double ohms)
    {
        if (!std::isfinite(ohms) || ohms < 0.0)
            throw std::invalid_argument("Source load probe requires finite, non-negative source resistance");
        return ohms;
    }

    const double sourceOhms;
    InputBuffer idealSource;
    chowdsp::wdft::ResistorT<double> rsource;
    chowdsp::wdft::ResistorT<double> r1 { kR1 };
    chowdsp::wdft::ResistorT<double> r2 { kR2 };
    chowdsp::wdft::CapacitorT<double> c1 { kC1 };
    chowdsp::wdft::WDFSeriesT<double, decltype(c1), decltype(r2)> seriesC1R2 { c1, r2 };
    chowdsp::wdft::WDFParallelT<double, decltype(r1), decltype(seriesC1R2)> inputLoad { r1, seriesC1R2 };
    chowdsp::wdft::WDFSeriesT<double, decltype(rsource), decltype(inputLoad)> total { rsource, inputLoad };
    chowdsp::wdft::PolarityInverterT<double, decltype(total)> inv { total };
    chowdsp::wdft::IdealVoltageSourceT<double, decltype(inv)> vs { inv };

    InputBufferSourceLoadProbe(const InputBufferSourceLoadProbe&) = delete;
    InputBufferSourceLoadProbe& operator=(const InputBufferSourceLoadProbe&) = delete;
};

// =============================================================================
// OFFLINE NOMINAL-IMPEDANCE WHAT-IF REFERENCE — NOT A B7K ULTRA NETLIST.
// =============================================================================
// Darkglass lists 1 MΩ nominal INPUT impedance for the B7K Ultra, whereas the
// publicly drawn Black Mirror VII clone approaches 500 kΩ in the audio band.
// The manufacturer does NOT publish the Ultra's frequency-dependent impedance
// or input circuitry, so that single number cannot determine a unique R/C
// network. Keep the clone network above untouched.
//
// This independent behavioral *reference* holds the input port at a constant
// real impedance Znom while retaining the existing buffer's isolated C1/R2 HPF:
//
//   Hnom(s) = [Znom / (Rsource + Znom)] * [s R2 C1 / (1 + s R2 C1)]
//
// This is a valid sensitivity experiment: it answers how the known input
// high-pass *would* respond if fed through a nominally resistive 1 MΩ port.
// It does NOT assert that a real B7K Ultra has either this internal high-pass
// or the (hypothetical) isolation between port loading and coupling filter.
// Real pickup source impedances are reactive, not simply Rsource. Never place
// this probe in the recorded plugin signal path: that could double-count
// pickup loading already captured by an audio interface.
class InputBufferNominalPortProbe
{
public:
    static constexpr double kUltraPublishedNominalOhms = 1.0e6;

    explicit InputBufferNominalPortProbe(
        double sourceResistanceOhms = 0.0,
        double nominalInputOhms = kUltraPublishedNominalOhms)
        : sourceOhms(validateSource(sourceResistanceOhms)),
          nominalOhms(validateNominal(nominalInputOhms))
    {
        // Ratio form avoids overflow in (sourceOhms + nominalOhms) for large
        // but individually finite diagnostic impedances.
        attenuation = sourceOhms <= nominalOhms
            ? 1.0 / (1.0 + sourceOhms / nominalOhms)
            : (nominalOhms / sourceOhms) / (1.0 + nominalOhms / sourceOhms);
    }

    void prepare(double sampleRate) { isolatedHpf.prepare(sampleRate); }
    void reset() { isolatedHpf.reset(); }

    inline double process(double openCircuitSourceVolts) noexcept
    {
        return isolatedHpf.process(openCircuitSourceVolts * attenuation);
    }

    double nominalInputImpedanceOhms() const noexcept { return nominalOhms; }
    double sourceResistanceOhms() const noexcept { return sourceOhms; }
    double sourceAttenuation() const noexcept { return attenuation; }

private:
    static double validateSource(double ohms)
    {
        if (!std::isfinite(ohms) || ohms < 0.0)
            throw std::invalid_argument("Nominal port reference requires finite non-negative source resistance");
        return ohms;
    }
    static double validateNominal(double ohms)
    {
        if (!std::isfinite(ohms) || ohms <= 0.0)
            throw std::invalid_argument("Nominal port reference requires finite positive input impedance");
        return ohms;
    }

    const double sourceOhms, nominalOhms;
    double attenuation = 1.0;
    InputBuffer isolatedHpf;

    InputBufferNominalPortProbe(const InputBufferNominalPortProbe&) = delete;
    InputBufferNominalPortProbe& operator=(const InputBufferNominalPortProbe&) = delete;
};
