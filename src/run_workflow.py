#!/usr/bin/env python3
"""
Run the full crowdnoise MVP pipeline for a song query.

Pipeline:
1) Scrape/download song audio into resources/
2) Split stems with Demucs (htdemucs_6s)
3) In parallel:
   - isolate drums + export hits/one-shots
   - convert piano stem to MIDI
4) Visualize hit times and save plot PNG

Example:
  python3 src/run_workflow.py "Kanye West - Homecoming"
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

MODEL_NAME = "htdemucs_6s"


def _slugify(text: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return slug or "track"


def _tokenize(text: str) -> set[str]:
    return set(re.findall(r"[a-z0-9]+", text.lower()))


def _score_name(name: str, query_tokens: set[str]) -> tuple[int, int]:
    name_tokens = _tokenize(name)
    overlap = len(query_tokens & name_tokens)
    return (overlap, len(name_tokens))


def _run_cmd(cmd: list[str], cwd: Path, step_name: str) -> int:
    print(f"\n[{step_name}]")
    print(" ".join(cmd))
    return subprocess.call(cmd, cwd=str(cwd))


def _prompt_nonempty(prompt: str) -> str:
    while True:
        value = input(prompt).strip()
        if value:
            return value
        print("Please enter a value.")


def _prompt_float(prompt: str, default: float) -> float:
    raw = input(f"{prompt} [{default}]: ").strip()
    if not raw:
        return float(default)
    try:
        value = float(raw)
    except ValueError:
        print("Invalid number, using default.")
        return float(default)
    return value


def _pick_target_file(resources_dir: Path, query: str, before_paths: set[Path]) -> Path:
    after_paths = {
        p.resolve()
        for p in resources_dir.iterdir()
        if p.is_file() and p.suffix.lower() == ".mp3" and p.name != ".gitkeep"
    }
    if not after_paths:
        raise RuntimeError(f"No MP3 files found in {resources_dir}")

    new_files = [p for p in after_paths if p not in before_paths]
    if new_files:
        return max(new_files, key=lambda p: p.stat().st_mtime)

    # Fallback when scrape reused an existing file.
    query_tokens = _tokenize(query)
    scored = sorted(
        after_paths,
        key=lambda p: (_score_name(p.stem, query_tokens), p.stat().st_mtime),
        reverse=True,
    )
    return scored[0]


def _pick_song_dir(model_output_dir: Path, target_stem: str, query: str) -> Path:
    exact = model_output_dir / target_stem
    if exact.is_dir():
        return exact

    song_dirs = [p for p in model_output_dir.iterdir() if p.is_dir()]
    if not song_dirs:
        raise RuntimeError(f"No split song folders found in {model_output_dir}")

    query_tokens = _tokenize(query)
    scored = sorted(
        song_dirs,
        key=lambda p: (_score_name(p.name, query_tokens), p.stat().st_mtime),
        reverse=True,
    )
    return scored[0]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run scrape -> split -> drum isolate + piano midi -> visualize.")
    parser.add_argument(
        "song",
        nargs="?",
        default=None,
        help="Song title/query to scrape (e.g. 'Kanye West - Homecoming').",
    )
    parser.add_argument(
        "--max-seconds",
        type=float,
        default=15.0,
        help="Max seconds to show in hit visualization (default: 15).",
    )
    args = parser.parse_args(argv)
    song = args.song.strip() if isinstance(args.song, str) else None
    max_seconds = float(args.max_seconds)

    if not song:
        print("Interactive workflow setup")
        song = _prompt_nonempty("Song title/query: ")
        max_seconds = _prompt_float("Visualizer max seconds", default=max_seconds)

    project_root = Path(__file__).resolve().parents[1]
    src_dir = project_root / "src"
    resources_dir = project_root / "resources"
    output_dir = project_root / "output"
    model_output_dir = output_dir / MODEL_NAME
    track_decomp_root = output_dir / "trackDecomp"
    plots_dir = output_dir / "plots"

    resources_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)
    track_decomp_root.mkdir(parents=True, exist_ok=True)
    plots_dir.mkdir(parents=True, exist_ok=True)

    before_paths = {
        p.resolve()
        for p in resources_dir.iterdir()
        if p.is_file() and p.suffix.lower() == ".mp3" and p.name != ".gitkeep"
    }

    scrape_cmd = [sys.executable, str(src_dir / "scrape_audio.py"), song]
    if _run_cmd(scrape_cmd, project_root, "scrape_audio") != 0:
        return 1

    target_file = _pick_target_file(resources_dir, song, before_paths)
    print(f"\nTarget audio: {target_file.name}")

    split_cmd = [sys.executable, str(src_dir / "split_stems.py")]
    if _run_cmd(split_cmd, project_root, "split_stems") != 0:
        return 1

    if not model_output_dir.is_dir():
        print(f"ERROR: Expected model output folder not found: {model_output_dir}", file=sys.stderr)
        return 1

    song_dir = _pick_song_dir(model_output_dir, target_file.stem, song)
    drums_path = song_dir / "drums.mp3"
    piano_path = song_dir / "piano.mp3"
    if not drums_path.exists():
        print(f"ERROR: Missing drums stem: {drums_path}", file=sys.stderr)
        return 1
    if not piano_path.exists():
        print(f"ERROR: Missing piano stem: {piano_path}", file=sys.stderr)
        return 1

    track_dir = track_decomp_root / song_dir.name
    drum_dir = track_dir / "drum"
    piano_dir = track_dir / "piano"
    drum_dir.mkdir(parents=True, exist_ok=True)
    piano_dir.mkdir(parents=True, exist_ok=True)
    midi_out = piano_dir / "piano.mid"

    drum_cmd = [
        sys.executable,
        str(src_dir / "isolate_drums.py"),
        "--export-hits",
        str(drums_path),
        "--out-dir",
        str(track_decomp_root),
    ]
    midi_cmd = [
        sys.executable,
        str(src_dir / "basic_pitch_to_midi.py"),
        str(piano_path),
        "--out",
        str(midi_out),
    ]

    with ThreadPoolExecutor(max_workers=2) as pool:
        drum_future = pool.submit(_run_cmd, drum_cmd, project_root, "isolate_drums")
        midi_future = pool.submit(_run_cmd, midi_cmd, project_root, "basic_pitch_to_midi")
        drum_rc = drum_future.result()
        midi_rc = midi_future.result()
    if drum_rc != 0 or midi_rc != 0:
        return 1

    kick_times = drum_dir / "kick_times.csv"
    hats_times = drum_dir / "hats_times.csv"
    if not kick_times.exists() or not hats_times.exists():
        print("ERROR: Expected hit CSV files were not generated.", file=sys.stderr)
        print(f"Missing: {kick_times if not kick_times.exists() else hats_times}", file=sys.stderr)
        return 1

    plot_out = plots_dir / f"{_slugify(song_dir.name)}_hits.png"
    viz_cmd = [
        sys.executable,
        str(src_dir / "visualize_hit_times.py"),
        "--kick-times",
        str(kick_times),
        "--hats-times",
        str(hats_times),
        "--audio",
        str(drums_path),
        "--max-seconds",
        str(max_seconds),
        "--out",
        str(plot_out),
        "--no-show",
    ]
    if _run_cmd(viz_cmd, project_root, "visualize_hit_times") != 0:
        return 1

    print("\nWorkflow complete.")
    print(f"- Song folder: {song_dir}")
    print(f"- Drum decomposition: {drum_dir}")
    print(f"- Piano MIDI: {midi_out}")
    print(f"- Hit plot: {plot_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
