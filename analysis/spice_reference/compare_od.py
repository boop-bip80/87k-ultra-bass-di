#!/usr/bin/env python3
"""OVERDRIVE-path small-signal parity: ngspice-of-the-drawn-schematic vs the plugin.

BLEND = 100 % OD, LEVEL at max, EQ flat, MASTER at max, input -60 dBFS so the CD4049 and the
J201 sit in their linear region.  The comparison is end-to-end (jack -> OUT).

Columns: plugin(shipped) = default FitParams.  The spice curve uses the SPICE-typical J201
(VTO -0.718 V, BETA 1.031 mA/V^2 -- a ~5:1 part-to-part spread exists, see
docs/nonlinear-component-modeling.md section 2) and the DAFx-2020 two-MOSFET CD4049 model.
"""
import argparse
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from b7k_spice import build_netlist, run_ac
from plugin_fr import plugin_fr, sample_at

FREQS = np.geomspace(30, 16000, 90)
REPORT = np.array([40, 80, 160, 320, 640, 1000, 2000, 4000, 8000, 12000.0])
DRIVE_P = 1.98  # shipped driveTaperExp: R = 100k * (1 - x)^p


def drive_R(x, p=DRIVE_P):
    return 100e3 * (1.0 - x) ** p


def spice_out(drive_x, grunt="cut", attack="flat", gbw=None, P=None, node="out"):
    # C21 removed (1 F) so the OD comparison is not polluted by the C21 loading issue
    PP = dict(C21=1.0)
    if P:
        PP.update(P)
    nl = build_netlist(P=PP, blend=1.0, level_R=(0.0, 100e3), drive_R=drive_R(drive_x), grunt=grunt,
                       attack=attack, gbw=gbw)
    f, o = run_ac(nl, [node], f0=10, f1=30e3, ppd=60)
    return f, o[node]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=2)
    ap.add_argument("--amp", type=float, default=-60.0)
    ap.add_argument("--gbw", type=float, default=0.0, help="0 = ideal op-amps")
    a = ap.parse_args()
    gbw = a.gbw or None
    hdr = "  ".join(f"{int(x):>6d}" for x in REPORT)
    print(f"{'case':24s} {'':10s} {hdr}   rms/max over 30 Hz-16 kHz")
    for grunt in ("cut", "flat", "boost"):
        for dx in (0.0, 0.5, 1.0):
            fs, Hs = spice_out(dx, grunt=grunt, gbw=gbw, P=dict(C21=100e-9))
            sp = sample_at(fs, Hs, REPORT)
            knobs = {"--blend": 1.0, "--level": 1.0, "--drive": dx, "--grunt": grunt, "--master": 1.0}
            name = f"grunt={grunt:5s} drive={dx:.1f}"
            print(f"{name:24s} {'spice':10s} " + "  ".join(f"{v:6.1f}" for v in sp))
            for label, extra in (("schematic", ["--schematic"]), ("shipped", [])):
                f, H = plugin_fr(knobs, None, os_factor=a.os, amp_dbfs=a.amp, extra=extra)
                pl = sample_at(f, H, REPORT)
                d_all = sample_at(f, H, FREQS) - sample_at(fs, Hs, FREQS)
                print(f"{'':24s} {label+' d':10s} " + "  ".join(f"{v:6.1f}" for v in (pl - sp)) +
                      f"   {np.sqrt(np.mean(d_all**2)):5.2f}/{np.max(np.abs(d_all)):5.2f}")


if __name__ == "__main__":
    main()
