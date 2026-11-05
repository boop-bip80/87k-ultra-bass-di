#!/usr/bin/env python3
"""Cross-check the *clone drawing's* coupled bridged-T + IC4_B stage in ngspice.

The isolated 8-component netlist is solved by ngspice independently of both
the DSP's 4x4 trapezoidal matrix and the closed-form circuit-elimination
oracle in tests/SchematicOracleExtensionTest.cpp. Running both verifies three
implementations of the same *Black Mirror VII clone* circuit; none establish
parity with measured Darkglass B7K Ultra hardware.

With ngspice installed:
    python3 analysis/spice_reference/verify_coupled_stage.py

Without ngspice, inspect the exact generated netlist:
    python3 analysis/spice_reference/verify_coupled_stage.py --print-netlist

The test intentionally exits 2 (not PASS/SKIP) when ngspice is missing.
The pure C++ deterministic DSP-vs-analytic gate requires only g++:
    g++ -std=c++17 -O2 tests/SchematicOracleExtensionTest.cpp -o /tmp/coupled-test
    /tmp/coupled-test
"""

from __future__ import annotations

import argparse
import ast
import cmath
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


REPO = Path(__file__).resolve().parents[2]
DRAWN = {
    "R22": 100e3, "R23": 33e3, "C16": 680e-12, "C17": 22e-9,
    "R24": 10e3, "R25": 22e3, "C18": 1e-9, "C27": 1e-9,
}
PERTURBED = {
    "R22": 91e3, "R23": 39e3, "C16": 820e-12, "C17": 18e-9,
    "R24": 15e3, "R25": 18e3, "C18": 1.5e-9, "C27": 820e-12,
}
FREQUENCIES = (40.0, 100.0, 500.0, 717.0, 1000.0, 2000.0,
               4000.0, 8000.0, 12000.0)
# Verified with actual ngspice-42: `ac lin 2` returns only one data row.
# Use three points over a modest span and score the requested first point.
AC_POINTS = 3
AC_END_RATIO = 1.01


def check_full_netlist_values() -> None:
    """Detect drift between this isolated oracle and the full-chain SPICE file.

    Static-parse the DRAWN literal, rather than importing a module that needs
    numpy: this provenance check also runs on a stdlib-only Python install.
    """
    source = (REPO / "analysis/spice_reference/b7k_spice.py").read_text(encoding="utf-8")
    tree = ast.parse(source)
    assignments = [node for node in tree.body
                   if isinstance(node, ast.Assign)
                   and any(isinstance(target, ast.Name) and target.id == "DRAWN"
                           for target in node.targets)]
    if len(assignments) != 1 or not isinstance(assignments[0].value, ast.Call):
        raise ValueError("missing or ambiguous DRAWN literal in b7k_spice.py")
    original = {kw.arg: ast.literal_eval(kw.value) for kw in assignments[0].value.keywords}
    for part, here in DRAWN.items():
        there = original.get(part)
        if there is None or not math.isclose(here, there, rel_tol=1e-12):
            raise ValueError(f"clone branch {part} drifted: isolated={here!r}, full SPICE={there!r}")


def build_isolated_netlist(parts: dict[str, float], frequency: float, output: Path) -> str:
    """Express the eight original clone R/C parts as literal SPICE elements.

    The ideal VCVS is just the IC4_B unity follower (not a high-frequency
    physical TL07x model), with C18 feeding back from its *output*, whereas
    R25 and C27 terminate at the non-inverting input.
    """
    p = parts
    return "\n".join([
        "* Isolated Black Mirror VII clone recovery bridged-T + IC4_B",
        "* vin is the ideal buffered IC2_B output; skout is the IC4_B output",
        ".option reltol=1e-9 abstol=1e-15 gmin=1e-15",
        "Vin vin 0 DC 0 AC 1",
        f"R22 vin nm {p['R22']:.16g}",
        f"R23 nm no {p['R23']:.16g}",
        f"C16 vin no {p['C16']:.16g}",
        f"C17 nm 0 {p['C17']:.16g}",
        f"R24 no n1sk {p['R24']:.16g}",
        f"R25 n1sk kp {p['R25']:.16g}",
        f"C18 n1sk skout {p['C18']:.16g}",
        f"C27 kp 0 {p['C27']:.16g}",
        "Ebuffer skout 0 kp 0 1",
        ".control",
        "set noaskquit",
        "set wr_singlescale",
        "option numdgt=15",
        f"ac lin {AC_POINTS} {frequency:.15g} {frequency*AC_END_RATIO:.15g}",
        "let hre = real(v(skout) / v(vin))",
        "let him = imag(v(skout) / v(vin))",
        f"wrdata {output} hre him",
        "quit",
        ".endc",
        ".end",
        "",
    ])


def analytic_transfer(p: dict[str, float], frequency: float) -> complex:
    """Continuous-time closed-form elimination; same equations as C++ gate."""
    s = complex(0, 2 * math.pi * frequency)
    dsk = (1 + s*p["C27"]*(p["R24"]+p["R25"])
           + s*s*p["R24"]*p["R25"]*p["C18"]*p["C27"])
    yin = s*p["C27"]*(1+s*p["R25"]*p["C18"]) / dsk
    t = 1/p["R22"] + 1/p["R23"] + s*p["C17"]
    num = s*p["C16"] + 1/(p["R22"]*p["R23"]*t)
    den = 1/p["R23"] + s*p["C16"] + yin - 1/(p["R23"]**2*t)
    return num / den / dsk


def parse_ac_wrdata(text: str, frequency: float) -> complex:
    """Read exactly the real(H)/imag(H) three-column `wr_singlescale` table."""
    rows = []
    for line in text.splitlines():
        try:
            nums = [float(value) for value in line.split()]
        except ValueError:  # optional wrdata header
            continue
        if not nums:
            continue
        if len(nums) != 3:
            raise RuntimeError(f"ngspice wrdata row has {len(nums)} columns, expected 3")
        if not all(math.isfinite(value) for value in nums):
            raise RuntimeError("ngspice wrdata contains nonfinite values")
        if nums[0] <= 0.0:
            raise RuntimeError("ngspice wrdata frequency must be positive")
        rows.append((nums[0], complex(nums[1], nums[2])))
    if len(rows) != AC_POINTS:
        raise RuntimeError(f"unexpected ngspice wrdata format ({len(rows)} numeric rows)")
    if any(rows[i][0] >= rows[i+1][0] for i in range(len(rows)-1)):
        raise RuntimeError("ngspice wrdata frequencies not strictly increasing")
    for i, (actual, _) in enumerate(rows):
        expected = frequency*(1+(AC_END_RATIO-1)*i/(AC_POINTS-1))
        if not math.isclose(actual, expected, rel_tol=3e-6):
            raise RuntimeError(f"ngspice wrdata frequency {actual} != expected {expected}")
    return rows[0][1]


def spice_transfer(ngspice: str, parts: dict[str, float], frequency: float,
                   workdir: Path) -> complex:
    netlist = workdir / "coupled.cir"
    data = workdir / "coupled.txt"
    netlist.write_text(build_isolated_netlist(parts, frequency, data), encoding="utf-8")
    if data.exists():
        data.unlink()
    result = subprocess.run([ngspice, "-b", str(netlist)], cwd=workdir,
                            capture_output=True, text=True, timeout=20, check=False)
    if result.returncode != 0 or not data.is_file():
        raise RuntimeError("ngspice did not produce coupled-stage AC data:\n"
                           + result.stdout[-1600:] + "\n" + result.stderr[-1600:])
    return parse_ac_wrdata(data.read_text(encoding="utf-8"), frequency)


def verify(ngspice: str) -> int:
    failures = 0
    worst_db, worst_phase = 0.0, 0.0
    with tempfile.TemporaryDirectory(prefix="b7k-coupled-spice-") as temp:
        workdir = Path(temp)
        for title, parts, fs in (("drawn", DRAWN, 48000.0),
                                 ("drawn", DRAWN, 96000.0),
                                 ("perturbed", PERTURBED, 96000.0)):
            print(f"\n{title} parts, DSP sample rate {fs:g} Hz")
            for digital_freq in FREQUENCIES:
                # Exact frequency mapping of trapezoidal capacitors. Without
                # warping, ngspice-vs-DSP errors near Nyquist are expected.
                analog_freq = fs / math.pi * math.tan(math.pi*digital_freq/fs)
                want = analytic_transfer(parts, analog_freq)
                got = spice_transfer(ngspice, parts, analog_freq, workdir)
                db_error = abs(20 * math.log10(abs(got/want)))
                phase_error = abs(math.degrees(cmath.phase(got/want)))
                worst_db, worst_phase = max(worst_db, db_error), max(worst_phase, phase_error)
                ok = db_error < 0.015 and phase_error < 0.1
                failures += not ok
                print(f"  {digital_freq:6.0f} Hz:  oracle={20*math.log10(abs(want)):+9.4f} dB"
                      f"  ngspice={20*math.log10(abs(got)):+9.4f} dB"
                      f"  error={db_error:.6f} dB / {phase_error:.6f}°"
                      f"  {'PASS' if ok else 'FAIL'}")
    print(f"\n{'PASS' if not failures else 'FAIL'}: {failures} discrepancies;"
          f" worst {worst_db:.6f} dB / {worst_phase:.6f}°."
          " Clone circuit only; real pedal not yet measured.")
    return int(bool(failures))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-netlist", action="store_true",
                        help="display nominal 1 kHz netlist; works without ngspice")
    args = parser.parse_args()
    try:
        check_full_netlist_values()
    except (ValueError, SyntaxError, OSError) as exc:
        print(f"ERROR: branch provenance check failed: {exc}", file=sys.stderr)
        return 1
    if args.print_netlist:
        print(build_isolated_netlist(DRAWN, 1000.0, Path("coupled.txt")), end="")
        return 0
    ngspice = shutil.which("ngspice")
    if not ngspice:
        print("ERROR: ngspice is not installed; SPICE comparison was NOT run. "
              "Use --print-netlist for the circuit and run the independent "
              "C++ SchematicOracleExtensionTest for offline DSP checks.", file=sys.stderr)
        return 2
    try:
        return verify(ngspice)
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as exc:
        print(f"ERROR: ngspice verification failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
