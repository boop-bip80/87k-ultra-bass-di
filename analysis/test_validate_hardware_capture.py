"""Standard-library-only tests; no JUCE or hardware required."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import wave

from validate_hardware_capture import validate


class CaptureValidatorTests(unittest.TestCase):
    @staticmethod
    def make_wav(path, rate=48000):
        index = int(path.stem.rsplit("-", 1)[-1]) if "-" in path.stem else 0
        role_offset = 1000 if path.name.startswith("dry-") else (2000 if path.name.startswith("output-") else 3000)
        amplitude = role_offset + index
        positive = amplitude.to_bytes(2, "little", signed=True)
        negative = (-amplitude).to_bytes(2, "little", signed=True)
        with wave.open(str(path), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(rate)
            wav.writeframes((positive + negative) * (rate // 2)
                            + (positive if rate % 2 else b""))
        return hashlib.sha256(path.read_bytes()).hexdigest()

    @classmethod
    def make_complete_manifest(cls, root):
        takes = []
        controls = dict.fromkeys(("master", "drive", "blend", "level", "bass", "low_mids", "high_mids", "treble"), 0.5)
        switches = dict(attack="flat", grunt="flat", low_mid_frequency_hz=500,
                        high_mid_frequency_hz=1500)
        stimulus_sha = cls.make_wav(root / "stimulus.wav")
        def record(output, mode, level, control_change=None, switch_change=None):
            index = len(takes)
            filename = f"output-{index:03d}.wav"
            monitor = f"dry-{index:03d}.wav"
            output_sha = cls.make_wav(root / filename)
            monitor_sha = cls.make_wav(root / monitor)
            takes.append(dict(file=filename, sha256=output_sha,
                              stimulus_file="stimulus.wav", stimulus_sha256=stimulus_sha,
                              dry_input_file=monitor, dry_input_sha256=monitor_sha,
                              dry_monitor=dict(measurement_point="loaded_pedal_input",
                                               probe_input_impedance_ohm=10000000,
                                               calibration_db_to_pedal_input=0., shared_adc_clock=True),
                              output=output, mode=mode, input_level_dbfs=level, playback_gain_db=level,
                              gain_db_re_bypass=0., controls=dict(controls, **(control_change or {})),
                              switches=dict(switches, **(switch_change or {}))))

        for output in ("quarter_inch", "xlr"):
            for mode in ("bypass", "clean", "od"):
                for level in ((-18,) if mode == "bypass" else (-24, -12)):
                    record(output, mode, level)
        for control in controls:
            for position in (0., .25, .75, 1.):
                record("quarter_inch", "od", -18, {control: position})
        switch_grid = {"attack": ("boost", "cut"), "grunt": ("boost", "cut"),
                       "low_mid_frequency_hz": (250, 1000),
                       "high_mid_frequency_hz": (750, 3000)}
        for switch, positions in switch_grid.items():
            for position in positions:
                record("quarter_inch", "od", -18, switch_change={switch: position})
        manifest = dict(schema_version=2,
                        pedal=dict(manufacturer="Darkglass", model="B7K Ultra", hardware_revision="test", serial_number="TEST"),
                        power=dict(voltage_v=9, polarity="center-negative"),
                        fixture=dict(source_impedance_ohm=1000, interface_input_impedance_ohm=1000000,
                                     measured_pedal_input_impedance_ohm=1000000, measured_output_impedance_ohm=1000,
                                     sample_rate_hz=48000, cab_sim=False, pickup_loading_measured=True), takes=takes)
        path = root / "manifest.json"
        path.write_text(json.dumps(manifest))
        return path, manifest

    def test_missing_is_never_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = validate(Path(tmp) / "manifest.json")
            self.assertEqual(result["status"], "UNVERIFIED")

    def test_complete_synthetic_metadata_still_not_parity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            self.assertEqual(validate(path)["status"], "REVIEW_REQUIRED")
            manifest["fixture"]["pickup_loading_measured"] = False
            path.write_text(json.dumps(manifest))
            self.assertEqual(validate(path)["status"], "UNVERIFIED")


    def test_nonzero_dc_only_wav_is_not_usable_ac_stimulus(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            take = manifest["takes"][0]
            dry_path = root / take["dry_input_file"]
            with wave.open(str(dry_path), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(48000)
                wav.writeframes((1600).to_bytes(2, "little", signed=True) * 48000)
            take["dry_input_sha256"] = hashlib.sha256(dry_path.read_bytes()).hexdigest()
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("DC-only" in issue for issue in result["problems"]))

    def test_unphysical_probe_calibration_is_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            manifest["takes"][0]["dry_monitor"]["calibration_db_to_pedal_input"] = 10000.0
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("within ±120 dB" in issue for issue in result["problems"]))

    def test_missing_master_or_switch_is_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            manifest["takes"][0]["controls"].pop("master")
            manifest["takes"][0]["switches"].pop("attack")
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("controls" in issue for issue in result["problems"]))
            self.assertTrue(any("switches" in issue for issue in result["problems"]))

    def test_tampered_pedal_or_stimulus_bytes_are_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            first = manifest["takes"][0]
            with (root / first["file"]).open("ab") as stream:
                stream.write(b"tampered")
            with (root / first["stimulus_file"]).open("ab") as stream:
                stream.write(b"tampered")
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("SHA-256 mismatch" in issue for issue in result["problems"]))

    def test_missing_dry_input_and_old_schema_are_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            del manifest["schema_version"]
            del manifest["takes"][0]["dry_input_file"]
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("schema_version" in issue for issue in result["problems"]))
            self.assertTrue(any("dry_input_file" in issue for issue in result["problems"]))

    def test_traversal_and_reused_output_are_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            manifest["takes"][0]["dry_input_file"] = "../escape.wav"
            manifest["takes"][1]["file"] = manifest["takes"][0]["file"]
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("confined relative path" in issue for issue in result["problems"]))
            self.assertTrue(any("reuses a pedal output WAV" in issue for issue in result["problems"]))

    def test_shared_digital_stimulus_is_allowed_but_reused_dry_monitor_is_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            path, manifest = self.make_complete_manifest(Path(tmp))
            self.assertEqual(validate(path)["status"], "REVIEW_REQUIRED")
            manifest["takes"][1]["dry_input_file"] = manifest["takes"][0]["dry_input_file"]
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("reuses a recorded dry-input monitor WAV" in issue for issue in result["problems"]))

    def test_independent_control_and_switch_sweeps_are_mandatory(self):
        with tempfile.TemporaryDirectory() as tmp:
            path, manifest = self.make_complete_manifest(Path(tmp))
            # Mutating a sweep take so two knobs move together does not count
            # as an independent bass control transfer measurement.
            manifest["takes"][10]["controls"]["treble"] = 0
            # This changes the first of the eight single-switch sweeps into a
            # duplicate reference setting, removing one switch position.
            manifest["takes"][-8]["switches"]["attack"] = "flat"
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("Independent OD/quarter-inch control sweep missing" in p for p in result["problems"]))
            self.assertTrue(any("Independent OD/quarter-inch switch sweep missing" in p for p in result["problems"]))

    def test_valid_hash_does_not_hide_truncated_or_silent_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            take = manifest["takes"][0]
            stimulus = root / take["dry_input_file"]
            stimulus.write_bytes(stimulus.read_bytes()[:1024])  # header claims 1s
            take["dry_input_sha256"] = hashlib.sha256(stimulus.read_bytes()).hexdigest()
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("PCM sample data is truncated" in p for p in result["problems"]))

            with wave.open(str(stimulus), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(48000)
                wav.writeframes(b"\x00\x00" * 48000)
            take["dry_input_sha256"] = hashlib.sha256(stimulus.read_bytes()).hexdigest()
            path.write_text(json.dumps(manifest))
            result = validate(path)
            self.assertEqual(result["status"], "UNVERIFIED")
            self.assertTrue(any("digitally silent" in p for p in result["problems"]))

    def test_malformed_json_field_types_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path, manifest = self.make_complete_manifest(root)
            manifest["takes"][0]["mode"] = ["od"]  # unhashable: must not crash the validator
            manifest["takes"][0]["input_level_dbfs"] = True
            manifest["takes"][0]["switches"]["attack"] = {"bad": "shape"}
            path.write_text(json.dumps(manifest))
            self.assertEqual(validate(path)["status"], "UNVERIFIED")


if __name__ == "__main__":
    unittest.main()
