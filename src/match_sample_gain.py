#!/usr/bin/env python3
"""
Compute gain so user-recorded sample matches volume of reference.

Use one-shots (kick_one_shot.mp3, hats_one_shot.mp3) as reference for best results:
  python3 src/match_sample_gain.py --reference MVP\ demo/drum/kick_one_shot.mp3 \\
    --sample MVP\ demo/my_kick.mp3 --times MVP\ demo/drum/kick_times.csv

Prints the gain multiplier to stdout (for --gain in repeat_sample_at_times_cli).
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import numpy as np

ONE_SHOT_MAX_SEC = 1.2  # Treat reference as one-shot if duration <= this
SILENCE_THRESH_FRAC = 0.02  # Trim samples below this fraction of max


def _load_audio(path: Path) -> tuple[np.ndarray, int]:
    try:
        import librosa
    except ImportError as e:
        raise RuntimeError("librosa required: pip install librosa") from e
    y, sr = librosa.load(str(path), sr=None, mono=True)
    return y.astype(np.float32), int(sr)


def _rms(x: np.ndarray) -> float:
    if x.size == 0:
        return 1e-10  # avoid division by zero
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


def _trim_silence(y: np.ndarray, threshold_frac: float = SILENCE_THRESH_FRAC) -> np.ndarray:
    """Drop leading/trailing samples below threshold (fraction of max abs)."""
    if y.size == 0:
        return y
    peak = float(np.max(np.abs(y)))
    thresh = peak * threshold_frac
    if thresh < 1e-10:
        return y
    start = 0
    while start < y.size and abs(y[start]) < thresh:
        start += 1
    end = y.size
    while end > start and abs(y[end - 1]) < thresh:
        end -= 1
    if end <= start:
        return y  # all below threshold, return as-is
    return y[start:end]


def _load_times(path: Path) -> list[float]:
    times: list[float] = []
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        if "time_seconds" not in (reader.fieldnames or []):
            raise ValueError("CSV must have 'time_seconds' column")
        for row in reader:
            t = row.get("time_seconds", "").strip()
            if not t:
                continue
            try:
                times.append(float(t))
            except ValueError:
                continue
    return sorted(times)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compute gain so user sample matches reference volume at hit times."
    )
    parser.add_argument(
        "--reference",
        type=Path,
        required=True,
        help="Reference: kick_one_shot.mp3 / hats_one_shot.mp3 (or full stem).",
    )
    parser.add_argument("--sample", type=Path, required=True, help="User-recorded sample (mp3/wav).")
    parser.add_argument(
        "--times",
        type=Path,
        required=True,
        help="Hit times CSV (needed when reference is full stem; ignored for one-shot).",
    )
    parser.add_argument(
        "--min-gain",
        type=float,
        default=0.1,
        help="Clamp gain to at least this (default 0.1).",
    )
    parser.add_argument(
        "--max-gain",
        type=float,
        default=20.0,
        help="Clamp gain to at most this (default 20.0).",
    )
    parser.add_argument(
        "--trim-silence",
        action="store_true",
        default=True,
        help="Trim leading/trailing silence from user sample before RMS (default: on).",
    )
    parser.add_argument(
        "--no-trim-silence",
        action="store_false",
        dest="trim_silence",
        help="Disable silence trimming.",
    )
    parser.add_argument(
        "--window-ms",
        type=float,
        default=180.0,
        help="Window around each hit when reference is full stem (default 180ms).",
    )
    args = parser.parse_args()

    ref_path = args.reference.resolve()
    sample_path = args.sample.resolve()
    times_path = args.times.resolve()

    if not ref_path.exists():
        print(f"ERROR: Reference not found: {ref_path}", file=sys.stderr)
        return 1
    if not sample_path.exists():
        print(f"ERROR: Sample not found: {sample_path}", file=sys.stderr)
        return 1
    if not times_path.exists():
        print(f"ERROR: Times CSV not found: {times_path}", file=sys.stderr)
        return 1

    ref_y, ref_sr = _load_audio(ref_path)
    sample_y, _ = _load_audio(sample_path)

    # Trim silence from user sample so RMS reflects the actual hit, not dead air
    if args.trim_silence:
        sample_y = _trim_silence(sample_y)

    sample_rms = _rms(sample_y)
    if sample_rms < 1e-10:
        print("ERROR: Sample is silent.", file=sys.stderr)
        return 1

    ref_duration_sec = ref_y.shape[0] / float(ref_sr)
    use_one_shot = ref_duration_sec <= ONE_SHOT_MAX_SEC

    if use_one_shot:
        # One-shot: direct comparison (single hit vs single hit)
        ref_rms = _rms(ref_y)
        if ref_rms < 1e-10:
            print("1.0", end="")
            return 0
        ref_avg_rms = ref_rms
    else:
        # Full stem: measure reference at each hit time
        times = _load_times(times_path)
        if not times:
            print("ERROR: No timestamps in CSV (required when reference is full stem).", file=sys.stderr)
            return 1
        window_samples = int(round((args.window_ms / 1000.0) * ref_sr))
        window_samples = max(1, min(window_samples, ref_y.shape[0] // 2))
        ref_rms_list: list[float] = []
        for t in times:
            center = int(round(t * ref_sr))
            start = max(0, center - window_samples // 2)
            end = min(ref_y.shape[0], start + window_samples)
            if end <= start:
                continue
            chunk = ref_y[start:end]
            r = _rms(chunk)
            if r > 1e-10:
                ref_rms_list.append(r)
        if not ref_rms_list:
            print("1.0", end="")
            return 0
        ref_avg_rms = float(np.median(ref_rms_list))

    gain = ref_avg_rms / sample_rms
    gain = max(args.min_gain, min(args.max_gain, gain))
    print(f"{gain:.4f}", end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
