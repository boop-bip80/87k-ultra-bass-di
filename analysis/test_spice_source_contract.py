"""Stdlib-only fail-closed contract tests for independent NJFET SPICE verifier.

Passing these checks does not substitute for running the real ngspice binary.
"""

import cmath
from contextlib import redirect_stderr
import io
import math
from unittest import TestCase, mock

from spice_reference import verify_jfet_source as source


class SourceSpiceContractTests(TestCase):
    def test_part_provenance_tracks_clone(self):
        source.check_drawn_provenance()

    def test_source_bias_j201_reference(self):
        bias, gm = source.dc_bias(source.NOMINAL)
        self.assertAlmostEqual(bias, 0.3826413, delta=2e-6)
        self.assertAlmostEqual(gm, 0.00069151, delta=1e-8)
        self.assertAlmostEqual(bias / source.NOMINAL.r6,
                               source.NOMINAL.beta *
                               (-source.NOMINAL.vto - bias) ** 2, delta=1e-15)

    def test_ac_transfer_has_correct_source_bypass_corner(self):
        low = source.source_transfer(source.NOMINAL, 2.0)
        high = source.source_transfer(source.NOMINAL, 3000.0)
        self.assertGreater(abs(low), abs(high))
        self.assertLess(abs(high), 0.2)
        self.assertLess(cmath.phase(low), 0)
        self.assertLess(cmath.phase(high), 0)
        for hz in source.FREQUENCIES:
            response = source.source_transfer(source.NOMINAL, hz)
            self.assertTrue(math.isfinite(response.real))
            self.assertTrue(math.isfinite(response.imag))

    def test_ngspice_deck_contains_primitive_and_no_dsp_waveshaper(self):
        netlist = source.build_netlist(source.NOMINAL, 1000.0, source.Path("data.txt"))
        self.assertIn("J1 drain gate source Q1TEST", netlist)
        self.assertIn(".model Q1TEST NJF(", netlist)
        self.assertIn("R6 source 0 3300", netlist)
        self.assertIn("C3 source 0 2.2e-07", netlist)
        self.assertIn("Vgate gate 0 DC 0 AC 1", netlist)
        self.assertIn("set wr_singlescale", netlist)
        self.assertIn("option numdgt=15", netlist)
        self.assertIn("wrdata data.txt hre him", netlist)
        self.assertNotIn("waveshape", netlist)
        self.assertNotIn("setPhysicalSourceFeedbackModel", netlist)

    def test_invalid_parameters_rejected_before_execution(self):
        invalid = source.Parts(0.0, source.NOMINAL.c3, source.NOMINAL.beta,
                               source.NOMINAL.vto)
        with self.assertRaises(ValueError):
            source.build_netlist(invalid, 1000.0, source.Path("data.txt"))
        with self.assertRaises(ValueError):
            source.build_netlist(source.NOMINAL, 0.0, source.Path("data.txt"))

    def test_bias_parser_rejects_missing_and_accepts_scientific(self):
        self.assertAlmostEqual(source.parse_bias("v(source) = 3.826413d-01"), 0.3826413)
        with self.assertRaises(ValueError):
            source.parse_bias("No operating-point output")

    def test_wrdata_parser_is_strict(self):
        rows = "frequency\thre\thim\n1000.0 0.21 -0.32\n1005 0.20 -0.33\n1010 0.19 -0.34\n"
        self.assertEqual(source.parse_ac_wrdata(rows, 1000), complex(0.21, -0.32))
        with self.assertRaises(ValueError):
            source.parse_ac_wrdata("1000.0 0.21 -0.32\n", 1000)
        with self.assertRaises(ValueError):
            source.parse_ac_wrdata("bad data\n", 1000)
        with self.assertRaises(ValueError):
            source.parse_ac_wrdata(rows + "nan 0 0\n1001 0 0\n", 1000)
        with self.assertRaises(ValueError):
            source.parse_ac_wrdata(rows + "nan 0 0\n", 1000)
        with self.assertRaises(ValueError):
            source.parse_ac_wrdata("2000 0.21 -0.32\n2010 0.2 -0.33\n2020 0.19 -0.34\n", 1000)

    def test_wrdata_rejects_extra_scale_columns_and_duplicate_frequency(self):
        # The documented ngspice default (without wr_singlescale) may repeat
        # a scale column for every vector. It must never be interpreted as H.
        repeated_scale = "1000 0.21 1000 -0.32\n1005 0.20 1005 -0.33\n1010 0.19 1010 -0.34\n"
        with self.assertRaisesRegex(ValueError, "exactly 3"):
            source.parse_ac_wrdata(repeated_scale, 1000)
        # An accidental stale or truncated sweep must not silently score the
        # same physical frequency twice.
        duplicate_frequency = "1000 0.21 -0.32\n1000 0.20 -0.33\n1010 0.19 -0.34\n"
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            source.parse_ac_wrdata(duplicate_frequency, 1000)

    def test_wrdata_rejects_wrong_second_frequency_and_negative_frequency(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            source.parse_ac_wrdata("1000 0.21 -0.32\n1001 0.20 -0.33\n1010 0.19 -0.34\n", 1000)
        with self.assertRaisesRegex(ValueError, "non-positive"):
            source.parse_ac_wrdata("-1000 0.21 -0.32\n1005 0.20 -0.33\n1010 0.19 -0.34\n", 1000)

    def test_missing_ngspice_fails_closed(self):
        with mock.patch.object(source.shutil, "which", return_value=None), \
             mock.patch("sys.argv", ["verify_jfet_source.py"]):
            with redirect_stderr(io.StringIO()) as err:
                self.assertEqual(source.main(), 2)
            self.assertIn("was NOT run", err.getvalue())
