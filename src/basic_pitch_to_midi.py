#!/usr/bin/env python3
"""
Extract MIDI from an audio file using Spotify Basic Pitch.

Default repo layout (if you pass a file from `resources/`):
  - input:  resources/<file>.mp3
  - output: output/trackDecomp/<file>.mid

Setup:
  pip install basic-pitch

Notes:
  - MP3 decoding may require ffmpeg on some systems (e.g. `brew install ffmpeg`).
"""

from __future__ import annotations

import argparse
import inspect
import os
import re
import shutil
import sys
from pathlib import Path

DEMUX_STEM_NAMES = {"drums", "bass", "other", "vocals", "guitar", "piano"}


def _require_python310_or_newer() -> None:
    major, minor = sys.version_info[:2]
    if (major, minor) < (3, 10):
        raise RuntimeError(
            f"This script must be run with Python 3.10+ (you are running {major}.{minor}).\n"
            "Run it with:\n"
            "  python3 src/basic_pitch_to_midi.py path/to/audio.mp3\n"
        )


def _repo_root() -> Path:
    # src/basic_pitch_to_midi.py -> repo root
    return Path(__file__).resolve().parents[1]


def _normalized_name(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", name.lower())


def _default_midi_out(root: Path, audio_path: Path) -> Path:
    instrument = "piano"
    if _normalized_name(audio_path.stem) in DEMUX_STEM_NAMES and audio_path.parent.name:
        song_name = audio_path.parent.name
    else:
        song_name = audio_path.stem
    return root / "output" / "trackDecomp" / song_name / instrument / f"{instrument}.mid"


def _call_predict_and_save(audio_path: Path, output_dir: Path) -> None:
    """
    Call Basic Pitch's Python API, but only pass kwargs that exist in the
    installed version (the signature has changed across releases).
    """
    # Basic Pitch depends on librosa -> numba. In restricted environments (or when
    # site-packages isn't writable), numba's on-disk cache can fail unless we
    # provide a writable cache directory.
    cache_dir = output_dir / ".numba_cache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("NUMBA_CACHE_DIR", str(cache_dir))

    try:
        from basic_pitch.inference import ICASSP_2022_MODEL_PATH, predict_and_save  # type: ignore
    except Exception as e:  # noqa: BLE001 - want a friendly error in CLI context
        raise RuntimeError(
            "Basic Pitch is not installed (or failed to import).\n"
            "Install it with:\n"
            "  pip install basic-pitch\n"
        ) from e

    sig = inspect.signature(predict_and_save)
    kwargs = {}

    # Common flags across versions (only set if supported).
    if "save_midi" in sig.parameters:
        kwargs["save_midi"] = True
    if "save_notes" in sig.parameters:
        kwargs["save_notes"] = False
    if "save_model_outputs" in sig.parameters:
        kwargs["save_model_outputs"] = False
    if "sonify_midi" in sig.parameters:
        kwargs["sonify_midi"] = False

    if "model_or_model_path" in sig.parameters:
        # Use the built-in ICASSP 2022 model path constant.
        kwargs["model_or_model_path"] = str(ICASSP_2022_MODEL_PATH)

    # Most versions accept: predict_and_save(audio_path_list, output_directory, **kwargs)
    predict_and_save([str(audio_path)], str(output_dir), **kwargs)


def extract_midi(audio_path: Path, out_midi_path: Path) -> Path:
    audio_path = audio_path.resolve()
    out_midi_path = out_midi_path.resolve()
    out_dir = out_midi_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    if not audio_path.exists():
        raise FileNotFoundError(f"Audio file not found: {audio_path}")

    _call_predict_and_save(audio_path=audio_path, output_dir=out_dir)

    # Basic Pitch typically writes <stem>.mid, but some versions may use a suffix.
    if out_midi_path.exists():
        return out_midi_path

    candidates = sorted(out_dir.glob(f"{audio_path.stem}*.mid"))
    if not candidates:
        raise RuntimeError(
            "Basic Pitch completed but no MIDI was found in the output directory.\n"
            f"Looked in: {out_dir}\n"
        )

    # Copy the first candidate to the requested path (keep original too).
    shutil.copyfile(candidates[0], out_midi_path)
    return out_midi_path


def main(argv: list[str] | None = None) -> int:
    _require_python310_or_newer()
    root = _repo_root()

    parser = argparse.ArgumentParser(description="Extract MIDI using Spotify Basic Pitch.")
    parser.add_argument(
        "audio",
        type=Path,
        help="Path to input audio file.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Path to output .mid file. Defaults to output/trackDecomp/<song>/piano/piano.mid",
    )
    args = parser.parse_args(argv)

    try:
        out_path = args.out
        if out_path is None:
            out_path = _default_midi_out(root, args.audio)
        out = extract_midi(args.audio, out_path)
    except Exception as e:  # noqa: BLE001 - CLI script
        msg = str(e).strip() or e.__class__.__name__
        print(f"ERROR: {msg}", file=sys.stderr)
        return 1

    print(f"Wrote MIDI: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

