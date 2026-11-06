#!/usr/bin/env python3
"""Independent, JUCE-free Black Mirror VII input-network impedance calculation.

Assumed topology: R1 from jack tip to ground; C1 in series from jack tip
to the bias node; R2 from that node to AC ground. The following R3/op-amp
is an ideal infinite-impedance buffer and does not load the bias node.

This analyzes the documented clone topology, **not** measured retail B7K
Ultra hardware. In particular, a published nominal 1 MΩ retail input spec
cannot establish or refute this clone's frequency dependence by itself.
"""

from __future__ import annotations

import argparse
import json
import math
from typing import Sequence


DEFAULT_R1_OHM = 1_000_000.0
DEFAULT_R2_OHM = 1_000_000.0
DEFAULT_C1_FARAD = 100e-9


def _positive_finite(name: str, value: float) -> None:
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be positive and finite")


def _nonnegative_finite(name: str, value: float) -> None:
    if not math.isfinite(value) or value < 0:
        raise ValueError(f"{name} must be nonnegative and finite")


def input_impedance(
    frequency_hz: float,
    *,
    r1_ohm: float = DEFAULT_R1_OHM,
    r2_ohm: float = DEFAULT_R2_OHM,
    c1_farad: float = DEFAULT_C1_FARAD,
) -> complex:
    """Return complex jack impedance in ohms, with capacitive negative Im.

    Exact circuit expression: Z = R1 || (R2 + 1/(j*2*pi*f*C1)).
    The algebra below avoids subtractive cancellation and overflow in q².
    Limits: Z(0) = R1; Z(infinity) = R1*R2/(R1+R2).
    """
    _nonnegative_finite("frequency_hz", frequency_hz)
    _positive_finite("r1_ohm", r1_ohm)
    _positive_finite("r2_ohm", r2_ohm)
    _positive_finite("c1_farad", c1_farad)
    if frequency_hz == 0:
        return complex(r1_ohm, 0.0)

    # Calculate the conductance ratio without an overflowing R1+R2 sum.
    if r1_ohm >= r2_ohm:
        ratio = r2_ohm / r1_ohm
        alpha = ratio / (1.0 + ratio)
    else:
        ratio = r1_ohm / r2_ohm
        alpha = 1.0 / (1.0 + ratio)
    beta = 1.0 - alpha
    q = (2.0 * math.pi * frequency_hz * c1_farad) * r1_ohm + (2.0 * math.pi * frequency_hz * c1_farad) * r2_ohm
    if q <= 1.0:
        denominator = 1.0 + q * q
        return complex(r1_ohm * (alpha + beta / denominator),
                       -r1_ohm * beta * q / denominator)
    inv_q = 1.0 / q
    denominator = 1.0 + inv_q * inv_q
    return complex(r1_ohm * (alpha + beta * inv_q * inv_q / denominator),
                   -r1_ohm * beta * inv_q / denominator)


def unloaded_buffer_transfer(
    frequency_hz: float,
    *,
    r2_ohm: float = DEFAULT_R2_OHM,
    c1_farad: float = DEFAULT_C1_FARAD,
) -> complex:
    """Bias-node/op-amp output voltage divided by ideal jack-tip voltage.

    H0 = (j*w*C1*R2)/(1+j*w*C1*R2), an ideal high-pass response.
    """
    _nonnegative_finite("frequency_hz", frequency_hz)
    _positive_finite("r2_ohm", r2_ohm)
    _positive_finite("c1_farad", c1_farad)
    if frequency_hz == 0:
        return 0j
    x = (2.0 * math.pi * frequency_hz * c1_farad) * r2_ohm
    if x <= 1:
        return complex(x*x, x) / (1.0 + x*x)
    inv_x = 1.0 / x
    return complex(1.0, inv_x) / (1.0 + inv_x*inv_x)


def source_loaded_transfer(
    frequency_hz: float,
    *,
    source_ohm: float,
    r1_ohm: float = DEFAULT_R1_OHM,
    r2_ohm: float = DEFAULT_R2_OHM,
    c1_farad: float = DEFAULT_C1_FARAD,
) -> complex:
    """Buffer output / open-circuit source voltage for resistive source Rs.

    H = H0 * Zin/(Rs+Zin). With Rs=0 this reduces exactly to H0.
    This is not the transfer through a retail pedal's entire audio path.
    """
    _nonnegative_finite("source_ohm", source_ohm)
    z = input_impedance(frequency_hz, r1_ohm=r1_ohm,
                        r2_ohm=r2_ohm, c1_farad=c1_farad)
    h0 = unloaded_buffer_transfer(frequency_hz, r2_ohm=r2_ohm,
                                  c1_farad=c1_farad)
    if source_ohm == 0:
        return h0
    # For extreme magnitudes, use a voltage division form minimizing overflow.
    return h0 / (1.0 + source_ohm / z)


def describe_complex(z: complex, *, units: str) -> dict[str, float | None]:
    """Represent a finite phasor as rectangular, polar, phase, and gain.

    Gain in dB is null for zero voltage, instead of non-JSON -Infinity.
    """
    if not math.isfinite(z.real) or not math.isfinite(z.imag):
        raise ValueError("nonfinite computed phasor")
    magnitude = abs(z)
    if not math.isfinite(magnitude):
        raise ValueError("nonfinite computed magnitude")
    return {
        f"real_{units}": z.real,
        f"imag_{units}": z.imag,
        f"magnitude_{units}": magnitude,
        "phase_degrees": math.degrees(math.atan2(z.imag, z.real)),
        "gain_db": None if magnitude == 0.0 else 20.0 * math.log10(magnitude),
    }


def measurement(
    frequency_hz: float,
    *,
    source_ohm: float = 100_000.0,
    r1_ohm: float = DEFAULT_R1_OHM,
    r2_ohm: float = DEFAULT_R2_OHM,
    c1_farad: float = DEFAULT_C1_FARAD,
) -> dict[str, object]:
    """Computed analytical measurements, with no physical-device calibration."""
    z = input_impedance(frequency_hz, r1_ohm=r1_ohm,
                        r2_ohm=r2_ohm, c1_farad=c1_farad)
    return {
        "frequency_hz": frequency_hz,
        "zin": describe_complex(z, units="ohm"),
        "unloaded_transfer": describe_complex(unloaded_buffer_transfer(
            frequency_hz, r2_ohm=r2_ohm, c1_farad=c1_farad), units="ratio"),
        "source_loaded_transfer": describe_complex(source_loaded_transfer(
            frequency_hz, source_ohm=source_ohm, r1_ohm=r1_ohm,
            r2_ohm=r2_ohm, c1_farad=c1_farad), units="ratio"),
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frequencies", metavar="HZ", type=float, nargs="*",
                        default=[0, 0.1, 1, 10, 20, 1000, 20000, 100000])
    parser.add_argument("--r1-ohm", type=float, default=DEFAULT_R1_OHM)
    parser.add_argument("--r2-ohm", type=float, default=DEFAULT_R2_OHM)
    parser.add_argument("--c1-farad", type=float, default=DEFAULT_C1_FARAD)
    parser.add_argument("--source-ohm", type=float, default=100_000.0)
    args = parser.parse_args(argv)
    try:
        data = {
            "assumption": "Black Mirror VII documented clone input; not retail B7K measurement",
            "components": {"r1_ohm": args.r1_ohm, "r2_ohm": args.r2_ohm,
                           "c1_farad": args.c1_farad, "source_ohm": args.source_ohm},
            "measurements": [measurement(f, source_ohm=args.source_ohm,
                            r1_ohm=args.r1_ohm, r2_ohm=args.r2_ohm,
                            c1_farad=args.c1_farad) for f in args.frequencies],
        }
        print(json.dumps(data, indent=2, allow_nan=False))
    except (ValueError, OverflowError, ZeroDivisionError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
