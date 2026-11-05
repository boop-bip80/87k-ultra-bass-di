"""Synthetic known-answer tests: diagnostics are never physical-pedal parity."""

import hashlib
import contextlib
import io
import json
import math
from pathlib import Path
import tempfile
import unittest

import numpy as np
from scipy import signal
from scipy.io import wavfile

from compare_hardware_captures import (
    MeasurementError, compare_arrays, load_wav, main,
)


FS = 48000


def broadband(nseconds=2.0):
    return np.random.default_rng(1207).normal(0, 0.08, int(FS * nseconds))


def shifted(data, samples, gain=1.0):
    return gain * np.pad(data, (samples, 0))[:len(data)]


class HardwareComparisonTests(unittest.TestCase):
    def test_known_gain_latency_and_phase_are_independent(self):
        dry = broadband()
        reference = shifted(dry, 12)
        model = shifted(dry, 19, gain=1.1)
        result = compare_arrays(dry, reference, model, FS,
                                fixture_delays=(12.0, 19.0),
                                output_calibration=(1., 1.))
        self.assertEqual(result["comparison_status"], "DIAGNOSTIC_ONLY")
        self.assertFalse(result["parity_certified"])
        self.assertEqual(result["latency_reference_from_dry"]["integer_samples"], 12)
        self.assertEqual(result["latency_render_from_dry"]["integer_samples"], 19)
        self.assertEqual(result["render_minus_reference_lag_samples"], 7)
        self.assertAlmostEqual(result["calibrated_output_level_difference_db"], 20 * math.log10(1.1), places=3)
        self.assertLess(abs(result["frequency_response"]["magnitude_residual_rms_db"] - 20 * math.log10(1.1)), .015)
        self.assertLess(result["frequency_response"]["phase_residual_rms_deg"], .2)
        # Gain mismatch remains visible: time alignment NEVER implies gain matching.
        self.assertAlmostEqual(result["latency_aligned_waveform_error_db_re_reference"], -20., delta=.1)

    def test_uncalibrated_phase_withheld_and_wrong_filter_is_visible(self):
        dry = broadband()
        reference = shifted(dry, 4)
        # First-order difference severely reshapes frequency response.
        filtered = dry - .95 * np.pad(dry, (1, 0))[:len(dry)]
        model = shifted(filtered, 4)
        report = compare_arrays(dry, reference, model, FS, output_calibration=(1., 1.))
        self.assertIsNone(report["frequency_response"]["phase_residual_rms_deg"])
        self.assertTrue(report["frequency_response"]["phase_requires_calibrated_fixture_delays"])
        self.assertGreater(report["frequency_response"]["magnitude_residual_rms_db"], 3.0)
        self.assertIsNone(report["stationary_single_tone_thd"])

    def test_single_tone_harmonic_analysis_and_periodic_latency_guard(self):
        n = int(FS * 2.6)
        t = np.arange(n) / FS
        fundamental = 220.17
        dry = .1 * np.sin(2 * np.pi * fundamental * t)
        reference = dry + .002 * np.sin(4 * np.pi * fundamental * t) + .001 * np.sin(6 * np.pi * fundamental * t)
        model = dry + .004 * np.sin(4 * np.pi * fundamental * t) + .002 * np.sin(6 * np.pi * fundamental * t)
        result = compare_arrays(dry, reference, model, FS)
        self.assertIsNone(result["render_minus_reference_lag_samples"])
        self.assertFalse(result["latency_reference_from_dry"]["identifiable"])
        self.assertFalse(result["latency_render_from_dry"]["identifiable"])
        thd = result["stationary_single_tone_thd"]
        self.assertIsNotNone(thd)
        self.assertAlmostEqual(thd["input_frequency_hz"], fundamental, delta=.006)
        self.assertAlmostEqual(thd["physical"]["thd_percent"], math.sqrt(5), delta=.02)
        self.assertAlmostEqual(thd["plugin"]["thd_percent"], math.sqrt(20), delta=.03)

    def test_invalid_or_incomplete_data_refused(self):
        dry = broadband(1.5)
        with self.assertRaisesRegex(MeasurementError, "duration"):
            compare_arrays(dry, dry[:FS], dry, FS)
        with self.assertRaisesRegex(MeasurementError, "non-finite"):
            x = dry.copy()
            x[1] = float("nan")
            compare_arrays(x, dry, dry, FS)
        with self.assertRaisesRegex(MeasurementError, "insufficiently excited"):
            compare_arrays(np.zeros_like(dry), dry, dry, FS)
        with self.assertRaisesRegex(MeasurementError, "max_lag_ms"):
            compare_arrays(dry, dry, dry, FS, max_lag_ms=1000)
        with self.assertRaisesRegex(MeasurementError, "fixture delays"):
            compare_arrays(dry, dry, dry, FS, fixture_delays=(1, float("inf")))
        with self.assertRaisesRegex(MeasurementError, "volts-per-FS"):
            compare_arrays(dry, dry, dry, FS, output_calibration=(0, 1))
        with self.assertRaisesRegex(MeasurementError, "volts-per-FS"):
            compare_arrays(dry, dry, dry, FS, output_calibration=(1e-308, 1e308))

    def test_uncalibrated_amplitude_metrics_are_withheld(self):
        dry = broadband()
        result = compare_arrays(dry, shifted(dry, 3), shifted(dry, 3, 1.2), FS)
        self.assertIsNone(result["calibrated_output_level_difference_db"])
        self.assertIsNone(result["frequency_response"]["magnitude_residual_rms_db"])
        self.assertIsNone(result["latency_aligned_waveform_error_db_re_reference"])
        self.assertAlmostEqual(result["uncalibrated_pcm_level_difference_db"],
                               20 * math.log10(1.2), places=3)

    def test_independent_output_voltage_scales_change_calibrated_gain(self):
        dry = broadband()
        reference = shifted(dry, 3)
        report = compare_arrays(dry, reference, reference, FS,
                                output_calibration=(2., 1.))
        self.assertAlmostEqual(report["uncalibrated_pcm_level_difference_db"], 0., places=6)
        self.assertAlmostEqual(report["calibrated_output_level_difference_db"],
                               -20 * math.log10(2.), places=6)
        self.assertAlmostEqual(report["frequency_response"]["magnitude_residual_rms_db"],
                               20 * math.log10(2.), delta=.02)

    def test_moving_sweep_is_not_mistaken_for_stationary_welch_h1(self):
        n = int(FS * 2.0)
        t = np.arange(n) / FS
        dry = .1 * signal.chirp(t, f0=80., f1=12000., t1=2., method="logarithmic")
        report = compare_arrays(dry, dry, dry, FS,
                                output_calibration=(1., 1.))
        self.assertIsNone(report["frequency_response"]["magnitude_residual_rms_db"])
        self.assertIn("nonstationary", report["frequency_response"]["withheld_reason"])

    def test_impure_stimulus_does_not_get_called_pedal_thd(self):
        n = int(FS * 1.8)
        t = np.arange(n) / FS
        dry = .08 * np.sin(2 * np.pi * 220 * t) + .006 * np.sin(2 * np.pi * 440 * t)
        report = compare_arrays(dry, dry, dry, FS)
        self.assertIsNone(report["stationary_single_tone_thd"])
        self.assertIn("source impurity", report["tone_thd_withheld_reason"])

    def test_wav_channel_explicit_and_digital_range_conservation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "stereo.wav"
            data = np.column_stack((np.full(FS, 16384, np.int16),
                                    np.full(FS, -8192, np.int16)))
            wavfile.write(path, FS, data)
            with self.assertRaisesRegex(MeasurementError, "stereo requires explicit"):
                load_wav(path)
            _, left = load_wav(path, 0)
            _, right = load_wav(path, 1)
            self.assertAlmostEqual(float(left.mean()), .5)
            self.assertAlmostEqual(float(right.mean()), -.25)
            floatpath = Path(tmp) / "floating.wav"
            wavfile.write(floatpath, FS, np.full(FS, 1.2, np.float32))
            _, floating = load_wav(floatpath)
            self.assertAlmostEqual(float(floating.mean()), 1.2, places=6)

    @staticmethod
    def _make_manifest(root):
        dry = broadband(1.3)
        take = {}
        for key, digest_key, name, samples in (
            ("stimulus_file", "stimulus_sha256", "source.wav", dry),
            ("dry_input_file", "dry_input_sha256", "measured-dry.wav", dry),
            ("file", "sha256", "pedal-output.wav", dry * .75),
        ):
            wavfile.write(root / name, FS, (samples * 32767).astype(np.int16))
            take[key] = name
            take[digest_key] = hashlib.sha256((root / name).read_bytes()).hexdigest()
        take.update({"mode": "clean", "output": "quarter_inch", "input_level_dbfs": -18,
                     "playback_gain_db": 0., "gain_db_re_bypass": 0.,
                     "dry_monitor": {"measurement_point": "loaded_pedal_input",
                                     "probe_input_impedance_ohm": 10_000_000,
                                     "calibration_db_to_pedal_input": 0.,
                                     "shared_adc_clock": True},
                     "controls": dict.fromkeys(("master", "drive", "blend", "level",
                                               "bass", "low_mids", "high_mids", "treble"), 0.5),
                     "switches": {"attack": "flat", "grunt": "flat",
                                  "low_mid_frequency_hz": 500,
                                  "high_mid_frequency_hz": 1500}})
        manifest = {"schema_version": 2,
                    "pedal": {"manufacturer": "Synthetic Test", "model": "Not real hardware",
                              "hardware_revision": "test", "serial_number": "test"},
                    "power": {"voltage_v": 9., "polarity": "center-negative"},
                    "fixture": {"sample_rate_hz": FS, "source_impedance_ohm": 1000,
                                "measured_pedal_input_impedance_ohm": 1000000,
                                "measured_output_impedance_ohm": 1000,
                                "interface_input_impedance_ohm": 1000000,
                                "cab_sim": False, "pickup_loading_measured": True},
                    "takes": [take]}
        path = root / "manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        return path, take

    def test_manifest_selects_hashed_measured_dry_and_reports_unverified_coverage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, take = self._make_manifest(root)
            render_path = root / "render.wav"
            wavfile.write(render_path, FS, np.full(int(FS * 1.3), .001, np.float32))
            render_input_path = root / "render-input.wav"
            render_dry = (wavfile.read(root / take["dry_input_file"])[1].astype(np.float64) / 32768)
            wavfile.write(render_input_path, FS, render_dry.astype(np.float32))
            report_path = root / "report.json"
            with contextlib.redirect_stdout(io.StringIO()):
                rc = main(["--manifest", str(path), "--take-index", "0",
                           "--render-input", str(render_input_path),
                           "--render", str(render_path), "--output", str(report_path)])
            self.assertEqual(rc, 0)
            report = json.loads(report_path.read_text())
            self.assertEqual(report["capture_evidence_status"], "UNVERIFIED")
            self.assertEqual(report["comparison_status"], "DIAGNOSTIC_ONLY")
            self.assertFalse(report["parity_certified"])
            self.assertTrue(report["capture_paths"]["measured_dry_input"].endswith(take["dry_input_file"]))
            self.assertIsNotNone(report["render_input_audit"])
            self.assertIsNone(report["frequency_response"]["magnitude_residual_rms_db"])
            take["dry_input_sha256"] = "0" * 64
            path.write_text(json.dumps(json.loads(path.read_text()) | {"takes": [take]}))
            with contextlib.redirect_stdout(io.StringIO()):
                rc = main(["--manifest", str(path), "--take-index", "0",
                           "--render-input", str(render_input_path), "--render", str(render_path)])
            self.assertEqual(rc, 2)

    def test_nonzero_input_monitor_gain_requires_calibrated_render_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest_path, take = self._make_manifest(root)
            calibration_db = 6.0205999132796
            take["dry_monitor"]["calibration_db_to_pedal_input"] = calibration_db
            data = json.loads(manifest_path.read_text())
            data["takes"] = [take]
            manifest_path.write_text(json.dumps(data))
            raw_input = wavfile.read(root / take["dry_input_file"])[1].astype(np.float64) / 32768
            render_input_path = root / "render-input.wav"
            render_path = root / "render.wav"
            wavfile.write(render_input_path, FS, raw_input.astype(np.float32))
            wavfile.write(render_path, FS, raw_input.astype(np.float32))
            command = ["--manifest", str(manifest_path), "--take-index", "0",
                       "--render-input", str(render_input_path), "--render", str(render_path)]
            with contextlib.redirect_stdout(io.StringIO()) as stdout:
                rc = main(command)
            self.assertEqual(rc, 2)
            self.assertIn("calibration", stdout.getvalue())
            wavfile.write(render_input_path, FS, (raw_input * 2.0).astype(np.float32))
            with contextlib.redirect_stdout(io.StringIO()):
                rc = main(command)
            self.assertEqual(rc, 0)

            take["dry_monitor"]["calibration_db_to_pedal_input"] = 10000.
            data = json.loads(manifest_path.read_text())
            data["takes"] = [take]
            manifest_path.write_text(json.dumps(data))
            with contextlib.redirect_stdout(io.StringIO()) as stdout:
                rc = main(command)
            self.assertEqual(rc, 2)
            self.assertIn("calibration metadata", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
