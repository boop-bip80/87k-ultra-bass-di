#!/usr/bin/env python3
"""Large-signal CD4049 clipper stage: ngspice (two-MOSFET device model in the drawn loop with R19)
versus the plugin's fitted-VTC Clipper.  Both are driven by an ideal sine at the DRIVE-stage output
node.  Reports the output fundamental and per-order harmonics (dB re the fundamental), plus the
peak-to-peak swing.  `probe` = path to a compiled clipper probe (see --help)."""
import argparse
import os
import subprocess
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from b7k_spice import clipper_netlist, run_tran


def harmonics(t, y, f0, nh=7):
    # integer number of cycles in the window
    T = 1.0 / f0
    ncyc = int(np.floor((t[-1] - t[0]) / T))
    t1 = t[0] + ncyc * T
    m = t <= t1 + 1e-12
    tt, yy = t[m], y[m] - np.mean(y[m])
    tu = np.linspace(tt[0], tt[0] + ncyc * T, 4096 * ncyc, endpoint=False)
    yu = np.interp(tu, tt, yy)
    Y = np.fft.rfft(yu) / len(yu) * 2
    mags = np.abs(Y[[ncyc * k for k in range(1, nh + 1)]])
    return mags, np.ptp(y[m])


def spice_case(freq, amp, grunt):
    nl = clipper_netlist(grunt=grunt, stim_args=f"SIN(0 {amp} {freq})")
    tstop = 0.05
    t, o = run_tran(nl, ["co"], tstop=tstop, tstep=1e-6, tstart=0.03)
    return harmonics(t, o["co"], freq)


def plugin_case(probe, freq, amp, grunt):
    out = subprocess.run([probe, str(freq), str(amp), grunt], capture_output=True, text=True).stdout
    a = np.array([[float(x) for x in l.split()] for l in out.strip().splitlines()])
    return harmonics(a[:, 0], a[:, 1], freq)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", required=True)
    ap.add_argument("--freq", type=float, default=1000.0)
    ap.add_argument("--grunt", default="cut")
    a = ap.parse_args()
    print(f"clipper input node sine at {a.freq:.0f} Hz, GRUNT={a.grunt}")
    print(f"{'in(V pk)':>9s} | {'':^34s} spice | {'':^34s} plugin")
    print(f"{'':9s} | {'pp(V)':>7s} " + " ".join(f"{'H'+str(k):>6s}" for k in range(1, 8)) +
          " | " + f"{'pp(V)':>7s} " + " ".join(f"{'H'+str(k):>6s}" for k in range(1, 8)))
    for amp in (0.01, 0.03, 0.1, 0.3, 1.0, 3.0):
        ms, pps = spice_case(a.freq, amp, a.grunt)
        mp, ppp = plugin_case(a.probe, a.freq, amp, a.grunt)
        fmt = lambda m: " ".join(f"{20*np.log10(m[k]/m[0]+1e-30):6.1f}" if k else f"{20*np.log10(m[0]/amp):6.1f}"
                                 for k in range(7))
        print(f"{amp:9.3f} | {pps:7.3f} {fmt(ms)} | {ppp:7.3f} {fmt(mp)}")
    print("(H1 column = fundamental gain dB re input; H2..H7 = dB re the fundamental)")


if __name__ == "__main__":
    main()
