#!/usr/bin/env python3
"""Independent ngspice NJFET check of the Black Mirror VII clone's Q1 R6/C3.

This gate verifies the *source-feedback subcircuit*, not the full J201 Q1/Q2
drain network or an actual Darkglass B7K Ultra. The production
JfetStage::setPhysicalSourceFeedbackModel() is a research-only idealized
square-law solver. This test uses ngspice's own native LEVEL-1 NJF primitive
instead of the DSP's square-law current function, with drain held at +9 V,
lambda/parasitics intentionally disabled, and the clone's R6/C3 to ground.

Run after installing ngspice:
  python3 analysis/spice_reference/verify_jfet_source.py
Without ngspice, inspect the full generated netlist and static provenance:
  python3 analysis/spice_reference/verify_jfet_source.py --print-netlist

Missing ngspice is exit 2, never a false PASS/SKIP.
"""

from __future__ import annotations

import argparse
import ast
import cmath
from dataclasses import dataclass
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
NOMINAL_R6 = 3.3e3
NOMINAL_C3 = 220e-9
FREQUENCIES = (20.0, 100.0, 219.0, 500.0, 719.0, 1000.0, 3000.0, 8000.0)
RATES = (48000.0, 96000.0)
# ngspice-42 emitted a single row for "ac lin 2" in an actual smoke test.
# Three separated points reliably produce the expected three-row table;
# only the first stimulus frequency is scored.
AC_END_RATIO = 1.01
AC_POINTS = 3


@dataclass(frozen=True)
class Parts:
    r6: float
    c3: float
    beta: float
    vto: float


NOMINAL = Parts(NOMINAL_R6, NOMINAL_C3, 1.031e-3, -0.718)
# Pure sensitivity control. These are NOT measured J201 tolerances.
PERTURBED = Parts(4.7e3, 330e-9, 0.8e-3, -0.65)


def check_drawn_provenance() -> None:
    """Fail if this independent schematic fixture drifts from clone values.

    The full-chain netlist source defines DRAWN through a Values(...) literal;
    static AST parsing avoids importing its non-stdlib Python dependencies.
    """
    path = ROOT / "analysis" / "spice_reference" / "b7k_spice.py"
    tree = ast.parse(path.read_text(encoding="utf-8"))
    matches = [node for node in tree.body
               if isinstance(node, ast.Assign)
               and any(isinstance(target, ast.Name) and target.id == "DRAWN"
                       for target in node.targets)]
    if len(matches) != 1 or not isinstance(matches[0].value, ast.Call):
        raise ValueError("missing or ambiguous clone DRAWN Values(...) literal")
    values = {kw.arg: ast.literal_eval(kw.value) for kw in matches[0].value.keywords}
    for component, expected in (("R6", NOMINAL.r6), ("C3", NOMINAL.c3)):
        actual = values.get(component)
        if actual is None or not math.isclose(actual, expected, rel_tol=1e-12):
            raise ValueError(f"clone {component} drift: fixture {expected:g}, full netlist {actual!r}")


def dc_bias(parts: Parts) -> tuple[float, float]:
    """Standalone DC Q1 self-bias oracle; only used to score external ngspice.

    The NJF drain is at +9 V in the SPICE fixture. With lambda=0 and no
    extrinsic resistances, saturation gives Iq=beta*(|VTO|-Vs)^2.
    """
    lo, hi = 0.0, -parts.vto
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        residual = mid / parts.r6 - parts.beta * (-parts.vto - mid) ** 2
        if residual > 0.0:
            hi = mid
        else:
            lo = mid
    vs = 0.5 * (lo + hi)
    gm = 2.0 * parts.beta * (-parts.vto - vs)
    return vs, gm


def source_transfer(parts: Parts, frequency: float) -> complex:
    """Expected source/gate AC response for ideal JFET source degeneration."""
    _, gm = dc_bias(parts)
    s = complex(0.0, 2.0 * math.pi * frequency)
    return gm / (gm + 1.0 / parts.r6 + s * parts.c3)


def build_netlist(parts: Parts, frequency: float, data: Path) -> str:
    """Primitive-level R6/C3 source network; no DSP waveshaper code imported."""
    if any(value <= 0.0 or not math.isfinite(value)
           for value in (parts.r6, parts.c3, parts.beta, -parts.vto, frequency)):
        raise ValueError("invalid positive R/C/NJF/frequency parameter")
    # Ngspice NJF model: ideal Shockley with no channel-length modulation or
    # gate capacitances, to isolate the R6/C3 feedback law under examination.
    return "\n".join([
        "* Black Mirror VII 1.6v Q1 source network — not retail Darkglass",
        "* native NJFET primitive, independently solved by ngspice",
        ".option reltol=1e-9 abstol=1e-15 gmin=1e-15",
        "Vdrain drain 0 DC 9",
        "Vgate gate 0 DC 0 AC 1",
        f"R6 source 0 {parts.r6:.16g}",
        f"C3 source 0 {parts.c3:.16g}",
        f".model Q1TEST NJF(VTO={parts.vto:.16g} BETA={parts.beta:.16g} LAMBDA=0 RS=0 RD=0 CGS=0 CGD=0)",
        "J1 drain gate source Q1TEST",
        ".control",
        "set noaskquit",
        "set wr_singlescale",
        "option numdgt=15",
        "op",
        "print v(source)",
        f"ac lin {AC_POINTS} {frequency:.15g} {frequency*AC_END_RATIO:.15g}",
        "let hre = real(v(source) / v(gate))",
        "let him = imag(v(source) / v(gate))",
        f"wrdata {data} hre him",
        "quit",
        ".endc",
        ".end",
        "",
    ])


_BIAS = re.compile(r"(?im)\bv\(source\)\s*=\s*([+\-]?(?:\d+\.?\d*|\.\d+)(?:[edED][+\-]?\d+)?)")


def parse_bias(output: str) -> float:
    matched = _BIAS.search(output)
    if matched is None:
        raise ValueError("ngspice operating-point source bias not in output")
    voltage = float(matched.group(1).replace("D", "E").replace("d", "e"))
    if not math.isfinite(voltage):
        raise ValueError("ngspice operating-point source bias not finite")
    return voltage


def parse_ac_wrdata(text: str, frequency: float) -> complex:
    """Require the exact ``wr_singlescale`` table shape and AC sweep grid.

    ngspice prints one frequency scale followed by the two *real* vectors
    ``hre`` and ``him``. Accepting additional numeric columns would silently
    reinterpret another scale vector as the real transfer component if
    ``wr_singlescale`` failed to take effect.
    """
    rows = []
    for line in text.splitlines():
        try:
            columns = [float(item) for item in line.split()]
        except ValueError:
            continue
        if not columns:
            continue
        if len(columns) != 3:
            raise ValueError(f"ngspice AC row has {len(columns)} columns; expected exactly 3")
        if not all(map(math.isfinite, columns)):
            raise ValueError("non-finite ngspice AC column")
        if columns[0] <= 0.0:
            raise ValueError("non-positive ngspice AC frequency")
        rows.append((columns[0], complex(columns[1], columns[2])))
    if len(rows) != AC_POINTS:
        raise ValueError(f"expected exactly {AC_POINTS} ngspice AC rows, received {len(rows)}")
    if any(rows[i][0] >= rows[i + 1][0] for i in range(len(rows) - 1)):
        raise ValueError("ngspice AC frequencies are not strictly increasing")
    for i, (actual, _) in enumerate(rows):
        expected = frequency * (1.0 + (AC_END_RATIO - 1.0) * i / (AC_POINTS - 1))
        # wrdata's default output precision may be shorter than Python's
        # .15g netlist precision; allow rounding, but reject wrong sweeps.
        if not math.isclose(actual, expected, rel_tol=3e-6):
            raise ValueError(f"ngspice output frequency {actual} does not match {expected}")
    return rows[0][1]


def run_ngspice(ngspice: str, parts: Parts, frequency: float, workdir: Path) -> tuple[float, complex]:
    path = workdir / "q1_source.cir"
    data = workdir / "q1_source_ac.txt"
    path.write_text(build_netlist(parts, frequency, data), encoding="utf-8")
    if data.exists():
        data.unlink()
    result = subprocess.run([ngspice, "-b", str(path)], cwd=workdir,
                            capture_output=True, text=True, timeout=20, check=False)
    if result.returncode != 0 or not data.exists():
        raise RuntimeError("ngspice did not produce NJF AC data:\n"
                           + result.stdout[-1200:] + "\n" + result.stderr[-1200:])
    bias = parse_bias(result.stdout + "\n" + result.stderr)
    response = parse_ac_wrdata(data.read_text(encoding="utf-8"), frequency)
    return bias, response


def verify(ngspice: str) -> int:
    failures, worst_db, worst_phase, worst_bias = 0, 0.0, 0.0, 0.0
    with tempfile.TemporaryDirectory(prefix="b7k-q1-source-spice-") as temp:
        workdir = Path(temp)
        for title, parts in (("drawn", NOMINAL), ("perturbed", PERTURBED)):
            expected_bias, expected_gm = dc_bias(parts)
            print(f"\n{title}: expected Q1 Vs,DC={expected_bias:.7f} V,"
                  f" gm={expected_gm*1e3:.6f} mS")
            for fs in RATES:
                for digital_freq in FREQUENCIES:
                    # Exact trapezoidal capacitor mapping from digital omega
                    # to the SPICE continuous-time AC stimulus.
                    analog_freq = fs / math.pi * math.tan(math.pi * digital_freq / fs)
                    expected = source_transfer(parts, analog_freq)
                    observed_bias, observed = run_ngspice(ngspice, parts, analog_freq, workdir)
                    bias_error = abs(expected_bias - observed_bias)
                    mag_error = abs(20.0 * math.log10(abs(observed / expected)))
                    phase_error = abs(math.degrees(cmath.phase(observed / expected)))
                    worst_db = max(worst_db, mag_error)
                    worst_phase = max(worst_phase, phase_error)
                    worst_bias = max(worst_bias, bias_error)
                    passed = (bias_error < 2e-4 and mag_error < 0.015
                              and phase_error < 0.1)
                    failures += not passed
                    print(f"  fs={fs:6.0f} f={digital_freq:6.0f} Hz"
                          f"  bias error={bias_error:.3g} V"
                          f"  AC error={mag_error:.5f} dB/{phase_error:.5f}°"
                          f"  {'PASS' if passed else 'FAIL'}")
    print(f"\n{'PASS' if not failures else 'FAIL'}: {failures} discrepancies;"
          f" worst bias={worst_bias:.5g} V,"
          f" magnitude={worst_db:.5f} dB, phase={worst_phase:.5f}°;"
          " Black Mirror clone Q1 primitive only — retail parity unverified.")
    return int(bool(failures))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-netlist", action="store_true")
    args = parser.parse_args()
    try:
        check_drawn_provenance()
    except (ValueError, OSError, SyntaxError) as exc:
        print(f"ERROR: clone provenance check failed: {exc}", file=sys.stderr)
        return 1
    if args.print_netlist:
        print(build_netlist(NOMINAL, 1000.0, Path("q1_source_ac.txt")), end="")
        return 0
    ngspice = shutil.which("ngspice")
    if ngspice is None:
        print("ERROR: ngspice is not installed; NJF source-circuit verification"
              " was NOT run. Use --print-netlist for the independent primitive"
              " deck.", file=sys.stderr)
        return 2
    try:
        return verify(ngspice)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f"ERROR: ngspice NJF reference failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
