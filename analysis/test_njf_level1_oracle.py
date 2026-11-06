"""Stdlib tests for the native ngspice NJF Level-1 DC reference."""

import unittest

from spice_reference.verify_njf_level1_oracle import channel_current, parse_wrdata


class ChannelEquationTests(unittest.TestCase):
    def test_cutoff_triode_saturation_and_reverse(self):
        cases = (
            (-4.0, 9.0, 0.0),
            (-2.0, 0.5, 0.00075),
            (-2.0, 9.0, 0.001),
            (-2.0, -1.0, -0.003),
        )
        for vgs, vds, expected in cases:
            with self.subTest(vgs=vgs, vds=vds):
                self.assertAlmostEqual(channel_current(vgs, vds), expected)


class NgspiceParserTests(unittest.TestCase):
    def test_valid_supply_current_sign(self):
        self.assertEqual(
            parse_wrdata("0 -0\n0.5 -0.00075\n", (0.0, 0.5)),
            (0.0, 0.00075),
        )

    def test_rejects_missing_extra_unsorted_nonfinite_and_malformed_rows(self):
        bad_outputs = (
            "0 0\n",
            "0 0\n0.5 0\n1 0\n",
            "0.5 0\n0 0\n",
            "0 nan\n0.5 0\n",
            "0 inf\n0.5 0\n",
            "0 0 0\n0.5 0\n",
            "bad 0\n0.5 0\n",
        )
        for output in bad_outputs:
            with self.subTest(output=output):
                with self.assertRaises(ValueError):
                    parse_wrdata(output, (0.0, 0.5))


if __name__ == "__main__":
    unittest.main()
