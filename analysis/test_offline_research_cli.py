#!/usr/bin/env python3
"""Cross-platform no-WAV smoke test for OfflineRender's research-only CLI.

CTest passes the built `OfflineRender` binary as argv[1]. All successful
commands use --print-fit, so they exercise the production parser and its
observable configuration without audio IO or a proprietary reference capture.

The research modes are *not* proof of retail B7K Ultra circuit parity.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


RESEARCH_FIELDS = (
    "research.clipper_diodes",
    "research.jfet_gds",
    "research.jfet_source",
    "research.bax_rails",
    "research.mid_rails",
)


def run(binary: Path, *options: str, expected_returncode: int = 0) -> dict[str, str]:
    command = [str(binary), "--print-fit", *options]
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
        errors="replace",
        timeout=15,
    )

    if result.returncode != expected_returncode:
        raise AssertionError(
            f"{' '.join(command)!r}: expected exit {expected_returncode}, "
            f"got {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )

    if expected_returncode != 0:
        # Invalid flags must fail loudly, not silently preserve defaults.
        if "OfflineRender:" not in result.stderr:
            raise AssertionError(f"{command!r}: error was not explained on stderr")
        return {}

    fields: dict[str, str] = {}
    for line in result.stdout.splitlines():
        if "=" not in line or line.startswith("#"):
            continue
        name, value = line.split("=", 1)
        fields[name.strip()] = value.strip()

    if any(name not in fields for name in RESEARCH_FIELDS):
        raise AssertionError(
            f"{command!r}: missing research fields from --print-fit: {fields!r}"
        )
    if any(fields[name] not in ("0", "1") for name in RESEARCH_FIELDS):
        raise AssertionError(f"{command!r}: malformed research booleans: {fields!r}")
    return fields


def expect_modes(report: dict[str, str], expected: tuple[int, ...]) -> None:
    actual = tuple(int(report[field]) for field in RESEARCH_FIELDS)
    if actual != expected:
        raise AssertionError(f"research modes expected {expected!r}, got {actual!r}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_offline_research_cli.py /path/to/OfflineRender", file=sys.stderr)
        return 2

    binary = Path(sys.argv[1]).resolve()
    if not binary.is_file():
        raise FileNotFoundError(f"OfflineRender binary missing: {binary}")

    zero = (0, 0, 0, 0, 0)
    one = (1, 1, 1, 1, 1)

    expect_modes(run(binary), zero)
    expect_modes(run(binary, "--research", "all"), one)
    expect_modes(run(binary, "--research", "clipper-diodes", "--research", "bax-rails"),
                 (1, 0, 0, 1, 0))
    expect_modes(run(binary, "--research", "JFET-GDS", "--research", "MID-RAILS"),
                 (0, 1, 0, 0, 1))
    expect_modes(run(binary, "--research", "all", "--research", "none"), zero)
    expect_modes(run(binary, "--research", "all", "--research", "none",
                     "--research", "jfet-source"), (0, 0, 1, 0, 0))
    expect_modes(run(binary, "--research", "none", "--research", "all"), one)

    # Unknown, missing, and comma-separated modes must be loudly rejected.
    run(binary, "--research", "not-a-model", expected_returncode=2)
    run(binary, "--research", expected_returncode=2)
    run(binary, "--research", "jfet-gds,bax-rails", expected_returncode=2)
    run(binary, "--research", "all", "--fit", "clipA0=oops", expected_returncode=2)

    # The --schematic baseline is established before all --fit assignments,
    # regardless of the user-entered order; research routing is orthogonal.
    fit_first = run(binary, "--fit", "clipA0=27", "--schematic",
                    "--research", "jfet-gds")
    schematic_first = run(binary, "--research", "jfet-gds", "--schematic",
                          "--fit", "clipA0=27")
    for report in (fit_first, schematic_first):
        if report.get("fit.clipA0") != "27":
            raise AssertionError("--fit clipA0=27 lost to --schematic or was not reported")
        expect_modes(report, (0, 1, 0, 0, 0))
    if fit_first != schematic_first:
        raise AssertionError("--schematic/--fit/research order changed resolved report")

    print("OfflineResearchCliSmoke: 13 valid/invalid scenarios passed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, OSError, subprocess.TimeoutExpired) as exc:
        print(f"OfflineResearchCliSmoke FAILED: {exc}", file=sys.stderr)
        sys.exit(1)
