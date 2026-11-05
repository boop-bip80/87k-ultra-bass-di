#!/usr/bin/env python3
"""Validate physical pedal reference capture provenance; never certify DSP parity.

Exit 0: complete capture metadata, REVIEW_REQUIRED (audio comparison still needed).
Exit 2: missing/incomplete/inconsistent references, UNVERIFIED.
"""
import argparse
import json
import math
from pathlib import Path
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


def validate(path):
    problems = []
    if not path.is_file():
        return {"status": "UNVERIFIED", "problems": ["No real-hardware capture manifest at " + str(path)]}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        return {"status": "UNVERIFIED", "problems": ["Unreadable manifest: " + str(exc)]}
    if not isinstance(data, dict):
        return {"status": "UNVERIFIED", "problems": ["Manifest must be a JSON object"]}

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
    for i, take in enumerate(takes):
        name = "takes[" + str(i) + "]"
        if not isinstance(take, dict):
            problems.append(name + " must be an object")
            continue
        mode, output = take.get("mode"), take.get("output")
        if mode not in MODES or output not in OUTPUTS:
            problems.append(name + ".mode/output invalid")
        req_number(take, "input_level_dbfs", name)
        req_number(take, "gain_db_re_bypass", name)
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
        wavname = take.get("file")
        if not isinstance(wavname, str) or not wavname.lower().endswith(".wav"):
            problems.append(name + ".file must be a WAV filename")
        else:
            target = (path.parent / wavname).resolve()
            if not target.is_relative_to(path.parent.resolve()):
                problems.append(name + ".file escapes manifest directory")
            elif not target.is_file():
                problems.append(name + ".file is absent: " + wavname)
            else:
                try:
                    with wave.open(str(target), "rb") as audio:
                        if audio.getnframes() < audio.getframerate():
                            problems.append(name + ".file must be at least one second")
                        if isinstance(fixture, dict) and audio.getframerate() != fixture.get("sample_rate_hz"):
                            problems.append(name + ".file sample rate differs from fixture")
                except (OSError, EOFError, wave.Error) as exc:
                    problems.append(name + ".file is not readable PCM WAV: " + str(exc))
        if mode in MODES and output in OUTPUTS and isinstance(take.get("input_level_dbfs"), (int, float)):
            seen.setdefault((mode, output), set()).add(take["input_level_dbfs"])

    for output in OUTPUTS:
        for mode in MODES:
            levels = seen.get((mode, output), set())
            if len(levels) < (1 if mode == "bypass" else 2):
                problems.append(f"Coverage requires {'one bypass' if mode == 'bypass' else 'two input levels'} for {mode}/{output}")
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
