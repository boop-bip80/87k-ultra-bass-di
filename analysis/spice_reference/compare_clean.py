#!/usr/bin/env python3
"""CLEAN-path (BLEND = 0) parity: ngspice-of-the-drawn-schematic vs the plugin.

Three columns per control setting:
  spice(drawn)      ngspice, schematic component values, ideal op-amps (what the plugin assumes)
  plugin(schematic) OfflineRender --schematic: every capture-fitted constant back to its DRAWN value
  plugin(shipped)   OfflineRender with the shipped FitParams defaults

Metric: dB difference vs spice, 20 Hz - 16 kHz, log-spaced, after *no* gain matching
(absolute) and after removing the single best constant offset (shape).
"""
import argparse
import sys
import os
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from b7k_spice import build_netlist, run_ac
from plugin_fr import plugin_fr, sample_at

FREQS = np.geomspace(20, 16000, 120)

# kept for the scripts that import it; --schematic now supplies these (FitParams::schematic())
NOMINAL_CLEAN = dict(
    midLoCap250=22e-9, midLoCap500=22e-9, midLoCap1k=22e-9, midCapRatioLo=1.0, midWiperRLo=0.0,
    midHiCap750=680e-12, midHiCap1500=680e-12, midHiCap3k=680e-12, midCapRatioHi=10.0,
    midWiperRHi=0.0, trebleWiperR=3300.0,
)

SETTINGS = {
    "flat":            dict(),
    "bass+":           dict(bass=1.0),
    "bass-":           dict(bass=0.0),
    "treble+":         dict(treble=1.0),
    "treble-":         dict(treble=0.0),
    "lomid+ (1k)":     dict(lomid=1.0),
    "lomid- (1k)":     dict(lomid=0.0),
    "himid+ (3k)":     dict(himid=1.0),
    "himid- (3k)":     dict(himid=0.0),
    "all boost":       dict(bass=1.0, treble=1.0, lomid=1.0, himid=1.0),
    "all cut":         dict(bass=0.0, treble=0.0, lomid=0.0, himid=0.0),
}
KNOB = {"bass": "--lo", "treble": "--hi", "lomid": "--lo-mid", "himid": "--hi-mid"}


def spice_h(s, gbw=None, P=None):
    nl = build_netlist(P=P, blend=0.0, gbw=gbw, bass=s.get("bass", .5), treble=s.get("treble", .5),
                       lomid=s.get("lomid", .5), himid=s.get("himid", .5))
    f, o = run_ac(nl, ["out"], f0=10, f1=30e3, ppd=80)
    return f, o["out"]


def stats(a, b):
    d = a - b
    sh = d - np.mean(d)
    return np.sqrt(np.mean(d ** 2)), np.max(np.abs(d)), np.sqrt(np.mean(sh ** 2)), np.max(np.abs(sh))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=2)
    a = ap.parse_args()
    print(f"{'setting':14s} | {'plugin(--schematic) vs spice(drawn)':^36s} | {'plugin(shipped) vs spice(drawn)':^36s}")
    print(f"{'':14s} | {'rms':>6s} {'max':>6s} {'rmsShp':>7s} {'maxShp':>7s}  | {'rms':>6s} {'max':>6s} {'rmsShp':>7s} {'maxShp':>7s}")
    for name, s in SETTINGS.items():
        fs, Hs = spice_h(s)
        ref = 20 * np.log10(np.abs(np.interp(FREQS, fs, np.abs(Hs))))
        knobs = {"--blend": 0, "--master": 1.0}
        for k, v in s.items():
            knobs[KNOB[k]] = v
        fn, Hn = plugin_fr(knobs, None, os_factor=a.os, extra=["--schematic"])
        fh, Hh = plugin_fr(knobs, None, os_factor=a.os)
        pn = sample_at(fn, Hn, FREQS)
        ph = sample_at(fh, Hh, FREQS)
        sn, sh = stats(pn, ref), stats(ph, ref)
        print(f"{name:14s} | {sn[0]:6.2f} {sn[1]:6.2f} {sn[2]:7.2f} {sn[3]:7.2f}  | {sh[0]:6.2f} {sh[1]:6.2f} {sh[2]:7.2f} {sh[3]:7.2f}")


if __name__ == "__main__":
    main()
