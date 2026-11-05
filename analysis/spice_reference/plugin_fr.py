#!/usr/bin/env python3
"""Measure the plugin's small-signal transfer function by rendering an exponential sweep
through OfflineRender (the same gain staging as PluginProcessor::processBlock).

Returns complex H(f) in *chain-volts per input-volt* (the DAW-domain scalars kInputRef and
kOutputMakeup are divided back out), so it is directly comparable with ngspice's V(out)/V(in).
"""
import os
import subprocess
import tempfile

import numpy as np
from scipy.io import wavfile

RENDER_BIN = os.path.join(os.path.dirname(__file__), "..", "..", "build",
                          "OfflineRender_artefacts", "Release", "OfflineRender")
FS = 48000
KIN = 0.90        # GainStaging::kInputRefNominal  (volts per full-scale)
KOUT = 4.3297     # GainStaging::kOutputMakeupNominal


def _sweep(f0=15.0, f1=21000.0, dur=6.0, amp_dbfs=-50.0, pad=0.5):
    n = int(dur * FS)
    t = np.arange(n) / FS
    L = dur / np.log(f1 / f0)
    ph = 2 * np.pi * f0 * L * (np.exp(t / L) - 1)
    x = 10 ** (amp_dbfs / 20) * np.sin(ph)
    # 10 ms fade in/out
    w = int(0.01 * FS)
    x[:w] *= np.linspace(0, 1, w)
    x[-w:] *= np.linspace(1, 0, w)
    z = np.zeros(int(pad * FS))
    return np.concatenate([z, x, z]), len(z)


def plugin_fr(knobs=None, fits=None, os_factor=2, amp_dbfs=-50.0, extra=None, nfft=None):
    """knobs: dict of CLI flag -> value (e.g. {'--blend': 0, '--lo': 1.0}); fits: dict name->value."""
    x, padn = _sweep(amp_dbfs=amp_dbfs)
    with tempfile.TemporaryDirectory() as td:
        inp = os.path.join(td, "in.wav")
        out = os.path.join(td, "out.wav")
        wavfile.write(inp, FS, x.astype(np.float32))
        cmd = [RENDER_BIN, inp, out, "--os", str(os_factor)]
        for k, v in (knobs or {}).items():
            cmd += [k, str(v)]
        for k, v in (fits or {}).items():
            cmd += ["--fit", f"{k}={v!r}" if not isinstance(v, str) else f"{k}={v}"]
        cmd += list(extra or [])
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(r.stderr[-2000:])
        sr, y = wavfile.read(out)
        assert sr == FS, sr
        y = y.astype(np.float64)
    y = y[: len(x)] if y.ndim == 1 else y[: len(x), 0]
    n = nfft or int(2 ** np.ceil(np.log2(len(x) + 4096)))
    X = np.fft.rfft(x, n)
    Y = np.fft.rfft(y, n)
    f = np.fft.rfftfreq(n, 1 / FS)
    band = (f > 10) & (f < 22000)
    H = np.full_like(X, np.nan)
    H[band] = Y[band] / X[band]
    # convert DAW-domain to chain volts per input volt: gain_FS = G_V * KOUT
    H = H / KOUT
    return f, H


def sample_at(f, H, freqs):
    """Log-frequency interpolation of |H| (dB) and unwrapped-free phase sampling."""
    mag = 20 * np.log10(np.abs(H) + 1e-30)
    ok = np.isfinite(mag) & (f > 0)
    return np.interp(np.log(freqs), np.log(f[ok]), mag[ok])
