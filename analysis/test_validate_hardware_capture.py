"""Standard-library-only tests; no JUCE or hardware required."""
import json
from pathlib import Path
import tempfile
import unittest
import wave

from validate_hardware_capture import validate


class CaptureValidatorTests(unittest.TestCase):
    def test_missing_is_never_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = validate(Path(tmp) / "manifest.json")
            self.assertEqual(result["status"], "UNVERIFIED")

    def test_complete_synthetic_metadata_still_not_parity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            takes = []
            controls = dict.fromkeys(("master", "drive", "blend", "level", "bass", "low_mids", "high_mids", "treble"), 0.5)
            switches = dict(attack="flat", grunt="flat", low_mid_frequency_hz=500,
                            high_mid_frequency_hz=1500)
            for output in ("quarter_inch", "xlr"):
                for mode in ("bypass", "clean", "od"):
                    for level in ((-18,) if mode == "bypass" else (-24, -12)):
                        filename = f"{output}-{mode}-{abs(level)}.wav"
                        with wave.open(str(root / filename), "wb") as wav:
                            wav.setnchannels(1)
                            wav.setsampwidth(2)
                            wav.setframerate(48000)
                            wav.writeframes(b"\x00\x00" * 48000)
                        takes.append(dict(file=filename, output=output, mode=mode,
                                          input_level_dbfs=level, gain_db_re_bypass=0.,
                                          controls=controls, switches=switches))
            manifest = dict(pedal=dict(manufacturer="Darkglass", model="B7K Ultra", hardware_revision="test", serial_number="TEST"),
                            power=dict(voltage_v=9, polarity="center-negative"),
                            fixture=dict(source_impedance_ohm=1000, interface_input_impedance_ohm=1000000,
                                         measured_pedal_input_impedance_ohm=1000000, measured_output_impedance_ohm=1000,
                                         sample_rate_hz=48000, cab_sim=False, pickup_loading_measured=True), takes=takes)
            path = root / "manifest.json"
            path.write_text(json.dumps(manifest))
            self.assertEqual(validate(path)["status"], "REVIEW_REQUIRED")
            manifest["fixture"]["pickup_loading_measured"] = False
            path.write_text(json.dumps(manifest))
            self.assertEqual(validate(path)["status"], "UNVERIFIED")

    def test_missing_master_or_switch_is_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            # Even a real WAV cannot prove all settings were documented.
            root = Path(tmp)
            with wave.open(str(root / "hardware.wav"), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(48000)
                wav.writeframes(b"\x00\x00" * 48000)
            manifest = {"takes": [{"file": "hardware.wav", "mode": "clean", "output": "quarter_inch",
                                    "input_level_dbfs": -18, "gain_db_re_bypass": 0,
                                    "controls": dict.fromkeys(("drive", "blend", "level", "bass", "low_mids", "high_mids", "treble"), 0.5),
                                    "switches": {"attack": "flat", "grunt": "flat"}}]}
            path = root / "manifest.json"
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("controls" in issue for issue in result["problems"]))
            self.assertTrue(any("switches" in issue for issue in result["problems"]))


if __name__ == "__main__":
    unittest.main()
