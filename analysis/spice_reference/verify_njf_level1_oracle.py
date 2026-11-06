#!/usr/bin/env python3
"""Independent, fail-closed ngspice 42 NJF Level-1 DC oracle.

No repository circuit/model equations are imported.  The analytic expression is
the textbook SPICE JFET Level-1 Shichman--Hodges channel law, with LAMBDA=0,
RS=RD=0, and junction leakage deliberately suppressed in the reference deck.
"""

from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import subprocess
import tempfile


VTO = -3.0
BETA = 1.0e-3
FORWARD_GATES = (-5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 0.5)
REVERSE_GATES = (-4.0, -3.0, -2.0)
FORWARD_DRAINS = tuple(0.5 * n for n in range(19))
REVERSE_DRAINS = tuple(-2.0 + 0.25 * n for n in range(9))


def channel_current(vgs: float, vds: float, *, beta: float = BETA,
                    vto: float = VTO) -> float:
    """NJF channel drain current; negative Vds interchanges source and drain."""
    if vds < 0.0:
        return -channel_current(vgs - vds, -vds, beta=beta, vto=vto)
    overdrive = vgs - vto
    if overdrive <= 0.0:
        return 0.0
    if vds < overdrive:
        return beta * vds * (2.0 * overdrive - vds)
    return beta * overdrive * overdrive


def parse_wrdata(text: str, expected_drains: tuple[float, ...]) -> tuple[float, ...]:
    """Reject malformed, nonfinite, shuffled, missing, or extra sample rows."""
    rows: list[tuple[float, float]] = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        fields = line.split()
        if len(fields) != 2:
            raise ValueError(f"line {number}: expected exactly two columns, got {len(fields)}")
        try:
            voltage, supply_current = (float(field) for field in fields)
        except ValueError as exc:
            raise ValueError(f"line {number}: nonnumeric sample") from exc
        if not (math.isfinite(voltage) and math.isfinite(supply_current)):
            raise ValueError(f"line {number}: nonfinite sample")
        rows.append((voltage, -supply_current))  # I(Vdrain) is INTO the supply.
    if len(rows) != len(expected_drains):
        raise ValueError(f"expected {len(expected_drains)} samples, got {len(rows)}")
    for index, ((voltage, _), expected) in enumerate(zip(rows, expected_drains)):
        if not math.isclose(voltage, expected, abs_tol=1.0e-7, rel_tol=0.0):
            raise ValueError(f"sample {index}: expected Vds={expected:g}, got {voltage:g}")
    return tuple(current for _, current in rows)


def _deck(vgs: float, drains: tuple[float, ...], output_path: Path) -> str:
    step = drains[1] - drains[0]
    # IS is nonzero because SPICE requires a junction diode.  Keeping it far
    # below the channel currents isolates the channel law through Vgs=+0.5V.
    return f"""Native Level-1 NJF DC sweep, no extracted behavioral model
.option reltol=1e-10 abstol=1e-16 gmin=1e-16
.model DUT NJF(VTO={VTO} BETA={BETA} LAMBDA=0 RS=0 RD=0 IS=1e-30 CGS=0 CGD=0)
Vdrain drain 0 0
Vgate gate 0 {vgs:.8g}
J1 drain gate 0 DUT
.control
set noaskquit
set wr_singlescale
set numdgt=15
dc Vdrain {drains[0]:.8g} {drains[-1]:.8g} {step:.8g}
wrdata {output_path} i(Vdrain)
quit
.endc
.end
"""


def run_sweep(binary: Path, vgs: float, drains: tuple[float, ...]) -> tuple[float, ...]:
    with tempfile.TemporaryDirectory(prefix="njf-level1-") as temp:
        root = Path(temp)
        netlist = root / "oracle.cir"
        result_file = root / "measured.dat"
        netlist.write_text(_deck(vgs, drains, result_file), encoding="ascii")
        env = os.environ.copy()
        # The known ngspice42 binary was unpacked without a system install.
        # Caller may also supply the library path for another binary.
        bundled_lib = binary.resolve().parent.parent / "lib" / "x86_64-linux-gnu"
        if bundled_lib.is_dir():
            env["LD_LIBRARY_PATH"] = str(bundled_lib) + (
                ":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else ""
            )
        process = subprocess.run(
            [str(binary), "-b", str(netlist)], cwd=root, env=env,
            capture_output=True, text=True, timeout=20, check=False,
        )
        if process.returncode != 0 or not result_file.is_file():
            raise RuntimeError(
                f"ngspice failed at Vgs={vgs:g}, exit={process.returncode}\n"
                f"stdout:\n{process.stdout}\nstderr:\n{process.stderr}"
            )
        try:
            return parse_wrdata(result_file.read_text(encoding="ascii"), drains)
        except ValueError as exc:
            raise RuntimeError(
                f"ngspice malformed data at Vgs={vgs:g}: {exc}\n"
                f"stdout:\n{process.stdout}\nstderr:\n{process.stderr}"
            ) from exc


def verify(binary: Path) -> tuple[int, float, float]:
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise FileNotFoundError(f"executable ngspice binary not found: {binary}")
    count = 0
    greatest_absolute_error = 0.0
    greatest_relative_error = 0.0
    for label, gates, drains in (
        ("forward", FORWARD_GATES, FORWARD_DRAINS),
        ("reverse", REVERSE_GATES, REVERSE_DRAINS),
    ):
        for vgs in gates:
            measured = run_sweep(binary, vgs, drains)
            for vds, current in zip(drains, measured):
                predicted = channel_current(vgs, vds)
                absolute_error = abs(current - predicted)
                relative_error = absolute_error / max(abs(predicted), 1.0e-12)
                greatest_absolute_error = max(greatest_absolute_error, absolute_error)
                greatest_relative_error = max(greatest_relative_error, relative_error)
                count += 1
                # A milliampere-scale DC reference should agree much more
                # tightly than numerical model-device tolerances.
            if absolute_error > 1.0e-12 + 1.0e-7 * abs(predicted):
                    raise AssertionError(
                        f"{label} mismatch Vgs={vgs:g}, Vds={vds:g}: "
                        f"ngspice={current:.12g} A, analytic={predicted:.12g} A, "
                        f"error={absolute_error:.6g} A"
                    )
    return count, greatest_absolute_error, greatest_relative_error


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ngspice", type=Path, default=Path("/usr/bin/ngspice"))
    args = parser.parse_args()
    count, max_abs, max_rel = verify(args.ngspice)
    print(
        f"PASS native NJF Level-1 ngspice DC oracle: {count} points "
        f"(forward {len(FORWARD_GATES)}x{len(FORWARD_DRAINS)}, "
        f"reverse {len(REVERSE_GATES)}x{len(REVERSE_DRAINS)}), "
        f"max |delta Id|={max_abs:.4g} A, "
        f"max relative delta (floor 1e-12 A)={max_rel:.4g}"
    )


if __name__ == "__main__":
    main()
