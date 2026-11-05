#!/usr/bin/env python3
"""Evidence-aware, offline diagnostic comparison of raw pedal and plugin WAVs.

This is NOT a parity certificate. In particular, even identical files cannot
authenticate a pedal, its schematic, the capture path, or the plugin settings.

Examples (the plugin must be rendered from the *measured* dry-input track):

    python3 analysis/compare_hardware_captures.py \
        --manifest analysis/hardware-captures/manifest.json --take-index 0 \
        --render-input /path/to/calibrated-plugin-input.wav \
        --render /path/to/plugin-render.wav \
        --reference-output-volts-per-fs 1.0 --render-output-volts-per-fs 1.0

    python3 analysis/compare_hardware_captures.py \
        --dry-input measured-dry.wav --reference physical-pedal.wav \
        --render plugin-render.wav

The second form is deliberately UNVERIFIED. Output volts-per-FS flags are
example placeholders, NOT measurements: omit them to withhold calibrated
output-gain/FR magnitude. No gain matching, independent A/B normalization,
timing correction, or phase de-embedding happens silently.

Dependencies: NumPy and SciPy (already required by the existing analysis harness).
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from scipy import optimize, signal
from scipy.io import wavfile


MIN_SECONDS = 1.0
MAX_DURATION_MISMATCH_SECONDS = 0.25
MIN_INPUT_RMS = 1e-6
MAX_LAG_MS = 10.0
COHERENCE_FLOOR = 0.9
FREQUENCY_MIN_HZ = 30.0
FREQUENCY_MAX_HZ = 16000.0
MAX_ANALYSIS_FRAMES = 262144  # bounds worst-case memory/CPU at 192 kHz
MAX_SPECTRAL_FRAMES = 1048576


class MeasurementError(ValueError):
    """An invalid capture or unsupported inference, not an accuracy result."""


def _rms(x):
    return float(np.sqrt(np.mean(np.square(x, dtype=np.float64))))


def _db(value):
    return float(20.0 * math.log10(max(float(value), 1e-15)))


def _audio_stats(samples):
    samples = np.asarray(samples, dtype=np.float64)
    return {
        "rms_dbfs": _db(_rms(samples)),
        "peak_dbfs": _db(np.max(np.abs(samples))),
        "dc_offset": float(np.mean(samples)),
        # A clipped ADC is NOT evidence of the circuit's own hard clipping.
        "near_full_scale_fraction": float(np.mean(np.abs(samples) >= 0.9995)),
    }


def load_wav(path, channel=None):
    """Read mono 16/24/32-bit PCM or IEEE 32/64-bit float WAV accurately.

    SciPy left-justifies 24-bit PCM in an int32, so division by 2**31 (not
    2**23) is correct. Never mix stereo down automatically: a balanced XLR
    pair could otherwise cancel, yielding a false model mismatch.
    """
    path = Path(path)
    try:
        sample_rate, original = wavfile.read(str(path))
    except (OSError, ValueError, EOFError) as exc:
        raise MeasurementError(f"{path}: unreadable WAV: {exc}") from exc
    if sample_rate < 8000 or sample_rate > 192000:
        raise MeasurementError(f"{path}: implausible sample rate {sample_rate}")
    if original.ndim == 2:
        if channel is None:
            raise MeasurementError(f"{path}: stereo requires explicit --channel 0 or 1")
        if not 0 <= channel < original.shape[1]:
            raise MeasurementError(f"{path}: channel {channel} is out of range")
        original = original[:, channel]
    elif original.ndim != 1:
        raise MeasurementError(f"{path}: expected mono/stereo audio")
    if original.dtype == np.int16:
        samples = original.astype(np.float64) / (2**15)
    elif original.dtype == np.int32:
        samples = original.astype(np.float64) / (2**31)
    elif original.dtype.kind == "f" and original.dtype.itemsize in (4, 8):
        samples = original.astype(np.float64)
    else:
        raise MeasurementError(f"{path}: expected 16/24/32-bit PCM or 32/64-bit float WAV")
    if len(samples) < sample_rate * MIN_SECONDS:
        raise MeasurementError(f"{path}: less than {MIN_SECONDS:g} s of audio")
    if not np.all(np.isfinite(samples)):
        raise MeasurementError(f"{path}: non-finite sample detected")
    return int(sample_rate), samples


def _mono_tone_frequency(dry, fs):
    """Accept only a sustained, stationary, nearly pure single-tone stimulus.

    THD is deliberately unavailable on a riff, a swept sine, a two-tone IMD
    test, or broadband noise. A focused tone detector avoids the common error
    of interpreting other musical partials as harmonic distortion.
    """
    n = min(len(dry), int(3 * fs), MAX_ANALYSIS_FRAMES)
    x = dry[:n] - np.mean(dry[:n])
    if _rms(x) < MIN_INPUT_RMS:
        return None
    window = signal.windows.hann(n, sym=False)
    spectrum = np.abs(np.fft.rfft(x * window)) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / fs)
    band = (freqs >= 30.0) & (freqs < fs / 4.0)
    if not np.any(band):
        return None
    index = int(np.argmax(np.where(band, spectrum, -1.0)))
    central = spectrum[max(0, index - 2):index + 3].sum()
    total = spectrum[1:].sum()
    if total <= 1e-30 or central / total < 0.985:
        return None

    rough = freqs[index]
    resolution = fs / n
    times = np.arange(n, dtype=np.float64) / fs
    weighted = x * window

    def negative_energy(frequency):
        return -float(abs(np.dot(weighted, np.exp(-2j * np.pi * frequency * times))) ** 2)

    fit = optimize.minimize_scalar(negative_energy, method="bounded",
                                   bounds=(rough - resolution, rough + resolution),
                                   options={"xatol": 1e-5})
    if not fit.success:
        return None
    f0 = float(fit.x)
    # Require all harmonics analysed below the 95%-Nyquist anti-alias margin.
    if not 30.0 <= f0 <= fs / 4.0:
        return None
    return f0


def _harmonics(samples, f0, fs, max_order=8):
    """Simultaneous least-squares sinusoid fit; avoids FFT-bin leakage bias."""
    nyquist_order = min(max_order, int((fs * 0.475) // f0))
    if nyquist_order < 2:
        return None
    n = min(len(samples), int(3 * fs), MAX_ANALYSIS_FRAMES)
    # One source-independent frequency and one time origin for both signals.
    t = np.arange(n, dtype=np.float64) / fs
    columns = [np.ones(n)]
    for order in range(1, nyquist_order + 1):
        columns.extend((np.cos(2 * np.pi * order * f0 * t),
                        np.sin(2 * np.pi * order * f0 * t)))
    design = np.column_stack(columns)
    coefficients, *_ = np.linalg.lstsq(design, samples[:n], rcond=None)
    amps = [float(np.hypot(coefficients[2 * order - 1], coefficients[2 * order]))
            for order in range(1, nyquist_order + 1)]
    if amps[0] <= MIN_INPUT_RMS:
        return None
    thd_ratio = float(np.linalg.norm(amps[1:]) / amps[0])
    return {"thd_percent": thd_ratio * 100.0,
            "fundamental_dbfs_peak": _db(amps[0]),
            "harmonic_db_re_fundamental": [_db(a / amps[0]) for a in amps[1:]],
            "max_harmonic_order": nyquist_order}


def _correlation_lag(dry, output, fs, max_lag_ms):
    """Constrained FFT correlation; only defensible on broadband stimuli."""
    if _mono_tone_frequency(dry, fs) is not None:
        return {"identifiable": False, "reason": "periodic tone has ambiguous integer-period lag"}
    n = min(len(dry), len(output), int(3.0 * fs), MAX_ANALYSIS_FRAMES)
    x, y = dry[:n] - dry[:n].mean(), output[:n] - output[:n].mean()
    if _rms(x) < MIN_INPUT_RMS or _rms(y) < MIN_INPUT_RMS:
        return {"identifiable": False, "reason": "insufficient excitation"}
    max_lag = max(1, round(max_lag_ms * fs / 1000.0))
    cross = signal.correlate(y, x, mode="full", method="fft")
    mid = n - 1
    vicinity = cross[mid - max_lag:mid + max_lag + 1]
    index = int(np.argmax(np.abs(vicinity)))
    lag = index - max_lag
    if lag == -max_lag or lag == max_lag:
        return {"identifiable": False, "reason": "correlation maximum is at allowed latency boundary"}
    if lag >= 0:
        a, b = x[:n - lag], y[lag:]
    else:
        a, b = x[-lag:], y[:n + lag]
    rho = float(np.dot(a, b) / max(1e-30, np.linalg.norm(a) * np.linalg.norm(b)))
    if abs(rho) < 0.35:
        return {"identifiable": False, "reason": "weak stimulus/output correlation"}
    return {"identifiable": True, "integer_samples": lag,
            "milliseconds": float(lag * 1000.0 / fs), "signed_correlation": rho}


def _stationary_excitation(dry, fs):
    """Conservative multiscale screen for stationary-noise Welch-H1 excitation.

    Four repeated sweeps can conceal their drift in four equally large parts.
    Sixteen subwindows expose that within-block drift. Neither scale alone
    proves stationarity; stimulus type must still be recorded by the fixture.
    """
    n = min(len(dry), int(6 * fs), MAX_ANALYSIS_FRAMES)
    for count in (4, 16):
        centroids = []
        for part in np.array_split(dry[:n], count):
            if _rms(part - part.mean()) < MIN_INPUT_RMS:
                return False
            freq, psd = signal.welch(part, fs=fs, nperseg=min(2048, len(part)))
            band = (freq >= FREQUENCY_MIN_HZ) & (freq <= min(FREQUENCY_MAX_HZ, fs * 0.45))
            p = psd[band]
            if p.sum() <= 1e-24:
                return False
            centroids.append(float(np.dot(freq[band], p) / p.sum()))
        if min(centroids) <= 0 or max(centroids) / min(centroids) > 1.5:
            return False
    return True


def _spectral_metrics(dry, physical, rendered, fs, fixture_delays,
                      output_calibration):
    """Compute H1 cross-spectral diagnostics only where BOTH outputs coherent.

    A nonlinear swept-sine response is *not* isolated correctly by ordinary
    Welch/CSD (harmonics fold into the sweep track). Therefore this report does
    not present FR estimates for a swept sine as certified linear response.
    Use controlled low-level stationary broadband excitation for this read,
    or the existing Farina time-gated H1 tool for swept OD material.
    """
    if not _stationary_excitation(dry, fs):
        return {
            "method": "stationary-Welch-H1",
            "qualified_log_frequency_bins": 0,
            "magnitude_residual_rms_db": None,
            "magnitude_residual_p95_abs_db": None,
            "phase_residual_rms_deg": None,
            "phase_requires_calibrated_fixture_delays": fixture_delays is None,
            "magnitude_requires_output_voltage_calibration": output_calibration is None,
            "withheld_reason": "input is nonstationary; swept sine/music requires gated or segmented analysis",
        }
    dry, physical, rendered = (a[:MAX_SPECTRAL_FRAMES] for a in (dry, physical, rendered))
    nseg = min(8192, max(1024, len(dry) // 8))
    kwargs = {"fs": fs, "nperseg": nseg, "noverlap": nseg // 2,
              "detrend": "constant", "window": "hann"}
    freq, pxx = signal.welch(dry, **kwargs)
    _, py_ref = signal.csd(dry, physical, **kwargs)
    _, py_mod = signal.csd(dry, rendered, **kwargs)
    _, p_ref = signal.welch(physical, **kwargs)
    _, p_mod = signal.welch(rendered, **kwargs)
    power_floor = max(float(np.max(pxx)) * 1e-5, 1e-24)
    coherence_ref = np.square(np.abs(py_ref)) / np.maximum(pxx * p_ref, 1e-30)
    coherence_mod = np.square(np.abs(py_mod)) / np.maximum(pxx * p_mod, 1e-30)
    valid = ((freq >= FREQUENCY_MIN_HZ) & (freq <= min(FREQUENCY_MAX_HZ, fs * 0.45))
             & (pxx >= power_floor) & (coherence_ref >= COHERENCE_FLOOR)
             & (coherence_mod >= COHERENCE_FLOOR)
             & (np.abs(py_ref) > 1e-16) & (np.abs(py_mod) > 1e-16))
    # Resample to LOG-spaced frequency positions so 8-16 kHz does not dominate
    # an RMS score merely by having more FFT bins than 30-200 Hz.
    max_f = min(FREQUENCY_MAX_HZ, fs * 0.45)
    grid = np.geomspace(FREQUENCY_MIN_HZ, max_f, num=96)
    selected = np.unique(np.searchsorted(freq, grid, side="left").clip(0, len(freq) - 1))
    selected = selected[valid[selected]]
    result = {
        "method": "stationary-Welch-H1; not a nonlinear swept-sine or full THD transfer",
        "qualified_log_frequency_bins": int(len(selected)),
        "qualifying_fraction": float(np.mean(valid[(freq >= FREQUENCY_MIN_HZ) & (freq <= max_f)])),
        "coherence_threshold": COHERENCE_FLOOR,
        "analysed_spectral_frames": int(len(dry)),
        "magnitude_residual_rms_db": None,
        "magnitude_residual_p95_abs_db": None,
        "phase_residual_rms_deg": None,
        "phase_requires_calibrated_fixture_delays": fixture_delays is None,
        "magnitude_requires_output_voltage_calibration": output_calibration is None,
    }
    if len(selected) < 12 or freq[selected[-1]] / freq[selected[0]] < 8:
        result["withheld_reason"] = "insufficient input excitation and/or coherent frequency coverage"
        return result

    h_ref = py_ref[selected] / pxx[selected]
    h_mod = py_mod[selected] / pxx[selected]
    ratios = h_mod / h_ref
    # ADC channel gains and model render full-scale volts must be measured.
    # Omitting the conversion to common volts leaves AMPLITUDE unavailable.
    if output_calibration is not None:
        physical_volts_per_fs, render_volts_per_fs = output_calibration
        ratios *= render_volts_per_fs / physical_volts_per_fs
    magnitudes_db = 20.0 * np.log10(np.maximum(np.abs(ratios), 1e-15))
    if output_calibration is not None:
        result["magnitude_residual_rms_db"] = float(np.sqrt(np.mean(magnitudes_db**2)))
        result["magnitude_residual_p95_abs_db"] = float(np.percentile(np.abs(magnitudes_db), 95))
    result["qualifying_hz_range"] = [float(freq[selected[0]]), float(freq[selected[-1]])]
    if fixture_delays is not None:
        # De-embed only delays supplied by a calibrated external fixture.
        reference_delay, render_delay = fixture_delays
        corrected = ratios * np.exp(2j * np.pi * freq[selected] *
                                     (render_delay - reference_delay) / fs)
        phase_deg = np.rad2deg(np.angle(corrected))
        result["phase_residual_rms_deg"] = float(np.sqrt(np.mean(phase_deg**2)))
    return result


def compare_arrays(dry, physical, rendered, sample_rate, *,
                   max_lag_ms=MAX_LAG_MS, fixture_delays=None,
                   output_calibration=None):
    """Quantitative diagnostics only, including validity/conditioning checks."""
    if not 8000 <= sample_rate <= 192000:
        raise MeasurementError("invalid sample rate")
    if not 0 < max_lag_ms <= 500:
        raise MeasurementError("max_lag_ms must be in (0, 500]")
    tracks = [np.asarray(x, dtype=np.float64) for x in (dry, physical, rendered)]
    for name, track in zip(("dry", "physical", "rendered"), tracks):
        if track.ndim != 1 or len(track) < sample_rate * MIN_SECONDS:
            raise MeasurementError(name + " must be a mono track lasting at least one second")
        if not np.all(np.isfinite(track)):
            raise MeasurementError(name + " contains non-finite samples")
    dry, physical, rendered = tracks
    durations = [len(x) / sample_rate for x in tracks]
    if max(durations) - min(durations) > max(MAX_DURATION_MISMATCH_SECONDS,
                                           .05 * max(durations)):
        raise MeasurementError("take durations differ by >250 ms or 5%; do not pad/truncate silently")
    n = min(map(len, tracks))
    discarded = {key: int(len(samples) - n)
                 for key, samples in zip(("dry", "physical", "render"), tracks)}
    dry, physical, rendered = dry[:n], physical[:n], rendered[:n]
    if _rms(dry - dry.mean()) < MIN_INPUT_RMS:
        raise MeasurementError("measured dry input is insufficiently excited")
    if fixture_delays is not None:
        if len(fixture_delays) != 2 or not all(math.isfinite(v) for v in fixture_delays):
            raise MeasurementError("both fixture delays must be finite samples")
    if output_calibration is not None:
        if (len(output_calibration) != 2 or
                any(isinstance(v, bool) or not isinstance(v, (int, float)) or
                    not math.isfinite(v) or v < 1e-9 or v > 1e9
                    for v in output_calibration) or
                abs(math.log10(output_calibration[1]) -
                    math.log10(output_calibration[0])) > 6):
            raise MeasurementError("both output volts-per-FS calibration factors must be finite positive numbers")
    result = {
        "comparison_status": "DIAGNOSTIC_ONLY",
        "parity_certified": False,
        "sample_rate_hz": sample_rate,
        "analysed_frames": int(n),
        "tail_frames_discarded": discarded,
        "dry_input": _audio_stats(dry),
        "physical_pedal": _audio_stats(physical),
        "plugin_render": _audio_stats(rendered),
        "uncalibrated_pcm_level_difference_db": (_db(_rms(rendered - rendered.mean()) /
                                                   _rms(physical - physical.mean()))
                                               if _rms(physical - physical.mean()) >= MIN_INPUT_RMS else None),
        "calibrated_output_level_difference_db": None,
        "latency_reference_from_dry": _correlation_lag(dry, physical, sample_rate, max_lag_ms),
        "latency_render_from_dry": _correlation_lag(dry, rendered, sample_rate, max_lag_ms),
        "frequency_response": _spectral_metrics(dry, physical, rendered,
                                                  sample_rate, fixture_delays,
                                                  output_calibration),
    }
    if output_calibration is not None and result["uncalibrated_pcm_level_difference_db"] is not None:
        ref_vfs, mod_vfs = output_calibration
        result["calibrated_output_level_difference_db"] = (
            result["uncalibrated_pcm_level_difference_db"] + _db(mod_vfs / ref_vfs))
    if result["latency_reference_from_dry"]["identifiable"] and result["latency_render_from_dry"]["identifiable"]:
        delta = (result["latency_render_from_dry"]["integer_samples"] -
                 result["latency_reference_from_dry"]["integer_samples"])
        result["render_minus_reference_lag_samples"] = int(delta)
        if delta >= 0:
            aligned_ref, aligned_mod = physical[:n - delta], rendered[delta:]
        else:
            aligned_ref, aligned_mod = physical[-delta:], rendered[:n + delta]
        if output_calibration is not None:
            ref_vfs, mod_vfs = output_calibration
            result["latency_aligned_waveform_error_db_re_reference"] = _db(
                _rms(aligned_mod * mod_vfs - aligned_ref * ref_vfs) /
                max(_rms(aligned_ref * ref_vfs), 1e-15))
        else:
            result["latency_aligned_waveform_error_db_re_reference"] = None
        result["alignment_note"] = "diagnostic alignment only; measured latency difference is reported separately"
    else:
        result["render_minus_reference_lag_samples"] = None
        result["latency_aligned_waveform_error_db_re_reference"] = None

    tone_frequency = _mono_tone_frequency(dry, sample_rate)
    result["stationary_single_tone_thd"] = None
    if tone_frequency is not None:
        input_thd = _harmonics(dry, tone_frequency, sample_rate)
        physical_thd = _harmonics(physical, tone_frequency, sample_rate)
        plugin_thd = _harmonics(rendered, tone_frequency, sample_rate)
        # An impure input can create H2/H3 at the OUTPUT without any pedal
        # distortion. Require source THD at least 20 dB below both outputs.
        if (input_thd and physical_thd and plugin_thd and
                input_thd["thd_percent"] <= .1 * min(physical_thd["thd_percent"],
                                                       plugin_thd["thd_percent"])):
            result["stationary_single_tone_thd"] = {
                "input_frequency_hz": tone_frequency,
                "measured_source_thd_percent": input_thd["thd_percent"],
                "physical": physical_thd,
                "plugin": plugin_thd,
                "thd_difference_percentage_points":
                    plugin_thd["thd_percent"] - physical_thd["thd_percent"],
                "caveat": ("THD excludes noise; model-vs-hardware distortion also requires "
                           "matched, measured physical input volts-per-FS and verified render settings"),
            }
        else:
            result["tone_thd_withheld_reason"] = "source impurity is not >=20 dB below both outputs, or output lacks a measurable fundamental"

    # Near full scale indicates SUSPECTED converter clipping, not proof:
    # a clean 0 dBFS sine may also be near full-scale. Report raw PCM metrics,
    # but withhold pedal-attributed THD and calibrated comparisons until a
    # capture is repeated with measured headroom.
    clipping_codes = []
    if result["physical_pedal"]["near_full_scale_fraction"] > .001:
        clipping_codes.append("physical_capture_clipping_suspected")
    if result["dry_input"]["near_full_scale_fraction"] > .001:
        clipping_codes.append("dry_monitor_clipping_suspected")
    result["warning_codes"] = clipping_codes
    if clipping_codes:
        result["calibrated_output_level_difference_db"] = None
        result["latency_aligned_waveform_error_db_re_reference"] = None
        result["stationary_single_tone_thd"] = None
        result["tone_thd_withheld_reason"] = (
            "possible converter clipping in physical capture or dry input; "
            "cannot attribute measured harmonics to pedal circuitry"
        )
        response = result["frequency_response"]
        response["magnitude_residual_rms_db"] = None
        response["magnitude_residual_p95_abs_db"] = None
        response["phase_residual_rms_deg"] = None
        response["withheld_reason"] = (
            "physical capture or dry monitor has samples near full scale; "
            "repeat with calibrated ADC headroom before grading frequency response"
        )

    concerns = []
    if clipping_codes:
        concerns.append("possible converter clipping: " + ", ".join(clipping_codes)
                        + "; THD, calibrated gain, waveform and FR grading withheld")
    for label, track_stats in (("dry input", result["dry_input"]),
                               ("physical capture", result["physical_pedal"]),
                               ("plugin render", result["plugin_render"])):
        if track_stats["near_full_scale_fraction"] > .001:
            concerns.append(label + " has many samples at/above +/-1 FS; examine PCM converter clipping (float renders may exceed 1 legitimately)")
    if fixture_delays is None:
        concerns.append("no calibrated capture/render fixture delays; phase comparison withheld")
    if any(discarded.values()):
        concerns.append("track lengths differ; unmatched tails discarded for intersection-only diagnostics")
    if output_calibration is None:
        concerns.append("no measured output volts-per-FS scales; physical gain, FR magnitude and waveform-error grades withheld")
    concerns.append("model generation, serial, circuit provenance, clocks, and render settings must be independently verified")
    result["warnings"] = concerns
    return result


def _verify_manifest_file(root, take, key, digest_key):
    """Fail closed on a selected v2 capture even if overall coverage is incomplete."""
    from validate_hardware_capture import _verify_capture_file

    issues = []
    info = _verify_capture_file(root, take.get(key), take.get(digest_key),
                                "selected_take." + key, None, issues, require_signal=(key == "dry_input_file"))
    if info is None or issues:
        raise MeasurementError("; ".join(issues) or "invalid " + key)
    return info["path"]


def _manifest_paths(path, index):
    from validate_hardware_capture import validate

    path = Path(path)
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise MeasurementError(f"cannot read physical reference manifest: {exc}") from exc
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 2:
        raise MeasurementError("a capture comparison requires v2 paired/hash-verified manifest")
    takes = manifest.get("takes")
    if not isinstance(takes, list) or index < 0 or index >= len(takes) or not isinstance(takes[index], dict):
        raise MeasurementError("selected manifest take is absent/invalid")
    take = takes[index]
    monitor = take.get("dry_monitor")
    if not isinstance(monitor, dict) or monitor.get("measurement_point") != "loaded_pedal_input":
        raise MeasurementError("selected take needs an actual loaded-pedal-input monitor, not just the source WAV")
    if monitor.get("shared_adc_clock") is not True:
        raise MeasurementError("measured dry input and pedal output need a documented shared ADC clock")
    impedance = monitor.get("probe_input_impedance_ohm")
    calibration = monitor.get("calibration_db_to_pedal_input")
    if (isinstance(impedance, bool) or not isinstance(impedance, (int, float))
            or not math.isfinite(impedance) or impedance <= 0
            or isinstance(calibration, bool) or not isinstance(calibration, (int, float))
            or not math.isfinite(calibration) or abs(calibration) > 120):
        raise MeasurementError("dry-input monitoring probe and calibration metadata are invalid")
    base = path.parent.resolve()
    reference = _verify_manifest_file(base, take, "file", "sha256")
    dry = _verify_manifest_file(base, take, "dry_input_file", "dry_input_sha256")
    stimulus = _verify_manifest_file(base, take, "stimulus_file", "stimulus_sha256")
    if len({reference, dry, stimulus}) != 3:
        raise MeasurementError("physical output, measured dry input, and source stimulus must be distinct files")
    evidence = validate(path)
    return dry, reference, evidence, float(calibration)


def _verify_calibrated_render_input(measured_dry, render_input, calibration_db):
    """Check the *file* fed to the renderer has the measured loaded-input volts.

    This is not a cryptographic proof that the renderer used it; the render
    process/CLI settings still need their own reproducible provenance log.
    """
    if len(render_input) != len(measured_dry):
        raise MeasurementError("render input must be sample-for-sample aligned with measured dry input")
    expected = measured_dry * (10.0 ** (calibration_db / 20.0))
    difference = render_input - expected
    error_rms = _rms(difference)
    if error_rms > max(1e-7, 5e-4 * max(_rms(expected), 1e-10)):
        raise MeasurementError("renderer input WAV does not reflect monitored-pedal-input calibration "
                               f"({calibration_db:g} dB); cannot compare nonlinear gain/THD")
    return {"dry_to_pedal_input_db": calibration_db,
            "render_input_verification_rms_error": error_rms,
            "decoded_render_input_samples_sha256": hashlib.sha256(render_input.tobytes()).hexdigest(),
            "note": "audited input samples only; renderer settings and output lineage remain unverified"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_argument_group("capture provenance")
    group.add_argument("--manifest", type=Path, help="v2 SHA-256 capture manifest")
    group.add_argument("--take-index", type=int, help="zero-based index in --manifest")
    group.add_argument("--dry-input", type=Path, help="measured pedal-input monitor (diagnostic-only)")
    group.add_argument("--reference", type=Path, help="physical-pedal output (diagnostic-only)")
    parser.add_argument("--render", type=Path, required=True, help="plugin WAV rendered from the same measured dry input")
    parser.add_argument("--render-input", type=Path,
                        help="REQUIRED with --manifest: actual renderer input WAV, including dry-monitor calibration")
    parser.add_argument("--reference-output-volts-per-fs", type=float,
                        help="measured physical-output ADC volts at +1 FS, not a guess")
    parser.add_argument("--render-output-volts-per-fs", type=float,
                        help="measured plugin-render volts at +1 FS, in same physical units")
    parser.add_argument("--physical-pedal-input-volts-per-fs", type=float,
                        help="measured volts at the pedal input per FS of the calibrated renderer-input WAV")
    parser.add_argument("--effective-renderer-input-ref-volts-per-fs", type=float,
                        help="effective GainStaging volts/FS for this render (including any input-ref override)")
    parser.add_argument("--channel", type=int, choices=(0, 1), help="select explicit channel for stereo WAVs")
    parser.add_argument("--max-lag-ms", type=float, default=MAX_LAG_MS)
    parser.add_argument("--reference-fixture-delay-samples", type=float)
    parser.add_argument("--render-fixture-delay-samples", type=float)
    parser.add_argument("--output", type=Path, help="optional JSON report file")
    args = parser.parse_args(argv)
    try:
        if args.manifest is not None:
            if args.take_index is None or args.dry_input or args.reference:
                raise MeasurementError("--manifest requires --take-index and forbids manual WAV paths")
            dry_path, reference_path, evidence, monitor_correction_db = _manifest_paths(args.manifest, args.take_index)
            if args.render_input is None:
                raise MeasurementError("--manifest requires --render-input to audit the dry-monitor calibration and actual renderer input")
        else:
            if args.take_index is not None or args.dry_input is None or args.reference is None:
                raise MeasurementError("use --manifest/--take-index OR --dry-input/--reference")
            dry_path, reference_path = args.dry_input, args.reference
            evidence = {"status": "UNVERIFIED", "problems": ["no capture manifest or hash authentication supplied"]}
            monitor_correction_db = 0.0
        delays = (args.reference_fixture_delay_samples, args.render_fixture_delay_samples)
        if any(x is not None for x in delays) and not all(x is not None for x in delays):
            raise MeasurementError("both fixture-delay arguments must be specified together")
        volts = (args.reference_output_volts_per_fs, args.render_output_volts_per_fs)
        if any(x is not None for x in volts) and not all(x is not None for x in volts):
            raise MeasurementError("both output volts-per-FS calibration factors must be specified together")
        input_volts = (args.physical_pedal_input_volts_per_fs,
                       args.effective_renderer_input_ref_volts_per_fs)
        if any(v is not None for v in input_volts) and not all(v is not None for v in input_volts):
            raise MeasurementError("both physical and renderer input volts-per-FS factors must be supplied together")
        if all(v is not None for v in input_volts) and (
                any(not math.isfinite(v) or v <= 0 or v > 1e9 or v < 1e-9 for v in input_volts)):
            raise MeasurementError("input volts-per-FS factors must be finite and positive")
        fs_dry, dry = load_wav(dry_path, args.channel)
        fs_ref, reference = load_wav(reference_path, args.channel)
        fs_mod, rendered = load_wav(args.render, args.channel)
        if fs_dry != fs_ref or fs_dry != fs_mod:
            raise MeasurementError("WAV sample rates disagree; resampling and clock uncertainty require separate calibration")
        render_input_provenance = None
        if args.render_input is not None:
            fs_input, plugin_input = load_wav(args.render_input, args.channel)
            if fs_input != fs_dry:
                raise MeasurementError("plugin input sample rate differs from measured dry monitor")
            render_input_provenance = _verify_calibrated_render_input(
                dry, plugin_input, monitor_correction_db)
        report = compare_arrays(dry, reference, rendered, fs_dry,
                                max_lag_ms=args.max_lag_ms,
                                fixture_delays=delays if all(x is not None for x in delays) else None,
                                output_calibration=volts if all(x is not None for x in volts) else None)
        report["capture_evidence_status"] = evidence["status"]
        report["capture_evidence_problem_count"] = len(evidence["problems"])
        report["capture_evidence_problems"] = evidence["problems"][:12]
        report["capture_evidence_problems_truncated"] = len(evidence["problems"]) > 12
        report["capture_paths"] = {"measured_dry_input": str(dry_path),
                                   "physical_reference": str(reference_path),
                                   "plugin_render": str(args.render)}
        report["render_input_audit"] = render_input_provenance
        input_voltage_match = None
        input_voltage_difference_db = None
        if all(v is not None for v in input_volts):
            input_voltage_difference_db = 20.0 * math.log10(input_volts[0] / input_volts[1])
            input_voltage_match = abs(input_voltage_difference_db) <= 0.1
        report["pedal_input_voltage_calibration"] = {
            "physical_volts_per_fs": input_volts[0],
            "renderer_input_ref_volts_per_fs": input_volts[1],
            "difference_db": input_voltage_difference_db,
            "matched_within_0p1_db": input_voltage_match,
            "note": ("declared electrical scales require independent measurement and proof "
                     "the plugin render used the stated input reference"),
        }
        # Keep digital-domain THD diagnostics in direct compare_arrays() calls,
        # but fail closed for MANIFEST-backed attribution when nonlinear drive
        # voltages are missing or unmatched. These scalar declarations still do
        # not certify hardware authenticity or actual render lineage.
        if args.manifest is not None and input_voltage_match is not True:
            report["nonlinear_attribution_withheld_reason"] = (
                "physical pedal-input volts-per-FS and effective renderer input reference "
                "are missing or differ by more than 0.1 dB"
            )
            report["calibrated_output_level_difference_db"] = None
            report["latency_aligned_waveform_error_db_re_reference"] = None
            response = report["frequency_response"]
            response["magnitude_residual_rms_db"] = None
            response["magnitude_residual_p95_abs_db"] = None
            response["phase_residual_rms_deg"] = None
            response["withheld_reason"] = report["nonlinear_attribution_withheld_reason"]
            # The numerical THD table is retained as an explicitly UNATTRIBUTED
            # digital-domain diagnostic; do not interpret its difference as
            # a transistor/op-amp modelling error without voltage matching.
            report["warnings"].append(report["nonlinear_attribution_withheld_reason"])
        report["hardware_vs_model_nonlinear_attribution_eligible"] = bool(
            args.manifest is not None
            and evidence["status"] == "REVIEW_REQUIRED"
            and input_voltage_match is True
            and not report["warning_codes"]
            and render_input_provenance is not None
        )
        if args.manifest is not None:
            report["warnings"].append(
                "render-input samples verified, but proof the provided render WAV came from that input/settings is still required")
        report["parity_certified"] = False
        blob = json.dumps(report, indent=2, allow_nan=False)
        if args.output:
            args.output.write_text(blob + "\n", encoding="utf-8")
        print(blob)
        return 0
    except (MeasurementError, OSError) as exc:
        print(json.dumps({"comparison_status": "UNVERIFIED", "parity_certified": False,
                          "error": str(exc)}, indent=2))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
