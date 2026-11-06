"""Independent nodal-admittance and high-precision impedance regression tests."""

from __future__ import annotations

import contextlib
from decimal import Decimal, localcontext
from io import StringIO
import json
import math
import unittest

try:
    from analysis.input_impedance import (
        input_impedance, unloaded_buffer_transfer, source_loaded_transfer,
        measurement, main,
    )
except ModuleNotFoundError:
    from input_impedance import (
        input_impedance, unloaded_buffer_transfer, source_loaded_transfer,
        measurement, main,
    )


PI_70 = Decimal("3.1415926535897932384626433832795028841971693993751058209749445923")


def reference_nodal_decimal(frequency_hz: float, r1: float = 1e6,
                            r2: float = 1e6, c: float = 100e-9) -> complex:
    """Independent inverse-admittance solution at ~70 decimal digits.

    Parallel jack admittances: Y_R1=1/R1 and Y_branch=1/(R2-jXc).
    This does not invoke or rearrange the impedance production formula.
    """
    with localcontext() as ctx:
        ctx.prec = 70
        a = Decimal(str(r1))
        b = Decimal(str(r2))
        cap = Decimal(str(c))
        f = Decimal(str(frequency_hz))
        if f == 0:
            return complex(float(a), 0.0)
        xc = 1 / (2 * PI_70 * f * cap)
        y_real = 1 / a + b / (b*b + xc*xc)
        y_imag = xc / (b*b + xc*xc)
        denom = y_real*y_real + y_imag*y_imag
        return complex(float(y_real/denom), -float(y_imag/denom))


def reference_loaded_nodal(f: float, rs: float, r1: float = 1e6,
                           r2: float = 1e6, c: float = 100e-9) -> complex:
    """Independent 2-node Kirchhoff-admittance solution for nonzero Rs.

    [Gs+G1+Yc, -Yc] [Vjack] = [Gs*Vsource]
    [-Yc, G2+Yc] [Vbias]   = [0].
    """
    if rs == 0:
        raise ValueError("reference nodal method requires Rs > 0")
    gs, g1, g2 = 1/rs, 1/r1, 1/r2
    yc = complex(0, 2*math.pi*f*c)
    determinant = (gs + g1 + yc)*(g2 + yc) - yc*yc
    return gs*yc/determinant


class InputImpedanceTests(unittest.TestCase):
    def assertComplexClose(self, observed: complex, expected: complex,
                           *, relative: float = 1e-12, absolute: float = 1e-9):
        self.assertLessEqual(abs(observed - expected),
                             absolute + relative*max(abs(expected), abs(observed)))

    def test_dc_and_topology_limits(self):
        self.assertEqual(input_impedance(0), 1e6+0j)
        self.assertEqual(unloaded_buffer_transfer(0), 0j)
        self.assertEqual(source_loaded_transfer(0, source_ohm=100_000), 0j)
        self.assertComplexClose(input_impedance(1e12), complex(500_000, 0),
                                absolute=1e-3)
        self.assertComplexClose(unloaded_buffer_transfer(1e12), 1+0j)

    def test_exact_highpass_breakpoint(self):
        f = 1/(2*math.pi*1e6*100e-9)
        self.assertComplexClose(unloaded_buffer_transfer(f), 0.5+0.5j)
        # At this same R2*C corner, jack Z = 600k - j200k, not 1 MΩ.
        self.assertComplexClose(input_impedance(f), 600_000-200_000j)

    def test_independent_high_precision_nodal_sweep(self):
        frequencies = [0, 0.001, 0.1, 1, 1.5915494309189535,
                       20, 100, 1000, 20000, 100000]
        for components in [(1e6, 1e6, 100e-9),
                           (470e3, 1.2e6, 33e-9),
                           (2e6, 220e3, 4.7e-9)]:
            for f in frequencies:
                with self.subTest(frequency_hz=f, components=components):
                    r1, r2, c = components
                    expected = reference_nodal_decimal(f, r1, r2, c)
                    actual = input_impedance(f, r1_ohm=r1,
                                             r2_ohm=r2, c1_farad=c)
                    self.assertComplexClose(actual, expected)

    def test_independent_two_node_loading_sweep(self):
        for rs in [1, 10_000, 100_000, 1_000_000]:
            for f in [0.001, 0.1, 1, 10, 20, 1000, 20000]:
                with self.subTest(source_ohm=rs, frequency_hz=f):
                    expected = reference_loaded_nodal(f, rs)
                    actual = source_loaded_transfer(f, source_ohm=rs)
                    self.assertComplexClose(actual, expected, relative=1e-8)

    def test_source_zero_and_high_frequency_division(self):
        for f in [0, 0.1, 1000, 20000]:
            self.assertComplexClose(source_loaded_transfer(f, source_ohm=0),
                                    unloaded_buffer_transfer(f))
        self.assertAlmostEqual(abs(source_loaded_transfer(1e12, source_ohm=100_000)),
                               5/6, places=12)

    def test_1khz_documented_regression(self):
        z = input_impedance(1000)
        self.assertAlmostEqual(z.real, 500_000.3166289, delta=0.001)
        self.assertAlmostEqual(z.imag, -397.886855, delta=0.001)
        self.assertAlmostEqual(abs(z), 500_000.4749, delta=0.001)
        self.assertLess(math.degrees(math.atan2(z.imag, z.real)), 0)

    def test_measurements_and_json_zero_gain(self):
        entry = measurement(0)
        self.assertEqual(entry["zin"]["magnitude_ohm"], 1e6)
        self.assertIsNone(entry["source_loaded_transfer"]["gain_db"])
        self.assertEqual(entry["zin"]["phase_degrees"], 0.0)
        buffer = StringIO()
        with contextlib.redirect_stdout(buffer):
            self.assertEqual(main(["0", "1000", "--source-ohm", "100000"]), 0)
        data = json.loads(buffer.getvalue())
        self.assertEqual(len(data["measurements"]), 2)
        self.assertIsNone(data["measurements"][0]["unloaded_transfer"]["gain_db"])

    def test_reject_nonfinite_and_invalid_inputs(self):
        for f in [float("nan"), float("inf"), -1]:
            with self.subTest(f=f), self.assertRaises(ValueError):
                input_impedance(f)
        for kwargs in [{"r1_ohm": 0}, {"r1_ohm": float("nan")},
                       {"r2_ohm": -1}, {"c1_farad": float("inf")}]:
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                input_impedance(1000, **kwargs)
        for rs in [-1, float("nan"), float("inf")]:
            with self.subTest(source_ohm=rs), self.assertRaises(ValueError):
                source_loaded_transfer(1000, source_ohm=rs)


if __name__ == "__main__":
    unittest.main()
