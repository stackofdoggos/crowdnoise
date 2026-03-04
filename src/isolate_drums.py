#!/usr/bin/env python3
"""
Isolate "low" and "high" percussion from a drum/percussion track using filters.

Default repo layout (if you pass a file from `resources/`):
  - input:  resources/<file>.mp3
  - output: output/trackDecomp/<file>_kick.mp3, output/trackDecomp/<file>_hats.mp3

What it does:
  - Loads the MP3 (mono) with librosa
  - "Kick-like" stem: band-pass around ~40–160 Hz
  - "Hat/cymbal-like" stem: high-pass above ~7 kHz
  - Optional simple transient gate (envelope threshold) to reduce bleed

Notes:
  - MP3 decoding may require ffmpeg on some systems (e.g. `brew install ffmpeg`).
  - MP3 encoding requires ffmpeg as well.
  - Filtering won't perfectly isolate drums (spectral overlap), but it’s a solid baseline.
"""

from __future__ import annotations

import argparse
import csv
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

DEMUX_STEM_NAMES = {"drums", "bass", "other", "vocals", "guitar", "piano"}


def _require_python310_or_newer() -> None:
    major, minor = sys.version_info[:2]
    if (major, minor) < (3, 10):
        raise RuntimeError(
            f"This script must be run with Python 3.10+ (you are running {major}.{minor}).\n"
            "Run it with:\n"
            "  python3 src/isolate_drums.py path/to/audio.mp3\n"
        )


def _repo_root() -> Path:
    # src/isolate_drums.py -> repo root
    return Path(__file__).resolve().parents[1]


def _track_output_name(audio_path: Path) -> str:
    """
    Name the output folder for this input track.

    For Demucs stem files like ".../<song>/drums.mp3", use "<song>" so the
    layout mirrors output/htdemucs_6s/<song>/...
    """
    if audio_path.stem.lower() in DEMUX_STEM_NAMES and audio_path.parent.name:
        return audio_path.parent.name
    return audio_path.stem


def _load_mono_audio(audio_path: Path) -> tuple[np.ndarray, int]:
    try:
        import librosa  # imported lazily for friendlier CLI errors
    except Exception as e:  # noqa: BLE001 - CLI tool
        raise RuntimeError(
            "librosa is not installed (or failed to import).\n"
            "Install dependencies (basic-pitch pulls in librosa), e.g.:\n"
            "  pip install basic-pitch\n"
        ) from e

    y, sr = librosa.load(str(audio_path), sr=None, mono=True)
    if y.size == 0:
        raise RuntimeError(f"Loaded empty audio: {audio_path}")
    return y.astype(np.float32, copy=False), int(sr)


def _butter_sos_filter(
    x: np.ndarray,
    sr: int,
    *,
    kind: str,
    f1_hz: float,
    f2_hz: float | None = None,
    order: int = 6,
) -> np.ndarray:
    """
    Butterworth IIR filter applied with zero-phase filtering (preserves transients).
    """
    try:
        from scipy.signal import butter, sosfiltfilt  # type: ignore
    except Exception as e:  # noqa: BLE001 - CLI tool
        raise RuntimeError(
            "scipy is not installed (or failed to import).\n"
            "Install it with:\n"
            "  pip install scipy\n"
        ) from e

    nyq = 0.5 * sr
    if nyq <= 0:
        raise ValueError("Invalid sample rate.")

    if kind == "lowpass":
        wn = f1_hz / nyq
        sos = butter(order, wn, btype="lowpass", output="sos")
    elif kind == "highpass":
        wn = f1_hz / nyq
        sos = butter(order, wn, btype="highpass", output="sos")
    elif kind == "bandpass":
        if f2_hz is None:
            raise ValueError("bandpass requires f2_hz.")
        wn = [f1_hz / nyq, f2_hz / nyq]
        sos = butter(order, wn, btype="bandpass", output="sos")
    else:
        raise ValueError("kind must be: lowpass | highpass | bandpass")

    y = sosfiltfilt(sos, x)
    return y.astype(np.float32, copy=False)


def _envelope_follower(
    x: np.ndarray,
    sr: int,
    *,
    attack_ms: float = 5.0,
    release_ms: float = 80.0,
) -> np.ndarray:
    """
    Simple attack/release envelope follower on |x|.
    """
    if sr <= 0:
        raise ValueError("Invalid sample rate.")

    attack = float(np.exp(-1.0 / (sr * (attack_ms / 1000.0))))
    release = float(np.exp(-1.0 / (sr * (release_ms / 1000.0))))

    env = np.zeros_like(x, dtype=np.float32)
    prev = 0.0
    for i in range(x.shape[0]):
        rect = float(abs(x[i]))
        if rect > prev:
            prev = attack * prev + (1.0 - attack) * rect
        else:
            prev = release * prev + (1.0 - release) * rect
        env[i] = prev
    return env


def _soft_gate_from_envelope(
    x: np.ndarray,
    env: np.ndarray,
    *,
    thresh_db: float,
    smooth_ms: float,
    sr: int,
    knee_db: float = 6.0,
) -> np.ndarray:
    """
    Create a smooth gate mask from an envelope using a soft knee.
    Gain transitions smoothly below threshold instead of hard cut.
    """
    env_db = 20.0 * np.log10(np.maximum(env, 1e-8))
    # Soft knee: gain = 1 above thresh, smooth ramp in [thresh-knee, thresh]
    if knee_db <= 0:
        mask = (env_db >= thresh_db).astype(np.float32)
    else:
        ramp_start = thresh_db - knee_db
        mask = np.zeros_like(env_db, dtype=np.float32)
        above = env_db >= thresh_db
        below = env_db <= ramp_start
        in_ramp = ~above & ~below
        mask[above] = 1.0
        mask[below] = 0.0
        # Smooth S-curve in dB space
        t = (env_db[in_ramp] - ramp_start) / knee_db
        mask[in_ramp] = t * t * (3.0 - 2.0 * t)  # smoothstep

    if smooth_ms > 0:
        win = int(max(1, round((smooth_ms / 1000.0) * sr)))
        kernel = np.ones(win, dtype=np.float32) / float(win)
        mask = np.convolve(mask, kernel, mode="same")

    return (x * mask).astype(np.float32, copy=False)


def _normalize_to_peak(y: np.ndarray, target_peak: float = 0.95, max_gain: float = 50.0) -> np.ndarray:
    """
    Scale so peak reaches target_peak. Boosts quiet decomposed stems; limits gain to avoid noise.
    """
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak < 1e-10:
        return y
    gain = target_peak / peak
    gain = min(gain, max_gain)  # cap boost to avoid amplifying near-silence
    return (y * gain).astype(np.float32, copy=False)


def _isolate_filtered(
    y: np.ndarray,
    sr: int,
    *,
    kick_low_hz: float,
    kick_high_hz: float,
    hats_highpass_hz: float,
    filter_order: int,
) -> tuple[np.ndarray, np.ndarray]:
    kick = _butter_sos_filter(
        y,
        sr,
        kind="bandpass",
        f1_hz=kick_low_hz,
        f2_hz=kick_high_hz,
        order=filter_order,
    )
    hats = _butter_sos_filter(
        y,
        sr,
        kind="highpass",
        f1_hz=hats_highpass_hz,
        order=filter_order,
    )
    return kick, hats


def _apply_gate(
    x: np.ndarray,
    sr: int,
    *,
    thresh_db: float,
    attack_ms: float,
    release_ms: float,
    smooth_ms: float,
    knee_db: float = 6.0,
) -> np.ndarray:
    env = _envelope_follower(x, sr, attack_ms=attack_ms, release_ms=release_ms)
    return _soft_gate_from_envelope(
        x, env, thresh_db=thresh_db, smooth_ms=smooth_ms, sr=sr, knee_db=knee_db
    )


def _detect_hit_times(
    x: np.ndarray,
    sr: int,
    *,
    hop_length: int,
    fmin: float,
    wait_ms: float,
    delta: float,
) -> np.ndarray:
    """
    Return an array of hit timestamps (seconds) by analyzing when the stem has onsets.
    """
    try:
        import librosa  # imported lazily for friendlier CLI errors
    except Exception as e:  # noqa: BLE001 - CLI tool
        raise RuntimeError(
            "librosa is not installed (or failed to import).\n"
            "Install dependencies (basic-pitch pulls in librosa), e.g.:\n"
            "  pip install basic-pitch\n"
        ) from e

    if hop_length <= 0:
        raise ValueError("hop_length must be > 0")
    if sr <= 0:
        raise ValueError("Invalid sample rate.")

    # Onset strength envelope; fmin focuses the band that should drive onsets.
    onset_env = librosa.onset.onset_strength(y=x, sr=sr, hop_length=hop_length, fmin=float(fmin))
    wait = int(max(0, round((wait_ms / 1000.0) * sr / hop_length)))

    frames = librosa.onset.onset_detect(
        onset_envelope=onset_env,
        sr=sr,
        hop_length=hop_length,
        backtrack=True,
        delta=float(delta),
        wait=wait,
        units="frames",
    )
    times = librosa.frames_to_time(frames, sr=sr, hop_length=hop_length)
    return times.astype(np.float64, copy=False)


def _slice_hits(
    x: np.ndarray,
    sr: int,
    times_s: np.ndarray,
    *,
    pre_ms: float,
    post_ms: float,
) -> list[np.ndarray]:
    pre = int(max(0, round((pre_ms / 1000.0) * sr)))
    post = int(max(1, round((post_ms / 1000.0) * sr)))
    hits: list[np.ndarray] = []
    n = x.shape[0]
    for t in times_s:
        center = int(round(float(t) * sr))
        start = max(0, center - pre)
        end = min(n, center + post)
        if end - start <= 1:
            continue
        hits.append(x[start:end].astype(np.float32, copy=False))
    return hits


def _rms(x: np.ndarray) -> float:
    if x.size == 0:
        return 0.0
    return float(math.sqrt(float(np.mean(x.astype(np.float64) ** 2))))


def _slice_hits_nonoverlapping(
    x: np.ndarray,
    sr: int,
    times_s: np.ndarray,
    *,
    pre_ms: float,
    post_ms: float,
    margin_ms: float,
) -> list[tuple[int, np.ndarray]]:
    """
    Slice hit windows, but never let a slice overlap the neighboring hit.

    We constrain each slice to the interval between midpoints:
      left_limit  = midpoint(prev, current) + margin
      right_limit = midpoint(current, next) - margin

    This prevents one-shots from accidentally including the next drum hit
    (common for fast hats / dense patterns).
    """
    pre = int(max(0, round((pre_ms / 1000.0) * sr)))
    post = int(max(1, round((post_ms / 1000.0) * sr)))
    margin = int(max(0, round((margin_ms / 1000.0) * sr)))

    n = x.shape[0]
    out: list[tuple[int, np.ndarray]] = []
    if times_s.size == 0:
        return out

    times = times_s.astype(np.float64, copy=False)
    for i, t in enumerate(times.tolist()):
        center = int(round(float(t) * sr))

        # Midpoint limits in samples.
        if i == 0:
            left_limit = 0
        else:
            left_limit = int(round(((times[i - 1] + times[i]) * 0.5) * sr)) + margin

        if i == len(times) - 1:
            right_limit = n
        else:
            right_limit = int(round(((times[i] + times[i + 1]) * 0.5) * sr)) - margin

        start = max(0, center - pre, left_limit)
        end = min(n, center + post, right_limit)

        if end - start <= 1:
            continue
        out.append((i, x[start:end].astype(np.float32, copy=False)))

    return out


def _pick_one_shot(
    sliced_hits: list[tuple[int, np.ndarray]],
    times_s: np.ndarray,
    *,
    mode: str,
) -> np.ndarray | None:
    """
    Pick a single representative hit from a list of (time_index, slice).
    """
    if not sliced_hits:
        return None

    # Precompute RMS for tie-breaking / modes.
    energies = np.array([_rms(h) for _, h in sliced_hits], dtype=np.float64)

    if mode == "max_rms":
        return sliced_hits[int(np.argmax(energies))][1]
    if mode == "median_rms":
        target = float(np.median(energies))
        return sliced_hits[int(np.argmin(np.abs(energies - target)))][1]
    if mode == "most_isolated":
        times = times_s.astype(np.float64, copy=False)

        def isolation_score(time_index: int) -> float:
            # Prefer hits with the largest gap to their nearest neighbor.
            # Endpoints only have one neighbor, so score is that single gap.
            if times.size <= 1:
                return float("inf")
            if time_index <= 0:
                return float(times[1] - times[0])
            if time_index >= times.size - 1:
                return float(times[-1] - times[-2])
            prev_gap = float(times[time_index] - times[time_index - 1])
            next_gap = float(times[time_index + 1] - times[time_index])
            return min(prev_gap, next_gap)

        scores = np.array([isolation_score(i) for i, _ in sliced_hits], dtype=np.float64)
        # Tie-break by RMS so we don't pick silence.
        best = int(np.argmax(scores + (energies * 1e-3)))
        return sliced_hits[best][1]

    raise ValueError("hit_pick must be: max_rms | median_rms | most_isolated")


def _apply_fade(y: np.ndarray, sr: int, *, fade_ms: float) -> np.ndarray:
    """
    Apply a short fade in/out to reduce clicks on tightly-trimmed one-shots.
    """
    if fade_ms <= 0:
        return y
    n = y.shape[0]
    fade = int(round((fade_ms / 1000.0) * sr))
    fade = max(0, min(fade, n // 2))
    if fade <= 1:
        return y

    y2 = y.astype(np.float32, copy=True)
    ramp = np.linspace(0.0, 1.0, fade, dtype=np.float32)
    y2[:fade] *= ramp
    y2[-fade:] *= ramp[::-1]
    return y2


def _pick_representative_hit(hits: list[np.ndarray], *, mode: str) -> np.ndarray | None:
    if not hits:
        return None
    energies = np.array([_rms(h) for h in hits], dtype=np.float64)
    if mode == "max_rms":
        idx = int(np.argmax(energies))
        return hits[idx]
    if mode == "median_rms":
        target = float(np.median(energies))
        idx = int(np.argmin(np.abs(energies - target)))
        return hits[idx]
    raise ValueError("hit_pick must be: max_rms | median_rms")


def _write_times_csv(path: Path, times_s: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["index", "time_seconds"])
        for i, t in enumerate(times_s.tolist()):
            writer.writerow([i, f"{float(t):.6f}"])


def isolate_drums(
    audio_path: Path,
    *,
    kick_low_hz: float = 40.0,
    kick_high_hz: float = 160.0,
    hats_highpass_hz: float = 7000.0,
    filter_order: int = 6,
    gate: bool = True,
    kick_gate_db: float = -35.0,
    hats_gate_db: float = -40.0,
    gate_attack_ms: float = 5.0,
    gate_release_ms: float = 80.0,
    gate_smooth_ms: float = 10.0,
) -> tuple[np.ndarray, np.ndarray, int]:
    y, sr = _load_mono_audio(audio_path)

    kick, hats = _isolate_filtered(
        y,
        sr,
        kick_low_hz=kick_low_hz,
        kick_high_hz=kick_high_hz,
        hats_highpass_hz=hats_highpass_hz,
        filter_order=filter_order,
    )

    if gate:
        kick = _apply_gate(
            kick,
            sr,
            thresh_db=kick_gate_db,
            attack_ms=gate_attack_ms,
            release_ms=gate_release_ms,
            smooth_ms=gate_smooth_ms,
        )
        hats = _apply_gate(
            hats,
            sr,
            thresh_db=hats_gate_db,
            attack_ms=gate_attack_ms,
            release_ms=gate_release_ms,
            smooth_ms=gate_smooth_ms,
        )

    return kick, hats, sr


def _write_wav(path: Path, y: np.ndarray, sr: int, subtype: str = "FLOAT") -> None:
    try:
        import soundfile as sf  # type: ignore
    except Exception as e:  # noqa: BLE001 - CLI tool
        raise RuntimeError(
            "soundfile is not installed (or failed to import).\n"
            "Install it with:\n"
            "  pip install soundfile\n"
        ) from e

    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(path), _normalize_to_peak(y), sr, subtype=subtype)


def _write_mp3(path: Path, y: np.ndarray, sr: int, *, bitrate: str = "192k") -> None:
    """
    Write an MP3 by writing a temporary WAV and converting with ffmpeg.
    """
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError(
            "ffmpeg was not found on your PATH. MP3 encoding requires ffmpeg.\n"
            "Install it with:\n"
            "  brew install ffmpeg\n"
        )

    path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.NamedTemporaryFile(
        prefix=f".{path.stem}_",
        suffix=".wav",
        dir=str(path.parent),
        delete=False,
    ) as tmp:
        tmp_wav = Path(tmp.name)

    try:
        _write_wav(tmp_wav, y, sr)
        cmd = [
            ffmpeg,
            "-y",
            "-hide_banner",
            "-loglevel",
            "error",
            "-i",
            str(tmp_wav),
            "-vn",
            "-codec:a",
            "libmp3lame",
            "-b:a",
            bitrate,
            str(path),
        ]
        subprocess.run(cmd, check=True)
    finally:
        try:
            tmp_wav.unlink(missing_ok=True)
        except Exception:
            pass


def main(argv: list[str] | None = None) -> int:
    _require_python310_or_newer()
    root = _repo_root()

    parser = argparse.ArgumentParser(
        description="Isolate low/high percussion from a drum/percussion track using filters."
    )
    parser.add_argument(
        "audio",
        type=Path,
        help="Path to input drum/percussion audio (mp3/wav/etc).",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=root / "output" / "trackDecomp",
        help="Directory to write output MP3 stems.",
    )
    parser.add_argument(
        "--mvp-demo",
        action="store_true",
        help="Also copy drum outputs (kick, hats, times, one-shots) to MVP demo/drum/.",
    )

    # Filter parameters (wider bands + lower order = more natural sound, less ringing)
    parser.add_argument("--kick-low-hz", type=float, default=35.0)
    parser.add_argument("--kick-high-hz", type=float, default=220.0, help="Wider to capture kick body/harmonics.")
    parser.add_argument("--hats-highpass-hz", type=float, default=5500.0, help="Lower = more body, less thin.")
    parser.add_argument("--order", type=int, default=4, help="Lower order = gentler rolloff, less phase distortion.")

    # Gate parameters
    parser.add_argument("--no-gate", action="store_true", help="Disable transient gating.")
    parser.add_argument("--kick-gate-db", type=float, default=-40.0, help="Softer default to reduce artifacts.")
    parser.add_argument("--hats-gate-db", type=float, default=-45.0, help="Softer default to reduce artifacts.")
    parser.add_argument("--gate-attack-ms", type=float, default=5.0)
    parser.add_argument("--gate-release-ms", type=float, default=80.0)
    parser.add_argument("--gate-smooth-ms", type=float, default=15.0, help="Longer smooth = less pumping.")
    parser.add_argument("--gate-knee-db", type=float, default=8.0, help="Soft-knee width; 0 = hard gate.")

    # Output quality
    parser.add_argument(
        "--mp3-bitrate",
        type=str,
        default="256k",
        help="MP3 bitrate (256k or 320k for higher fidelity).",
    )
    parser.add_argument(
        "--wav",
        action="store_true",
        help="Output WAV instead of MP3 (lossless, better fidelity).",
    )

    # Hit detection / one-shot export
    parser.add_argument(
        "--export-hits",
        action="store_true",
        help="Also export per-hit timestamps CSV + a single representative hit (one-shot) for each stem.",
    )
    parser.add_argument("--hop-length", type=int, default=256, help="Hop length for onset detection.")

    parser.add_argument("--kick-hit-fmin", type=float, default=40.0, help="fmin for kick onset strength.")
    parser.add_argument("--hats-hit-fmin", type=float, default=5000.0, help="fmin for hats onset strength.")
    parser.add_argument("--kick-wait-ms", type=float, default=120.0, help="Min spacing between kick hits.")
    parser.add_argument("--hats-wait-ms", type=float, default=40.0, help="Min spacing between hats hits.")
    parser.add_argument("--kick-delta", type=float, default=0.2, help="Sensitivity for kick onset detection.")
    parser.add_argument("--hats-delta", type=float, default=0.15, help="Sensitivity for hats onset detection.")

    parser.add_argument("--kick-pre-ms", type=float, default=20.0, help="Pre-roll before kick hit (ms).")
    parser.add_argument("--kick-post-ms", type=float, default=500.0, help="Tail after kick hit (ms).")
    parser.add_argument("--hats-pre-ms", type=float, default=10.0, help="Pre-roll before hats hit (ms).")
    parser.add_argument("--hats-post-ms", type=float, default=180.0, help="Tail after hats hit (ms).")

    parser.add_argument(
        "--hit-pick",
        type=str,
        default="most_isolated",
        choices=["most_isolated", "max_rms", "median_rms"],
        help="How to choose the single representative hit from all detected hits.",
    )
    parser.add_argument(
        "--slice-margin-ms",
        type=float,
        default=8.0,
        help="Extra margin to keep away from neighboring hits when slicing one-shots.",
    )
    parser.add_argument(
        "--one-shot-fade-ms",
        type=float,
        default=5.0,
        help="Fade in/out applied to one-shot samples to reduce clicks (ms).",
    )

    args = parser.parse_args(argv)

    audio_path: Path = args.audio.resolve()
    if not audio_path.exists():
        print(f"ERROR: Audio file not found: {audio_path}", file=sys.stderr)
        return 1

    try:
        y, sr = _load_mono_audio(audio_path)
        kick_filt, hats_filt = _isolate_filtered(
            y,
            sr,
            kick_low_hz=args.kick_low_hz,
            kick_high_hz=args.kick_high_hz,
            hats_highpass_hz=args.hats_highpass_hz,
            filter_order=args.order,
        )

        # Gated outputs (what we write as full stems)
        if args.no_gate:
            kick_out, hats_out = kick_filt, hats_filt
        else:
            kick_out = _apply_gate(
                kick_filt,
                sr,
                thresh_db=args.kick_gate_db,
                attack_ms=args.gate_attack_ms,
                release_ms=args.gate_release_ms,
                smooth_ms=args.gate_smooth_ms,
                knee_db=args.gate_knee_db,
            )
            hats_out = _apply_gate(
                hats_filt,
                sr,
                thresh_db=args.hats_gate_db,
                attack_ms=args.gate_attack_ms,
                release_ms=args.gate_release_ms,
                smooth_ms=args.gate_smooth_ms,
                knee_db=args.gate_knee_db,
            )
    except Exception as e:  # noqa: BLE001 - CLI script
        msg = str(e).strip() or e.__class__.__name__
        print(f"ERROR: {msg}", file=sys.stderr)
        return 1

    out_root: Path = args.out_dir.resolve()
    track_dir = out_root / _track_output_name(audio_path)
    drum_dir = track_dir / "drum"
    ext = "wav" if args.wav else "mp3"
    kick_path = drum_dir / f"kick.{ext}"
    hats_path = drum_dir / f"hats.{ext}"

    try:
        if args.wav:
            _write_wav(kick_path, kick_out, sr)
            _write_wav(hats_path, hats_out, sr)
        else:
            _write_mp3(kick_path, kick_out, sr, bitrate=args.mp3_bitrate)
            _write_mp3(hats_path, hats_out, sr, bitrate=args.mp3_bitrate)
    except Exception as e:  # noqa: BLE001 - CLI script
        msg = str(e).strip() or e.__class__.__name__
        print(f"ERROR: {msg}", file=sys.stderr)
        return 1

    print(f"Wrote: {kick_path}")
    print(f"Wrote: {hats_path}")

    if args.export_hits:
        # Detect hit times from kick.mp3 and hats.mp3 stems (when each has onsets).
        try:
            kick_times = _detect_hit_times(
                kick_out,
                sr,
                hop_length=args.hop_length,
                fmin=args.kick_hit_fmin,
                wait_ms=args.kick_wait_ms,
                delta=args.kick_delta,
            )
            hats_times = _detect_hit_times(
                hats_out,
                sr,
                hop_length=args.hop_length,
                fmin=args.hats_hit_fmin,
                wait_ms=args.hats_wait_ms,
                delta=args.hats_delta,
            )
        except Exception as e:  # noqa: BLE001 - CLI script
            msg = str(e).strip() or e.__class__.__name__
            print(f"ERROR: hit detection failed: {msg}", file=sys.stderr)
            return 1

        kick_times_path = drum_dir / "kick_times.csv"
        hats_times_path = drum_dir / "hats_times.csv"
        _write_times_csv(kick_times_path, kick_times)
        _write_times_csv(hats_times_path, hats_times)
        print(f"Wrote: {kick_times_path}")
        print(f"Wrote: {hats_times_path}")

        # Slice one-shot candidates using non-overlapping windows.
        kick_slices = _slice_hits_nonoverlapping(
            kick_out,
            sr,
            kick_times,
            pre_ms=args.kick_pre_ms,
            post_ms=args.kick_post_ms,
            margin_ms=args.slice_margin_ms,
        )
        hats_slices = _slice_hits_nonoverlapping(
            hats_out,
            sr,
            hats_times,
            pre_ms=args.hats_pre_ms,
            post_ms=args.hats_post_ms,
            margin_ms=args.slice_margin_ms,
        )

        kick_one = _pick_one_shot(kick_slices, kick_times, mode=args.hit_pick)
        hats_one = _pick_one_shot(hats_slices, hats_times, mode=args.hit_pick)

        if kick_one is not None:
            kick_one = _apply_fade(kick_one, sr, fade_ms=args.one_shot_fade_ms)
        if hats_one is not None:
            hats_one = _apply_fade(hats_one, sr, fade_ms=args.one_shot_fade_ms)

        if kick_one is not None:
            kick_one_path = drum_dir / f"kick_one_shot.{ext}"
            if args.wav:
                _write_wav(kick_one_path, kick_one, sr)
            else:
                _write_mp3(kick_one_path, kick_one, sr, bitrate=args.mp3_bitrate)
            print(f"Wrote: {kick_one_path}")
        else:
            print("WARN: no kick hits detected; skipping kick one-shot.")

        if hats_one is not None:
            hats_one_path = drum_dir / f"hats_one_shot.{ext}"
            if args.wav:
                _write_wav(hats_one_path, hats_one, sr)
            else:
                _write_mp3(hats_one_path, hats_one, sr, bitrate=args.mp3_bitrate)
            print(f"Wrote: {hats_one_path}")
        else:
            print("WARN: no hats hits detected; skipping hats one-shot.")

    if args.mvp_demo:
        mvp_drum = root / "MVP demo" / "drum"
        mvp_drum.mkdir(parents=True, exist_ok=True)
        for f in drum_dir.iterdir():
            if f.is_file():
                shutil.copy2(f, mvp_drum / f.name)
                print(f"Copied to {mvp_drum / f.name}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

