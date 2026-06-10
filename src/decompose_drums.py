#!/usr/bin/env python3
"""
Decompose a drum stem into kick / snare / hats with higher fidelity than
plain band-pass filtering (src/isolate_drums.py).

Approach:
  1. HPSS (harmonic/percussive separation) on the STFT to drop tonal bleed.
  2. Per-band onset envelopes (low / mid / high) -> per-class hit candidates.
  3. Every candidate is validated with full-spectrum features (band energy
     fractions + spectral flatness) so e.g. a kick thump doesn't register as
     a hat. Layered hits (kick + hat on the same beat) are kept in BOTH
     classes -- this is what single-label classification gets wrong.
  4. Stems are rebuilt with soft time-frequency masks: a frequency weighting
     per class * a smooth time gate that only opens around that class's
     classified hits. This removes inter-class bleed between hits.
  5. One-shots picked from the most isolated hit per class (reuses the
     battle-tested slicing helpers from isolate_drums.py).

Outputs (per song, mirrors the old layout plus snare):
  <out-dir>/<song>/drum/kick.mp3        + kick_times.csv  + kick_one_shot.mp3
  <out-dir>/<song>/drum/snare.mp3       + snare_times.csv + snare_one_shot.mp3
  <out-dir>/<song>/drum/hats.mp3        + hats_times.csv  + hats_one_shot.mp3
  <out-dir>/<song>/drum/decomposition.json   (summary for the app frontend)

CSV format is unchanged (index,time_seconds) so repeat_sample_at_times_cli
and match_sample_gain.py keep working.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import isolate_drums as legacy  # noqa: E402  (reuse slicing/IO helpers)

N_FFT = 2048
HOP = 256


@dataclass
class ClassSpec:
    name: str
    # Band used for the onset envelope (Hz).
    env_lo: float
    env_hi: float
    # Min spacing between hits (ms).
    wait_ms: float
    # Onset detection sensitivity (delta on normalized envelope).
    delta: float
    # Time-gate decay after each hit (ms).
    decay_ms: float
    # One-shot slicing window (ms).
    pre_ms: float
    post_ms: float
    # Frequency weighting breakpoints: list of (hz, weight); linear interp.
    band_weights: list[tuple[float, float]] = field(default_factory=list)


CLASS_SPECS = [
    ClassSpec(
        name="kick",
        env_lo=30.0, env_hi=180.0,
        wait_ms=110.0, delta=0.22, decay_ms=320.0,
        pre_ms=15.0, post_ms=450.0,
        band_weights=[(0, 1.0), (150, 1.0), (400, 0.35), (1500, 0.12), (8000, 0.05)],
    ),
    ClassSpec(
        name="snare",
        env_lo=200.0, env_hi=2500.0,
        wait_ms=110.0, delta=0.30, decay_ms=260.0,
        pre_ms=12.0, post_ms=350.0,
        band_weights=[(0, 0.05), (120, 0.1), (180, 1.0), (6000, 1.0), (11000, 0.35)],
    ),
    ClassSpec(
        name="hats",
        env_lo=6000.0, env_hi=16000.0,
        wait_ms=40.0, delta=0.16, decay_ms=160.0,
        pre_ms=8.0, post_ms=200.0,
        band_weights=[(0, 0.0), (2500, 0.0), (5000, 0.9), (7000, 1.0)],
    ),
]


def _stft_mag(y: np.ndarray) -> np.ndarray:
    import librosa

    return np.abs(librosa.stft(y, n_fft=N_FFT, hop_length=HOP))


def _percussive_mag(S: np.ndarray) -> np.ndarray:
    """HPSS on magnitude STFT; keep the percussive part (drops tonal bleed)."""
    import librosa

    _harm, perc = librosa.decompose.hpss(S, margin=(1.0, 3.0))
    return perc


def _band_bins(freqs: np.ndarray, lo: float, hi: float) -> np.ndarray:
    return np.where((freqs >= lo) & (freqs < hi))[0]


def _band_onset_envelope(S_power: np.ndarray, bins: np.ndarray) -> np.ndarray:
    """Spectral-flux onset envelope restricted to a frequency band."""
    band = S_power[bins, :]
    band_db = 10.0 * np.log10(np.maximum(band, 1e-10))
    flux = np.diff(band_db, axis=1, prepend=band_db[:, :1])
    flux = np.maximum(0.0, flux)
    env = flux.mean(axis=0)
    peak = float(env.max()) if env.size else 0.0
    if peak > 0:
        env = env / peak
    return env.astype(np.float32)


def _detect_band_onsets(env: np.ndarray, sr: int, *, wait_ms: float, delta: float) -> np.ndarray:
    import librosa

    wait = int(max(1, round((wait_ms / 1000.0) * sr / HOP)))
    frames = librosa.onset.onset_detect(
        onset_envelope=env,
        sr=sr,
        hop_length=HOP,
        backtrack=True,
        delta=float(delta),
        wait=wait,
        units="frames",
        normalize=True,
    )
    return np.asarray(frames, dtype=int)


@dataclass
class HitFeatures:
    frame: int
    time_s: float
    f_low: float    # fraction of energy < 180 Hz
    f_mid: float    # fraction in 180-4000 Hz
    f_high: float   # fraction > 5000 Hz
    flatness: float  # spectral flatness in 1-8 kHz (noise-iness)
    strength: float  # band envelope value at the hit


def _hit_features(
    S_power: np.ndarray,
    freqs: np.ndarray,
    frame: int,
    sr: int,
    env_value: float,
    *,
    window_ms: float = 70.0,
) -> HitFeatures:
    n_frames = S_power.shape[1]
    win = int(max(1, round((window_ms / 1000.0) * sr / HOP)))
    a = min(frame, n_frames - 1)
    b = min(n_frames, a + win)
    chunk = S_power[:, a:b].mean(axis=1)

    total = float(chunk.sum()) + 1e-12
    low = float(chunk[_band_bins(freqs, 0, 180)].sum())
    mid = float(chunk[_band_bins(freqs, 180, 4000)].sum())
    high = float(chunk[_band_bins(freqs, 5000, freqs[-1] + 1)].sum())

    noise_bins = _band_bins(freqs, 1000, 8000)
    nb = np.maximum(chunk[noise_bins], 1e-12)
    flatness = float(np.exp(np.mean(np.log(nb))) / np.mean(nb)) if nb.size else 0.0

    time_s = a * HOP / float(sr)
    return HitFeatures(
        frame=a,
        time_s=time_s,
        f_low=low / total,
        f_mid=mid / total,
        f_high=high / total,
        flatness=flatness,
        strength=env_value,
    )


def _accept_hits(cls: str, candidates: list[HitFeatures]) -> list[HitFeatures]:
    """
    Validate band-onset candidates against full-spectrum features.

    Thresholds are ADAPTIVE: a candidate's class-band energy is compared
    against the distribution of the other candidates in the same song, so
    dark mixes (hats with little absolute high-band energy) still classify
    correctly. Multi-label by design: a layered kick+hat hit can pass both
    classes.
    """
    if not candidates:
        return []

    def p75(values: list[float]) -> float:
        return float(np.percentile(np.asarray(values, dtype=np.float64), 75))

    if cls == "kick":
        ref = p75([c.f_low for c in candidates])
        floor = max(0.10, 0.55 * ref)
        return [c for c in candidates if c.f_low >= floor]

    if cls == "hats":
        ref = p75([c.f_high for c in candidates])
        floor = max(0.004, 0.45 * ref)
        # Veto candidates that are really low-band thumps with a faint click.
        return [c for c in candidates if c.f_high >= floor and c.f_low <= 0.60]

    if cls == "snare":
        mid_ref = p75([c.f_mid for c in candidates])
        flat_ref = p75([c.flatness for c in candidates])
        mid_floor = 0.70 * mid_ref
        flat_floor = max(0.02, 0.45 * flat_ref)
        # Snares are broadband + noisy in the mids; reject low-dominated
        # kicks and tonal (non-noisy) percussive content.
        return [
            c for c in candidates
            if c.f_mid >= mid_floor and c.f_low <= 0.45 and c.flatness >= flat_floor
        ]

    return []


def _dedupe_times(times_s: np.ndarray, min_gap_s: float) -> np.ndarray:
    if times_s.size == 0:
        return times_s
    out = [float(times_s[0])]
    for t in times_s[1:].tolist():
        if t - out[-1] >= min_gap_s:
            out.append(float(t))
    return np.asarray(out, dtype=np.float64)


def _freq_weights(freqs: np.ndarray, breakpoints: list[tuple[float, float]]) -> np.ndarray:
    xs = np.array([b[0] for b in breakpoints], dtype=np.float64)
    ys = np.array([b[1] for b in breakpoints], dtype=np.float64)
    return np.interp(freqs, xs, ys, left=ys[0], right=ys[-1]).astype(np.float32)


def _time_gate(
    n_frames: int,
    sr: int,
    times_s: np.ndarray,
    *,
    attack_ms: float = 8.0,
    decay_ms: float,
) -> np.ndarray:
    """Smooth 0..1 gate that opens at each hit and decays exponentially."""
    gate = np.zeros(n_frames, dtype=np.float32)
    attack_frames = max(1, int(round((attack_ms / 1000.0) * sr / HOP)))
    decay_frames = max(1, int(round((decay_ms / 1000.0) * sr / HOP)))
    decay_curve = np.exp(-3.0 * np.arange(decay_frames * 2) / decay_frames).astype(np.float32)

    for t in times_s.tolist():
        center = int(round(float(t) * sr / HOP))
        a = max(0, center - attack_frames)
        ramp_len = center - a
        if ramp_len > 0:
            ramp = np.linspace(0.0, 1.0, ramp_len, endpoint=False, dtype=np.float32)
            gate[a:center] = np.maximum(gate[a:center], ramp)
        b = min(n_frames, center + decay_curve.shape[0])
        if b > center:
            gate[center:b] = np.maximum(gate[center:b], decay_curve[: b - center])
    return gate


def _synthesize_stem(
    S_complex: np.ndarray,
    freqs: np.ndarray,
    sr: int,
    n_samples: int,
    times_s: np.ndarray,
    spec: ClassSpec,
) -> np.ndarray:
    import librosa

    w_f = _freq_weights(freqs, spec.band_weights)
    g_t = _time_gate(S_complex.shape[1], sr, times_s, decay_ms=spec.decay_ms)
    masked = S_complex * w_f[:, None] * g_t[None, :]
    y = librosa.istft(masked, hop_length=HOP, n_fft=N_FFT, length=n_samples)
    return y.astype(np.float32, copy=False)


def decompose(
    audio_path: Path,
    out_dir: Path,
    *,
    bitrate: str = "256k",
    write_wav: bool = False,
) -> dict:
    import librosa

    y, sr = legacy._load_mono_audio(audio_path)
    n_samples = y.shape[0]
    duration_s = n_samples / float(sr)

    S = librosa.stft(y, n_fft=N_FFT, hop_length=HOP)
    S_mag = np.abs(S)
    S_perc_mag = _percussive_mag(S_mag)
    # Complex percussive STFT via soft mask (keeps phase of the original).
    perc_mask = S_perc_mag / np.maximum(S_mag, 1e-10)
    S_perc = S * perc_mask
    S_power = S_perc_mag ** 2
    freqs = librosa.fft_frequencies(sr=sr, n_fft=N_FFT)

    ext = "wav" if write_wav else "mp3"
    drum_dir = out_dir / legacy._track_output_name(audio_path) / "drum"
    drum_dir.mkdir(parents=True, exist_ok=True)

    summary: dict = {
        "schema_version": 2,
        "method": "hpss+band-onsets+spectral-classification",
        "source": str(audio_path),
        "sr": sr,
        "duration_s": round(duration_s, 3),
        "classes": {},
    }

    for spec in CLASS_SPECS:
        bins = _band_bins(freqs, spec.env_lo, spec.env_hi)
        env = _band_onset_envelope(S_power, bins)
        frames = _detect_band_onsets(env, sr, wait_ms=spec.wait_ms, delta=spec.delta)

        candidates = [
            _hit_features(S_power, freqs, int(fr), sr, float(env[min(int(fr), env.shape[0] - 1)]))
            for fr in frames.tolist()
        ]
        accepted = _accept_hits(spec.name, candidates)

        times = np.asarray([h.time_s for h in accepted], dtype=np.float64)
        times = _dedupe_times(times, spec.wait_ms / 1000.0)

        cls_info: dict = {"count": int(times.size)}
        times_path = drum_dir / f"{spec.name}_times.csv"
        legacy._write_times_csv(times_path, times)
        cls_info["times_csv"] = times_path.name
        print(f"[{spec.name}] {times.size} hits -> {times_path}")

        if times.size == 0:
            summary["classes"][spec.name] = cls_info
            continue

        stem = _synthesize_stem(S_perc, freqs, sr, n_samples, times, spec)
        stem_path = drum_dir / f"{spec.name}.{ext}"
        if write_wav:
            legacy._write_wav(stem_path, stem, sr)
        else:
            legacy._write_mp3(stem_path, stem, sr, bitrate=bitrate)
        cls_info["stem"] = stem_path.name
        print(f"[{spec.name}] wrote stem -> {stem_path}")

        slices = legacy._slice_hits_nonoverlapping(
            stem, sr, times,
            pre_ms=spec.pre_ms, post_ms=spec.post_ms, margin_ms=8.0,
        )
        one_shot = legacy._pick_one_shot(slices, times, mode="most_isolated")
        if one_shot is not None:
            one_shot = legacy._apply_fade(one_shot, sr, fade_ms=5.0)
            one_path = drum_dir / f"{spec.name}_one_shot.{ext}"
            if write_wav:
                legacy._write_wav(one_path, one_shot, sr)
            else:
                legacy._write_mp3(one_path, one_shot, sr, bitrate=bitrate)
            cls_info["one_shot"] = one_path.name
            print(f"[{spec.name}] wrote one-shot -> {one_path}")

        summary["classes"][spec.name] = cls_info

    summary_path = drum_dir / "decomposition.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote summary -> {summary_path}")
    return summary


def main(argv: list[str] | None = None) -> int:
    legacy._require_python310_or_newer()
    root = legacy._repo_root()

    parser = argparse.ArgumentParser(
        description="Decompose a drum stem into kick/snare/hats (high fidelity)."
    )
    parser.add_argument("audio", type=Path, help="Drum/percussion audio (typically Demucs drums.mp3).")
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=root / "output" / "trackDecomp",
        help="Root output directory (default: output/trackDecomp).",
    )
    parser.add_argument("--mp3-bitrate", type=str, default="256k")
    parser.add_argument("--wav", action="store_true", help="Write WAV instead of MP3.")
    args = parser.parse_args(argv)

    audio_path = args.audio.resolve()
    if not audio_path.exists():
        print(f"ERROR: Audio file not found: {audio_path}", file=sys.stderr)
        return 1

    try:
        decompose(
            audio_path,
            args.out_dir.resolve(),
            bitrate=args.mp3_bitrate,
            write_wav=args.wav,
        )
    except Exception as e:  # noqa: BLE001 - CLI script
        msg = str(e).strip() or e.__class__.__name__
        print(f"ERROR: {msg}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
