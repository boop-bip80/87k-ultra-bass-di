#!/usr/bin/env python3
"""END-TO-END large-signal OD chain: ngspice of the drawn schematic vs the plugin.

Plugin side: OfflineRender (--schematic by default; --shipped to render the calibrated defaults),
BLEND = 100 % OD, LEVEL max, DRIVE as given, EQ flat, MASTER max.  Spice side: full netlist with
single-pole op-amps clamped to the SAME rail window the plugin uses (VD - railNeg, VD + railPos).
Reports output fundamental (dBV) and H2..H7 (dB re fundamental) for each input amplitude.
Both are steady-state, integer-cycle FFT.  The J201 uses the typical-part SPICE model; the plugin's
J201 *shaper* keeps its fitted shape constants even in --schematic (it has no schematic-derived
values), so this table is where the remaining nonlinear gap shows up.
"""
import argparse
import os
import sys
import tempfile
import subprocess
from concurrent.futures import ProcessPoolExecutor
import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.dirname(__file__))
from b7k_spice import build_netlist, run_tran
from plugin_fr import RENDER_BIN, KIN, KOUT, FS

DRIVE_P = 1.98


def drive_R(x, p=DRIVE_P):
    return 100e3 * (1.0 - x) ** p


def harm(t, y, f0, nh=7):
    T = 1.0 / f0
    ncyc = int(np.floor((t[-1] - t[0]) / T)) if t[-1] > t[0] else 0
    ncyc = max(ncyc, 1)
    tu = np.linspace(t[0], t[0] + ncyc * T, 2048 * ncyc, endpoint=False)
    yu = np.interp(tu, t, y)
    yu -= np.mean(yu)
    Y = np.fft.rfft(yu) / len(yu) * 2
    return np.abs(Y[[ncyc * k for k in range(1, nh + 1)]])


def spice_case(args):
    f0, amp_v, drive, grunt, railNeg, railPos = args
    nl = build_netlist(blend=1.0, level_R=(0.0, 100e3), drive_R=drive_R(drive), grunt=grunt, gbw=None,
                       rails=(4.5 - railNeg, 4.5 + railPos), stim="tran",
                       stim_args=f"SIN(0 {amp_v} {f0})")
    ncyc_total = max(int(0.45 * f0), 40)
    tstop = ncyc_total / f0
    t, o = run_tran(nl, ["out"], tstop=tstop, tstep=2e-6, tstart=tstop * 0.6)
    return harm(t, o["out"], f0)


def plugin_case(args):
    f0, amp_v, drive, grunt, shipped = args
    n = int(FS * 0.6)
    tt = np.arange(n) / FS
    x = (amp_v / KIN) * np.sin(2 * np.pi * f0 * tt)
    with tempfile.TemporaryDirectory() as td:
        inp, out = os.path.join(td, "i.wav"), os.path.join(td, "o.wav")
        wavfile.write(inp, FS, x.astype(np.float32))
        cmd = [RENDER_BIN, inp, out, "--os", "8", "--blend", "1", "--level", "1", "--drive", str(drive),
               "--grunt", grunt, "--master", "1"]
        if not shipped:
            cmd += ["--schematic"]
        subprocess.run(cmd, check=True, capture_output=True)
        _, y = wavfile.read(out)
    y = y.astype(np.float64) / KOUT
    seg = y[int(FS * 0.3):]
    ncyc = int(np.floor(len(seg) * f0 / FS))
    seg = seg[: int(round(ncyc * FS / f0))]
    t = np.arange(len(seg)) / FS
    return harm(t, seg, f0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--freq", type=float, default=440.0)
    ap.add_argument("--drive", type=float, default=0.5)
    ap.add_argument("--grunt", default="cut")
    ap.add_argument("--shipped", action="store_true", help="render the plugin with its calibrated defaults")
    ap.add_argument("--rails", type=float, nargs=2, default=[2.9, 2.7], help="railNeg railPos (V about VD)")
    ap.add_argument("--dbfs", type=float, nargs="+", default=[-42, -36, -30, -24, -18, -12, -6])
    a = ap.parse_args()
    amps = [KIN * 10 ** (d / 20) for d in a.dbfs]
    with ProcessPoolExecutor(4) as ex:
        sp = list(ex.map(spice_case, [(a.freq, v, a.drive, a.grunt, *a.rails) for v in amps]))
        pl = list(ex.map(plugin_case, [(a.freq, v, a.drive, a.grunt, a.shipped) for v in amps]))
    print(f"OD chain, {a.freq:.0f} Hz, drive={a.drive}, grunt={a.grunt}, plugin={'shipped' if a.shipped else 'schematic'}")
    print(f"{'in dBFS':>8s} | {'H1 dBV':>7s} " + " ".join(f"{'H'+str(k):>6s}" for k in range(2, 8)) +
          " | " + f"{'H1 dBV':>7s} " + " ".join(f"{'H'+str(k):>6s}" for k in range(2, 8)) + " |  dH1")
    print(f"{'':8s} | {'-- spice --':^55s} | {'-- plugin --':^55s} |")
    for d, s, p in zip(a.dbfs, sp, pl):
        fmt = lambda m: f"{20*np.log10(m[0]+1e-30):7.1f} " + " ".join(f"{20*np.log10(m[k]/m[0]+1e-30):6.1f}" for k in range(1, 7))
        print(f"{d:8.0f} | {fmt(s)} | {fmt(p)} | {20*np.log10((p[0]+1e-30)/(s[0]+1e-30)):5.1f}")


if __name__ == "__main__":
    main()
