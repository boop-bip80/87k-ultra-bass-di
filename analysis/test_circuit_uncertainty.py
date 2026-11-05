"""Regression tests for an assumption-only parameter envelope, not retail parity."""

import json
import math
import unittest

from circuit_uncertainty import (
    C3_F, J201_IDSS_A, J201_VP_V, R6_OHM,
    assumed_diode_forward_v, illustrative_opamp_output, jfet_beta,
    jfet_bias, jfet_harmonics, jfet_incremental_gm, jfet_transfer,
    source_attenuation, summarize,
)


class SensitivityModelTest(unittest.TestCase):
    def test_clone_vs_nominal_input_rigid_reference(self):
        clone = source_attenuation(100e3, 500e3)
        ultra_nominal = source_attenuation(100e3, 1e6)
        self.assertAlmostEqual(clone, 5.0 / 6.0)
        self.assertAlmostEqual(ultra_nominal, 10.0 / 11.0)
        self.assertAlmostEqual(20 * math.log10(ultra_nominal / clone),
                               0.755771217787995, places=11)
        self.assertEqual(source_attenuation(0, 1e6), 1.0)
        self.assertTrue(0 < source_attenuation(1e300, 1.0) < 1)
        for source, load in ((-1, 1), (1, 0), (math.inf, 1), (1, math.nan)):
            with self.assertRaises(ValueError):
                source_attenuation(source, load)

    def test_jfet_bias_envelope_and_monotone_current(self):
        for idss in J201_IDSS_A:
            for vp in J201_VP_V:
                beta = jfet_beta(idss, vp)
                vs = jfet_bias(vp, beta)
                self.assertGreater(vs, 0)
                self.assertLess(vs, vp)
                self.assertAlmostEqual(vs / R6_OHM,
                                       beta * (vp - vs) ** 2, delta=1e-15)
                self.assertGreater(jfet_incremental_gm(idss, vp), 0)
                g40 = jfet_transfer(40, idss, vp)
                g8k = jfet_transfer(8000, idss, vp)
                self.assertTrue(math.isfinite(abs(g40)))
                self.assertGreater(abs(g8k), abs(g40))
        self.assertAlmostEqual(C3_F, 220e-9)

    def test_transconductance_is_not_the_full_chain_gain(self):
        for f in (40, 1000, 8000):
            estimates = [abs(jfet_transfer(f, i, vp))
                         for i in J201_IDSS_A for vp in J201_VP_V]
            self.assertGreater(max(estimates), min(estimates))
            self.assertTrue(all(1e-6 < g < 1e-2 for g in estimates))

    def test_independent_current_harmonics_respond_to_drive(self):
        low = jfet_harmonics(0.6e-3, 0.9, 0.02)
        high = jfet_harmonics(0.6e-3, 0.9, 0.6)
        self.assertGreater(high["h1_amp_a"], low["h1_amp_a"])
        self.assertGreater(high["h2_db_re_h1"], low["h2_db_re_h1"])
        for result in (low, high):
            self.assertTrue(all(math.isfinite(v) for v in result.values()))

    def test_diode_sensitivity_not_labeled_datasheet_fit(self):
        v1 = assumed_diode_forward_v(1e-6, 5e-9, 0.052)
        self.assertGreater(assumed_diode_forward_v(1e-4, 5e-9, 0.052), v1)
        self.assertGreater(assumed_diode_forward_v(1e-6, 1e-10, 0.052), v1)
        self.assertGreater(assumed_diode_forward_v(1e-6, 5e-9, 0.078), v1)
        for current, sat, nvt in ((-1, 1e-9, 0.05), (1e-6, 0, 0.05),
                                  (1e-6, 1e-9, math.nan)):
            with self.assertRaises(ValueError):
                assumed_diode_forward_v(current, sat, nvt)

    def test_illustrative_feedback_rail_limits_and_knee(self):
        for gain in (1e3, 1e5):
            for rail in (2.7, 3.3):
                self.assertEqual(illustrative_opamp_output(1e6, gain, .1, rail), rail)
                self.assertEqual(illustrative_opamp_output(-1e6, gain, .1, rail), -rail)
                self.assertAlmostEqual(illustrative_opamp_output(0, gain, .1, rail), 0.0)
                small = illustrative_opamp_output(0.02, gain, .1, rail)
                self.assertGreater(small, 0)
                self.assertLess(small, rail)

    def test_full_report_deterministic_and_uncertified(self):
        report = summarize()
        self.assertEqual(report["status"],
                         "ASSUMPTION_SENSITIVITY_ONLY_NO_RETAIL_PARITY_SCORE")
        self.assertEqual(report["metrics"]["jfet_q1_incremental_gm"]["samples"], 9)
        self.assertGreater(report["metrics"]["jfet_q1_incremental_gm"]
                           ["frequency_db"]["1000"]["span_db"], 5)
        self.assertEqual(json.dumps(report, sort_keys=True),
                         json.dumps(summarize(), sort_keys=True))
        self.assertTrue(report["not_proven"])


if __name__ == "__main__":
    unittest.main()
