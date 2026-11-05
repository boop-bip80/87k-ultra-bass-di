#!/usr/bin/env python3
"""Validate physical pedal reference capture provenance; never certify DSP parity.

Exit 0: complete capture metadata and hashed playback/dry-monitor/output WAVs, REVIEW_REQUIRED.
Exit 2: missing/incomplete/inconsistent references, UNVERIFIED.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import wave


MODES = {"bypass", "clean", "od"}
OUTPUTS = {"quarter_inch", "xlr"}
CONTROLS = {"master", "drive", "blend", "level", "bass", "low_mids", "high_mids", "treble"}
SWITCHES = {
    "attack": {"flat", "boost", "cut"},
    "grunt": {"flat", "boost", "cut"},
    # These are printed switch labels/nominal settings, not measured EQ centers.
    "low_mid_frequency_hz": {250, 500, 1000},
    "high_mid_frequency_hz": {750, 1500, 3000},
}
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
BASE_SWITCHES = {"attack": "flat", "grunt": "flat",
                 "low_mid_frequency_hz": 500, "high_mid_frequency_hz": 1500}
CONTROL_GRID = (0., 0.25, 0.5, 0.75, 1.)


def _near(a, b):
    return isinstance(a, (int, float)) and not isinstance(a, bool) and math.isfinite(a) and abs(a - b) <= 0.01


def _verify_capture_file(root, filename, digest, name, sample_rate, problems, require_signal=False):
    """Check confined path, content hash and PCM header; return WAV properties.

    Re-opening for the hash and decoder intentionally makes both checks read
    actual on-disk evidence, not a filename that merely looks convincing.
    This is a provenance check, not authentication of a physical pedal.
    """
    if not isinstance(filename, str) or not filename.strip() or not filename.lower().endswith(".wav"):
        problems.append(name + " must name a PCM WAV")
        return None
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        problems.append(name + " needs its SHA-256 digest (64 lowercase hex digits)")
        # Continue reading when possible so the report contains all problems.

    relative = Path(filename)
    if relative.is_absolute() or ".." in relative.parts:
        problems.append(name + " must be a confined relative path (no '..' or absolute paths)")
        return None
    try:
        target = (root / relative).resolve(strict=True)
        if not target.is_relative_to(root) or not target.is_file():
            problems.append(name + " escapes the manifest directory or is not a regular file")
            return None
        h = hashlib.sha256()
        with target.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                h.update(block)
        if isinstance(digest, str) and SHA256_RE.fullmatch(digest) and h.hexdigest() != digest:
            problems.append(name + " SHA-256 mismatch (the WAV has changed)")
        with wave.open(str(target), "rb") as audio:
            rate, frames, channels, sample_width = (
                audio.getframerate(), audio.getnframes(), audio.getnchannels(), audio.getsampwidth()
            )
            if frames < rate or rate <= 0:
                problems.append(name + " must contain at least one second")
            if isinstance(sample_rate, (int, float)) and rate != sample_rate:
                problems.append(name + " sample rate differs from fixture")
            if channels not in (1, 2) or sample_width not in (2, 3, 4):
                problems.append(name + " must be mono/stereo 16/24/32-bit PCM")
            if channels > 0 and sample_width > 0:
                # WAV headers can claim millions of frames while the real data
                # chunk is truncated. Read the frames, bounded in memory.
                counts = [0] * channels
                sums = [0] * channels
                squares = [0] * channels
                remaining = frames
                while remaining:
                    block_frames = min(8192, remaining)
                    block = audio.readframes(block_frames)
                    if len(block) != block_frames * channels * sample_width:
                        problems.append(name + " PCM sample data is truncated")
                        break
                    if require_signal and sample_width in (2, 3, 4) and channels in (1, 2):
                        # Exact integer moments per channel reject a nonzero
                        # constant DC waveform without overflow/cancellation.
                        # Decode signed little-endian PCM; no NumPy dependency.
                        if sample_width in (2, 4):
                            code = "h" if sample_width == 2 else "i"
                            values = struct.unpack("<" + code * (block_frames * channels), block)
                            for ch in range(channels):
                                samples = values[ch::channels]
                                counts[ch] += len(samples)
                                sums[ch] += sum(samples)
                                squares[ch] += sum(s * s for s in samples)
                        else:
                            for index in range(0, len(block), sample_width):
                                ch = (index // sample_width) % channels
                                sample = int.from_bytes(block[index:index + sample_width],
                                                        "little", signed=True)
                                counts[ch] += 1
                                sums[ch] += sample
                                squares[ch] += sample * sample
                    remaining -= block_frames
                if require_signal and sample_width in (2, 3, 4) and channels in (1, 2):
                    full_scale = 1 << (8 * sample_width - 1)
                    active = any(
                        counts[ch] > 0 and
                        counts[ch] * squares[ch] - sums[ch] * sums[ch] >
                        (counts[ch] * full_scale * 1e-6) ** 2
                        for ch in range(channels)
                    )
                    if not active:
                        problems.append(name + " stimulus is DC-only or digitally silent; "
                                        "a calibrated AC excitation is required")
            return {"path": target, "sample_rate": rate, "frames": frames,
                    "channels": channels, "sample_width": sample_width}
    except (OSError, RuntimeError, EOFError, wave.Error, ValueError) as exc:
        problems.append(name + " is absent, unreadable or not valid PCM WAV: " + str(exc))
        return None


def validate(path):
    problems = []
    path = Path(path)
    if not path.is_file():
        return {"status": "UNVERIFIED", "problems": ["No real-hardware capture manifest at " + str(path)]}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        return {"status": "UNVERIFIED", "problems": ["Unreadable manifest: " + str(exc)]}
    if not isinstance(data, dict):
        return {"status": "UNVERIFIED", "problems": ["Manifest must be a JSON object"]}
    if data.get("schema_version") != 2 or isinstance(data.get("schema_version"), bool):
        problems.append("schema_version must be 2 for paired, integrity-checked capture evidence")
    root = path.parent.resolve()

    def req_text(obj, key, prefix):
        if not isinstance(obj, dict) or not isinstance(obj.get(key), str) or not obj[key].strip():
            problems.append(prefix + "." + key + " is required")

    def req_number(obj, key, prefix, positive=False):
        val = obj.get(key) if isinstance(obj, dict) else None
        if isinstance(val, bool) or not isinstance(val, (int, float)) or not math.isfinite(val) or (positive and val <= 0):
            problems.append(prefix + "." + key + " must be a finite" + (" positive" if positive else "") + " number")

    pedal = data.get("pedal")
    for key in ("manufacturer", "model", "hardware_revision", "serial_number"):
        req_text(pedal, key, "pedal")
    power = data.get("power")
    req_number(power, "voltage_v", "power", True)
    req_text(power, "polarity", "power")
    fixture = data.get("fixture")
    for key in ("source_impedance_ohm", "interface_input_impedance_ohm", "measured_pedal_input_impedance_ohm", "measured_output_impedance_ohm", "sample_rate_hz"):
        req_number(fixture, key, "fixture", True)
    if not isinstance(fixture, dict) or fixture.get("cab_sim") is not False:
        problems.append("fixture.cab_sim must explicitly be false for unfiltered circuit path")
    if not isinstance(fixture, dict) or fixture.get("pickup_loading_measured") is not True:
        problems.append("fixture.pickup_loading_measured must explicitly be true; driven line sweeps are insufficient")

    takes = data.get("takes")
    if not isinstance(takes, list) or not takes:
        problems.append("takes must contain recorded physical-hardware WAVs")
        takes = []
    seen = {}
    used_output_files = set()
    used_dry_monitor_files = set()
    used_output_hashes = set()
    used_dry_monitor_hashes = set()
    validated_takes = []
    for i, take in enumerate(takes):
        name = "takes[" + str(i) + "]"
        if not isinstance(take, dict):
            problems.append(name + " must be an object")
            continue
        mode, output = take.get("mode"), take.get("output")
        if not isinstance(mode, str) or mode not in MODES or not isinstance(output, str) or output not in OUTPUTS:
            problems.append(name + ".mode/output invalid")
        req_number(take, "input_level_dbfs", name)
        req_number(take, "gain_db_re_bypass", name)
        req_number(take, "playback_gain_db", name)
        dry_monitor = take.get("dry_monitor")
        if not isinstance(dry_monitor, dict) or dry_monitor.get("measurement_point") != "loaded_pedal_input":
            problems.append(name + ".dry_monitor.measurement_point must be loaded_pedal_input, not open-circuit playback")
        req_number(dry_monitor, "probe_input_impedance_ohm", name + ".dry_monitor", positive=True)
        req_number(dry_monitor, "calibration_db_to_pedal_input", name + ".dry_monitor")
        calibration_db = (dry_monitor.get("calibration_db_to_pedal_input")
                          if isinstance(dry_monitor, dict) else None)
        if (isinstance(calibration_db, (int, float)) and not isinstance(calibration_db, bool)
                and math.isfinite(calibration_db) and abs(calibration_db) > 120.0):
            problems.append(name + ".dry_monitor.calibration_db_to_pedal_input must be within ±120 dB")
        if not isinstance(dry_monitor, dict) or dry_monitor.get("shared_adc_clock") is not True:
            problems.append(name + ".dry_monitor.shared_adc_clock must be true for paired timing")
        level = take.get("input_level_dbfs")
        gain = take.get("gain_db_re_bypass")
        if isinstance(gain, (int, float)) and not isinstance(gain, bool) and math.isfinite(gain) and not -120 <= gain <= 120:
            problems.append(name + ".gain_db_re_bypass must be in [-120, 120] dB")
        if isinstance(level, (int, float)) and not isinstance(level, bool) and math.isfinite(level):
            if not -120 <= level <= 0:
                problems.append(name + ".input_level_dbfs must be in [-120, 0] dBFS")
        controls = take.get("controls")
        if not isinstance(controls, dict) or not CONTROLS.issubset(controls):
            problems.append(name + ".controls must record all: " + ", ".join(sorted(CONTROLS)))
        else:
            for c in CONTROLS:
                v = controls[c]
                if not isinstance(v, (int, float)) or isinstance(v, bool) or not math.isfinite(v) or not 0 <= v <= 1:
                    problems.append(name + ".controls." + c + " must be a 0..1 position")
        switches = take.get("switches")
        if not isinstance(switches, dict):
            problems.append(name + ".switches must record all four physical selector positions")
        else:
            for switch, allowed in SWITCHES.items():
                choice = switches.get(switch)
                if (isinstance(choice, bool) or not isinstance(choice, (str, int, float))
                        or choice not in allowed):
                    problems.append(name + ".switches." + switch + " is absent or invalid")
        rate = fixture.get("sample_rate_hz") if isinstance(fixture, dict) else None
        pedal_wav = _verify_capture_file(root, take.get("file"), take.get("sha256"),
                                         name + ".file", rate, problems)
        stimulus_wav = _verify_capture_file(root, take.get("stimulus_file"), take.get("stimulus_sha256"),
                                            name + ".stimulus_file", rate, problems, require_signal=True)
        dry_wav = _verify_capture_file(root, take.get("dry_input_file"), take.get("dry_input_sha256"),
                                       name + ".dry_input_file", rate, problems, require_signal=True)
        paths = [w["path"] for w in (pedal_wav, stimulus_wav, dry_wav) if w]
        if len(set(paths)) != len(paths):
            problems.append(name + " playback source, recorded loaded-input monitor and pedal output must be separate WAV files")
        if dry_wav and pedal_wav and take.get("sha256") == take.get("dry_input_sha256"):
            problems.append(name + " dry input monitor and pedal output are byte-identical, not independently recorded")
        if dry_wav and pedal_wav and dry_wav["sample_rate"] and pedal_wav["sample_rate"]:
            durations = (dry_wav["frames"] / dry_wav["sample_rate"],
                         pedal_wav["frames"] / pedal_wav["sample_rate"])
            if abs(durations[0] - durations[1]) > max(0.25, 0.05 * max(durations)):
                problems.append(name + " loaded-input monitor/pedal duration mismatch (>250 ms or 5%)")
        if pedal_wav:
            if pedal_wav["path"] in used_output_files:
                problems.append(name + " reuses a pedal output WAV already assigned to another take")
            used_output_files.add(pedal_wav["path"])
            if isinstance(take.get("sha256"), str) and SHA256_RE.fullmatch(take["sha256"]):
                if take["sha256"] in used_output_hashes:
                    problems.append(name + " reuses byte-identical pedal output data assigned to another take")
                used_output_hashes.add(take["sha256"])
        if dry_wav:
            if dry_wav["path"] in used_dry_monitor_files:
                problems.append(name + " reuses a recorded dry-input monitor WAV already assigned to another take")
            used_dry_monitor_files.add(dry_wav["path"])
            if isinstance(take.get("dry_input_sha256"), str) and SHA256_RE.fullmatch(take["dry_input_sha256"]):
                if take["dry_input_sha256"] in used_dry_monitor_hashes:
                    problems.append(name + " reuses byte-identical recorded dry-input monitor data assigned to another take")
                used_dry_monitor_hashes.add(take["dry_input_sha256"])
        if (isinstance(mode, str) and mode in MODES and isinstance(output, str) and output in OUTPUTS
                and isinstance(level, (int, float)) and not isinstance(level, bool) and math.isfinite(level)
                and -120 <= level <= 0):
            seen.setdefault((mode, output), set()).add(level)
            if (isinstance(controls, dict) and CONTROLS.issubset(controls)
                    and isinstance(switches, dict) and SWITCHES.keys() <= switches.keys()
                    and all(isinstance(controls[c], (int, float)) and not isinstance(controls[c], bool)
                            and math.isfinite(controls[c]) and 0 <= controls[c] <= 1 for c in CONTROLS)
                    and all(isinstance(switches[s], (str, int, float))
                            and not isinstance(switches[s], bool) and switches[s] in allowed
                            for s, allowed in SWITCHES.items())):
                validated_takes.append((mode, output, controls, switches))

    for output in OUTPUTS:
        for mode in MODES:
            levels = seen.get((mode, output), set())
            if len(levels) < (1 if mode == "bypass" else 2):
                problems.append(f"Coverage requires {'one bypass' if mode == 'bypass' else 'two input levels'} for {mode}/{output}")

    if used_output_files & used_dry_monitor_files or used_output_hashes & used_dry_monitor_hashes:
        problems.append("A pedal output recording is reused as a loaded-input monitor recording (file or bytes)")

    # The five-position sweeps must be independent: all other pots at noon,
    # all switches at reference positions, same OD/quarter-inch signal path.
    # Merely placing five values in a manifest while simultaneously changing
    # every control is *not* evidence for any individual control's transfer.
    reference = [(c, s) for m, o, c, s in validated_takes if m == "od" and o == "quarter_inch"]
    for control in sorted(CONTROLS):
        for position in CONTROL_GRID:
            if not any((all(_near(c[other], position if other == control else 0.5)
                            for other in CONTROLS) and s == BASE_SWITCHES)
                       for c, s in reference):
                problems.append(f"Independent OD/quarter-inch control sweep missing: {control}={position:g}, others=0.5, switches=reference")
    for switch, allowed in SWITCHES.items():
        for position in allowed:
            expected = dict(BASE_SWITCHES, **{switch: position})
            if not any(all(_near(c[k], 0.5) for k in CONTROLS) and s == expected for c, s in reference):
                problems.append(f"Independent OD/quarter-inch switch sweep missing: {switch}={position}")
    return {"status": "UNVERIFIED" if problems else "REVIEW_REQUIRED", "problems": problems,
            "take_count": len(takes), "note": "Metadata checks only. Real pedal circuit/DSP parity is NOT established."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).parent / "hardware-captures" / "manifest.json")
    args = parser.parse_args()
    result = validate(args.manifest)
    print(json.dumps(result, indent=2))
    return 2 if result["status"] == "UNVERIFIED" else 0


if __name__ == "__main__":
    raise SystemExit(main())
