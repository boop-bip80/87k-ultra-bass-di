"""Stdlib-only, adversarial contracts for the required coupled-stage SPICE gate.

These tests protect table parsing and netlist provenance; the separate CI
ngspice job must still execute the actual independent simulator.
"""

from contextlib import redirect_stderr
import io
import math
from unittest import TestCase, mock

from spice_reference import verify_coupled_stage as coupled


GOOD_ROWS = "1000 0.21 -0.32\n1005 0.20 -0.33\n1010 0.19 -0.34\n"


class CoupledSpiceContractTests(TestCase):
    def test_clone_provenance_and_nodal_reference(self):
        coupled.check_full_netlist_values()
        response = coupled.analytic_transfer(coupled.DRAWN, 1000.0)
        self.assertTrue(math.isfinite(response.real))
        self.assertTrue(math.isfinite(response.imag))

    def test_deck_uses_native_components_and_three_point_ac(self):
        deck = coupled.build_isolated_netlist(coupled.DRAWN, 1000.0,
                                               coupled.Path("data.txt"))
        self.assertIn("R22 vin nm 100000", deck)
        self.assertIn("Ebuffer skout 0 kp 0 1", deck)
        self.assertIn("set wr_singlescale", deck)
        self.assertIn("option numdgt=15", deck)
        self.assertIn("ac lin 3 1000 1010", deck)
        self.assertIn("wrdata data.txt hre him", deck)
        self.assertNotIn("waveshape", deck)

    def test_real_and_imag_columns_are_unambiguous(self):
        self.assertEqual(coupled.parse_ac_wrdata(GOOD_ROWS, 1000),
                         complex(0.21, -0.32))
        # A four-column repeated-scale table must not be misread as H.
        repeated_scale = "1000 0.21 1000 -0.32\n1005 0.20 1005 -0.33\n1010 0.19 1010 -0.34\n"
        with self.assertRaisesRegex(RuntimeError, "expected 3"):
            coupled.parse_ac_wrdata(repeated_scale, 1000)

    def test_incomplete_stale_or_out_of_order_sweeps_fail_closed(self):
        for bad in (
            "1000 0.21 -0.32\n",  # old ngspice-42 ac lin 2 failure
            "1000 0.21 -0.32\n1005 0.20 -0.33\n",
            "1000 0.21 -0.32\n1000 0.20 -0.33\n1010 0.19 -0.34\n",
            "1000 0.21 -0.32\n1010 0.19 -0.34\n1005 0.20 -0.33\n",
            "1000 0.21 -0.32\n1001 0.20 -0.33\n1010 0.19 -0.34\n",
            "2000 0.21 -0.32\n2010 0.20 -0.33\n2020 0.19 -0.34\n",
            "-1000 0.21 -0.32\n1005 0.20 -0.33\n1010 0.19 -0.34\n",
            "1000 nan -0.32\n1005 0.20 -0.33\n1010 0.19 -0.34\n",
        ):
            with self.subTest(bad=bad), self.assertRaises(RuntimeError):
                coupled.parse_ac_wrdata(bad, 1000)

    def test_missing_ngspice_remains_failure(self):
        with mock.patch.object(coupled.shutil, "which", return_value=None), \
             mock.patch("sys.argv", ["verify_coupled_stage.py"]):
            with redirect_stderr(io.StringIO()) as err:
                self.assertEqual(coupled.main(), 2)
            self.assertIn("NOT run", err.getvalue())
