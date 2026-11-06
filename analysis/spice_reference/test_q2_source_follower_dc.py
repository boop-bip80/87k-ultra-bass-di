#!/usr/bin/env python3
"""Independent ngspice DC check for the Black Mirror VII clone's Q1/Q2.

Usage: python3 analysis/spice_reference/test_q2_source_follower_dc.py \
    --ngspice /path/to/ngspice [--lib-dir /path/to/ngspice/libraries]

Missing ngspice is exit status 2, not a false-positive successful test.
"""

import argparse
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest


DECK = Path(__file__).with_name("q2_source_follower_dc.cir")
SIMPLIFIED_Q1_SOURCE_V = 0.375485448
SIMPLIFIED_Q2_SOURCE_V = 4.869116089
# Native SPICE J201 includes LAMBDA, RD, RS, and IS omitted by the simplified
# C++ square-law oracle: both nominal nodes differ by about 8 mV.
MODEL_DIFFERENCE_BUDGET_V = 0.02
VOLTAGE = re.compile(
    r"v\((q1s|q2g|outg)\)\s*=\s*"
    r"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)",
    re.IGNORECASE,
)


def measure_dc(ngspice: str, lib_dir: str | None) -> dict[str, float]:
    env = os.environ.copy()
    if lib_dir:
        env["LD_LIBRARY_PATH"] = (
            lib_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        )
    result = subprocess.run(
        [ngspice, "-b", str(DECK)],
        capture_output=True,
        text=True,
        env=env,
        timeout=30,
        check=False,
    )
    if result.returncode:
        raise RuntimeError(f"ngspice exit {result.returncode}:\n{result.stdout}\n{result.stderr}")
    values = {name.lower(): float(value) for name, value in VOLTAGE.findall(result.stdout)}
    if values.keys() != {"q1s", "q2g", "outg"}:
        raise ValueError(f"missing ngspice DC nodes: {values!r}\n{result.stdout}")
    if not all(math.isfinite(value) for value in values.values()):
        raise ValueError(f"non-finite ngspice DC node: {values!r}")
    return values


class Q2SourceFollowerDCTest(unittest.TestCase):
    ngspice = "ngspice"
    lib_dir = None

    @classmethod
    def setUpClass(cls) -> None:
        cls.dc = measure_dc(cls.ngspice, cls.lib_dir)
        print("ngspice Q1/Q2 DC: " + ", ".join(
            f"{key}={cls.dc[key]:.9f} V" for key in ("q1s", "q2g", "outg")
        ))

    def test_q1_source_matches_simplified_oracle_with_model_budget(self) -> None:
        self.assertAlmostEqual(
            self.dc["q1s"], SIMPLIFIED_Q1_SOURCE_V,
            delta=MODEL_DIFFERENCE_BUDGET_V,
        )

    def test_q2_gate_is_half_supply(self) -> None:
        self.assertAlmostEqual(self.dc["q2g"], 4.5, delta=0.001)

    def test_q2_source_matches_simplified_oracle_with_model_budget(self) -> None:
        self.assertAlmostEqual(
            self.dc["outg"], SIMPLIFIED_Q2_SOURCE_V,
            delta=MODEL_DIFFERENCE_BUDGET_V,
        )
        self.assertGreater(self.dc["outg"], self.dc["q2g"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ngspice", default=None, help="native ngspice executable")
    parser.add_argument("--lib-dir", default=None, help="directory for shared ngspice libraries")
    args = parser.parse_args()
    resolved = args.ngspice or shutil.which("ngspice")
    if not resolved or not (Path(resolved).is_file() or shutil.which(resolved)):
        print("ERROR: native ngspice missing; DC reference NOT VERIFIED", file=sys.stderr)
        sys.exit(2)
    Q2SourceFollowerDCTest.ngspice = resolved
    Q2SourceFollowerDCTest.lib_dir = args.lib_dir
    unittest.main(argv=[sys.argv[0]], verbosity=2)
