#!/usr/bin/env python3
"""Deterministic public-data/assumption sensitivity study for the clone circuit.

NOT a Darkglass B7K Ultra transfer model, a circuit-identification algorithm,
or a measurement-based accuracy score. No hardware recordings are used.

Run: python3 analysis/circuit_uncertainty.py --json
Pure stdlib, deliberately independent of the production DSP solver.
"""

import argparse
import cmath
import json
import math


# PCB Guitar Mania Black Mirror VII 1.6v component values, NOT authenticated
# Darkglass retail values. Only the *bounds* of IDSS and VGS(off) below are
# sourced from a public J201 manufacturer data sheet; there is no measured
# joint distribution or guarantee any particular pair occurs together.
R6_OHM = 3300.0
C3_F = 220e-9
J201_IDSS_A = (0.2e-3, 0.6e-3, 1.0e-3)
J201_VP_V = (0.3, 0.9, 1.5)  # magnitude of VGS(off)


def source_attenuation(source_ohm, input_ohm):
    """Voltage divider for an illustrative *resistive* input/load pair."""
    if not all(math.isfinite(x) for x in (source_ohm, input_ohm)):
        raise ValueError("impedances must be finite")
    if source_ohm < 0.0 or input_ohm <= 0.0:
        raise ValueError("source must be nonnegative and load must be positive")
    if source_ohm <= input_ohm:
        return 1.0 / (1.0 + source_ohm / input_ohm)
    ratio = input_ohm / source_ohm
    return ratio / (1.0 + ratio)


def jfet_beta(idss_a, vp_v):
    if not all(math.isfinite(x) and x > 0.0 for x in (idss_a, vp_v)):
        raise ValueError("JFET IDSS and cutoff magnitude must be finite positive")
    return idss_a / (vp_v * vp_v)


def jfet_bias(vp_v, beta, source_ohm=R6_OHM):
    """Unique long-channel self-bias Vs=R6*beta*(Vp-Vs)^2, bracketed."""
    if not all(math.isfinite(x) and x > 0 for x in (vp_v, beta, source_ohm)):
        raise ValueError("invalid JFET self-bias parameters")
    lo, hi = 0.0, vp_v
    for _ in range(80):
        mid = (lo + hi) * 0.5
        residual = mid / source_ohm - beta * (vp_v - mid) ** 2
        if residual > 0.0:
            hi = mid
        else:
            lo = mid
    return (lo + hi) * 0.5


def jfet_incremental_gm(idss_a, vp_v):
    beta = jfet_beta(idss_a, vp_v)
    vs = jfet_bias(vp_v, beta)
    return 2.0 * beta * (vp_v - vs)


def jfet_transfer(freq_hz, idss_a, vp_v, ro_ohm=200e3):
    """Illustrative Q1 gate-to-drain incremental Gm; Q2/load NOT included."""
    if not (math.isfinite(freq_hz) and freq_hz >= 0.0
            and math.isfinite(ro_ohm) and ro_ohm > 0.0):
        raise ValueError("frequency/ro out of range")
    gm = jfet_incremental_gm(idss_a, vp_v)
    zs = R6_OHM / (1.0 + 1j * 2.0 * math.pi * freq_hz * R6_OHM * C3_F)
    return gm / (1.0 + (gm + 1.0 / ro_ohm) * zs)


def jfet_harmonics(idss_a, vp_v, gate_peak_v, sample_rate=32768, freq_hz=512):
    """Incremental Q1 current harmonics for ideal R6/C3 square-law source KCL.

    This assumes Q1 remains in saturation and deliberately omits VDS/Q2,
    input coupling, pickup loading, and the following treble network. It is a
    sensitivity witness for the *assumed device law*, not a prediction of the
    measured pedal's output harmonics.
    """
    if not (math.isfinite(gate_peak_v) and gate_peak_v > 0.0
            and isinstance(sample_rate, int) and sample_rate >= 8000
            and isinstance(freq_hz, int) and 0 < freq_hz < sample_rate / 6
            and (2048 * freq_hz) % sample_rate == 0):
        raise ValueError("positive gate drive and coherent integer sample grid required")
    beta = jfet_beta(idss_a, vp_v)
    bias_vs = jfet_bias(vp_v, beta)
    vov = vp_v - bias_vs
    iq = bias_vs / R6_OHM
    gcap = 2.0 * sample_rate * C3_F
    gbase = 1.0 / R6_OHM + gcap
    vs, hist = 0.0, 0.0
    warmup = 1024
    capture = 2048
    output = []
    for n in range(warmup + capture):
        vg = gate_peak_v * math.sin(2.0 * math.pi * freq_hz * n / sample_rate)

        def current(source_v):
            return beta * max(0.0, vov + vg - source_v) ** 2 - iq

        def residual(source_v):
            return gbase * source_v - hist - current(source_v)

        f = residual(vs)
        if f > 0.0:
            lo, hi = vs - f / gbase, vs
        else:
            lo, hi = vs, vs - f / gbase
        for _ in range(42):
            middle = 0.5 * (lo + hi)
            if residual(middle) > 0.0:
                hi = middle
            else:
                lo = middle
        vs = 0.5 * (lo + hi)
        observed = current(vs)
        hist = 2.0 * gcap * vs - hist
        if n >= warmup:
            output.append(observed)

    amplitudes = []
    for order in (1, 2, 3):
        omega = 2.0 * math.pi * freq_hz * order / sample_rate
        projection = sum(value * cmath.exp(-1j * omega * idx)
                         for idx, value in enumerate(output))
        amplitudes.append(2.0 * abs(projection) / capture)
    return {
        "h1_amp_a": amplitudes[0],
        "h2_db_re_h1": 20.0 * math.log10(max(1e-30, amplitudes[1] / amplitudes[0])),
        "h3_db_re_h1": 20.0 * math.log10(max(1e-30, amplitudes[2] / amplitudes[0])),
    }


def assumed_diode_forward_v(current_a, saturation_a, nvt_v):
    """Shockley forward voltage; Is/nVT are NOT identified by 1N4148's BOM."""
    if not all(math.isfinite(x) and x > 0 for x in
               (current_a, saturation_a, nvt_v)):
        raise ValueError("diode parameters must be finite and positive")
    return nvt_v * math.log1p(current_a / saturation_a)


def illustrative_opamp_output(input_v, open_loop_gain, feedback_factor, rail_v,
                              knee_v=0.35):
    """One generic feedback loop with the repo's *chosen* soft rail law.

    Feedback factor 0.1 and rail/gain sweep are explicitly demonstration
    parameters, not a traced Baxandall/MidBand circuit.
    """
    if not (all(math.isfinite(x) for x in
                (input_v, open_loop_gain, feedback_factor, rail_v, knee_v))
            and open_loop_gain > 0 and 0 < feedback_factor < 1
            and knee_v >= 0 and rail_v > knee_v):
        raise ValueError("invalid illustrative feedback/rail")
    ideal = open_loop_gain * input_v / (1.0 + open_loop_gain * feedback_factor)
    if ideal >= rail_v + knee_v:
        return rail_v
    if ideal > rail_v - knee_v:
        return rail_v - (ideal - rail_v - knee_v) ** 2 / (4.0 * knee_v)
    if ideal <= -rail_v - knee_v:
        return -rail_v
    if ideal < -rail_v + knee_v:
        return -rail_v + (ideal + rail_v + knee_v) ** 2 / (4.0 * knee_v)
    return ideal


def _range(values):
    return {"min": min(values), "max": max(values)}


def summarize():
    pairs = [(idss, vp) for idss in J201_IDSS_A for vp in J201_VP_V]
    freqs = (40, 200, 1000, 8000)
    gm_results = {}
    for f in freqs:
        linear = [jfet_transfer(f, idss, vp) for idss, vp in pairs]
        db = [20.0 * math.log10(abs(v)) for v in linear]
        gm_results[str(f)] = {**_range(db), "span_db": max(db) - min(db)}

    nonlinear = {}
    for peak in (0.02, 0.2, 0.6):
        responses = [jfet_harmonics(idss, vp, peak) for idss, vp in pairs]
        # The floor prevents overinterpreting numerical/near-zero harmonics.
        nonlinear[f"{peak:g}"] = {
            "h1_current_amp_a": _range([r["h1_amp_a"] for r in responses]),
            "h2_db_re_h1": _range([r["h2_db_re_h1"] for r in responses]),
            "h3_db_re_h1": _range([r["h3_db_re_h1"] for r in responses]),
        }

    diode_samples = [(is_, nvt) for is_ in (1e-10, 5e-9, 1e-7)
                     for nvt in (0.026, 0.052, 0.078)]
    diode = {
        str(current): _range([assumed_diode_forward_v(current, is_, nvt)
                             for is_, nvt in diode_samples])
        for current in (1e-6, 1e-4)
    }
    source_ohm = 100e3
    clone_att = source_attenuation(source_ohm, 500e3)
    nominal_att = source_attenuation(source_ohm, 1e6)
    rail_cases = {
        str(v): _range([illustrative_opamp_output(v, gain, 0.1, rail)
                        for gain in (1e3, 1e5)
                        for rail in (2.7, 3.3)])
        for v in (0.2, 0.4)
    }

    return {
        "status": "ASSUMPTION_SENSITIVITY_ONLY_NO_RETAIL_PARITY_SCORE",
        "source_scope": {
            "bom": "PCB Guitar Mania Black Mirror VII v1.6 (R6=3k3, C3=220n, R1=R2=1M)",
            "j201": "Central Semiconductor J201: IDSS 0.2..1 mA; |VGS(off)| 0.3..1.5 V",
            "ultra": "Darkglass B7K Ultra manual nominal input impedance 1 MOhm",
            "diode": "Vishay 1N4148 general electrical data, no unique model Is/nVT",
        },
        "assumptions": {
            "jfet_grid": "3x3 full Cartesian IDSS/Vp corners, not a manufacturing distribution",
            "jfet_ro_ohm": 200e3,
            "jfet_coupling": "Q1 R6/C3 only, no Q2/drain compliance/pickup coupling",
            "diode_grid": "Is={1e-10,5e-9,1e-7} A; nVT={.026,.052,.078} V are illustrative",
            "opamp_example": "generic beta_feedback=0.1, static open-loop gains 1e3/1e5",
            "opamp_rail_grid_v": [2.7, 3.3],
            "input_example": "100 kOhm purely resistive source; nominal port not Ultra netlist",
        },
        "metrics": {
            "jfet_q1_incremental_gm": {"samples": len(pairs), "frequency_db": gm_results},
            "jfet_q1_source_current_harmonics": nonlinear,
            "diode_shockley_forward_voltage_v": diode,
            "input_port_example": {
                "source_ohm": source_ohm,
                "clone_500k_attenuation": clone_att,
                "nominal_1m_attenuation": nominal_att,
                "nominal_minus_clone_gain_db": 20.0 * math.log10(nominal_att / clone_att),
            },
            "illustrative_feedback_rail_output_v": {
                "output_by_input_v": rail_cases,
                "sampled_cases": 8,
            },
        },
        "not_proven": [
            "No authenticated physical B7K Ultra circuit or measured I-V curves",
            "No raw hardware audio captures, component joint distribution or calibration uncertainty",
            "No physical J201 drain/Q2 compliance, TL07x GBW/slew/overload recovery or pickup model",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="print deterministic machine-readable report")
    args = parser.parse_args()
    report = summarize()
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        jfet = report["metrics"]["jfet_q1_incremental_gm"]["frequency_db"]
        print("ASSUMPTION-ONLY Q1/diode/input/rail sensitivity; NOT a hardware-parity score")
        print("Q1 gate-to-drain incremental gain spread, 40 Hz: %.2f dB; 1 kHz: %.2f dB"
              % (jfet["40"]["span_db"], jfet["1000"]["span_db"]))
        print("100 kOhm source: nominal 1M vs clone 500k => +%.4f dB"
              % report["metrics"]["input_port_example"]["nominal_minus_clone_gain_db"])
        print("Use --json for all assumed parameter grids, harmonic/diode/rail ranges.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
